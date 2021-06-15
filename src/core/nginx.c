
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>


static void ngx_show_version_info(void);
static ngx_int_t ngx_add_inherited_sockets(ngx_cycle_t *cycle);
static void ngx_cleanup_environment(void *data);
static ngx_int_t ngx_get_options(int argc, char *const *argv);
static ngx_int_t ngx_process_options(ngx_cycle_t *cycle);
static ngx_int_t ngx_save_argv(ngx_cycle_t *cycle, int argc, char *const *argv);
static void *ngx_core_module_create_conf(ngx_cycle_t *cycle);
static char *ngx_core_module_init_conf(ngx_cycle_t *cycle, void *conf);
static char *ngx_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_set_env(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_set_priority(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_set_cpu_affinity(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_set_worker_processes(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_load_module(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
#if (NGX_HAVE_DLOPEN)
static void ngx_unload_module(void *data);
#endif


static ngx_conf_enum_t  ngx_debug_points[] = {
    { ngx_string("stop"), NGX_DEBUG_POINTS_STOP },
    { ngx_string("abort"), NGX_DEBUG_POINTS_ABORT },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_core_commands[] = {

    // 関係ない
    // nginx をデーモンとして起動するかどうか、主に開発者向け
    { ngx_string("daemon"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_core_conf_t, daemon),
      NULL },

    // 関係ない（常にシングルなので）
    // ワーカプロセスを起動させるかどうか、主に開発者向け
    { ngx_string("master_process"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_core_conf_t, master),
      NULL },

    // 〇
    // ワーカプロセスの時間制度を落とす代わりに実行負荷を下げる
    { ngx_string("timer_resolution"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_core_conf_t, timer_resolution),
      NULL },

    // 関係ない（ファイルは使わない）
    // ぷろせすID を記録するファイルのパス
    { ngx_string("pid"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, pid),
      NULL },

    // 関係ない
    // 複数ワーカプロセスが accept() を互いにロックして行うので
    // ロックをファイルを用いて行うシステムにおいてここでパスを指定する
    { ngx_string("lock_file"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, lock_file),
      NULL },

    // 関係ない
    // ワーカプロセスの数を指定する
    { ngx_string("worker_processes"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_worker_processes,
      0,
      0,
      NULL },

    // 関係ない
    // デバッグに使用する
    { ngx_string("debug_points"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      0,
      offsetof(ngx_core_conf_t, debug_points),
      &ngx_debug_points },

    // ？
    // このプロセスのユーザ名を指定する
    { ngx_string("user"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE12,
      ngx_set_user,
      0,
      0,
      NULL },

    // 〇
    // nice コマンドのようにワーカプロセスの実行優先度を指定する
    { ngx_string("worker_priority"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_priority,
      0,
      0,
      NULL },

    // 〇
    // どのCPUでワーカプロセスを実行するか指定する
    { ngx_string("worker_cpu_affinity"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_1MORE,
      ngx_set_cpu_affinity,
      0,
      0,
      NULL },

    { ngx_string("worker_rlimit_nofile"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_core_conf_t, rlimit_nofile),
      NULL },

    { ngx_string("worker_rlimit_core"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_off_slot,
      0,
      offsetof(ngx_core_conf_t, rlimit_core),
      NULL },

    { ngx_string("worker_shutdown_timeout"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_core_conf_t, shutdown_timeout),
      NULL },

    { ngx_string("working_directory"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, working_directory),
      NULL },

    { ngx_string("env"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_env,
      0,
      0,
      NULL },

    { ngx_string("load_module"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_load_module,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_core_module_ctx = {
    ngx_string("core"),
    ngx_core_module_create_conf,
    ngx_core_module_init_conf
};


ngx_module_t  ngx_core_module = {
    NGX_MODULE_V1,
    &ngx_core_module_ctx,                  /* module context */
    ngx_core_commands,                     /* module directives */
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


static ngx_uint_t   ngx_show_help;
static ngx_uint_t   ngx_show_version;
static ngx_uint_t   ngx_show_configure;
static u_char      *ngx_prefix;
static u_char      *ngx_conf_file;
static u_char      *ngx_conf_params;
static char        *ngx_signal;


static char **ngx_os_environ;


int ngx_cdecl
main(int argc, char *const *argv)
{
    ngx_buf_t        *b;
    ngx_log_t        *log;
    ngx_uint_t        i;
    ngx_cycle_t      *cycle, init_cycle;
    ngx_conf_dump_t  *cd;
    ngx_core_conf_t  *ccf;

    // malloc の動作をデバッグ用のものに設定する必要があるかどうかをチェック
    // FreeBSD と Darwin だとこのような動作がある、他は空のマクロ関数として定義されている
    // 外部読み込みあり
    //     環境変数
    // システム依存あり
    //     getenv()
    // マクロ
    //     NGX_DEBUG_MALLOC
    //         デバッグ用動作にするなら Yes
    // @post
    //     ngx_debug_malloc に 0 か 1（有効）が格納される
    ngx_debug_init();

    /**
     * @brief
     *     OS からエラー情報を取得して静的変数へセットする
     * @post
     *     ngx_strerror() が使用可能になる
     */
    // 外部読み込みなし
    // システム依存あり
    //     strerror()
    //     strlen()
    //     memcpy()
    //     malloc()
    // マクロ
    //     NGX_SYS_NERR
    //         nginx が使用するエラーの種類
    //     NGX_MEMCPY_LIMIT
    //         memcpy() できるサイズを制限する
    if (ngx_strerror_init() != NGX_OK) {
        return 1;
    }

    /**
     * @brief
     *     コマンドライン引数を解析して、内容を受けて静的変数へ値を代入する
     * @post
     *     ngx_show_version
     *     ngx_show_help
     *     ngx_test_config
     *     ngx_show_configure
     *     ngx_dump_config
     *     ngx_quiet_mode
     *     ngx_prefix
     *     ngx_conf_file
     *     ngx_conf_params
     *     ngx_signal
     *     ngx_process
     *     ↑の静的変数に、コマンドライン引数に応じて正しい値がセットされる
     */
    // 外部読み込みなし
    // システム依存あり
    //     strcmp()
    // マクロなし
    if (ngx_get_options(argc, argv) != NGX_OK) {
        return 1;
    }

    // ngx_show_version は↑のngx_get_options() で設定される
    // 呼び出さなくてもよいので飛ばす・・・
    if (ngx_show_version) {
        ngx_show_version_info();

        if (!ngx_test_config) {
            return 0;
        }
    }

    // この後、ngx_os_init() で初期化される
    /* TODO */ ngx_max_sockets = -1;

    /**
     * @brief
     *     5 種類の文字列時間キャッシュと通常時間キャッシュを初期化する
     * @post
     *     ngx_cached_time が 最新の時間キャッシュを指している
     *
     *     ngx_cached_err_log_time     文字列時間キャッシュが "1970/09/28 12:00:00"           でサイズのみ初期化された
     *     ngx_cached_http_time        文字列時間キャッシュが "Mon, 28 Sep 1970 06:00:00 GMT" でサイズのみ初期化された
     *     ngx_cached_http_log_time    文字列時間キャッシュが "28/Sep/1970:12:00:00 +0600"    でサイズのみ初期化された
     *     ngx_cached_http_log_iso8601 文字列時間キャッシュが "1970-09-28T12:00:00+06:00"     でサイズのみ初期化された
     *     ngx_cached_syslog_time      文字列時間キャッシュが "Sep 28 12:00:00"               でサイズのみ初期化された
     * @syscall
     *     gettimeofday()
     *     va_start()
     *     va_arg()
     *     va_end()
     * @macro
     *     NGX_TIME_SLOT
     *     NGX_HAVE_GETTIMEZONE
     *     NGX_HAVE_GMTOFF
     */
    // 外部読み込みなし
    ngx_time_init();

#if (NGX_PCRE)
    // OK
    ngx_regex_init();
#endif
    // プロセス ID を取得
    // 外部読み込みなし
    // システム依存なし
    // マクロなし
    ngx_pid = ngx_getpid();
    // 親プロセス ID を取得
    // 外部読み込みなし
    // システム依存なし
    // マクロなし
    ngx_parent = ngx_getppid();

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
    log = ngx_log_init(ngx_prefix);
    if (log == NULL) {
        return 1;
    }

    /* STUB */
#if (NGX_OPENSSL)
    ngx_ssl_init(log);
#endif

    /*
     * init_cycle->log is required for signal handlers and
     * ngx_process_options()
     */

    // 初期化用のサイクルを生成（サイクルの初期化は、別のサイクルをもとに行われるので）
    ngx_memzero(&init_cycle, sizeof(ngx_cycle_t));
    // サイクルのログを初期化
    init_cycle.log = log;
    ngx_cycle = &init_cycle;

    // サイクルのプールを初期化
    init_cycle.pool = ngx_create_pool(1024, log);
    if (init_cycle.pool == NULL) {
        return 1;
    }

    /**
     * @brief 静的変数群にコマンドライン引数と環境変数について値をセットする
     * @param[in] cycle サイクル（この構造体が保有しているログを使用する）
     * @param[in] argc  セットしたい「コマンドライン引数の数」
     * @param[in] argv  セットしたいコマンドライン引数
     * @retval
     *     NGX_OK    成功
     *     NGX_ERROR 失敗
     * @pre
     *     environ に環境変数の情報がセットされている（こいつはシステムが自動で準備する）
     * @post
     *     ngx_os_argv はコマンドラインそのもの（argv）を指している（つまり、直後に environ が続く可能性がある）
     *     ngx_argc にコマンドライン引数の数がセットされている
     *     ngx_argv にコマンドライン引数のハードコピーが格納されている
     *     ngx_os_environ に環境変数の情報がセットされている
     */
    // 静的変数群にコマンドライン引数と環境変数について値をセットする
    if (ngx_save_argv(&init_cycle, argc, argv) != NGX_OK) {
        return 1;
    }

    // cycle の conf_prefix, prefix, conf_file, conf_param に値をセットする
    if (ngx_process_options(&init_cycle) != NGX_OK) {
        return 1;
    }

    /**
     * @brief
     *     OS に依存する初期化処理を一挙に行う
     * @param[in]
     *     log: ログの出力に使用
     * @retval
     *     NGX_OK: 成功
     *     NGX_ERROR: 失敗
     * @post
     *     静的変数 ngx_linux_kern_ostype に os のタイプ情報が格納されている
     *     静的変数 ngx_linux_kern_osrelease に os のリリース情報が格納されている
     *     静的変数 ngx_os_io に OS 依存の入出力処理群がセットされている
     *     静的変数 ngx_os_argv_last が ngx_os_argv の最後の番地を指している
     *     静的変数 environ が必要であればメモリ割り当て直しされている
     *     静的変数 ngx_pagsize にページサイズが格納されている
     *     静的変数 ngx_cacheline_size にキャッシュラインサイズが格納されている
     *     静的変数 ngx_pagesize_shift に ngx_pagesize が 2 の何乗か格納されている
     *     静的変数 ngx_ncpu に現在利用可能なプロセッサの数が格納されている
     *     静的変数 ngx_max_sockets にオープン可能なソケットの最大数が格納されている
     *     静的変数 ngx_inherited_nonblocking に fcntl() でいちいちノンブロッキングモードに変更する必要があるか格納されている
     *     os のランダム関数の種による初期化が完了している
     */
    if (ngx_os_init(log) != NGX_OK) {
        return 1;
    }

    /*
     * ngx_crc32_table_init() requires ngx_cacheline_size set in ngx_os_init()
     */
    /**
     * @brief
     *     crc32 が内部で使用する短い方のテーブルをページサイズ（キャッシュラインサイズ）上に乗せる
     * @retval
     *     NGX_OK: 成功
     *     NGX_ERROR: 失敗
     * @pre
     *     ngx_cacheline_size にページサイズ（キャッシュラインサイズ）が格納されている
     */
    if (ngx_crc32_table_init() != NGX_OK) {
        return 1;
    }

    /*
     * ngx_slab_sizes_init() requires ngx_pagesize set in ngx_os_init()
     */
    /**
     * @brief
     *     スラブが使用する静的変数群をページサイズをもとに初期化する
     * @pre
     *     ngx_pagesize に本プロセス実行環境におけるページサイズが格納されている
     * @post
     *     ngx_slab_max_size に値が格納されている
     *     ngx_slab_exact_size に値が格納されている
     *     ngx_slab_exact_shift に値が格納されている
     */
    ngx_slab_sizes_init();

    /**
     * @brief
     *     環境変数 NGINX_VAR に fd がセットされていた場合、サイクルに受け継ぐ
     * @param[in:out]
     *     cycle: このサイクルの listening メンバにセットする
     * @retval
     *     NGX_OK: 正常終了
     *     NGX_ERROR: 異常終了
     */
    // シングルプロセスモードでは何もしない
    if (ngx_add_inherited_sockets(&init_cycle) != NGX_OK) {
        return 1;
    }

    /**
     * @brief
     *     ngx_modules.c にて定義される静的モジュール群について
     *     そこで置かれている ngx_modules[] 内の各要素について、何番目に置かれているかを各モジュールの
     *     index メンバに格納する
     *     また、同ファイルに格納されている ngx_module_names[] を参照して name メンバを初期化する
     *     最後に、静的モジュールと静的動的合わせての最大合計モジュールを計算する
     * @pre
     *     ngx_modules.c にて ngx_modules[] と ngx_module_names[] が定義されている
     * @post
     *     ngx_modules[] の各要素の index メンバに添え字（ngx_modules[] 内で上から何番目に置かれているか）が格納されている
     *     ngx_modules[] の各要素の name メンバにモジュール名がセットされている
     *     静的変数 ngx__modules_n に静的モジュールの総数が格納されている
     *     静的変数 ngx_max_module に静的＋動的モジュールの最大合計数が格納されている
     * @retval
     *     NGX_OK: 必ずこれが返る
     */
    if (ngx_preinit_modules() != NGX_OK) {
        return 1;
    }

    // サイクルの初期化
    // ここで設定ファイルをパースする
    cycle = ngx_init_cycle(&init_cycle);
    if (cycle == NULL) {
        if (ngx_test_config) {
            ngx_log_stderr(0, "configuration file %s test failed",
                           init_cycle.conf_file.data);
        }

        return 1;
    }

    // 実行オプションで t か T が指定された場合は ngx_test_config が 1、それ以外は 0
    // 普通は実行されない
    if (ngx_test_config) {
        if (!ngx_quiet_mode) {
            ngx_log_stderr(0, "configuration file %s test is successful",
                           cycle->conf_file.data);
        }

        if (ngx_dump_config) {
            cd = cycle->config_dump.elts;

            for (i = 0; i < cycle->config_dump.nelts; i++) {

                ngx_write_stdout("# configuration file ");
                (void) ngx_write_fd(ngx_stdout, cd[i].name.data,
                                    cd[i].name.len);
                ngx_write_stdout(":" NGX_LINEFEED);

                b = cd[i].buffer;

                (void) ngx_write_fd(ngx_stdout, b->pos, b->last - b->pos);
                ngx_write_stdout(NGX_LINEFEED);
            }
        }

        return 0;
    }

    // 実行オプションで --stop などのシグナル文字列が指定された場合は、その文字列が ngx_signal に入っている
    // 普通は実行されない
    if (ngx_signal) {
        return ngx_signal_process(cycle, ngx_signal);
    }

    /**
     * @brief
     *     freebsd や solaris といった OS 特有の設定事項をログへ出力する
     * @param[in]
     *     log: 出力先のログ
     */
    ngx_os_status(cycle->log);

    // 先ほど生成したサイクルを静的変数として管理
    ngx_cycle = cycle;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    // ディレクティブで複数プロセスモードが指定されていたら移行
    // 今回は必要ない
    if (ccf->master && ngx_process == NGX_PROCESS_SINGLE) {
        ngx_process = NGX_PROCESS_MASTER;
    }

#if !(NGX_WIN32)

    if (ngx_init_signals(cycle->log) != NGX_OK) {
        return 1;
    }

    // 今回はここは実行されない
    if (!ngx_inherited && ccf->daemon) {
        if (ngx_daemon(cycle->log) != NGX_OK) {
            return 1;
        }

        ngx_daemonized = 1;
    }

    if (ngx_inherited) {
        ngx_daemonized = 1;
    }

#endif

    if (ngx_create_pidfile(&ccf->pid, cycle->log) != NGX_OK) {
        return 1;
    }

    if (ngx_log_redirect_stderr(cycle) != NGX_OK) {
        return 1;
    }

    if (log->file->fd != ngx_stderr) {
        if (ngx_close_file(log->file->fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_close_file_n " built-in log failed");
        }
    }

    ngx_use_stderr = 0;

    if (ngx_process == NGX_PROCESS_SINGLE) {
        ngx_single_process_cycle(cycle);

    } else {
        ngx_master_process_cycle(cycle);
    }

    return 0;
}


static void
ngx_show_version_info(void)
{
    ngx_write_stderr("nginx version: " NGINX_VER_BUILD NGX_LINEFEED);

    if (ngx_show_help) {
        ngx_write_stderr(
            "Usage: nginx [-?hvVtTq] [-s signal] [-c filename] "
                         "[-p prefix] [-g directives]" NGX_LINEFEED
                         NGX_LINEFEED
            "Options:" NGX_LINEFEED
            "  -?,-h         : this help" NGX_LINEFEED
            "  -v            : show version and exit" NGX_LINEFEED
            "  -V            : show version and configure options then exit"
                               NGX_LINEFEED
            "  -t            : test configuration and exit" NGX_LINEFEED
            "  -T            : test configuration, dump it and exit"
                               NGX_LINEFEED
            "  -q            : suppress non-error messages "
                               "during configuration testing" NGX_LINEFEED
            "  -s signal     : send signal to a master process: "
                               "stop, quit, reopen, reload" NGX_LINEFEED
#ifdef NGX_PREFIX
            "  -p prefix     : set prefix path (default: " NGX_PREFIX ")"
                               NGX_LINEFEED
#else
            "  -p prefix     : set prefix path (default: NONE)" NGX_LINEFEED
#endif
            "  -c filename   : set configuration file (default: " NGX_CONF_PATH
                               ")" NGX_LINEFEED
            "  -g directives : set global directives out of configuration "
                               "file" NGX_LINEFEED NGX_LINEFEED
        );
    }

    if (ngx_show_configure) {

#ifdef NGX_COMPILER
        ngx_write_stderr("built by " NGX_COMPILER NGX_LINEFEED);
#endif

#if (NGX_SSL)
        if (ngx_strcmp(ngx_ssl_version(), OPENSSL_VERSION_TEXT) == 0) {
            ngx_write_stderr("built with " OPENSSL_VERSION_TEXT NGX_LINEFEED);
        } else {
            ngx_write_stderr("built with " OPENSSL_VERSION_TEXT
                             " (running with ");
            ngx_write_stderr((char *) (uintptr_t) ngx_ssl_version());
            ngx_write_stderr(")" NGX_LINEFEED);
        }
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
        ngx_write_stderr("TLS SNI support enabled" NGX_LINEFEED);
#else
        ngx_write_stderr("TLS SNI support disabled" NGX_LINEFEED);
#endif
#endif

        ngx_write_stderr("configure arguments:" NGX_CONFIGURE NGX_LINEFEED);
    }
}


/**
 * @brief
 *     環境変数 NGINX_VAR に fd がセットされていた場合、サイクルに受け継ぐ
 * @param[in:out]
 *     cycle: このサイクルの listening メンバにセットする
 * @retval
 *     NGX_OK: 正常終了
 *     NGX_ERROR: 異常終了
 */
static ngx_int_t
ngx_add_inherited_sockets(ngx_cycle_t *cycle)
{
    u_char           *p, *v, *inherited;
    ngx_int_t         s;
    ngx_listening_t  *ls;

    // 環境変数 NGINX_VAR の値を取得する
    // この環境変数は複数プロセスモードでのみ、ngx_exec_new_binary() 内で定義される
    // よって、シングルプロセスモードではここで終了する
    inherited = (u_char *) getenv(NGINX_VAR);

    // そんな値がないなら終了
    if (inherited == NULL) {
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                  "using inherited sockets from \"%s\"", inherited);

    if (ngx_array_init(&cycle->listening, cycle->pool, 10,
                       sizeof(ngx_listening_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (p = inherited, v = p; *p; p++) {
        if (*p == ':' || *p == ';') {
            s = ngx_atoi(v, p - v);
            if (s == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "invalid socket number \"%s\" in " NGINX_VAR
                              " environment variable, ignoring the rest"
                              " of the variable", v);
                break;
            }

            v = p + 1;

            ls = ngx_array_push(&cycle->listening);
            if (ls == NULL) {
                return NGX_ERROR;
            }

            ngx_memzero(ls, sizeof(ngx_listening_t));

            ls->fd = (ngx_socket_t) s;
        }
    }

    if (v != p) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "invalid socket number \"%s\" in " NGINX_VAR
                      " environment variable, ignoring", v);
    }

    ngx_inherited = 1;

    return ngx_set_inherited_sockets(cycle);
}


/**
 * @brief
 *     静的変数 environ とコアモジュール設定の environment を env ディレクティブで有効化されたもので厳選する
 * @param[in]
 *     cycle: コアモジュールの設定を取得する際などに使用
 *     last: 
 * @pre
 *     ngx_os_environ にシステムから渡された環境変数の情報がセットされている
 * @post
 *     コアモジュール設定の environment メンバが厳選済み
 *     静的変数 environ が厳選済み
 * @detail
 *     environ は env ディレクティブを元に厳選される
 *     ngx_os_environ はシステムから渡されたものをそのまま保持する
 */
char **
ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last)
{
    char                **p, **env;
    ngx_str_t            *var;
    ngx_uint_t            i, n;
    ngx_core_conf_t      *ccf;
    ngx_pool_cleanup_t   *cln;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    // 既にコアモジュールに環境変数がセットされているなら何もしない
    if (last == NULL && ccf->environment) {
        return ccf->environment;
    }

    var = ccf->env.elts;

    // env ディレクティブで登録した環境変数の中から TZ= を探す、見つかったら tz_found へ飛ぶ
    for (i = 0; i < ccf->env.nelts; i++) {
        if (ngx_strcmp(var[i].data, "TZ") == 0
            || ngx_strncmp(var[i].data, "TZ=", 3) == 0)
        {
            goto tz_found;
        }
    }

    var = ngx_array_push(&ccf->env);
    if (var == NULL) {
        return NULL;
    }

    // TZ がないなら無理やり作る
    var->len = 2;
    var->data = (u_char *) "TZ";

    var = ccf->env.elts;

tz_found:

    n = 0;

    // コアモジュールの env リストの各要素について
    // 〇〇= 形式か、システムの環境変数に一致するものがあってそちらでは 〇〇= 形式のものはカウントする
    // つまり、有効な環境変数の数をカウントする
    for (i = 0; i < ccf->env.nelts; i++) {

        // 〇〇= 形式だったらカウントして次へ
        if (var[i].data[var[i].len] == '=') {
            n++;
            continue;
        }

        // システムからコピーした環境変数を参照
        for (p = ngx_os_environ; *p; p++) {

            if (ngx_strncmp(*p, var[i].data, var[i].len) == 0
                && (*p)[var[i].len] == '=')
            {
                n++;
                break;
            }
        }
    }

    if (last) {
        env = ngx_alloc((*last + n + 1) * sizeof(char *), cycle->log);
        if (env == NULL) {
            return NULL;
        }

        *last = n;

    } else {
        // 環境変数群を格納するためにシステムからメモリを獲得してクリーンアップ関数を仕掛ける
        cln = ngx_pool_cleanup_add(cycle->pool, 0);
        if (cln == NULL) {
            return NULL;
        }

        env = ngx_alloc((n + 1) * sizeof(char *), cycle->log);
        if (env == NULL) {
            return NULL;
        }

        cln->handler = ngx_cleanup_environment;
        cln->data = env;
    }

    n = 0;

    for (i = 0; i < ccf->env.nelts; i++) {

        if (var[i].data[var[i].len] == '=') {
            env[n++] = (char *) var[i].data;
            continue;
        }

        for (p = ngx_os_environ; *p; p++) {

            if (ngx_strncmp(*p, var[i].data, var[i].len) == 0
                && (*p)[var[i].len] == '=')
            {
                env[n++] = *p;
                break;
            }
        }
    }

    // 最後に NULL をセット
    env[n] = NULL;

    if (last == NULL) {
        // コアモジュールの設定に環境変数群をセットする
        ccf->environment = env;
        // システムから渡されたものではなくて、今、生成したコピーで上書き（nginx が使用するものだけ）
        environ = env;
    }

    return env;
}


static void
ngx_cleanup_environment(void *data)
{
    char  **env = data;

    if (environ == env) {

        /*
         * if the environment is still used, as it happens on exit,
         * the only option is to leak it
         */

        return;
    }

    ngx_free(env);
}


// ngx_master_process_cycle() 内でのみ実行される
// よって、今回は決して実行されない
ngx_pid_t
ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv)
{
    char             **env, *var;
    u_char            *p;
    ngx_uint_t         i, n;
    ngx_pid_t          pid;
    ngx_exec_ctx_t     ctx;
    ngx_core_conf_t   *ccf;
    ngx_listening_t   *ls;

    ngx_memzero(&ctx, sizeof(ngx_exec_ctx_t));

    ctx.path = argv[0];
    ctx.name = "new binary process";
    ctx.argv = argv;

    n = 2;
    env = ngx_set_environment(cycle, &n);
    if (env == NULL) {
        return NGX_INVALID_PID;
    }

    var = ngx_alloc(sizeof(NGINX_VAR)
                    + cycle->listening.nelts * (NGX_INT32_LEN + 1) + 2,
                    cycle->log);
    if (var == NULL) {
        ngx_free(env);
        return NGX_INVALID_PID;
    }

    p = ngx_cpymem(var, NGINX_VAR "=", sizeof(NGINX_VAR));

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        p = ngx_sprintf(p, "%ud;", ls[i].fd);
    }

    *p = '\0';

    env[n++] = var;

#if (NGX_SETPROCTITLE_USES_ENV)

    /* allocate the spare 300 bytes for the new binary process title */

    env[n++] = "SPARE=XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

#endif

    env[n] = NULL;

#if (NGX_DEBUG)
    {
    char  **e;
    for (e = env; *e; e++) {
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0, "env: %s", *e);
    }
    }
#endif

    ctx.envp = (char *const *) env;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ngx_rename_file(ccf->pid.data, ccf->oldpid.data) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_rename_file_n " %s to %s failed "
                      "before executing new binary process \"%s\"",
                      ccf->pid.data, ccf->oldpid.data, argv[0]);

        ngx_free(env);
        ngx_free(var);

        return NGX_INVALID_PID;
    }

    pid = ngx_execute(cycle, &ctx);

    if (pid == NGX_INVALID_PID) {
        if (ngx_rename_file(ccf->oldpid.data, ccf->pid.data)
            == NGX_FILE_ERROR)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_rename_file_n " %s back to %s failed after "
                          "an attempt to execute new binary process \"%s\"",
                          ccf->oldpid.data, ccf->pid.data, argv[0]);
        }
    }

    ngx_free(env);
    ngx_free(var);

    return pid;
}


static ngx_int_t
ngx_get_options(int argc, char *const *argv)
{
    u_char     *p;
    ngx_int_t   i;

    for (i = 1; i < argc; i++) {

        p = (u_char *) argv[i];

        if (*p++ != '-') {
            ngx_log_stderr(0, "invalid option: \"%s\"", argv[i]);
            return NGX_ERROR;
        }

        while (*p) {

            switch (*p++) {

            case '?':
            case 'h':
                ngx_show_version = 1;
                ngx_show_help = 1;
                break;

            case 'v':
                ngx_show_version = 1;
                break;

            case 'V':
                ngx_show_version = 1;
                ngx_show_configure = 1;
                break;

            case 't':
                ngx_test_config = 1;
                break;

            case 'T':
                ngx_test_config = 1;
                ngx_dump_config = 1;
                break;

            case 'q':
                ngx_quiet_mode = 1;
                break;

            case 'p':
                if (*p) {
                    ngx_prefix = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_prefix = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-p\" requires directory name");
                return NGX_ERROR;

            case 'c':
                if (*p) {
                    ngx_conf_file = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_conf_file = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-c\" requires file name");
                return NGX_ERROR;

            case 'g':
                if (*p) {
                    ngx_conf_params = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_conf_params = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-g\" requires parameter");
                return NGX_ERROR;

            case 's':
                if (*p) {
                    ngx_signal = (char *) p;

                } else if (argv[++i]) {
                    ngx_signal = argv[i];

                } else {
                    ngx_log_stderr(0, "option \"-s\" requires parameter");
                    return NGX_ERROR;
                }

                if (ngx_strcmp(ngx_signal, "stop") == 0
                    || ngx_strcmp(ngx_signal, "quit") == 0
                    || ngx_strcmp(ngx_signal, "reopen") == 0
                    || ngx_strcmp(ngx_signal, "reload") == 0)
                {
                    ngx_process = NGX_PROCESS_SIGNALLER;
                    goto next;
                }

                ngx_log_stderr(0, "invalid option: \"-s %s\"", ngx_signal);
                return NGX_ERROR;

            default:
                ngx_log_stderr(0, "invalid option: \"%c\"", *(p - 1));
                return NGX_ERROR;
            }
        }

    next:

        continue;
    }

    return NGX_OK;
}


/**
 * @brief 静的変数群にコマンドライン引数と環境変数について値をセットする
 * @param[in] cycle サイクル（この構造体が保有しているログを使用する）
 * @param[in] argc  セットしたい「コマンドライン引数の数」
 * @param[in] argv  セットしたいコマンドライン引数
 * @retval
 *     NGX_OK    成功
 *     NGX_ERROR 失敗
 * @pre
 *     environ に環境変数の情報がセットされている（こいつはシステムが自動で準備する）
 * @post
 *     ngx_os_argv はコマンドラインそのもの（argv）を指している（つまり、直後に environ が続く可能性がある）
 *     ngx_argc にコマンドライン引数の数がセットされている
 *     ngx_argv にコマンドライン引数のハードコピーが格納されている
 *     ngx_os_environ は環境変数の情報を指す（システム環境から渡されるもの）
 */
static ngx_int_t
ngx_save_argv(ngx_cycle_t *cycle, int argc, char *const *argv)
{
#if (NGX_FREEBSD)

    ngx_os_argv = (char **) argv;
    ngx_argc = argc;
    ngx_argv = (char **) argv;

#else
    size_t     len;
    ngx_int_t  i;

    ngx_os_argv = (char **) argv;
    ngx_argc = argc;

    ngx_argv = ngx_alloc((argc + 1) * sizeof(char *), cycle->log);
    if (ngx_argv == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < argc; i++) {
        len = ngx_strlen(argv[i]) + 1;

        ngx_argv[i] = ngx_alloc(len, cycle->log);
        if (ngx_argv[i] == NULL) {
            return NGX_ERROR;
        }

        (void) ngx_cpystrn((u_char *) ngx_argv[i], (u_char *) argv[i], len);
    }

    ngx_argv[i] = NULL;

#endif

    ngx_os_environ = environ;

    return NGX_OK;
}


/**
 * @brief
 *     サイクルの conf_prefix, prefix, conf_file, conf_param に値をセットする
 *     prefix はパス指定の時に使用するパス
 *     conf_prefix は設定ファイルへのパスだが名前を除く
 *     conf_file は設定ファイルへのフルパス
 * @retval
 *     NGX_OK:    成功
 *     NGX_ERROR: 失敗
 */
static ngx_int_t
ngx_process_options(ngx_cycle_t *cycle)
{
    u_char  *p;
    size_t   len;

    // ngx_prefix は main() 内の ngx_get_options() でセットされる
    // 通常はスルー
    if (ngx_prefix) {
        len = ngx_strlen(ngx_prefix);
        p = ngx_prefix;

        if (len && !ngx_path_separator(p[len - 1])) {
            p = ngx_pnalloc(cycle->pool, len + 1);
            if (p == NULL) {
                return NGX_ERROR;
            }

            ngx_memcpy(p, ngx_prefix, len);
            p[len++] = '/';
        }

        cycle->conf_prefix.len = len;
        cycle->conf_prefix.data = p;
        cycle->prefix.len = len;
        cycle->prefix.data = p;

    } else {

#ifndef NGX_PREFIX

        p = ngx_pnalloc(cycle->pool, NGX_MAX_PATH);
        if (p == NULL) {
            return NGX_ERROR;
        }

        // カレントディレクトリを取得して p にセット
        if (ngx_getcwd(p, NGX_MAX_PATH) == 0) {
            ngx_log_stderr(ngx_errno, "[emerg]: " ngx_getcwd_n " failed");
            return NGX_ERROR;
        }

        // ヌル文字を除いた長さを取得
        len = ngx_strlen(p);

        // 末尾のヌル文字を '/' で上書きする
        p[len++] = '/';

        cycle->conf_prefix.len = len;
        cycle->conf_prefix.data = p;
        cycle->prefix.len = len;
        cycle->prefix.data = p;

#else

  #ifdef NGX_CONF_PREFIX
        ngx_str_set(&cycle->conf_prefix, NGX_CONF_PREFIX);
  #else
        ngx_str_set(&cycle->conf_prefix, NGX_PREFIX);
  #endif
        ngx_str_set(&cycle->prefix, NGX_PREFIX);

#endif
    }

    if (ngx_conf_file) {
        cycle->conf_file.len = ngx_strlen(ngx_conf_file);
        cycle->conf_file.data = ngx_conf_file;

    } else {
        // コマンドライン引数で設定ファイルを指定していなければこちら
        // ""
        ngx_str_set(&cycle->conf_file, NGX_CONF_PATH);
    }

    // prefix と合わせて "/" 
    // これは NGX_OK が返る
    if (ngx_conf_full_name(cycle, &cycle->conf_file, 0) != NGX_OK) {
        return NGX_ERROR;
    }

    // conf_prefix は設定ファイル名を除いたパス
    // 人によっては prefix で指定するパスと設定ファイルのフルパスが
    // 異なる場合があるので、そのときのためにこのような分岐がされている
    // prefix と conf_prefix が一致していて、conf_file がファイル名のパターン
    // conf_file がパスで、prefix と conf_prefix が異なるパターン
    // "/" の場合はこのループは何も実行されない
    for (p = cycle->conf_file.data + cycle->conf_file.len - 1;
         p > cycle->conf_file.data;
         p--)
    {
        if (ngx_path_separator(*p)) {
            cycle->conf_prefix.len = p - cycle->conf_file.data + 1;
            cycle->conf_prefix.data = cycle->conf_file.data;
            break;
        }
    }

    // コマンドライン引数で渡されたパラメータをセット
    if (ngx_conf_params) {
        cycle->conf_param.len = ngx_strlen(ngx_conf_params);
        cycle->conf_param.data = ngx_conf_params;
    }

    // -t か -T が指定されていた場合はログレベルを変更する
    if (ngx_test_config) {
        cycle->log->log_level = NGX_LOG_INFO;
    }

    return NGX_OK;
}


/**
 * @brief
 *     コアモジュール向けの設定ファイルを生成して返す
 * @param[in]
 *     cycle: 設定ファイルを割り当てるプールをこのサイクルから借りる
 * @retval
 *     生成した設定ファイルへのポインタ
 */
static void *
ngx_core_module_create_conf(ngx_cycle_t *cycle)
{
    ngx_core_conf_t  *ccf;

    ccf = ngx_pcalloc(cycle->pool, sizeof(ngx_core_conf_t));
    if (ccf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc()
     *
     *     ccf->pid = NULL;
     *     ccf->oldpid = NULL;
     *     ccf->priority = 0;
     *     ccf->cpu_affinity_auto = 0;
     *     ccf->cpu_affinity_n = 0;
     *     ccf->cpu_affinity = NULL;
     */

    ccf->daemon = NGX_CONF_UNSET;
    ccf->master = NGX_CONF_UNSET;
    ccf->timer_resolution = NGX_CONF_UNSET_MSEC;
    ccf->shutdown_timeout = NGX_CONF_UNSET_MSEC;

    ccf->worker_processes = NGX_CONF_UNSET;
    ccf->debug_points = NGX_CONF_UNSET;

    ccf->rlimit_nofile = NGX_CONF_UNSET;
    ccf->rlimit_core = NGX_CONF_UNSET;

    ccf->user = (ngx_uid_t) NGX_CONF_UNSET_UINT;
    ccf->group = (ngx_gid_t) NGX_CONF_UNSET_UINT;

    if (ngx_array_init(&ccf->env, cycle->pool, 1, sizeof(ngx_str_t))
        != NGX_OK)
    {
        return NULL;
    }

    return ccf;
}


/**
 * @brief
 *     設定ファイルを初期化
 */
static char *
ngx_core_module_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    /**
     * @brief
     *     まだ create_conf() 呼び出し直後であれば、第二引数の値で初期化する
     */
    ngx_conf_init_value(ccf->daemon, 1);
    ngx_conf_init_value(ccf->master, 1);
    ngx_conf_init_msec_value(ccf->timer_resolution, 0);
    ngx_conf_init_msec_value(ccf->shutdown_timeout, 0);

    ngx_conf_init_value(ccf->worker_processes, 1);
    ngx_conf_init_value(ccf->debug_points, 0);

#if (NGX_HAVE_CPU_AFFINITY)

    if (!ccf->cpu_affinity_auto
        && ccf->cpu_affinity_n
        && ccf->cpu_affinity_n != 1
        && ccf->cpu_affinity_n != (ngx_uint_t) ccf->worker_processes)
    {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                      "the number of \"worker_processes\" is not equal to "
                      "the number of \"worker_cpu_affinity\" masks, "
                      "using last mask for remaining worker processes");
    }

#endif


    if (ccf->pid.len == 0) {
        ngx_str_set(&ccf->pid, NGX_PID_PATH);
    }

    if (ngx_conf_full_name(cycle, &ccf->pid, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    ccf->oldpid.len = ccf->pid.len + sizeof(NGX_OLDPID_EXT);

    ccf->oldpid.data = ngx_pnalloc(cycle->pool, ccf->oldpid.len);
    if (ccf->oldpid.data == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(ngx_cpymem(ccf->oldpid.data, ccf->pid.data, ccf->pid.len),
               NGX_OLDPID_EXT, sizeof(NGX_OLDPID_EXT));


#if !(NGX_WIN32)

    if (ccf->user == (uid_t) NGX_CONF_UNSET_UINT && geteuid() == 0) {
        struct group   *grp;
        struct passwd  *pwd;

        ngx_set_errno(0);
        pwd = getpwnam(NGX_USER);
        if (pwd == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getpwnam(\"" NGX_USER "\") failed");
            return NGX_CONF_ERROR;
        }

        ccf->username = NGX_USER;
        ccf->user = pwd->pw_uid;

        ngx_set_errno(0);
        grp = getgrnam(NGX_GROUP);
        if (grp == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getgrnam(\"" NGX_GROUP "\") failed");
            return NGX_CONF_ERROR;
        }

        ccf->group = grp->gr_gid;
    }


    if (ccf->lock_file.len == 0) {
        ngx_str_set(&ccf->lock_file, NGX_LOCK_PATH);
    }

    if (ngx_conf_full_name(cycle, &ccf->lock_file, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    {
    ngx_str_t  lock_file;

    lock_file = cycle->old_cycle->lock_file;

    if (lock_file.len) {
        lock_file.len--;

        if (ccf->lock_file.len != lock_file.len
            || ngx_strncmp(ccf->lock_file.data, lock_file.data, lock_file.len)
               != 0)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"lock_file\" could not be changed, ignored");
        }

        cycle->lock_file.len = lock_file.len + 1;
        lock_file.len += sizeof(".accept");

        cycle->lock_file.data = ngx_pstrdup(cycle->pool, &lock_file);
        if (cycle->lock_file.data == NULL) {
            return NGX_CONF_ERROR;
        }

    } else {
        cycle->lock_file.len = ccf->lock_file.len + 1;
        cycle->lock_file.data = ngx_pnalloc(cycle->pool,
                                      ccf->lock_file.len + sizeof(".accept"));
        if (cycle->lock_file.data == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memcpy(ngx_cpymem(cycle->lock_file.data, ccf->lock_file.data,
                              ccf->lock_file.len),
                   ".accept", sizeof(".accept"));
    }
    }

#endif

    return NGX_CONF_OK;
}


static char *
ngx_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_WIN32)

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"user\" is not supported, ignored");

    return NGX_CONF_OK;

#else

    ngx_core_conf_t  *ccf = conf;

    char             *group;
    struct passwd    *pwd;
    struct group     *grp;
    ngx_str_t        *value;

    if (ccf->user != (uid_t) NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    if (geteuid() != 0) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"user\" directive makes sense only "
                           "if the master process runs "
                           "with super-user privileges, ignored");
        return NGX_CONF_OK;
    }

    value = cf->args->elts;

    ccf->username = (char *) value[1].data;

    ngx_set_errno(0);
    pwd = getpwnam((const char *) value[1].data);
    if (pwd == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           "getpwnam(\"%s\") failed", value[1].data);
        return NGX_CONF_ERROR;
    }

    ccf->user = pwd->pw_uid;

    group = (char *) ((cf->args->nelts == 2) ? value[1].data : value[2].data);

    ngx_set_errno(0);
    grp = getgrnam(group);
    if (grp == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           "getgrnam(\"%s\") failed", group);
        return NGX_CONF_ERROR;
    }

    ccf->group = grp->gr_gid;

    return NGX_CONF_OK;

#endif
}


static char *
ngx_set_env(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    ngx_str_t   *value, *var;
    ngx_uint_t   i;

    var = ngx_array_push(&ccf->env);
    if (var == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;
    *var = value[1];

    for (i = 0; i < value[1].len; i++) {

        if (value[1].data[i] == '=') {

            var->len = i;

            return NGX_CONF_OK;
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_set_priority(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    ngx_str_t        *value;
    ngx_uint_t        n, minus;

    if (ccf->priority != 0) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (value[1].data[0] == '-') {
        n = 1;
        minus = 1;

    } else if (value[1].data[0] == '+') {
        n = 1;
        minus = 0;

    } else {
        n = 0;
        minus = 0;
    }

    ccf->priority = ngx_atoi(&value[1].data[n], value[1].len - n);
    if (ccf->priority == NGX_ERROR) {
        return "invalid number";
    }

    if (minus) {
        ccf->priority = -ccf->priority;
    }

    return NGX_CONF_OK;
}


static char *
ngx_set_cpu_affinity(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_HAVE_CPU_AFFINITY)
    ngx_core_conf_t  *ccf = conf;

    u_char            ch, *p;
    ngx_str_t        *value;
    ngx_uint_t        i, n;
    ngx_cpuset_t     *mask;

    if (ccf->cpu_affinity) {
        return "is duplicate";
    }

    mask = ngx_palloc(cf->pool, (cf->args->nelts - 1) * sizeof(ngx_cpuset_t));
    if (mask == NULL) {
        return NGX_CONF_ERROR;
    }

    ccf->cpu_affinity_n = cf->args->nelts - 1;
    ccf->cpu_affinity = mask;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "auto") == 0) {

        if (cf->args->nelts > 3) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid number of arguments in "
                               "\"worker_cpu_affinity\" directive");
            return NGX_CONF_ERROR;
        }

        ccf->cpu_affinity_auto = 1;

        CPU_ZERO(&mask[0]);
        for (i = 0; i < (ngx_uint_t) ngx_min(ngx_ncpu, CPU_SETSIZE); i++) {
            CPU_SET(i, &mask[0]);
        }

        n = 2;

    } else {
        n = 1;
    }

    for ( /* void */ ; n < cf->args->nelts; n++) {

        if (value[n].len > CPU_SETSIZE) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                         "\"worker_cpu_affinity\" supports up to %d CPUs only",
                         CPU_SETSIZE);
            return NGX_CONF_ERROR;
        }

        i = 0;
        CPU_ZERO(&mask[n - 1]);

        for (p = value[n].data + value[n].len - 1;
             p >= value[n].data;
             p--)
        {
            ch = *p;

            if (ch == ' ') {
                continue;
            }

            i++;

            if (ch == '0') {
                continue;
            }

            if (ch == '1') {
                CPU_SET(i - 1, &mask[n - 1]);
                continue;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid character \"%c\" in \"worker_cpu_affinity\"",
                          ch);
            return NGX_CONF_ERROR;
        }
    }

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"worker_cpu_affinity\" is not supported "
                       "on this platform, ignored");
#endif

    return NGX_CONF_OK;
}


ngx_cpuset_t *
ngx_get_cpu_affinity(ngx_uint_t n)
{
#if (NGX_HAVE_CPU_AFFINITY)
    ngx_uint_t        i, j;
    ngx_cpuset_t     *mask;
    ngx_core_conf_t  *ccf;

    static ngx_cpuset_t  result;

    ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                           ngx_core_module);

    if (ccf->cpu_affinity == NULL) {
        return NULL;
    }

    if (ccf->cpu_affinity_auto) {
        mask = &ccf->cpu_affinity[ccf->cpu_affinity_n - 1];

        for (i = 0, j = n; /* void */ ; i++) {

            if (CPU_ISSET(i % CPU_SETSIZE, mask) && j-- == 0) {
                break;
            }

            if (i == CPU_SETSIZE && j == n) {
                /* empty mask */
                return NULL;
            }

            /* void */
        }

        CPU_ZERO(&result);
        CPU_SET(i % CPU_SETSIZE, &result);

        return &result;
    }

    if (ccf->cpu_affinity_n > n) {
        return &ccf->cpu_affinity[n];
    }

    return &ccf->cpu_affinity[ccf->cpu_affinity_n - 1];

#else

    return NULL;

#endif
}


static char *
ngx_set_worker_processes(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t        *value;
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) conf;

    if (ccf->worker_processes != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "auto") == 0) {
        ccf->worker_processes = ngx_ncpu;
        return NGX_CONF_OK;
    }

    ccf->worker_processes = ngx_atoi(value[1].data, value[1].len);

    if (ccf->worker_processes == NGX_ERROR) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


static char *
ngx_load_module(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_HAVE_DLOPEN)
    void                *handle;
    char               **names, **order;
    ngx_str_t           *value, file;
    ngx_uint_t           i;
    ngx_module_t        *module, **modules;
    ngx_pool_cleanup_t  *cln;

    if (cf->cycle->modules_used) {
        return "is specified too late";
    }

    value = cf->args->elts;

    file = value[1];

    if (ngx_conf_full_name(cf->cycle, &file, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->cycle->pool, 0);
    if (cln == NULL) {
        return NGX_CONF_ERROR;
    }

    handle = ngx_dlopen(file.data);
    if (handle == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlopen_n " \"%s\" failed (%s)",
                           file.data, ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    cln->handler = ngx_unload_module;
    cln->data = handle;

    modules = ngx_dlsym(handle, "ngx_modules");
    if (modules == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlsym_n " \"%V\", \"%s\" failed (%s)",
                           &value[1], "ngx_modules", ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    names = ngx_dlsym(handle, "ngx_module_names");
    if (names == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlsym_n " \"%V\", \"%s\" failed (%s)",
                           &value[1], "ngx_module_names", ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    order = ngx_dlsym(handle, "ngx_module_order");

    for (i = 0; modules[i]; i++) {
        module = modules[i];
        module->name = names[i];

        if (ngx_add_module(cf, &file, module, order) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cf->log, 0, "module: %s i:%ui",
                       module->name, module->index);
    }

    return NGX_CONF_OK;

#else

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "\"load_module\" is not supported "
                       "on this platform");
    return NGX_CONF_ERROR;

#endif
}


#if (NGX_HAVE_DLOPEN)

static void
ngx_unload_module(void *data)
{
    void  *handle = data;

    if (ngx_dlclose(handle) != 0) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      ngx_dlclose_n " failed (%s)", ngx_dlerror());
    }
}

#endif
