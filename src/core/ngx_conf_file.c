
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>

#define NGX_CONF_BUFFER  4096

static ngx_int_t ngx_conf_add_dump(ngx_conf_t *cf, ngx_str_t *filename);
static ngx_int_t ngx_conf_handler(ngx_conf_t *cf, ngx_int_t last);
static ngx_int_t ngx_conf_read_token(ngx_conf_t *cf);
static void ngx_conf_flush_files(ngx_cycle_t *cycle);


static ngx_command_t  ngx_conf_commands[] = {

    // 関係ない
    // 設定ファイルとは別の設定ファイルをインクルードするためのディレクティブ
    { ngx_string("include"),
      NGX_ANY_CONF|NGX_CONF_TAKE1,
      ngx_conf_include,
      0,
      0,
      NULL },

      ngx_null_command
};


ngx_module_t  ngx_conf_module = {
    NGX_MODULE_V1,
    NULL,                                  /* module context */
    ngx_conf_commands,                     /* module directives */
    NGX_CONF_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_conf_flush_files,                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


/* The eight fixed arguments */

static ngx_uint_t argument_number[] = {
    NGX_CONF_NOARGS,
    NGX_CONF_TAKE1,
    NGX_CONF_TAKE2,
    NGX_CONF_TAKE3,
    NGX_CONF_TAKE4,
    NGX_CONF_TAKE5,
    NGX_CONF_TAKE6,
    NGX_CONF_TAKE7
};


/**
 * @brief
 *     サイクルの conf_param に値が入っていたら、
 *     そいつをバッファに移してパースする
 *     （設定ファイルのディレクティブをパースした後の要領で）
 * @param[in]
 *     cf: 処理対象の設定
 * @retval
 *     NGX_CONF_OK: 成功
 *     otherwise: 失敗
 */
char *
ngx_conf_param(ngx_conf_t *cf)
{
    char             *rv;
    ngx_str_t        *param;
    ngx_buf_t         b;
    ngx_conf_file_t   conf_file;

    param = &cf->cycle->conf_param;

    if (param->len == 0) {
        return NGX_CONF_OK;
    }

    ngx_memzero(&conf_file, sizeof(ngx_conf_file_t));

    ngx_memzero(&b, sizeof(ngx_buf_t));

    b.start = param->data;
    b.pos = param->data;
    b.last = param->data + param->len;
    b.end = b.last;
    b.temporary = 1;

    conf_file.file.fd = NGX_INVALID_FILE;
    conf_file.file.name.data = NULL;
    conf_file.line = 0;

    cf->conf_file = &conf_file;
    cf->conf_file->buffer = &b;

    // cf->cycle->conf_param をパースする
    rv = ngx_conf_parse(cf, NULL);

    cf->conf_file = NULL;

    return rv;
}


static ngx_int_t
ngx_conf_add_dump(ngx_conf_t *cf, ngx_str_t *filename)
{
    off_t             size;
    u_char           *p;
    uint32_t          hash;
    ngx_buf_t        *buf;
    ngx_str_node_t   *sn;
    ngx_conf_dump_t  *cd;

    // 32bit のハッシュ値を生成
    hash = ngx_crc32_long(filename->data, filename->len);

    // サイクルが保有する設定ダンプ赤黒木からハッシュで検索
    sn = ngx_str_rbtree_lookup(&cf->cycle->config_dump_rbtree, filename, hash);

    // 見つかったのなら既にダンプが存在するので、もうダンプをする必要はない
    if (sn) {
        cf->conf_file->dump = NULL;
        return NGX_OK;
    }

    p = ngx_pstrdup(cf->cycle->pool, filename);
    if (p == NULL) {
        return NGX_ERROR;
    }

    cd = ngx_array_push(&cf->cycle->config_dump);
    if (cd == NULL) {
        return NGX_ERROR;
    }

    // ファイルサイズを取得
    size = ngx_file_size(&cf->conf_file->file.info);

    // 一時バッファを生成
    buf = ngx_create_temp_buf(cf->cycle->pool, (size_t) size);
    if (buf == NULL) {
        return NGX_ERROR;
    }

    cd->name.data = p;
    cd->name.len = filename->len;
    cd->buffer = buf;

    cf->conf_file->dump = buf;

    sn = ngx_palloc(cf->temp_pool, sizeof(ngx_str_node_t));
    if (sn == NULL) {
        return NGX_ERROR;
    }

    sn->node.key = hash;
    sn->str = cd->name;

    ngx_rbtree_insert(&cf->cycle->config_dump_rbtree, &sn->node);

    return NGX_OK;
}


/**
 * @brief
 *     （めっちゃ難しい）
 *      設定ファイルを解析して、該当するすべてのコマンドを実行する
 * @param[in]
 *      cf: 設定をつかさどる構造体
 *      filename: 設定ファイルのパス
 * @retval
 *      NGX_OK: 成功
 *      NGX_ERROR: パース失敗
 * @detail
 *      cf 上のパラメータに格納されるものと、されないものもある（error_log など）
 *
 *      type ローカル変数が遷移しないので、一見すると変なコードだが、
 *      実は handler() 内で ngx_conf_parse(cf, NULL) でブロック解析モードで本メソッドを再帰的に読んでくれている
 */
char *
ngx_conf_parse(ngx_conf_t *cf, ngx_str_t *filename)
{
    char             *rv;
    ngx_fd_t          fd;
    ngx_int_t         rc;
    ngx_buf_t         buf;
    ngx_conf_file_t  *prev, conf_file;
    enum {
        parse_file = 0,
        parse_block,
        parse_param
    } type;

#if (NGX_SUPPRESS_WARN)
    fd = NGX_INVALID_FILE;
    prev = NULL;
#endif

    // filename が指定された場合は設定ファイルの構造をパースする
    if (filename) {

        /* open configuration file */

        fd = ngx_open_file(filename->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);

        if (fd == NGX_INVALID_FILE) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                               ngx_open_file_n " \"%s\" failed",
                               filename->data);
            return 

NGX_CONF_ERROR;
        }

        // 本関数の呼び出しが終わった際に、cf->conf_file を prev で戻す
        prev = cf->conf_file;

        // また別に開くということ？
        cf->conf_file = &conf_file;

        if (ngx_fd_info(fd, &cf->conf_file->file.info) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno,
                          ngx_fd_info_n " \"%s\" failed", filename->data);
        }

        cf->conf_file->buffer = &buf;

        buf.start = ngx_alloc(NGX_CONF_BUFFER, cf->log);
        if (buf.start == NULL) {
            goto failed;
        }

        // すべてブロック終了・ファイル終了で解放される
        buf.pos = buf.start;
        buf.last = buf.start;
        buf.end = buf.last + NGX_CONF_BUFFER;
        buf.temporary = 1;

        // このファイルもブロック終了・ファイル終了で閉じられる
        cf->conf_file->file.fd = fd;
        cf->conf_file->file.name.len = filename->len;
        cf->conf_file->file.name.data = filename->data;
        cf->conf_file->file.offset = 0;
        cf->conf_file->file.log = cf->log;
        cf->conf_file->line = 1;

        type = parse_file;

        // ダンプコンフィグが有効ならば
        if (ngx_dump_config
#if (NGX_DEBUG)
            || 1
#endif
           )
        {
            if (ngx_conf_add_dump(cf, filename) != NGX_OK) {
                goto failed;
            }

        } else {
            // 有効でないならダンプを空に
            cf->conf_file->dump = NULL;
        }

    } else if (cf->conf_file->file.fd != NGX_INVALID_FILE) {
        // 設定ファイルが指定されていない
        // and
        // 設定構造体にファイルディスクリプタが登録されているならブロック解析フェーズ
        type = parse_block;

    } else {
        // 設定ファイル名が指定されていない
        // and
        // 設定構造体に設定ファイルが登録もされていないならパラメータ解析フェーズ
        type = parse_param;
    }

    // このループで設定ファイルを読み切る
    for ( ;; ) {
        // トークン群を cf->args にプッシュする
        rc = ngx_conf_read_token(cf);

        /*
         * ngx_conf_read_token() may return
         *
         *    NGX_ERROR             there is error
         *    NGX_OK                the token terminated by ";" was found
         *    NGX_CONF_BLOCK_START  the token terminated by "{" was found
         *    NGX_CONF_BLOCK_DONE   the "}" was found
         *    NGX_CONF_FILE_DONE    the configuration file is done
         */

        if (rc == NGX_ERROR) {
            goto done;
        }

        // BLOCK の終了に到達するといったん、終了する
        if (rc == NGX_CONF_BLOCK_DONE) {

            if (type != parse_block) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"}\"");
                goto failed;
            }

            goto done;
        }

        // 設定ファイルを最後まで読むと終了する
        if (rc == NGX_CONF_FILE_DONE) {

            if (type == parse_block) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "unexpected end of file, expecting \"}\"");
                goto failed;
            }

            goto done;
        }

        if (rc == NGX_CONF_BLOCK_START) {

            if (type == parse_param) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "block directives are not supported "
                                   "in -g option");
                goto failed;
            }
        }

        /* rc == NGX_OK || rc == NGX_CONF_BLOCK_START */

        // NGX_CORE_MODULE の場合は関係ないっぽい？
        if (cf->handler) {

            /*
             * the custom handler, i.e., that is used in the http's
             * "types { ... }" directive
             */

            if (rc == NGX_CONF_BLOCK_START) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"{\"");
                goto failed;
            }

            rv = (*cf->handler)(cf, NULL, cf->handler_conf);
            if (rv == NGX_CONF_OK) {
                continue;
            }

            if (rv == NGX_CONF_ERROR) {
                goto failed;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%s", rv);

            goto failed;
        }

        // HTTP モジュール以外はこっち
        /**
         * @brief
         *     サイクルが保有する全モジュールのコマンドについて、名前が cf->args の先頭と一致するものを検索する
         *     一致するものがあったら、形式や引数について検証をする
         *     問題なければ、cf->ctx[モジュールID]に格納されているモジュール固有の設定構造体を取り出して、
         *     コマンドの登録された set() を呼び出して終了する
         * @param[in]
         *     cf: 設定管理構造体、こいつからサイクルをたどる、args（トークン情報）もこいつが持つ
         *     last: 今のトークン解析の状態
         *         NGX_ERROR             there is error
         *         NGX_OK                the token terminated by ";" was found
         *         NGX_CONF_BLOCK_START  the token terminated by "{" was found
         *         NGX_CONF_BLOCK_DONE   the "}" was found
         *         NGX_CONF_FILE_DONE    the configuration file is done
         * @retval
         *     NGX_OK: 成功
         *     NGX_ERROR: 失敗
         */
        rc = ngx_conf_handler(cf, rc);

        if (rc == NGX_ERROR) {
            goto failed;
        }
    }

failed:

    rc = NGX_ERROR;

done:
    // 後始末

    // もし、新しく設定ファイルを読んでいたなら
    if (filename) {
        if (cf->conf_file->buffer->start) {
            ngx_free(cf->conf_file->buffer->start);
        }

        if (ngx_close_file(fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                          ngx_close_file_n " %s failed",
                          filename->data);
            rc = NGX_ERROR;
        }

        // もともと保持していたものへ戻す
        cf->conf_file = prev;
    }

    if (rc == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/**
 * @brief
 *     サイクルが保有する全モジュールのコマンドについて、名前が cf->args の先頭と一致するものを検索する
 *     一致するものがあったら、形式や引数について検証をする
 *     問題なければ、cf->ctx[モジュールID]に格納されているモジュール固有の設定構造体を取り出して、
 *     コマンドの登録された set() を呼び出して終了する
 * @param[in]
 *     cf: 設定管理構造体、こいつからサイクルをたどる、args（トークン情報）もこいつが持つ
 *     last: 今のトークン解析の状態
 *         NGX_ERROR             there is error
 *         NGX_OK                the token terminated by ";" was found
 *         NGX_CONF_BLOCK_START  the token terminated by "{" was found
 *         NGX_CONF_BLOCK_DONE   the "}" was found
 *         NGX_CONF_FILE_DONE    the configuration file is done
 * @retval
 *     NGX_OK: 成功
 *     NGX_ERROR: 失敗
 */
static ngx_int_t
ngx_conf_handler(ngx_conf_t *cf, ngx_int_t last)
{
    char           *rv;
    void           *conf, **confp;
    ngx_uint_t      i, found;
    ngx_str_t      *name;
    ngx_command_t  *cmd;

    // args の先頭要素を取得
    name = cf->args->elts;

    found = 0;

    // 各モジュールはコマンド（文字列）で設定ファイルにおける設定識別子を保有する
    // 一致していたら処理にかける
    // (全モジュールの全コマンドから逐一、検索をかけるのか・・・ｗ）
    for (i = 0; cf->cycle->modules[i]; i++) {

        cmd = cf->cycle->modules[i]->commands;
        // そのモジュールのコマンドがない、次のモジュールへ
        if (cmd == NULL) {
            continue;
        }

        // 各コマンドについて
        for ( /* void */ ; cmd->name.len; cmd++) {

            if (name->len != cmd->name.len) {
                continue;
            }

            if (ngx_strcmp(name->data, cmd->name.data) != 0) {
                continue;
            }

            // 見つかった（一致していた）
            found = 1;

            /**
             * ↓　NGX_CONF_MODULE は include ディレクティブのみ対応
             *     まあ、要はどのモジュールタイプであったとしても、やることは変わらないので、
             *     つまり、httpモジュールの include ディレクティブであっても、ほかのモジュール向けにディレクティブを書くこともできるのであろう?
             */
            // NGX_CONF_MODULE 用の設定事項ではなくて
            //   かつ
            // 見つかったはいいけど、想定タイプが一致しなかった（httpモジュールのinclude とコアモジュールのinclude など）
            // とばす
            if (cf->cycle->modules[i]->type != NGX_CONF_MODULE
                && cf->cycle->modules[i]->type != cf->module_type)
            {
                continue;
            }

            /* is the directive's location right ? */

            // コマンドのタイプについても完全一致か調べる
            if (!(cmd->type & cf->cmd_type)) {
                continue;
            }

            // このコマンドはパラメタ形式なのに、パラメタ解析状態ではなかった
            if (!(cmd->type & NGX_CONF_BLOCK) && last != NGX_OK) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                  "directive \"%s\" is not terminated by \";\"",
                                  name->data);
                return NGX_ERROR;
            }

            // このコマンドはブロック形式なのに、ブロック解析状態ではなかった
            if ((cmd->type & NGX_CONF_BLOCK) && last != NGX_CONF_BLOCK_START) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "directive \"%s\" has no opening \"{\"",
                                   name->data);
                return NGX_ERROR;
            }

            /* is the directive's argument count right ? */

            // コマンドごとに定義された引数の数をもとに、正しいか検証する
            if (!(cmd->type & NGX_CONF_ANY)) {

                if (cmd->type & NGX_CONF_FLAG) {

                    if (cf->args->nelts != 2) {
                        goto invalid;
                    }

                } else if (cmd->type & NGX_CONF_1MORE) {

                    if (cf->args->nelts < 2) {
                        goto invalid;
                    }

                } else if (cmd->type & NGX_CONF_2MORE) {

                    if (cf->args->nelts < 3) {
                        goto invalid;
                    }

                } else if (cf->args->nelts > NGX_CONF_MAX_ARGS) {

                    goto invalid;

                } else if (!(cmd->type & argument_number[cf->args->nelts - 1]))
                {
                    goto invalid;
                }
            }

            /* set up the directive's configuration context */

            conf = NULL;

            if (cmd->type & NGX_DIRECT_CONF) {
                conf = ((void **) cf->ctx)[cf->cycle->modules[i]->index];

            } else if (cmd->type & NGX_MAIN_CONF) {
                conf = &(((void **) cf->ctx)[cf->cycle->modules[i]->index]);

            } else if (cf->ctx) {
                confp = *(void **) ((char *) cf->ctx + cmd->conf);

                if (confp) {
                    conf = confp[cf->cycle->modules[i]->ctx_index];
                }
            }

            // ここでコマンド特有のセットコマンドを呼び出す
            rv = cmd->set(cf, cmd, conf);

            if (rv == NGX_CONF_OK) {
                return NGX_OK;
            }

            if (rv == NGX_CONF_ERROR) {
                return NGX_ERROR;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"%s\" directive %s", name->data, rv);

            return NGX_ERROR;
        }
    }

    if (found) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"%s\" directive is not allowed here", name->data);

        return NGX_ERROR;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "unknown directive \"%s\"", name->data);

    return NGX_ERROR;

invalid:

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid number of arguments in \"%s\" directive",
                       name->data);

    return NGX_ERROR;
}


/**
 * @brief
 *     トークン単位で切り出して cf->args にプッシュする
 * @detail
 *     worker_processes  1;
 *
 *     events {
 *         worker_connections  1024;
 *     }
 *
 *     http {
 *         include       mime.types;
 *         default_type  application/octet-stream;
 *
 *         sendfile        on;
 *
 *         keepalive_timeout  65;
 *
 *         server {
 *             listen       80;
 *             server_name  localhost;
 *
 *             location / {
 *                 root   html;
 *                 index  index.html index.htm;
 *             }
 *
 *             error_page   500 502 503 504  /50x.html;
 *             location = /50x.html {
 *                 root   html;
 *             }
 *         }
 *     }
 *
 *
 *     呼び出すたびに↓のように args へ突っ込まれる
 *       worker_processes 1 (NGX_OK)
 *       events (NGX_CONF_BLOCK_START)
 *       worker_connections 1024 (NGX_OK)
 *       (NGX_CONF_BLOCK_DONE)
 *       http (NGX_CONF_BLOCK_START)
 *       include mime.types (NGX_OK)
 *       default_type application/octet-stream (NGX_OK)
 *       sendfile on (NGX_OK)
 *       keepalive_timeout 65 (NGX_OK)
 *       server (NGX_CONF_BLOCK_START)
 *       listen 80 (NGX_OK)
 *       server_name localhost (NGX_OK)
 *       location / (NGX_CONF_BLOCK_START)
 *       root html (NGX_OK)
 *       index index.html index.htm (NGX_OK)
 *       (NGX_CONF_BLOCK_DONE)
 *       error_page 500 502 503 504 /50x.html
 *       location = /50x.html (NGX_CONF_BLOCK_START)
 *       root html (NGX_OK
 *       (NGX_CONF_BLOCK_DONE)
 *       (NGX_CONF_BLOCK_DONE)
 *       (NGX_CONF_BLOCK_DONE)
 */
static ngx_int_t
ngx_conf_read_token(ngx_conf_t *cf)
{
    u_char      *start, ch, *src, *dst;
    off_t        file_size;
    size_t       len;
    ssize_t      n, size;
    ngx_uint_t   found, need_space, last_space, sharp_comment, variable;
    ngx_uint_t   quoted, s_quoted, d_quoted, start_line;
    ngx_str_t   *word;
    ngx_buf_t   *b, *dump;

    found = 0;
    need_space = 0;
    last_space = 1;
    sharp_comment = 0;
    variable = 0;
    quoted = 0;
    s_quoted = 0;
    d_quoted = 0;

    // 呼ぶたびに nelts は 0 に設定される
    cf->args->nelts = 0;
    b = cf->conf_file->buffer;
    dump = cf->conf_file->dump;
    start = b->pos;
    start_line = cf->conf_file->line;

    // 設定ファイルのサイズを取得
    file_size = ngx_file_size(&cf->conf_file->file.info);

    for ( ;; ) {

        // バッファの最後に到達した
        if (b->pos >= b->last) {

            // 設定ファイルの読み込みも完了している
            // パースパラメータの解析だと必ず true （どちらも０で初期化されている）
            if (cf->conf_file->file.offset >= file_size) {

                // まだ要素が残っている or 解析途中なのにバッファが終わるのはおかしい
                if (cf->args->nelts > 0 || !last_space) {

                    if (cf->conf_file->file.fd == NGX_INVALID_FILE) {
                        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                           "unexpected end of parameter, "
                                           "expecting \";\"");
                        return NGX_ERROR;
                    }

                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                  "unexpected end of file, "
                                  "expecting \";\" or \"}\"");
                    return NGX_ERROR;
                }

                // 何個かパースできたので、単に読み終わったものとして正常終了
                return NGX_CONF_FILE_DONE;
            }

            len = b->pos - start;

            if (len == NGX_CONF_BUFFER) {
                cf->conf_file->line = start_line;

                if (d_quoted) {
                    ch = '"';

                } else if (s_quoted) {
                    ch = '\'';

                } else {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "too long parameter \"%*s...\" started",
                                       10, start);
                    return NGX_ERROR;
                }

                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "too long parameter, probably "
                                   "missing terminating \"%c\" character", ch);
                return NGX_ERROR;
            }

            if (len) {
                ngx_memmove(b->start, start, len);
            }

            size = (ssize_t) (file_size - cf->conf_file->file.offset);

            if (size > b->end - (b->start + len)) {
                size = b->end - (b->start + len);
            }

            n = ngx_read_file(&cf->conf_file->file, b->start + len, size,
                              cf->conf_file->file.offset);

            if (n == NGX_ERROR) {
                return NGX_ERROR;
            }

            if (n != size) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   ngx_read_file_n " returned "
                                   "only %z bytes instead of %z",
                                   n, size);
                return NGX_ERROR;
            }

            b->pos = b->start + len;
            b->last = b->pos + n;
            start = b->start;

            if (dump) {
                dump->last = ngx_cpymem(dump->last, b->pos, size);
            }
        }

        // 今、指している文字を取得して、 pos は次の文字を指すようインクリメントする
        ch = *b->pos++;

        if (ch == LF) {
            // 改行をカウントする
            cf->conf_file->line++;

            // コメント行が終わったので、コメント行フラグを戻す
            if (sharp_comment) {
                sharp_comment = 0;
            }
        }

        // 今はコメント行を読んでいるので無視して次へ
        if (sharp_comment) {
            continue;
        }

        // ディレクトリクオートフラグが立っていたらリセットして次へ
        if (quoted) {
            quoted = 0;
            continue;
        }

        // シングルクオートやダブルクオートの終わり（スペースが必要）
        // ""       
        // "":
        // ""{
        // "")
        // ↑以外のパターンをエラーとしてはじく
        if (need_space) {
            // スペースがあったので解除
            if (ch == ' ' || ch == '\t' || ch == CR || ch == LF) {
                last_space = 1;
                need_space = 0;
                continue;
            }

            // セミコロン（行末）
            if (ch == ';') {
                return NGX_OK;
            }

            // 解析したトークンはブロック名でした
            if (ch == '{') {
                return NGX_CONF_BLOCK_START;
            }

            // なんか知らんけど、()の中に"" や '' が入っているケースも認めて要るっぽい
            if (ch == ')') {
                last_space = 1;
                need_space = 0;

            } else {
                // スペースがあるべきか所にスペースがなかった
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "unexpected \"%c\"", ch);
                return NGX_ERROR;
            }
        }

        // 直前がスペースだった場合（読み始めもここからスタート）
        // つまり、last_space == 0 は何かのパターンに遭遇して解析途中であることを示す
        if (last_space) {
            // 前にインクリメントしたので、今、指している文字については -1 する必要あり
            start = b->pos - 1;
            // 何行目か
            start_line = cf->conf_file->line;

            // スペースが続く
            if (ch == ' ' || ch == '\t' || ch == CR || ch == LF) {
                continue;
            }

            switch (ch) {

            // まだスペース解析
            case ';':
            case '{':
                // まだ何もトークンが登場していないのに、ブロックが始まったり行末終了が起きるのはおかしい
                if (cf->args->nelts == 0) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "unexpected \"%c\"", ch);
                    return NGX_ERROR;
                }

                // これはブロックのスタートである
                if (ch == '{') {
                    return NGX_CONF_BLOCK_START;
                }

                // 行末が完了した
                return NGX_OK;

            // まだスペース解析
            case '}':
                // nelts != 0 （なんかトークンが見つかってまだ終わっていない）のに } が登場するのはおかしい
                if (cf->args->nelts != 0) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "unexpected \"}\"");
                    return NGX_ERROR;
                }

                return NGX_CONF_BLOCK_DONE;

            // コメント行フラグを立てる（以降、改行までスキップする）
            case '#':
                sharp_comment = 1;
                continue;

            // ディレクトリクオート（C://home/kenta とかの / のこと)
            case '\\':
                quoted = 1;
                last_space = 0;
                continue;

            // ダブルくおーとによる囲い開始フラグを立てる
            case '"':
                start++;
                d_quoted = 1;
                last_space = 0;
                continue;

            // シングルくおーとによる囲い開始フラグを立てる
            case '\'':
                start++;
                s_quoted = 1;
                last_space = 0;
                continue;

            // 変数宣言ふらぐを立てる
            case '$':
                variable = 1;
                last_space = 0;
                continue;

            // 何にせよスペースではなかった
            default:
                last_space = 0;
            }

        } else {
            // 変数宣言フラグが立っていて（$の後）、文字が '{' だった場合はエラーではない
            // (変数宣言ではないのに { が来ることはないので文法エラー)
            if (ch == '{' && variable) {
                continue;
            }

            variable = 0;

            if (ch == '\\') {
                quoted = 1;
                continue;
            }

            if (ch == '$') {
                variable = 1;
                continue;
            }

            if (d_quoted) {
                if (ch == '"') {
                    d_quoted = 0;
                    need_space = 1;
                    found = 1;
                }

            } else if (s_quoted) {
                if (ch == '\'') {
                    s_quoted = 0;
                    need_space = 1;
                    found = 1;
                }

            } else if (ch == ' ' || ch == '\t' || ch == CR || ch == LF
                       || ch == ';' || ch == '{')
            {
                last_space = 1;
                found = 1;
            }

            // 今、解析しているトークンの終わりが見つかった
            if (found) {
                // cf->args 配列で次に割り当てるべき領域を取得する
                word = ngx_array_push(cf->args);
                if (word == NULL) {
                    return NGX_ERROR;
                }

                // トークンのサイズだけプールから割り当てる（文字の長さ＋ヌル文字）
                word->data = ngx_pnalloc(cf->pool, b->pos - 1 - start + 1);
                if (word->data == NULL) {
                    return NGX_ERROR;
                }

                // 設定ファイルから cf->args へ移す
                for (dst = word->data, src = start, len = 0;
                     src < b->pos - 1;
                     len++)
                {
                    // \" \\ \\\ \t \r \n について考慮
                    if (*src == '\\') {
                        switch (src[1]) {
                        case '"':
                        case '\'':
                        case '\\':
                            src++;
                            break;

                        case 't':
                            *dst++ = '\t';
                            src += 2;
                            continue;

                        case 'r':
                            *dst++ = '\r';
                            src += 2;
                            continue;

                        case 'n':
                            *dst++ = '\n';
                            src += 2;
                            continue;
                        }

                    }
                    *dst++ = *src++;
                }
                // ヌル文字と長さを設定
                *dst = '\0';
                word->len = len;

                // セミコロンで行末が終了するとここへ
                if (ch == ';') {
                    return NGX_OK;
                }

                // こっからブロックが続く（つまり今、パースしたトークンはブロック名を表す）
                if (ch == '{') {
                    return NGX_CONF_BLOCK_START;
                }

                found = 0;
            }
        }
    }
}


/**
 * @brief
 *     （パスに * ? [ が含まれていた場合の処理は飛ばした）
 *      別の設定ファイルをここで指定できる
 *      include ディレクティブのコマンド処理
 * @param[in:out]
 *     cf: 設定ファイルを表す構造体（トークンを保有する）
 * @param[in]
 *     cmd: コマンド
 *     conf: 設定値
 */
char *
ngx_conf_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char        *rv;
    ngx_int_t    n;
    ngx_str_t   *value, file, name;
    ngx_glob_t   gl;

    value = cf->args->elts;
    file = value[1];

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

    /**
     * @brief
     *     必要であれば、プリフィックスと結合したパス名を第二引数へ返す
     */
    if (ngx_conf_full_name(cf->cycle, &file, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    // パス名から * ? [ のいずれの文字も発見できなかった場合
    if (strpbrk((char *) file.data, "*?[") == NULL) {

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        // 変なパスではないので、パスで指定された別設定ファイルについて、解析して適用
        /**
         * @brief
         *     （めっちゃ難しい）
         *      設定ファイルを解析して、該当するすべてのコマンドを実行する
         * @param[in]
         *      cf: 設定をつかさどる構造体
         *      filename: 設定ファイルのパス
         * @retval
         *      NGX_OK: 成功
         *      NGX_ERROR: パース失敗
         * @detail
         *      cf 上のパラメータに格納されるものと、されないものもある（error_log など）
         *
         *      type ローカル変数が遷移しないので、一見すると変なコードだが、
         *      実は handler() 内で ngx_conf_parse(cf, NULL) でブロック解析モードで本メソッドを再帰的に読んでくれている
         */
        return ngx_conf_parse(cf, &file);
    }

    // glob はパターンマッチング関連
    ngx_memzero(&gl, sizeof(ngx_glob_t));

    gl.pattern = file.data;
    gl.log = cf->log;
    gl.test = 1;

    if (ngx_open_glob(&gl) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_open_glob_n " \"%s\" failed", file.data);
        return NGX_CONF_ERROR;
    }

    rv = NGX_CONF_OK;

    for ( ;; ) {
        n = ngx_read_glob(&gl, &name);

        if (n != NGX_OK) {
            break;
        }

        file.len = name.len++;
        file.data = ngx_pstrdup(cf->pool, &name);
        if (file.data == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        rv = ngx_conf_parse(cf, &file);

        if (rv != NGX_CONF_OK) {
            break;
        }
    }

    ngx_close_glob(&gl);

    return rv;
}


/**
 * @brief
 *     必要であれば、プリフィックスと結合したパス名を返す
 */
ngx_int_t
ngx_conf_full_name(ngx_cycle_t *cycle, ngx_str_t *name, ngx_uint_t conf_prefix)
{
    ngx_str_t  *prefix;

    // 設定ファイル用のプリフィクスが存在するならそちらを使う
    prefix = conf_prefix ? &cycle->conf_prefix : &cycle->prefix;

    return ngx_get_full_name(cycle->pool, prefix, name);
}


/**
 * @brief
 *     第２引数 name で指定されたファイルを cycle->open_files にプッシュする
 *     name が NULL の場合は標準エラー出力を開いて cycle->open_files リストにプッシュする
 *     name と該当するファイルがなかった場合は fd に NGX_INVALID_FD がセットされたファイルが返る
 *     cycle->open_files にあるファイル群は init_cycle() 内でオープンされる
 * @param[in]
 *     cycle: このサイクルが保有するオープンファイル群から、ログ用の出力先ファイルを探す
 *     name: ログ用の出力先ファイル名
 * @retval
 *     NULL: 致命的な失敗
 *     otherwise: ログ用の出力先ファイル
 *                なお、fd が INVALID_FILE として変えることもある（探したけど見つからなかった場合）
 * @detail
 *     内部で、サイクルが持つ設定ファイル用のプリフィクス情報を用いてパスを指定する
 *     内部で実際に開くのではなく、すでに開かれている cycle->open_files 下のファイルから名前が該当するものを返すだけ
 */
ngx_open_file_t *
ngx_conf_open_file(ngx_cycle_t *cycle, ngx_str_t *name)
{
    ngx_str_t         full;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

#if (NGX_SUPPRESS_WARN)
    ngx_str_null(&full);
#endif

    if (name->len) {
        full = *name;

        /**
         * @brief
         *     第3引数パスが絶対パスかどうかを検証して、そうでないなら第２引数のプリフィクスと結合する
         * @param[in:out]
         *     name: 処理対象のパス名
         * @param[in]
         *     pool: 新しい結合パス名を保存する先のプール
         *     prefix: 結合するプリフィックス
         * @retval
         *     NGX_OK: 成功
         *     NGX_ERROR: 失敗
         * @detail
         *     すでに name が絶対パスなら何もせずに終了する
         *     name が絶対パスではないなら prefix と結合する
         */
        if (ngx_conf_full_name(cycle, &full, 0) != NGX_OK) {
            return NULL;
        }

        // 先頭の部分リストを取得
        part = &cycle->open_files.part;
        file = part->elts;

        // サイクルが保有するファイルのうち、一致するものがあればそれを返す
        for (i = 0; /* void */ ; i++) {

            // 次の部分リストへ
            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                file = part->elts;
                i = 0;
            }

            if (full.len != file[i].name.len) {
                continue;
            }

            if (ngx_strcmp(full.data, file[i].name.data) == 0) {
                return &file[i];
            }
        }
    }

    // 次にプッシュすべきアドレスを取得
    file = ngx_list_push(&cycle->open_files);
    if (file == NULL) {
        return NULL;
    }

    if (name->len) {
        file->fd = NGX_INVALID_FILE;
        file->name = full;

    } else {
        file->fd = ngx_stderr;
        file->name = *name;
    }

    file->flush = NULL;
    file->data = NULL;

    return file;
}


/**
 * @brief
 *     サイクルが保有する open_files リストのすべてのファイルについて、保有しているなら flush() を呼び出す
 *     たぶん、すべてのファイルについて、バッファに残っているものがあれば、即座にすべて書き出させる処理？
 * @param[in]
 *     cycle: 対象サイクル
 */
static void
ngx_conf_flush_files(ngx_cycle_t *cycle)
{
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "flush files");

    // オープンファイルリストの先頭部分リストを取得
    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */ ; i++) {

        // 次の部分リストへ
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        // ファイルオブジェクトにフラッシュメソッドが定義されていたら、一斉に呼び出す
        if (file[i].flush) {
            file[i].flush(&file[i], cycle->log);
        }
    }
}


void ngx_cdecl
ngx_conf_log_error(ngx_uint_t level, ngx_conf_t *cf, ngx_err_t err,
    const char *fmt, ...)
{
    u_char   errstr[NGX_MAX_CONF_ERRSTR], *p, *last;
    va_list  args;

    last = errstr + NGX_MAX_CONF_ERRSTR;

    va_start(args, fmt);
    p = ngx_vslprintf(errstr, last, fmt, args);
    va_end(args);

    if (err) {
        p = ngx_log_errno(p, last, err);
    }

    if (cf->conf_file == NULL) {
        ngx_log_error(level, cf->log, 0, "%*s", p - errstr, errstr);
        return;
    }

    if (cf->conf_file->file.fd == NGX_INVALID_FILE) {
        ngx_log_error(level, cf->log, 0, "%*s in command line",
                      p - errstr, errstr);
        return;
    }

    ngx_log_error(level, cf->log, 0, "%*s in %s:%ui",
                  p - errstr, errstr,
                  cf->conf_file->file.name.data, cf->conf_file->line);
}


char *
ngx_conf_set_flag_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t        *value;
    ngx_flag_t       *fp;
    ngx_conf_post_t  *post;

    fp = (ngx_flag_t *) (p + cmd->offset);

    if (*fp != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        *fp = 1;

    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
        *fp = 0;

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                     "invalid value \"%s\" in \"%s\" directive, "
                     "it must be \"on\" or \"off\"",
                     value[1].data, cmd->name.data);
        return NGX_CONF_ERROR;
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, fp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_str_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t        *field, *value;
    ngx_conf_post_t  *post;

    field = (ngx_str_t *) (p + cmd->offset);

    if (field->data) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *field = value[1];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, field);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_str_array_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t         *value, *s;
    ngx_array_t      **a;
    ngx_conf_post_t   *post;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NGX_CONF_UNSET_PTR) {
        *a = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    s = ngx_array_push(*a);
    if (s == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    *s = value[1];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, s);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_keyval_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t         *value;
    ngx_array_t      **a;
    ngx_keyval_t      *kv;
    ngx_conf_post_t   *post;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NULL) {
        *a = ngx_array_create(cf->pool, 4, sizeof(ngx_keyval_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    kv = ngx_array_push(*a);
    if (kv == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    kv->key = value[1];
    kv->value = value[2];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, kv);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_num_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_int_t        *np;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    np = (ngx_int_t *) (p + cmd->offset);

    if (*np != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;
    *np = ngx_atoi(value[1].data, value[1].len);
    if (*np == NGX_ERROR) {
        return "invalid number";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, np);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_size_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    size_t           *sp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    sp = (size_t *) (p + cmd->offset);
    if (*sp != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *sp = ngx_parse_size(&value[1]);
    if (*sp == (size_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, sp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_off_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    off_t            *op;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    op = (off_t *) (p + cmd->offset);
    if (*op != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *op = ngx_parse_offset(&value[1]);
    if (*op == (off_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, op);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_msec_t       *msp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    msp = (ngx_msec_t *) (p + cmd->offset);
    if (*msp != NGX_CONF_UNSET_MSEC) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *msp = ngx_parse_time(&value[1], 0);
    if (*msp == (ngx_msec_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, msp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_sec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    time_t           *sp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    sp = (time_t *) (p + cmd->offset);
    if (*sp != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *sp = ngx_parse_time(&value[1], 1);
    if (*sp == (time_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, sp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_bufs_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *p = conf;

    ngx_str_t   *value;
    ngx_bufs_t  *bufs;


    bufs = (ngx_bufs_t *) (p + cmd->offset);
    if (bufs->num) {
        return "is duplicate";
    }

    value = cf->args->elts;

    bufs->num = ngx_atoi(value[1].data, value[1].len);
    if (bufs->num == NGX_ERROR || bufs->num == 0) {
        return "invalid value";
    }

    bufs->size = ngx_parse_size(&value[2]);
    if (bufs->size == (size_t) NGX_ERROR || bufs->size == 0) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_enum_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_uint_t       *np, i;
    ngx_str_t        *value;
    ngx_conf_enum_t  *e;

    np = (ngx_uint_t *) (p + cmd->offset);

    if (*np != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;
    e = cmd->post;

    for (i = 0; e[i].name.len != 0; i++) {
        if (e[i].name.len != value[1].len
            || ngx_strcasecmp(e[i].name.data, value[1].data) != 0)
        {
            continue;
        }

        *np = e[i].value;

        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid value \"%s\"", value[1].data);

    return NGX_CONF_ERROR;
}


char *
ngx_conf_set_bitmask_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_uint_t          *np, i, m;
    ngx_str_t           *value;
    ngx_conf_bitmask_t  *mask;


    np = (ngx_uint_t *) (p + cmd->offset);
    value = cf->args->elts;
    mask = cmd->post;

    for (i = 1; i < cf->args->nelts; i++) {
        for (m = 0; mask[m].name.len != 0; m++) {

            if (mask[m].name.len != value[i].len
                || ngx_strcasecmp(mask[m].name.data, value[i].data) != 0)
            {
                continue;
            }

            if (*np & mask[m].mask) {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "duplicate value \"%s\"", value[i].data);

            } else {
                *np |= mask[m].mask;
            }

            break;
        }

        if (mask[m].name.len == 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid value \"%s\"", value[i].data);

            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


#if 0

char *
ngx_conf_unsupported(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    return "unsupported on this platform";
}

#endif


char *
ngx_conf_deprecated(ngx_conf_t *cf, void *post, void *data)
{
    ngx_conf_deprecated_t  *d = post;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "the \"%s\" directive is deprecated, "
                       "use the \"%s\" directive instead",
                       d->old_name, d->new_name);

    return NGX_CONF_OK;
}


char *
ngx_conf_check_num_bounds(ngx_conf_t *cf, void *post, void *data)
{
    ngx_conf_num_bounds_t  *bounds = post;
    ngx_int_t  *np = data;

    if (bounds->high == -1) {
        if (*np >= bounds->low) {
            return NGX_CONF_OK;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "value must be equal to or greater than %i",
                           bounds->low);

        return NGX_CONF_ERROR;
    }

    if (*np >= bounds->low && *np <= bounds->high) {
        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "value must be between %i and %i",
                       bounds->low, bounds->high);

    return NGX_CONF_ERROR;
}
