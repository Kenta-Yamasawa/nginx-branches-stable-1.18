
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_LOG_H_INCLUDED_
#define _NGX_LOG_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


// ログレベル（エラー）：小さい方が緊急度が高い
#define NGX_LOG_STDERR            0
#define NGX_LOG_EMERG             1
#define NGX_LOG_ALERT             2
#define NGX_LOG_CRIT              3
#define NGX_LOG_ERR               4
#define NGX_LOG_WARN              5
#define NGX_LOG_NOTICE            6
#define NGX_LOG_INFO              7
#define NGX_LOG_DEBUG             8

// ログレベル（デバッグ）
#define NGX_LOG_DEBUG_CORE        0x010
#define NGX_LOG_DEBUG_ALLOC       0x020
#define NGX_LOG_DEBUG_MUTEX       0x040
#define NGX_LOG_DEBUG_EVENT       0x080
#define NGX_LOG_DEBUG_HTTP        0x100
#define NGX_LOG_DEBUG_MAIL        0x200
#define NGX_LOG_DEBUG_STREAM      0x400

/*
 * do not forget to update debug_levels[] in src/core/ngx_log.c
 * after the adding a new debug level
 */

#define NGX_LOG_DEBUG_FIRST       NGX_LOG_DEBUG_CORE
#define NGX_LOG_DEBUG_LAST        NGX_LOG_DEBUG_STREAM
#define NGX_LOG_DEBUG_CONNECTION  0x80000000
#define NGX_LOG_DEBUG_ALL         0x7ffffff0


typedef u_char *(*ngx_log_handler_pt) (ngx_log_t *log, u_char *buf, size_t len);
typedef void (*ngx_log_writer_pt) (ngx_log_t *log, ngx_uint_t level,
    u_char *buf, size_t len);


struct ngx_log_s {
    // ログレベル
    ngx_uint_t           log_level;
    // 書き出し先
    ngx_open_file_t     *file;

    ngx_atomic_uint_t    connection;

    // 書き込み先に空き容量がなかった最新時刻
    time_t               disk_full_time;

    ngx_log_handler_pt   handler;
    void                *data;

    // 書き出し先に syslog が指定されていた際に有効化される
    ngx_log_writer_pt    writer;
    // 書き出し先に syslog が指定されていた際に有効化される
    void                *wdata;

    /*
     * we declare "action" as "char *" because the actions are usually
     * the static strings and in the "u_char *" case we have to override
     * their types all the time
     */

    char                *action;

    ngx_log_t           *next;
};


// エラーメッセージの最大長さ（ヌル文字を含む）
#define NGX_MAX_ERROR_STR   2048


/*********************************/

// 関数マクロのパラメータに「... (ellipsis : 省略記号)」を指定することで、
// 可変個のパラメータを受け取れる。受け取ったパラメータを使用するには、
// __VA_ARGS__という特殊な識別子を使用する。
#if (NGX_HAVE_C99_VARIADIC_MACROS)

  #define NGX_HAVE_VARIADIC_MACROS  1

  /**
   * @brief
   *     エラーに関するログを出力する。
   *     なお、渡されたエラーレベルが高い（緊急度が低い）場合は出力されないこともある。
   * @param[in]
   *     level: 出力したいログのエラーレベル
   *     log: nginx プロセスのログ機能全般を管理する構造体
   *     ...: 出力したいログメッセージ（[エラー番号], フォーマット, [変数群]）
   * @detail
   *     ログメッセージは log 構造体が保有しているファイルに出力される。
   *     ログのエラーレベルは9段階（NGX_LOG_STDERR ~ NGX_LOG_DEBUG）で定義されている。
   *     エラーレベルは小さいほど緊急度が高いことを意味する。
   *     本関数呼び出し側は、出力ログのエラーレベルをこの中から指定して第1引数で渡す必要がある。
   *     本関数は、渡されたエラーレベルが、前もって設定された閾値エラーレベルより高い（緊急度が低い）なら出力しない。
   *     閾値エラーレベルは、設定ファイルの error_log ディレクティブで決定することができる。
   *     デフォルトの閾値エラーレベルは NGX_LOG_NOTICE である。
   *     つまり、デフォルトの動作では NGX_LOG_NOTICE より緊急度が低いログは出力されない。
   */
  #define ngx_log_error(level, log, ...)                                        \
      if ((log)->log_level >= level) ngx_log_error_core(level, log, __VA_ARGS__)

  /**
   * @brief
   *     ログを出力する。
   * @detail
   *     ngx_log_error(), ngx_log_debug() が内部的に使用する関数である。
   *     そのため、省略する。
   */
  void ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
      const char *fmt, ...);

  /**
   * @brief
   *     デバッグに関するログを出力する。
   *     なお、渡されたデバッグタイプが有効化されたものではない場合は出力されない。
   * @param[in]
   *     level: 出力したいログのデバッグタイプ
   *     log: nginx プロセスのログ機能全般を管理する構造体
   *     fmt: 出力したいログメッセージのフォーマット
   *     arg1: 変数
   * @detail
   *     フォーマットに登場する変数の数に応じて、異なる関数が用意されている。
   *     ngx_log_debug〇() という命名がされており、〇の部分に変数の数が入る。
   *     たとえば、"message: %d %d" などというメッセージを出力したい場合は、
   *     ngx_log_debug2(level, log, "message: %d %d", var1, var2) となる。
   *     変数は最大で8つまで対応できる。
   *     ログメッセージは log 構造体が保有しているファイルに出力される。
   *     ログのデバッグタイプは7つ定義されている。
   *     本関数呼び出し側は、出力ログのデバッグタイプをこの中から指定する必要がある。
   *     本関数は、渡されたデバッグタイプが有効化されていない場合は出力しない。
   *     デバッグタイプを有効化するためのディレクティブは用意されていない。
   *     ソースコード上で決め打ちで有効化してリビルドする必要がある。
   *     ちなみに、log->log_level = NGX_LOG_DEBUG_ALL ですべてのデバッグタイプを一括で有効化できる。
   */
  #define ngx_log_debug(level, log, ...)                                        \
      if ((log)->log_level & level)                                             \
          ngx_log_error_core(NGX_LOG_DEBUG, log, __VA_ARGS__)

  /*********************************/

// 可変長マクロの GCC 実装
// 関数における可変長の表現は同じ？
#elif (NGX_HAVE_GCC_VARIADIC_MACROS)

  #define NGX_HAVE_VARIADIC_MACROS  1

  #define ngx_log_error(level, log, args...)                                    \
      if ((log)->log_level >= level) ngx_log_error_core(level, log, args)

  void ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
      const char *fmt, ...);

  #define ngx_log_debug(level, log, args...)                                    \
      if ((log)->log_level & level)                                             \
          ngx_log_error_core(NGX_LOG_DEBUG, log, args)

  /*********************************/

#else /* no variadic macros */

  #define NGX_HAVE_VARIADIC_MACROS  0

  /**
   * マクロが許されていないだけで、関数の可変長は常に許されている
   */
  void ngx_cdecl ngx_log_error(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
      const char *fmt, ...);
  void ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
      const char *fmt, va_list args);
  void ngx_cdecl ngx_log_debug_core(ngx_log_t *log, ngx_err_t err,
      const char *fmt, ...);


#endif /* variadic macros */


/*********************************/

#if (NGX_DEBUG)

  #if (NGX_HAVE_VARIADIC_MACROS)

    #define ngx_log_debug0(level, log, err, fmt)                                  \
            ngx_log_debug(level, log, err, fmt)

    #define ngx_log_debug1(level, log, err, fmt, arg1)                            \
            ngx_log_debug(level, log, err, fmt, arg1)

    #define ngx_log_debug2(level, log, err, fmt, arg1, arg2)                      \
            ngx_log_debug(level, log, err, fmt, arg1, arg2)

    #define ngx_log_debug3(level, log, err, fmt, arg1, arg2, arg3)                \
            ngx_log_debug(level, log, err, fmt, arg1, arg2, arg3)

    #define ngx_log_debug4(level, log, err, fmt, arg1, arg2, arg3, arg4)          \
            ngx_log_debug(level, log, err, fmt, arg1, arg2, arg3, arg4)

    #define ngx_log_debug5(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5)    \
            ngx_log_debug(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5)

    #define ngx_log_debug6(level, log, err, fmt,                                  \
                           arg1, arg2, arg3, arg4, arg5, arg6)                    \
            ngx_log_debug(level, log, err, fmt,                                   \
                           arg1, arg2, arg3, arg4, arg5, arg6)

    #define ngx_log_debug7(level, log, err, fmt,                                  \
                           arg1, arg2, arg3, arg4, arg5, arg6, arg7)              \
            ngx_log_debug(level, log, err, fmt,                                   \
                           arg1, arg2, arg3, arg4, arg5, arg6, arg7)

    #define ngx_log_debug8(level, log, err, fmt,                                  \
                           arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)        \
            ngx_log_debug(level, log, err, fmt,                                   \
                           arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)


  #else /* no variadic macros */

    #define ngx_log_debug0(level, log, err, fmt)                                  \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt)

    #define ngx_log_debug1(level, log, err, fmt, arg1)                            \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt, arg1)

    #define ngx_log_debug2(level, log, err, fmt, arg1, arg2)                      \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt, arg1, arg2)

    #define ngx_log_debug3(level, log, err, fmt, arg1, arg2, arg3)                \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt, arg1, arg2, arg3)

    #define ngx_log_debug4(level, log, err, fmt, arg1, arg2, arg3, arg4)          \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt, arg1, arg2, arg3, arg4)

    #define ngx_log_debug5(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5)    \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt, arg1, arg2, arg3, arg4, arg5)

    #define ngx_log_debug6(level, log, err, fmt,                                  \
                           arg1, arg2, arg3, arg4, arg5, arg6)                    \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt, arg1, arg2, arg3, arg4, arg5, arg6)

    #define ngx_log_debug7(level, log, err, fmt,                                  \
                           arg1, arg2, arg3, arg4, arg5, arg6, arg7)              \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt,                                     \
                           arg1, arg2, arg3, arg4, arg5, arg6, arg7)

    #define ngx_log_debug8(level, log, err, fmt,                                  \
                           arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)        \
        if ((log)->log_level & level)                                             \
            ngx_log_debug_core(log, err, fmt,                                     \
                           arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)

  #endif

#else /* !NGX_DEBUG */

  #define ngx_log_debug0(level, log, err, fmt)
  #define ngx_log_debug1(level, log, err, fmt, arg1)
  #define ngx_log_debug2(level, log, err, fmt, arg1, arg2)
  #define ngx_log_debug3(level, log, err, fmt, arg1, arg2, arg3)
  #define ngx_log_debug4(level, log, err, fmt, arg1, arg2, arg3, arg4)
  #define ngx_log_debug5(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5)
  #define ngx_log_debug6(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5, arg6)
  #define ngx_log_debug7(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5,    \
                         arg6, arg7)
  #define ngx_log_debug8(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5,    \
                         arg6, arg7, arg8)

#endif

/*********************************/

/**
 * @brief
 *     nginx プロセスのログ機能を初期化する。
 *     ログに関するあらゆる関数を呼び出す前に、本関数を呼び出さなければならない。
 * @param[in]
 *     prefix: 
 * @detail
 *     本関数を呼び出した直後に、ログ出力ができるようになるわけではない。
 *     本関数呼び出し後に、ngx_log_set_log() や ngx_log_open_default() を呼び出す必要がある。
 */
ngx_log_t *ngx_log_init(u_char *prefix);
void ngx_cdecl ngx_log_abort(ngx_err_t err, const char *fmt, ...);
void ngx_cdecl ngx_log_stderr(ngx_err_t err, const char *fmt, ...);
u_char *ngx_log_errno(u_char *buf, u_char *last, ngx_err_t err);
ngx_int_t ngx_log_open_default(ngx_cycle_t *cycle);
ngx_int_t ngx_log_redirect_stderr(ngx_cycle_t *cycle);
ngx_log_t *ngx_log_get_file_log(ngx_log_t *head);
char *ngx_log_set_log(ngx_conf_t *cf, ngx_log_t **head);


/*
 * ngx_write_stderr() cannot be implemented as macro, since
 * MSVC does not allow to use #ifdef inside macro parameters.
 *
 * ngx_write_fd() is used instead of ngx_write_console(), since
 * CharToOemBuff() inside ngx_write_console() cannot be used with
 * read only buffer as destination and CharToOemBuff() is not needed
 * for ngx_write_stderr() anyway.
 */
static ngx_inline void
ngx_write_stderr(char *text)
{
    (void) ngx_write_fd(ngx_stderr, text, ngx_strlen(text));
}


static ngx_inline void
ngx_write_stdout(char *text)
{
    (void) ngx_write_fd(ngx_stdout, text, ngx_strlen(text));
}


extern ngx_module_t  ngx_errlog_module;
extern ngx_uint_t    ngx_use_stderr;


#endif /* _NGX_LOG_H_INCLUDED_ */
