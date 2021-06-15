
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @macro
 *     NGX_HAVE_VARIADIC_MACROS
 *         可変長表記（...）が許されているかどうか
 */


static char *ngx_error_log(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_log_set_levels(ngx_conf_t *cf, ngx_log_t *log);
static void ngx_log_insert(ngx_log_t *log, ngx_log_t *new_log);


#if (NGX_DEBUG)

  static void ngx_log_memory_writer(ngx_log_t *log, ngx_uint_t level,
      u_char *buf, size_t len);
  static void ngx_log_memory_cleanup(void *data);


  typedef struct {
      u_char        *start;
      u_char        *end;
      u_char        *pos;
      ngx_atomic_t   written;
  } ngx_log_memory_buf_t;

#endif


static ngx_command_t  ngx_errlog_commands[] = {

    // 関係ない
    // エラーを出力する先を指定するためのディレクティブ
    { ngx_string("error_log"),
      NGX_MAIN_CONF|NGX_CONF_1MORE,
      ngx_error_log,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_errlog_module_ctx = {
    ngx_string("errlog"),
    NULL,
    NULL
};


ngx_module_t  ngx_errlog_module = {
    NGX_MODULE_V1,
    &ngx_errlog_module_ctx,                /* module context */
    ngx_errlog_commands,                   /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


// ログに関するあらゆる情報を管理する構造体
// こいつがログの本体（こいつがさらに追加のログ構造体をリスト形式で保持することはできる）
static ngx_log_t        ngx_log;
// ログの出力に使用するファイルを表す
static ngx_open_file_t  ngx_log_file;
// nginx 自身が標準出力を管理するのかどうか（つまりログに管理権限はない？）
ngx_uint_t              ngx_use_stderr = 1;


// エラーレベル群（ログレベル）
static ngx_str_t err_levels[] = {
    ngx_null_string,
    ngx_string("emerg"),
    ngx_string("alert"),
    ngx_string("crit"),
    ngx_string("error"),
    ngx_string("warn"),
    ngx_string("notice"),
    ngx_string("info"),
    ngx_string("debug")
};

// デバッグレベル群（ログレベル）
static const char *debug_levels[] = {
    "debug_core", "debug_alloc", "debug_mutex", "debug_event",
    "debug_http", "debug_mail", "debug_stream"
};


// 可変長表記（...）が許されているかどうか
#if (NGX_HAVE_VARIADIC_MACROS)

/**
 * @brief
 *     第２引数 log を用いて第４引数以降のエラーメッセージを出力する
 * @param[in]
 *     level: 書き込むメッセージのログレベル
 *     log: このログを用いて書き込む
 *     err: エラー番号（指定しなくてもよい、指定されていたらエラーメッセージに含まれる）
 *     fmt: 書き込むエラーメッセージのフォーマット
 *     ...: 書き込むエラーメッセージ本体
 * @detail
 *     必ず成功するわけではない？
 *     err_log ディレクティブで指定したすべてのレベルのログについて、
 *     それぞれこのメッセージのレベルと比較して書き込むべきかどうかを検討する
 *     検討の結果、レベル的に問題なければそれらすべてのログに書き込む
 */
void
ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...)

#else

ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, va_list args)

#endif
{
#if (NGX_HAVE_VARIADIC_MACROS)
    va_list      args;
#endif
    u_char      *p, *last, *msg;
    ssize_t      n;
    ngx_uint_t   wrote_stderr, debug_connection;
    u_char       errstr[NGX_MAX_ERROR_STR];

    last = errstr + NGX_MAX_ERROR_STR;

    // errstr に文字列時間キャッシュに格納された時刻情報をコピーする
    p = ngx_cpymem(errstr, ngx_cached_err_log_time.data,
                   ngx_cached_err_log_time.len);

    /**
     * @brief
     *     第１引数 buf へ第４引数以降をコピー
     * @param[out]
     *     buf: 書き出し先メモリ領域
     * @param[in]
     *     last: buf の最終番地
     *     fmt: 書き出す書式
     *     ...: 書き出す文字列やパラメータ
     * @retval
     *     書き込んだ結果、次の書き込み先へのポインタ
     */
    // エラーレベル情報を errstr へ書き込む
    p = ngx_slprintf(p, last, " [%V] ", &err_levels[level]);

    // プロセス ID と TID を書き込む（TIDってなんだ？）
    /* pid#tid */
    p = ngx_slprintf(p, last, "%P#" NGX_TID_T_FMT ": ",
                    ngx_log_pid, ngx_log_tid);

    // ?
    if (log->connection) {
        p = ngx_slprintf(p, last, "*%uA ", log->connection);
    }

    // こっからメッセージ
    msg = p;

#if (NGX_HAVE_VARIADIC_MACROS)

    va_start(args, fmt);
    p = ngx_vslprintf(p, last, fmt, args);
    va_end(args);

#else

    // 本関数の引数で渡された args を errstr へ書き込む
    p = ngx_vslprintf(p, last, fmt, args);

#endif

    if (err) {
        /**
         * @brief
         *     第３引数で渡されたエラー番号を第１引数で渡されたバッファへ書き込む
         * @param[out]
         *     buf: 書き出し先バッファ
         * @param[in]
         *     last: 書き出し先バッファの最後の番地
         *     err: エラー番号
         * @retval
         *     次の書き込む先メモリ番地
         * @detail
         *     スペースが足りていなかったら強制的に、前に書き込んでいたものを消して書き込む
         *     その際は ... を書き込むことで消したことを指し示す
         */
        p = ngx_log_errno(p, last, err);
    }

    if (level != NGX_LOG_DEBUG && log->handler) {
        p = log->handler(log, p, last - p);
    }

    // 改行文字を書きこむスペースがなければ、強制的に作る
    if (p > last - NGX_LINEFEED_SIZE) {
        p = last - NGX_LINEFEED_SIZE;
    }

    /**
     * @brief
     *     ポインタが示す先に改行文字を代入して、ポインタを一つ進める
     * @param[in:out]
     *     p: このポインタが指す位置に改行文字を格納して、ポインタを一つ進める
     */
    ngx_linefeed(p);

    wrote_stderr = 0;
    debug_connection = (log->log_level & NGX_LOG_DEBUG_CONNECTION) != 0;

    while (log) {

        // このログのレベルより、書き込むレベルが大きい場合は、書き込まなくていいのでループから抜ける
        // もしくは、ログのレベルが NGX_LOG_DEBUG_CONNECTION なら抜ける（ログ出力しない）
        if (log->log_level < level && !debug_connection) {
            break;
        }

        // error_log ディレクティブで syslog を指定した際に有効化される
        // 今回は無視
        if (log->writer) {
            log->writer(log, level, errstr, p - errstr);
            goto next;
        }

        // 前にディスク空き容量がなかった時から最低、１秒は書き込みをスキップする
        // なぜなら、FreeBSD は空き容量がないディスクへの書き込みで多くの時間を浪費するから
        // これを防ぐため
        if (ngx_time() == log->disk_full_time) {

            /*
             * on FreeBSD writing to a full filesystem with enabled softupdates
             * may block process for much longer time than writing to non-full
             * filesystem, so we skip writing to a log for one second
             */
            // 何か知らんけど、最低、１秒は書き込みをスキップするらしい

            goto next;
        }

        // ファイルへ書き込み
        n = ngx_write_fd(log->file->fd, errstr, p - errstr);

        // 書き込み先のディスクに空き容量がないことを示す
        // それが検知された時間を更新する
        // https://qiita.com/kaitaku/items/fb7c84fe562530668614
        if (n == -1 && ngx_errno == NGX_ENOSPC) {
            // 時間を更新
            log->disk_full_time = ngx_time();
        }

        if (log->file->fd == ngx_stderr) {
            wrote_stderr = 1;
        }

    next:

        // 次のログへ
        log = log->next;
    }

    // nginx は stderr を使っていない（ログが使う）
    // レグレベルが WARN より大きかった（重要ではない）
    // 標準出力を用いてどれかの書き込みが成功した
    // ↑３つのどれかが満たされれば正常終了
    if (!ngx_use_stderr
        || level > NGX_LOG_WARN
        || wrote_stderr)
    {
        return;
    }

    // nginx は標準出力を保有していて、今回のログレベルが WARN より緊急度が高くて、
    // 今回の書き込みでまだ標準出力が用いられていないなら、↓の処理へ

    msg -= (7 + err_levels[level].len + 3);

    // このメッセージは nginx 自身が出力したことをマーキングする
    (void) ngx_sprintf(msg, "nginx: [%V] ", &err_levels[level]);

    // nginx 自らが標準出力へ書き込み
    (void) ngx_write_console(ngx_stderr, msg, p - msg);
}


#if !(NGX_HAVE_VARIADIC_MACROS)


/**
 * @brief
 *     第１引数レベルがログレベル以下か検証して、
 *     そうならそのログが保有するすべてのログについて出力検討・出力する
 * @param[in]
 *     level: レベル
 *     log: ログ出力に使用するログ構造体
 *     err: 
 *     fmt: エラーメッセージのフォーマット
 *     args: エラーメッセージ本体
 */
void ngx_cdecl
ngx_log_error(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...)
{
    va_list  args;

    // 渡されたレベルがログレベル以下の場合はログとみなして出力
    if (log->log_level >= level) {
        va_start(args, fmt);
        /**
         * @brief
         *     第２引数 log を用いて第４引数以降のエラーメッセージを出力する
         * @param[in]
         *     level: 書き込むメッセージのログレベル
         *     log: このログを用いて書き込む
         *     err: エラー番号（指定しなくてもよい、指定されていたらエラーメッセージに含まれる）
         *     fmt: 書き込むエラーメッセージのフォーマット
         *     ...: 書き込むエラーメッセージ本体
         * @detail
         *     必ず成功するわけではない？
         *     err_log ディレクティブで指定したすべてのレベルのログについて、
         *     それぞれこのメッセージのレベルと比較して書き込むべきかどうかを検討する
         *     検討の結果、レベル的に問題なければそれらすべてのログに書き込む
         */
        ngx_log_error_core(level, log, err, fmt, args);
        va_end(args);
    }
}


/**
 * @brief
 *     一番、緊急度が低いログレベル（NGX_LOG_DEBUG）で書き込む
 *     ログが保有するすべてのログについて出力検討・出力する
 * @param[in]
 *     level: レベル
 *     log: ログ出力に使用するログ構造体
 *     err: 
 *     fmt: エラーメッセージのフォーマット
 *     args: エラーメッセージ本体
 */
void ngx_cdecl
ngx_log_debug_core(ngx_log_t *log, ngx_err_t err, const char *fmt, ...)
{
    va_list  args;

    va_start(args, fmt);
    ngx_log_error_core(NGX_LOG_DEBUG, log, err, fmt, args);
    va_end(args);
}

#endif


void ngx_cdecl
ngx_log_abort(ngx_err_t err, const char *fmt, ...)
{
    u_char   *p;
    va_list   args;
    u_char    errstr[NGX_MAX_CONF_ERRSTR];

    // errstr にエラーメッセージをセット
    va_start(args, fmt);
    p = ngx_vsnprintf(errstr, sizeof(errstr) - 1, fmt, args);
    va_end(args);

    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, err,
                  "%*s", p - errstr, errstr);
}


void ngx_cdecl
ngx_log_stderr(ngx_err_t err, const char *fmt, ...)
{
    u_char   *p, *last;
    va_list   args;
    u_char    errstr[NGX_MAX_ERROR_STR];

    last = errstr + NGX_MAX_ERROR_STR;

    p = ngx_cpymem(errstr, "nginx: ", 7);

    va_start(args, fmt);
    p = ngx_vslprintf(p, last, fmt, args);
    va_end(args);

    if (err) {
        p = ngx_log_errno(p, last, err);
    }

    // 末尾に改行文字を入れるスペースを無理やり作る
    if (p > last - NGX_LINEFEED_SIZE) {
        p = last - NGX_LINEFEED_SIZE;
    }

    // 改行文字を追加
    ngx_linefeed(p);

    (void) ngx_write_console(ngx_stderr, errstr, p - errstr);
}


/**
 * @brief
 *     第３引数で渡されたエラー番号を第１引数で渡されたバッファへ書き込む
 * @param[out]
 *     buf: 書き出し先バッファ
 * @param[in]
 *     last: 書き出し先バッファの最後の番地
 *     err: エラー番号
 * @retval
 *     次の書き込む先メモリ番地
 * @detail
 *     スペースが足りていなかったら強制的に、前に書き込んでいたものを消して書き込む
 *     その際は ... を書き込むことで消したことを指し示す
 */
u_char *
ngx_log_errno(u_char *buf, u_char *last, ngx_err_t err)
{
    if (buf > last - 50) {

        /* leave a space for an error code */

        // スペースが足りていなかったら強制的に作る
        buf = last - 50;
        // スペースを強制的に作ったことを示しておく
        *buf++ = '.';
        *buf++ = '.';
        *buf++ = '.';
    }

#if (NGX_WIN32)
    buf = ngx_slprintf(buf, last, ((unsigned) err < 0x80000000)
                                       ? " (%d: " : " (%Xd: ", err);
#else
    // エラー番号を buf に書き込む
    buf = ngx_slprintf(buf, last, " (%d: ", err);
#endif

    buf = ngx_strerror(err, buf, last - buf);

    // まだスペースがあるなら ) を書き込んで終わりを示す
    if (buf < last) {
        *buf++ = ')';
    }

    return buf;
}


/**
 * @brief
 *     エントリーログを初期化する
 *       ファイルディスクリプタ：ビルド時のマクロで指定されたファイル
 *                               無効なら標準出力(ngx_stderr)
 *       ログレベル：NOTICE
 *     err_log ディレクティブで指定されたログはこのエントリーログが保有するログリストへ追加されていく
 * @param[in]
 *     prefix: 内部でパスの参照に使用する
 * @macro
 *     NGX_ERROR_LOG_PATH: エラーログを出力するファイルのパス
 *     NGX_PREFIX: 渡されたパスが相対パスのときに使用する
 *     ngx_string, os/unix/ngx_files の方
 */
ngx_log_t *
ngx_log_init(u_char *prefix)
{
    u_char  *p, *name;
    size_t   nlen, plen;

    ngx_log.file = &ngx_log_file;
    // レベルは NOTICE(6)、
    // つまり、ngx_log_error() によるログ出力は INFO, DEBUG 以外は行われる
    ngx_log.log_level = NGX_LOG_NOTICE;

    name = (u_char *) NGX_ERROR_LOG_PATH;

    /*
     * we use ngx_strlen() here since BCC warns about
     * condition is always false and unreachable code
     */

    nlen = ngx_strlen(name);

    if (nlen == 0) {
        ngx_log_file.fd = ngx_stderr;
        return &ngx_log;
    }

    p = NULL;

    // 絶対パスではないなら
    // 引数で prefix が与えられていたら、それを使って絶対パスを作る
    // そうでないなら、
    // NGX_PREFIX が指定されているならそれを使う
    // そうでないなら何もしない
#if (NGX_WIN32)
    if (name[1] != ':') {
#else
    if (name[0] != '/') {
#endif

        // 
        if (prefix) {
            plen = ngx_strlen(prefix);

        } else {
#ifdef NGX_PREFIX
            prefix = (u_char *) NGX_PREFIX;
            plen = ngx_strlen(prefix);
#else
            plen = 0;
#endif
        }

        if (plen) {
            // ここで別個にメモリ割り当てを行うので、
            // NGX_ERROR_LOG_PATH は変更されない
            name = malloc(plen + nlen + 2);
            if (name == NULL) {
                return NULL;
            }

            p = ngx_cpymem(name, prefix, plen);

            if (!ngx_path_separator(*(p - 1))) {
                *p++ = '/';
            }

            ngx_cpystrn(p, (u_char *) NGX_ERROR_LOG_PATH, nlen + 1);

            p = name;
        }
    }

    // ファイルを開く
    // 追加書き込みモード（write() のたびにオフセットを末尾へ）
    ngx_log_file.fd = ngx_open_file(name, NGX_FILE_APPEND,
                                    NGX_FILE_CREATE_OR_OPEN,
                                    NGX_FILE_DEFAULT_ACCESS);

    if (ngx_log_file.fd == NGX_INVALID_FILE) {
        ngx_log_stderr(ngx_errno,
                       "[alert] could not open error log file: "
                       ngx_open_file_n " \"%s\" failed", name);
#if (NGX_WIN32)
        ngx_event_log(ngx_errno,
                       "could not open error log file: "
                       ngx_open_file_n " \"%s\" failed", name);
#endif
        // src/os/unix/ngx_files.h で定義されている
        ngx_log_file.fd = ngx_stderr;
    }

    if (p) {
        ngx_free(p);
    }

    return &ngx_log;
}


/**
 * @brief
 *     サイクルの new_log が既に書き込み先をオープンしていないなら、
 *     NGX_ERROR_LOG_PATH でオープンして初期化する
 * @param[in:out]
 *     cycle: 処理対象のサイクル
 * @retval
 *     NGX_OK: 成功
 *     NGX_ERROR: 失敗
 */
ngx_int_t
ngx_log_open_default(ngx_cycle_t *cycle)
{
    ngx_log_t         *log;
    static ngx_str_t   error_log = ngx_string(NGX_ERROR_LOG_PATH);

    // もうすでに new_log がファイルを開いていたら終了
    // 初期化時には、ここは呼ばれないはず
    /**
     * @brief
     *     ログリストの要素を先頭から参照して、fd を持っていたらそれを返す
     * @param[in]
     *     head: ログリストの先頭
     * @retval
     *     NULL: 見つからなかった
     *     otherwise: 見つかった fd
     * @detail
     *     そもそも head が空でも NULL が返る
     */
    if (ngx_log_get_file_log(&cycle->new_log) != NULL) {
        return NGX_OK;
    }

    if (cycle->new_log.log_level != 0) {
        /* there are some error logs, but no files */

        log = ngx_pcalloc(cycle->pool, sizeof(ngx_log_t));
        if (log == NULL) {
            return NGX_ERROR;
        }

    } else {
        /* no error logs at all */
        log = &cycle->new_log;
    }

    log->log_level = NGX_LOG_ERR;

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
    log->file = ngx_conf_open_file(cycle, &error_log);
    if (log->file == NULL) {
        return NGX_ERROR;
    }

    // すでに new_log に１つ以上のログが存在しているケース
    // 初期化時は、ここは呼ばれない
    if (log != &cycle->new_log) {
        /**
         * @brief
         *     新しいログ構造体を既存のログ構造体リストに挿入する
         *     なお、緊急度による並び順に縛りがあるので、場合によっては log と new_log の位置を入れ替える
         *     new_log の緊急度が log より低い場合は、new_log の次に log を配置する
         *         そして、log と new_log が指すものを入れ替える
         *     そうでないなら、ログリストのうち、緊急度が高い順にうまく並ぶようにいい位置に挿入する
         * @para[out]
         *     log: こいつが保持するリストに挿入する
         * @param[in]
         *     new_log: こいつを挿入する
         */
        ngx_log_insert(&cycle->new_log, log);
    }

    return NGX_OK;
}


/**
 * @brief
 *     ログリストの要素を先頭から参照して、最初に見つかった fd を標準エラー出力に複製する
 * @param[out:in]
 *     cycle: このサイクルが所持するログリストが処理対象
 * @retval
 *     NGX_OK: 成功
 *     NGX_ERROR: 失敗
 */
ngx_int_t
ngx_log_redirect_stderr(ngx_cycle_t *cycle)
{
    ngx_fd_t  fd;

    if (cycle->log_use_stderr) {
        return NGX_OK;
    }

    /* file log always exists when we are called */
    /**
     * @brief
     *     ログリストの要素を先頭から参照して、fd を持っていたらそれを返す
     * @param[in]
     *     head: ログリストの先頭
     * @retval
     *     NULL: 見つからなかった
     *     otherwise: 見つかった fd
     * @detail
     *     そもそも head が空でも NULL が返る
     */
    fd = ngx_log_get_file_log(cycle->log)->file->fd;

    // 標準エラー出力ではないなら、dup() で複製して標準エラー出力を指すようにする
    if (fd != ngx_stderr) {
        // 異なるハンドルで同一ファイルを指すために dup() を使用する
        if (ngx_set_stderr(fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_set_stderr_n " failed");

            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


/**
 * @brief
 *     ログリストの要素を先頭から参照して、fd を持っていたらそれを返す
 * @param[in]
 *     head: ログリストの先頭
 * @retval
 *     NULL: 見つからなかった
 *     otherwise: 見つかった fd
 * @detail
 *     そもそも head が空でも NULL が返る
 */
ngx_log_t *
ngx_log_get_file_log(ngx_log_t *head)
{
    ngx_log_t  *log;

    for (log = head; log; log = log->next) {
        if (log->file != NULL) {
            return log;
        }
    }

    return NULL;
}


/**
 * @brief
 *     第１引数トークン群からログレベル情報を探して、ログのレベルを設定する
 * @param[in]
 *     cf: トークン群
 *     log: ログ
 * @retval
 *     NGX_CONF_OK: 成功
 *     NGX_CONF_ERROR: 失敗
 * @pre
 *     err_levels[] にエラーレベル情報がセットされている
 *     degug_levels[] にデバッグレベル情報がセットされている
 * @post
 *     log->log_level にログレベルがセットされている
 */
static char *
ngx_log_set_levels(ngx_conf_t *cf, ngx_log_t *log)
{
    ngx_uint_t   i, n, d, found;
    ngx_str_t   *value;

    if (cf->args->nelts == 2) {
        log->log_level = NGX_LOG_ERR;
        return NGX_CONF_OK;
    }

    value = cf->args->elts;

    // ファイル名の後にログレベル情報が続いていたら、それを処理する
    for (i = 2; i < cf->args->nelts; i++) {
        found = 0;

        // それは正当なログレベルなので、ログのレベルがまだ未設定なら適用して終了
        for (n = 1; n <= NGX_LOG_DEBUG; n++) {
            if (ngx_strcmp(value[i].data, err_levels[n].data) == 0) {

                if (log->log_level != 0) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "duplicate log level \"%V\"",
                                       &value[i]);
                    return NGX_CONF_ERROR;
                }

                log->log_level = n;
                found = 1;
                break;
            }
        }

        for (n = 0, d = NGX_LOG_DEBUG_FIRST; d <= NGX_LOG_DEBUG_LAST; d <<= 1) {
            if (ngx_strcmp(value[i].data, debug_levels[n++]) == 0) {
                if (log->log_level & ~NGX_LOG_DEBUG_ALL) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "invalid log level \"%V\"",
                                       &value[i]);
                    return NGX_CONF_ERROR;
                }

                log->log_level |= d;
                found = 1;
                break;
            }
        }


        if (!found) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid log level \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }
    }

    if (log->log_level == NGX_LOG_DEBUG) {
        log->log_level = NGX_LOG_DEBUG_ALL;
    }

    return NGX_CONF_OK;
}


/**
 * @brief
 *     設定ファイルの error_log ディレクティブを処理する
 * @param[in]
 *     cf: 設定をつかさどる構造体
 *     cmd: コマンド
 *     conf: この設定ファイルに格納する
 */
static char *
ngx_error_log(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_log_t  *dummy;

    dummy = &cf->cycle->new_log;

    return ngx_log_set_log(cf, &dummy);
}


/**
 * @brief
 *     新しいログをうまい具合にリストに追加する
 * @retval
 *     NGX_CONF_OK: 成功
 *     NGX_CONF_ERROR: 失敗
 */
char *
ngx_log_set_log(ngx_conf_t *cf, ngx_log_t **head)
{
    ngx_log_t          *new_log;
    ngx_str_t          *value, name;
    ngx_syslog_peer_t  *peer;

    // new_log リストに中身がないなら新しく割り当ててそいつを対象に選択、そして new_log リストの先頭にする
    // new_log リストに中身があるが、ログレベルが 0 ならそいつを対象に選択
    // new_log リストに中身があるが、ログレベルが 0 でないなら新しく割り当ててそいつを選択
    if (*head != NULL && (*head)->log_level == 0) {
        new_log = *head;

    } else {

        new_log = ngx_pcalloc(cf->pool, sizeof(ngx_log_t));
        if (new_log == NULL) {
            return NGX_CONF_ERROR;
        }

        if (*head == NULL) {
            *head = new_log;
        }
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "stderr") == 0) {
        // ngx_conf_open_file() は name が NULL だと標準エラー出力を返すので
        ngx_str_null(&name);
        // ログは標準エラー出力にエラーを書き込む
        cf->cycle->log_use_stderr = 1;

        /**
         * @brief
         *     第２引数 name で指定されたファイルを開いて返す
         *     name が NULL の場合は標準エラー出力を開いて cycle->open_files リストにプッシュする
         *     name と該当するファイルがなかった場合は fd に NGX_INVALID_FD がセットされたファイルが返る
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
        new_log->file = ngx_conf_open_file(cf->cycle, &name);
        if (new_log->file == NULL) {
            return NGX_CONF_ERROR;
        }

    } else if (ngx_strncmp(value[1].data, "memory:", 7) == 0) {

        // デバッグ用の設定っぽい
        // デバッグ関連はとりあえず飛ばす
#if (NGX_DEBUG)
        size_t                 size, needed;
        ngx_pool_cleanup_t    *cln;
        ngx_log_memory_buf_t  *buf;

        value[1].len -= 7;
        value[1].data += 7;

        needed = sizeof("MEMLOG  :" NGX_LINEFEED)
                 + cf->conf_file->file.name.len
                 + NGX_SIZE_T_LEN
                 + NGX_INT_T_LEN
                 + NGX_MAX_ERROR_STR;

        size = ngx_parse_size(&value[1]);

        if (size == (size_t) NGX_ERROR || size < needed) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid buffer size \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }

        buf = ngx_pcalloc(cf->pool, sizeof(ngx_log_memory_buf_t));
        if (buf == NULL) {
            return NGX_CONF_ERROR;
        }

        buf->start = ngx_pnalloc(cf->pool, size);
        if (buf->start == NULL) {
            return NGX_CONF_ERROR;
        }

        buf->end = buf->start + size;

        buf->pos = ngx_slprintf(buf->start, buf->end, "MEMLOG %uz %V:%ui%N",
                                size, &cf->conf_file->file.name,
                                cf->conf_file->line);

        ngx_memset(buf->pos, ' ', buf->end - buf->pos);

        cln = ngx_pool_cleanup_add(cf->pool, 0);
        if (cln == NULL) {
            return NGX_CONF_ERROR;
        }

        cln->data = new_log;
        cln->handler = ngx_log_memory_cleanup;

        new_log->writer = ngx_log_memory_writer;
        new_log->wdata = buf;

#else

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "nginx was built without debug support");
        // 何か知らんけど実稼働では強制エラー
        return NGX_CONF_ERROR;
#endif

    } else if (ngx_strncmp(value[1].data, "syslog:", 7) == 0) {
        peer = ngx_pcalloc(cf->pool, sizeof(ngx_syslog_peer_t));
        if (peer == NULL) {
            return NGX_CONF_ERROR;
        }

        if (ngx_syslog_process_conf(cf, peer) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;
        }

        // syslog が指定されていたときにのみ、writer 値が有効化される模様
        // peer とは？
        new_log->writer = ngx_syslog_writer;
        new_log->wdata = peer;

    } else {
        // memory: syslog: stderr: のどれでもないなら、それはファイルのパスである
        // 開くことを検討する
        new_log->file = ngx_conf_open_file(cf->cycle, &value[1]);
        if (new_log->file == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    /**
     * @brief
     *     第１引数トークン群からログレベル情報を探して、ログのレベルを設定する
     * @param[in]
     *     cf: トークン群
     *     log: ログ
     * @retval
     *     NGX_CONF_OK: 成功
     *     NGX_CONF_ERROR: 失敗
     * @pre
     *     err_levels[] にエラーレベル情報がセットされている
     *     degug_levels[] にデバッグレベル情報がセットされている
     * @post
     *     log->log_level にログレベルがセットされている
     */
    if (ngx_log_set_levels(cf, new_log) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }

    // 必要であれば new_log に追加
    if (*head != new_log) {
        ngx_log_insert(*head, new_log);
    }

    return NGX_CONF_OK;
}


/**
 * @brief
 *     新しいログ構造体を既存のログ構造体リストに挿入する
 *     なお、緊急度による並び順に縛りがあるので、場合によっては log と new_log の位置を入れ替える
 *     new_log の緊急度が log より低い場合は、new_log の次に log を配置する
 *         そして、log と new_log が指すものを入れ替える
 *     そうでないなら、ログリストのうち、緊急度が高い順にうまく並ぶようにいい位置に挿入する
 * @para[out]
 *     log: こいつが保持するリストに挿入する
 * @param[in]
 *     new_log: こいつを挿入する
 */
static void
ngx_log_insert(ngx_log_t *log, ngx_log_t *new_log)
{
    ngx_log_t  tmp;

    if (new_log->log_level > log->log_level) {

        /*
         * list head address is permanent, insert new log after
         * head and swap its contents with head
         */

        tmp = *log;
        *log = *new_log;
        *new_log = tmp;

        log->next = new_log;
        return;
    }

    while (log->next) {
        if (new_log->log_level > log->next->log_level) {
            new_log->next = log->next;
            log->next = new_log;
            return;
        }

        log = log->next;
    }

    log->next = new_log;
}


#if (NGX_DEBUG)


  /**
   * @brief
   *     バッファの内容をログ構造体のバッファへ書き込む
   * @param[out]
   *     log: このログの wdata メンバ（メモリバッファ）に書き込む
   * @param[in]
   *     level: 使用しない
   *     buf: 書き込みたいデータ
   *     len: 書き込みたいサイズ
   */
  static void
  ngx_log_memory_writer(ngx_log_t *log, ngx_uint_t level, u_char *buf,
      size_t len)
  {
      u_char                *p;
      size_t                 avail, written;
      ngx_log_memory_buf_t  *mem;

      mem = log->wdata;

      if (mem == NULL) {
          return;
      }

      // オフセットを進める
      written = ngx_atomic_fetch_add(&mem->written, len);

      // （これまでに書き込んだ量 % バッファの残り容量）
      //  ランダムに位置をずらしている？
      p = mem->pos + written % (mem->end - mem->pos);

      avail = mem->end - p;

      // 空き容量が足りているなら書き込む
      // そうでないなら、書きこめるところまで書き込んで、残りは mem->pos に書き込む
      if (avail >= len) {
          ngx_memcpy(p, buf, len);

      } else {
          ngx_memcpy(p, buf, avail);
          ngx_memcpy(mem->pos, buf + avail, len - avail);
      }
  }


  /**
   * @brief
   *     wdata メンバを NULL に置き換える（開放はしない、おそらくプールの開放時にまとめて開放するため）
   * @param[in]
   *     data: 処理対象のログ構造体
   */
  static void
  ngx_log_memory_cleanup(void *data)
  {
      ngx_log_t *log = data;

      ngx_log_debug0(NGX_LOG_DEBUG_CORE, log, 0, "destroy memory log buffer");

      log->wdata = NULL;
  }

#endif
