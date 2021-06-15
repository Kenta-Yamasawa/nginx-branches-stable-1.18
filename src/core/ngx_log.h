
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_LOG_H_INCLUDED_
#define _NGX_LOG_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


// ���O���x���i�G���[�j�F�����������ً}�x������
#define NGX_LOG_STDERR            0
#define NGX_LOG_EMERG             1
#define NGX_LOG_ALERT             2
#define NGX_LOG_CRIT              3
#define NGX_LOG_ERR               4
#define NGX_LOG_WARN              5
#define NGX_LOG_NOTICE            6
#define NGX_LOG_INFO              7
#define NGX_LOG_DEBUG             8

// ���O���x���i�f�o�b�O�j
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
    // ���O���x��
    ngx_uint_t           log_level;
    // �����o����
    ngx_open_file_t     *file;

    ngx_atomic_uint_t    connection;

    // �������ݐ�ɋ󂫗e�ʂ��Ȃ������ŐV����
    time_t               disk_full_time;

    ngx_log_handler_pt   handler;
    void                *data;

    // �����o����� syslog ���w�肳��Ă����ۂɗL���������
    ngx_log_writer_pt    writer;
    // �����o����� syslog ���w�肳��Ă����ۂɗL���������
    void                *wdata;

    /*
     * we declare "action" as "char *" because the actions are usually
     * the static strings and in the "u_char *" case we have to override
     * their types all the time
     */

    char                *action;

    ngx_log_t           *next;
};


// �G���[���b�Z�[�W�̍ő咷���i�k���������܂ށj
#define NGX_MAX_ERROR_STR   2048


/*********************************/

// �֐��}�N���̃p�����[�^�Ɂu... (ellipsis : �ȗ��L��)�v���w�肷�邱�ƂŁA
// �ό̃p�����[�^���󂯎���B�󂯎�����p�����[�^���g�p����ɂ́A
// __VA_ARGS__�Ƃ�������Ȏ��ʎq���g�p����B
#if (NGX_HAVE_C99_VARIADIC_MACROS)

  #define NGX_HAVE_VARIADIC_MACROS  1

  /**
   * @brief
   *     �G���[�Ɋւ��郍�O���o�͂���B
   *     �Ȃ��A�n���ꂽ�G���[���x���������i�ً}�x���Ⴂ�j�ꍇ�͏o�͂���Ȃ����Ƃ�����B
   * @param[in]
   *     level: �o�͂��������O�̃G���[���x��
   *     log: nginx �v���Z�X�̃��O�@�\�S�ʂ��Ǘ�����\����
   *     ...: �o�͂��������O���b�Z�[�W�i[�G���[�ԍ�], �t�H�[�}�b�g, [�ϐ��Q]�j
   * @detail
   *     ���O���b�Z�[�W�� log �\���̂��ۗL���Ă���t�@�C���ɏo�͂����B
   *     ���O�̃G���[���x����9�i�K�iNGX_LOG_STDERR ~ NGX_LOG_DEBUG�j�Œ�`����Ă���B
   *     �G���[���x���͏������قǋً}�x���������Ƃ��Ӗ�����B
   *     �{�֐��Ăяo�����́A�o�̓��O�̃G���[���x�������̒�����w�肵�đ�1�����œn���K�v������B
   *     �{�֐��́A�n���ꂽ�G���[���x�����A�O�����Đݒ肳�ꂽ臒l�G���[���x����荂���i�ً}�x���Ⴂ�j�Ȃ�o�͂��Ȃ��B
   *     臒l�G���[���x���́A�ݒ�t�@�C���� error_log �f�B���N�e�B�u�Ō��肷�邱�Ƃ��ł���B
   *     �f�t�H���g��臒l�G���[���x���� NGX_LOG_NOTICE �ł���B
   *     �܂�A�f�t�H���g�̓���ł� NGX_LOG_NOTICE ���ً}�x���Ⴂ���O�͏o�͂���Ȃ��B
   */
  #define ngx_log_error(level, log, ...)                                        \
      if ((log)->log_level >= level) ngx_log_error_core(level, log, __VA_ARGS__)

  /**
   * @brief
   *     ���O���o�͂���B
   * @detail
   *     ngx_log_error(), ngx_log_debug() �������I�Ɏg�p����֐��ł���B
   *     ���̂��߁A�ȗ�����B
   */
  void ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
      const char *fmt, ...);

  /**
   * @brief
   *     �f�o�b�O�Ɋւ��郍�O���o�͂���B
   *     �Ȃ��A�n���ꂽ�f�o�b�O�^�C�v���L�������ꂽ���̂ł͂Ȃ��ꍇ�͏o�͂���Ȃ��B
   * @param[in]
   *     level: �o�͂��������O�̃f�o�b�O�^�C�v
   *     log: nginx �v���Z�X�̃��O�@�\�S�ʂ��Ǘ�����\����
   *     fmt: �o�͂��������O���b�Z�[�W�̃t�H�[�}�b�g
   *     arg1: �ϐ�
   * @detail
   *     �t�H�[�}�b�g�ɓo�ꂷ��ϐ��̐��ɉ����āA�قȂ�֐����p�ӂ���Ă���B
   *     ngx_log_debug�Z() �Ƃ�������������Ă���A�Z�̕����ɕϐ��̐�������B
   *     ���Ƃ��΁A"message: %d %d" �ȂǂƂ������b�Z�[�W���o�͂������ꍇ�́A
   *     ngx_log_debug2(level, log, "message: %d %d", var1, var2) �ƂȂ�B
   *     �ϐ��͍ő��8�܂őΉ��ł���B
   *     ���O���b�Z�[�W�� log �\���̂��ۗL���Ă���t�@�C���ɏo�͂����B
   *     ���O�̃f�o�b�O�^�C�v��7��`����Ă���B
   *     �{�֐��Ăяo�����́A�o�̓��O�̃f�o�b�O�^�C�v�����̒�����w�肷��K�v������B
   *     �{�֐��́A�n���ꂽ�f�o�b�O�^�C�v���L��������Ă��Ȃ��ꍇ�͏o�͂��Ȃ��B
   *     �f�o�b�O�^�C�v��L�������邽�߂̃f�B���N�e�B�u�͗p�ӂ���Ă��Ȃ��B
   *     �\�[�X�R�[�h��Ō��ߑł��ŗL�������ă��r���h����K�v������B
   *     ���Ȃ݂ɁAlog->log_level = NGX_LOG_DEBUG_ALL �ł��ׂẴf�o�b�O�^�C�v���ꊇ�ŗL�����ł���B
   */
  #define ngx_log_debug(level, log, ...)                                        \
      if ((log)->log_level & level)                                             \
          ngx_log_error_core(NGX_LOG_DEBUG, log, __VA_ARGS__)

  /*********************************/

// �ϒ��}�N���� GCC ����
// �֐��ɂ�����ϒ��̕\���͓����H
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
   * �}�N����������Ă��Ȃ������ŁA�֐��̉ϒ��͏�ɋ�����Ă���
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
 *     nginx �v���Z�X�̃��O�@�\������������B
 *     ���O�Ɋւ��邠����֐����Ăяo���O�ɁA�{�֐����Ăяo���Ȃ���΂Ȃ�Ȃ��B
 * @param[in]
 *     prefix: 
 * @detail
 *     �{�֐����Ăяo��������ɁA���O�o�͂��ł���悤�ɂȂ�킯�ł͂Ȃ��B
 *     �{�֐��Ăяo����ɁAngx_log_set_log() �� ngx_log_open_default() ���Ăяo���K�v������B
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
