
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>


ngx_int_t   ngx_ncpu;
ngx_int_t   ngx_max_sockets;
ngx_uint_t  ngx_inherited_nonblocking;
ngx_uint_t  ngx_tcp_nodelay_and_tcp_nopush;


struct rlimit  rlmt;


ngx_os_io_t ngx_os_io = {
    ngx_unix_recv,
    ngx_readv_chain,
    ngx_udp_unix_recv,
    ngx_unix_send,
    ngx_udp_unix_send,
    ngx_udp_unix_sendmsg_chain,
    ngx_writev_chain,
    0
};


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
ngx_int_t
ngx_os_init(ngx_log_t *log)
{
    ngx_time_t  *tp;
    ngx_uint_t   n;
#if (NGX_HAVE_LEVEL1_DCACHE_LINESIZE)
    long         size;
#endif

#if (NGX_HAVE_OS_SPECIFIC_INIT)
    /**
     * @brief
     *     OS のタイプとリリースバージョンを静的変数へ代入する
     *     また、OS 依存の入出力関数群をセットする
     * @param[in]
     *     log: ログ出力に使用
     * @retval
     *     NGX_OK: 正常終了
     *     NGX_ERROR: 異常終了
     */
    if (ngx_os_specific_init(log) != NGX_OK) {
        return NGX_ERROR;
    }
#endif

    /**
     * @brief
     *     ngx_os_argv(argv) の後に environ が続いていることを確認する
     *     そうであれば、プロセスタイトルが長ったらしいと環境変数情報を上書きしてしまうので
     *     環境変数情報をコピーしてそっちを environ が指すように対策する
     *     また、ngx_os_argv_last が ngx_os_argv の最後を指すよう修正する
     * @param[in]
     *     log: ログ出力に使用
     * @retval
     *     NGX_OK: 成功
     *     NGX_ERROR: 失敗
     * @post
     *     environ がシステム由来のデータではなく、nginx が保有する環境変数文字列群を指している
     *     ngx_os_argv_last は ngx_os_argv の最後の番地を指している
     */
    if (ngx_init_setproctitle(log) != NGX_OK) {
        return NGX_ERROR;
    }

    // ページサイズを取得
    ngx_pagesize = getpagesize();
    // キャッシュラインサイズを取得
    ngx_cacheline_size = NGX_CPU_CACHE_LINE;

    // ページサイズは２の何乗かを ngx_pagesize_shift に格納する
    for (n = ngx_pagesize; n >>= 1; ngx_pagesize_shift++) { /* void */ }

#if (NGX_HAVE_SC_NPROCESSORS_ONLN)
    if (ngx_ncpu == 0) {
        // sysconf() はカーネルのオプション値を取得するメソッド
        // _SC_NPROCESSORS_ONLN で利用可能なプロセッサ数が取得できる
        // それをここでセット
        ngx_ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    }
#endif

    // うまく取得できていなかったら１つだけだということにしとく
    if (ngx_ncpu < 1) {
        ngx_ncpu = 1;
    }

#if (NGX_HAVE_LEVEL1_DCACHE_LINESIZE)
    // レベル１データキャッシュを持っている
    // そのサイズを取得して ngx_cacheline_size へセット
    size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (size > 0) {
        ngx_cacheline_size = size;
    }
#endif

    /**
     * @brief
     *     CPUID アセンブラ命令を介して、ベンダ情報をもとにキャッシュラインサイズを決定する
     * @post
     *     ngx_cacheline_size にキャッシュラインサイズが格納されている（CPU によっては格納されない）
     * @detail
     *     (( __i386__ || __amd64__ ) && ( __GNUC__ || __INTEL_COMPILER ))
     *     が満たされる場合に限り、有効なメソッド
     *     それ以外であれば何もしない
     */
    ngx_cpuinfo();

    // 最大ソケット数を取得
    if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
        ngx_log_error(NGX_LOG_ALERT, log, errno,
                      "getrlimit(RLIMIT_NOFILE) failed");
        return NGX_ERROR;
    }

    // 最大ソケット数をセット
    ngx_max_sockets = (ngx_int_t) rlmt.rlim_cur;

#if (NGX_HAVE_INHERITED_NONBLOCK || NGX_HAVE_ACCEPT4)
    // accept4() は accept() の機能拡張版
    // フラグに値をセットすることでさまざまな異なる動作ができる
    // そのうちの一つが、ノンブロッキングモードで accept するというもの
    // そのため、わざわざ fcntl() でフラグをセットする必要がない

    // わざわざノンブロッキングモードに変更しなくてよいことを表す
    ngx_inherited_nonblocking = 1;
#else
    // fcntl() によるノンブロッキングモードへの変更が必要であることを表す
    ngx_inherited_nonblocking = 0;
#endif

    // ランダム系関数の初期化（種による初期化）
    tp = ngx_timeofday();
    srandom(((unsigned) ngx_pid << 16) ^ tp->sec ^ tp->msec);

    return NGX_OK;
}


/**
 * @brief
 *     freebsd や solaris といった OS 特有の設定事項をログへ出力する
 * @param[in]
 *     log: 出力先のログ
 */
void
ngx_os_status(ngx_log_t *log)
{
    ngx_log_error(NGX_LOG_NOTICE, log, 0, NGINX_VER_BUILD);

#ifdef NGX_COMPILER
    ngx_log_error(NGX_LOG_NOTICE, log, 0, "built by " NGX_COMPILER);
#endif

#if (NGX_HAVE_OS_SPECIFIC_INIT)
    ngx_os_specific_status(log);
#endif

    ngx_log_error(NGX_LOG_NOTICE, log, 0,
                  "getrlimit(RLIMIT_NOFILE): %r:%r",
                  rlmt.rlim_cur, rlmt.rlim_max);
}


#if 0

ngx_int_t
ngx_posix_post_conf_init(ngx_log_t *log)
{
    ngx_fd_t  pp[2];

    if (pipe(pp) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "pipe() failed");
        return NGX_ERROR;
    }

    if (dup2(pp[1], STDERR_FILENO) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, errno, "dup2(STDERR) failed");
        return NGX_ERROR;
    }

    if (pp[1] > STDERR_FILENO) {
        if (close(pp[1]) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, errno, "close() failed");
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

#endif
