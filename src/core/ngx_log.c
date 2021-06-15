
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @macro
 *     NGX_HAVE_VARIADIC_MACROS
 *         �ϒ��\�L�i...�j��������Ă��邩�ǂ���
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

    // �֌W�Ȃ�
    // �G���[���o�͂������w�肷�邽�߂̃f�B���N�e�B�u
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


// ���O�Ɋւ��邠��������Ǘ�����\����
// ���������O�̖{�́i����������ɒǉ��̃��O�\���̂����X�g�`���ŕێ����邱�Ƃ͂ł���j
static ngx_log_t        ngx_log;
// ���O�̏o�͂Ɏg�p����t�@�C����\��
static ngx_open_file_t  ngx_log_file;
// nginx ���g���W���o�͂��Ǘ�����̂��ǂ����i�܂胍�O�ɊǗ������͂Ȃ��H�j
ngx_uint_t              ngx_use_stderr = 1;


// �G���[���x���Q�i���O���x���j
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

// �f�o�b�O���x���Q�i���O���x���j
static const char *debug_levels[] = {
    "debug_core", "debug_alloc", "debug_mutex", "debug_event",
    "debug_http", "debug_mail", "debug_stream"
};


// �ϒ��\�L�i...�j��������Ă��邩�ǂ���
#if (NGX_HAVE_VARIADIC_MACROS)

/**
 * @brief
 *     ��Q���� log ��p���đ�S�����ȍ~�̃G���[���b�Z�[�W���o�͂���
 * @param[in]
 *     level: �������ރ��b�Z�[�W�̃��O���x��
 *     log: ���̃��O��p���ď�������
 *     err: �G���[�ԍ��i�w�肵�Ȃ��Ă��悢�A�w�肳��Ă�����G���[���b�Z�[�W�Ɋ܂܂��j
 *     fmt: �������ރG���[���b�Z�[�W�̃t�H�[�}�b�g
 *     ...: �������ރG���[���b�Z�[�W�{��
 * @detail
 *     �K����������킯�ł͂Ȃ��H
 *     err_log �f�B���N�e�B�u�Ŏw�肵�����ׂẴ��x���̃��O�ɂ��āA
 *     ���ꂼ�ꂱ�̃��b�Z�[�W�̃��x���Ɣ�r���ď������ނׂ����ǂ�������������
 *     �����̌��ʁA���x���I�ɖ��Ȃ���΂���炷�ׂẴ��O�ɏ�������
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

    // errstr �ɕ����񎞊ԃL���b�V���Ɋi�[���ꂽ���������R�s�[����
    p = ngx_cpymem(errstr, ngx_cached_err_log_time.data,
                   ngx_cached_err_log_time.len);

    /**
     * @brief
     *     ��P���� buf �֑�S�����ȍ~���R�s�[
     * @param[out]
     *     buf: �����o���惁�����̈�
     * @param[in]
     *     last: buf �̍ŏI�Ԓn
     *     fmt: �����o������
     *     ...: �����o���������p�����[�^
     * @retval
     *     �������񂾌��ʁA���̏������ݐ�ւ̃|�C���^
     */
    // �G���[���x������ errstr �֏�������
    p = ngx_slprintf(p, last, " [%V] ", &err_levels[level]);

    // �v���Z�X ID �� TID ���������ށiTID���ĂȂ񂾁H�j
    /* pid#tid */
    p = ngx_slprintf(p, last, "%P#" NGX_TID_T_FMT ": ",
                    ngx_log_pid, ngx_log_tid);

    // ?
    if (log->connection) {
        p = ngx_slprintf(p, last, "*%uA ", log->connection);
    }

    // �������烁�b�Z�[�W
    msg = p;

#if (NGX_HAVE_VARIADIC_MACROS)

    va_start(args, fmt);
    p = ngx_vslprintf(p, last, fmt, args);
    va_end(args);

#else

    // �{�֐��̈����œn���ꂽ args �� errstr �֏�������
    p = ngx_vslprintf(p, last, fmt, args);

#endif

    if (err) {
        /**
         * @brief
         *     ��R�����œn���ꂽ�G���[�ԍ����P�����œn���ꂽ�o�b�t�@�֏�������
         * @param[out]
         *     buf: �����o����o�b�t�@
         * @param[in]
         *     last: �����o����o�b�t�@�̍Ō�̔Ԓn
         *     err: �G���[�ԍ�
         * @retval
         *     ���̏������ސ惁�����Ԓn
         * @detail
         *     �X�y�[�X������Ă��Ȃ������狭���I�ɁA�O�ɏ�������ł������̂������ď�������
         *     ���̍ۂ� ... ���������ނ��Ƃŏ��������Ƃ��w������
         */
        p = ngx_log_errno(p, last, err);
    }

    if (level != NGX_LOG_DEBUG && log->handler) {
        p = log->handler(log, p, last - p);
    }

    // ���s�������������ރX�y�[�X���Ȃ���΁A�����I�ɍ��
    if (p > last - NGX_LINEFEED_SIZE) {
        p = last - NGX_LINEFEED_SIZE;
    }

    /**
     * @brief
     *     �|�C���^��������ɉ��s�����������āA�|�C���^����i�߂�
     * @param[in:out]
     *     p: ���̃|�C���^���w���ʒu�ɉ��s�������i�[���āA�|�C���^����i�߂�
     */
    ngx_linefeed(p);

    wrote_stderr = 0;
    debug_connection = (log->log_level & NGX_LOG_DEBUG_CONNECTION) != 0;

    while (log) {

        // ���̃��O�̃��x�����A�������ރ��x�����傫���ꍇ�́A�������܂Ȃ��Ă����̂Ń��[�v���甲����
        // �������́A���O�̃��x���� NGX_LOG_DEBUG_CONNECTION �Ȃ甲����i���O�o�͂��Ȃ��j
        if (log->log_level < level && !debug_connection) {
            break;
        }

        // error_log �f�B���N�e�B�u�� syslog ���w�肵���ۂɗL���������
        // ����͖���
        if (log->writer) {
            log->writer(log, level, errstr, p - errstr);
            goto next;
        }

        // �O�Ƀf�B�X�N�󂫗e�ʂ��Ȃ�����������Œ�A�P�b�͏������݂��X�L�b�v����
        // �Ȃ��Ȃ�AFreeBSD �͋󂫗e�ʂ��Ȃ��f�B�X�N�ւ̏������݂ő����̎��Ԃ�Q��邩��
        // �����h������
        if (ngx_time() == log->disk_full_time) {

            /*
             * on FreeBSD writing to a full filesystem with enabled softupdates
             * may block process for much longer time than writing to non-full
             * filesystem, so we skip writing to a log for one second
             */
            // �����m��񂯂ǁA�Œ�A�P�b�͏������݂��X�L�b�v����炵��

            goto next;
        }

        // �t�@�C���֏�������
        n = ngx_write_fd(log->file->fd, errstr, p - errstr);

        // �������ݐ�̃f�B�X�N�ɋ󂫗e�ʂ��Ȃ����Ƃ�����
        // ���ꂪ���m���ꂽ���Ԃ��X�V����
        // https://qiita.com/kaitaku/items/fb7c84fe562530668614
        if (n == -1 && ngx_errno == NGX_ENOSPC) {
            // ���Ԃ��X�V
            log->disk_full_time = ngx_time();
        }

        if (log->file->fd == ngx_stderr) {
            wrote_stderr = 1;
        }

    next:

        // ���̃��O��
        log = log->next;
    }

    // nginx �� stderr ���g���Ă��Ȃ��i���O���g���j
    // ���O���x���� WARN ���傫�������i�d�v�ł͂Ȃ��j
    // �W���o�͂�p���Ăǂꂩ�̏������݂���������
    // ���R�̂ǂꂩ�����������ΐ���I��
    if (!ngx_use_stderr
        || level > NGX_LOG_WARN
        || wrote_stderr)
    {
        return;
    }

    // nginx �͕W���o�͂�ۗL���Ă��āA����̃��O���x���� WARN ���ً}�x�������āA
    // ����̏������݂ł܂��W���o�͂��p�����Ă��Ȃ��Ȃ�A���̏�����

    msg -= (7 + err_levels[level].len + 3);

    // ���̃��b�Z�[�W�� nginx ���g���o�͂������Ƃ��}�[�L���O����
    (void) ngx_sprintf(msg, "nginx: [%V] ", &err_levels[level]);

    // nginx ���炪�W���o�͂֏�������
    (void) ngx_write_console(ngx_stderr, msg, p - msg);
}


#if !(NGX_HAVE_VARIADIC_MACROS)


/**
 * @brief
 *     ��P�������x�������O���x���ȉ������؂��āA
 *     �����Ȃ炻�̃��O���ۗL���邷�ׂẴ��O�ɂ��ďo�͌����E�o�͂���
 * @param[in]
 *     level: ���x��
 *     log: ���O�o�͂Ɏg�p���郍�O�\����
 *     err: 
 *     fmt: �G���[���b�Z�[�W�̃t�H�[�}�b�g
 *     args: �G���[���b�Z�[�W�{��
 */
void ngx_cdecl
ngx_log_error(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...)
{
    va_list  args;

    // �n���ꂽ���x�������O���x���ȉ��̏ꍇ�̓��O�Ƃ݂Ȃ��ďo��
    if (log->log_level >= level) {
        va_start(args, fmt);
        /**
         * @brief
         *     ��Q���� log ��p���đ�S�����ȍ~�̃G���[���b�Z�[�W���o�͂���
         * @param[in]
         *     level: �������ރ��b�Z�[�W�̃��O���x��
         *     log: ���̃��O��p���ď�������
         *     err: �G���[�ԍ��i�w�肵�Ȃ��Ă��悢�A�w�肳��Ă�����G���[���b�Z�[�W�Ɋ܂܂��j
         *     fmt: �������ރG���[���b�Z�[�W�̃t�H�[�}�b�g
         *     ...: �������ރG���[���b�Z�[�W�{��
         * @detail
         *     �K����������킯�ł͂Ȃ��H
         *     err_log �f�B���N�e�B�u�Ŏw�肵�����ׂẴ��x���̃��O�ɂ��āA
         *     ���ꂼ�ꂱ�̃��b�Z�[�W�̃��x���Ɣ�r���ď������ނׂ����ǂ�������������
         *     �����̌��ʁA���x���I�ɖ��Ȃ���΂���炷�ׂẴ��O�ɏ�������
         */
        ngx_log_error_core(level, log, err, fmt, args);
        va_end(args);
    }
}


/**
 * @brief
 *     ��ԁA�ً}�x���Ⴂ���O���x���iNGX_LOG_DEBUG�j�ŏ�������
 *     ���O���ۗL���邷�ׂẴ��O�ɂ��ďo�͌����E�o�͂���
 * @param[in]
 *     level: ���x��
 *     log: ���O�o�͂Ɏg�p���郍�O�\����
 *     err: 
 *     fmt: �G���[���b�Z�[�W�̃t�H�[�}�b�g
 *     args: �G���[���b�Z�[�W�{��
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

    // errstr �ɃG���[���b�Z�[�W���Z�b�g
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

    // �����ɉ��s����������X�y�[�X�𖳗������
    if (p > last - NGX_LINEFEED_SIZE) {
        p = last - NGX_LINEFEED_SIZE;
    }

    // ���s������ǉ�
    ngx_linefeed(p);

    (void) ngx_write_console(ngx_stderr, errstr, p - errstr);
}


/**
 * @brief
 *     ��R�����œn���ꂽ�G���[�ԍ����P�����œn���ꂽ�o�b�t�@�֏�������
 * @param[out]
 *     buf: �����o����o�b�t�@
 * @param[in]
 *     last: �����o����o�b�t�@�̍Ō�̔Ԓn
 *     err: �G���[�ԍ�
 * @retval
 *     ���̏������ސ惁�����Ԓn
 * @detail
 *     �X�y�[�X������Ă��Ȃ������狭���I�ɁA�O�ɏ�������ł������̂������ď�������
 *     ���̍ۂ� ... ���������ނ��Ƃŏ��������Ƃ��w������
 */
u_char *
ngx_log_errno(u_char *buf, u_char *last, ngx_err_t err)
{
    if (buf > last - 50) {

        /* leave a space for an error code */

        // �X�y�[�X������Ă��Ȃ������狭���I�ɍ��
        buf = last - 50;
        // �X�y�[�X�������I�ɍ�������Ƃ������Ă���
        *buf++ = '.';
        *buf++ = '.';
        *buf++ = '.';
    }

#if (NGX_WIN32)
    buf = ngx_slprintf(buf, last, ((unsigned) err < 0x80000000)
                                       ? " (%d: " : " (%Xd: ", err);
#else
    // �G���[�ԍ��� buf �ɏ�������
    buf = ngx_slprintf(buf, last, " (%d: ", err);
#endif

    buf = ngx_strerror(err, buf, last - buf);

    // �܂��X�y�[�X������Ȃ� ) ����������ŏI��������
    if (buf < last) {
        *buf++ = ')';
    }

    return buf;
}


/**
 * @brief
 *     �G���g���[���O������������
 *       �t�@�C���f�B�X�N���v�^�F�r���h���̃}�N���Ŏw�肳�ꂽ�t�@�C��
 *                               �����Ȃ�W���o��(ngx_stderr)
 *       ���O���x���FNOTICE
 *     err_log �f�B���N�e�B�u�Ŏw�肳�ꂽ���O�͂��̃G���g���[���O���ۗL���郍�O���X�g�֒ǉ�����Ă���
 * @param[in]
 *     prefix: �����Ńp�X�̎Q�ƂɎg�p����
 * @macro
 *     NGX_ERROR_LOG_PATH: �G���[���O���o�͂���t�@�C���̃p�X
 *     NGX_PREFIX: �n���ꂽ�p�X�����΃p�X�̂Ƃ��Ɏg�p����
 *     ngx_string, os/unix/ngx_files �̕�
 */
ngx_log_t *
ngx_log_init(u_char *prefix)
{
    u_char  *p, *name;
    size_t   nlen, plen;

    ngx_log.file = &ngx_log_file;
    // ���x���� NOTICE(6)�A
    // �܂�Angx_log_error() �ɂ�郍�O�o�͂� INFO, DEBUG �ȊO�͍s����
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

    // ��΃p�X�ł͂Ȃ��Ȃ�
    // ������ prefix ���^�����Ă�����A������g���Đ�΃p�X�����
    // �����łȂ��Ȃ�A
    // NGX_PREFIX ���w�肳��Ă���Ȃ炻����g��
    // �����łȂ��Ȃ牽�����Ȃ�
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
            // �����ŕʌɃ��������蓖�Ă��s���̂ŁA
            // NGX_ERROR_LOG_PATH �͕ύX����Ȃ�
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

    // �t�@�C�����J��
    // �ǉ��������݃��[�h�iwrite() �̂��тɃI�t�Z�b�g�𖖔��ցj
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
        // src/os/unix/ngx_files.h �Œ�`����Ă���
        ngx_log_file.fd = ngx_stderr;
    }

    if (p) {
        ngx_free(p);
    }

    return &ngx_log;
}


/**
 * @brief
 *     �T�C�N���� new_log �����ɏ������ݐ���I�[�v�����Ă��Ȃ��Ȃ�A
 *     NGX_ERROR_LOG_PATH �ŃI�[�v�����ď���������
 * @param[in:out]
 *     cycle: �����Ώۂ̃T�C�N��
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
 */
ngx_int_t
ngx_log_open_default(ngx_cycle_t *cycle)
{
    ngx_log_t         *log;
    static ngx_str_t   error_log = ngx_string(NGX_ERROR_LOG_PATH);

    // �������ł� new_log ���t�@�C�����J���Ă�����I��
    // ���������ɂ́A�����͌Ă΂�Ȃ��͂�
    /**
     * @brief
     *     ���O���X�g�̗v�f��擪����Q�Ƃ��āAfd �������Ă����炻���Ԃ�
     * @param[in]
     *     head: ���O���X�g�̐擪
     * @retval
     *     NULL: ������Ȃ�����
     *     otherwise: �������� fd
     * @detail
     *     �������� head ����ł� NULL ���Ԃ�
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
     *     ��Q���� name �Ŏw�肳�ꂽ�t�@�C���� cycle->open_files �Ƀv�b�V������
     *     name �� NULL �̏ꍇ�͕W���G���[�o�͂��J���� cycle->open_files ���X�g�Ƀv�b�V������
     *     name �ƊY������t�@�C�����Ȃ������ꍇ�� fd �� NGX_INVALID_FD ���Z�b�g���ꂽ�t�@�C�����Ԃ�
     *     cycle->open_files �ɂ���t�@�C���Q�� init_cycle() ���ŃI�[�v�������
     * @param[in]
     *     cycle: ���̃T�C�N�����ۗL����I�[�v���t�@�C���Q����A���O�p�̏o�͐�t�@�C����T��
     *     name: ���O�p�̏o�͐�t�@�C����
     * @retval
     *     NULL: �v���I�Ȏ��s
     *     otherwise: ���O�p�̏o�͐�t�@�C��
     *                �Ȃ��Afd �� INVALID_FILE �Ƃ��ĕς��邱�Ƃ�����i�T�������ǌ�����Ȃ������ꍇ�j
     * @detail
     *     �����ŁA�T�C�N�������ݒ�t�@�C���p�̃v���t�B�N�X����p���ăp�X���w�肷��
     *     �����Ŏ��ۂɊJ���̂ł͂Ȃ��A���łɊJ����Ă��� cycle->open_files ���̃t�@�C�����疼�O���Y��������̂�Ԃ�����
     */
    log->file = ngx_conf_open_file(cycle, &error_log);
    if (log->file == NULL) {
        return NGX_ERROR;
    }

    // ���ł� new_log �ɂP�ȏ�̃��O�����݂��Ă���P�[�X
    // ���������́A�����͌Ă΂�Ȃ�
    if (log != &cycle->new_log) {
        /**
         * @brief
         *     �V�������O�\���̂������̃��O�\���̃��X�g�ɑ}������
         *     �Ȃ��A�ً}�x�ɂ����я��ɔ��肪����̂ŁA�ꍇ�ɂ���Ă� log �� new_log �̈ʒu�����ւ���
         *     new_log �ً̋}�x�� log ���Ⴂ�ꍇ�́Anew_log �̎��� log ��z�u����
         *         �����āAlog �� new_log ���w�����̂����ւ���
         *     �����łȂ��Ȃ�A���O���X�g�̂����A�ً}�x���������ɂ��܂����Ԃ悤�ɂ����ʒu�ɑ}������
         * @para[out]
         *     log: �������ێ����郊�X�g�ɑ}������
         * @param[in]
         *     new_log: ������}������
         */
        ngx_log_insert(&cycle->new_log, log);
    }

    return NGX_OK;
}


/**
 * @brief
 *     ���O���X�g�̗v�f��擪����Q�Ƃ��āA�ŏ��Ɍ������� fd ��W���G���[�o�͂ɕ�������
 * @param[out:in]
 *     cycle: ���̃T�C�N�����������郍�O���X�g�������Ώ�
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
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
     *     ���O���X�g�̗v�f��擪����Q�Ƃ��āAfd �������Ă����炻���Ԃ�
     * @param[in]
     *     head: ���O���X�g�̐擪
     * @retval
     *     NULL: ������Ȃ�����
     *     otherwise: �������� fd
     * @detail
     *     �������� head ����ł� NULL ���Ԃ�
     */
    fd = ngx_log_get_file_log(cycle->log)->file->fd;

    // �W���G���[�o�͂ł͂Ȃ��Ȃ�Adup() �ŕ������ĕW���G���[�o�͂��w���悤�ɂ���
    if (fd != ngx_stderr) {
        // �قȂ�n���h���œ���t�@�C�����w�����߂� dup() ���g�p����
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
 *     ���O���X�g�̗v�f��擪����Q�Ƃ��āAfd �������Ă����炻���Ԃ�
 * @param[in]
 *     head: ���O���X�g�̐擪
 * @retval
 *     NULL: ������Ȃ�����
 *     otherwise: �������� fd
 * @detail
 *     �������� head ����ł� NULL ���Ԃ�
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
 *     ��P�����g�[�N���Q���烍�O���x������T���āA���O�̃��x����ݒ肷��
 * @param[in]
 *     cf: �g�[�N���Q
 *     log: ���O
 * @retval
 *     NGX_CONF_OK: ����
 *     NGX_CONF_ERROR: ���s
 * @pre
 *     err_levels[] �ɃG���[���x����񂪃Z�b�g����Ă���
 *     degug_levels[] �Ƀf�o�b�O���x����񂪃Z�b�g����Ă���
 * @post
 *     log->log_level �Ƀ��O���x�����Z�b�g����Ă���
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

    // �t�@�C�����̌�Ƀ��O���x����񂪑����Ă�����A�������������
    for (i = 2; i < cf->args->nelts; i++) {
        found = 0;

        // ����͐����ȃ��O���x���Ȃ̂ŁA���O�̃��x�����܂����ݒ�Ȃ�K�p���ďI��
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
 *     �ݒ�t�@�C���� error_log �f�B���N�e�B�u����������
 * @param[in]
 *     cf: �ݒ�������ǂ�\����
 *     cmd: �R�}���h
 *     conf: ���̐ݒ�t�@�C���Ɋi�[����
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
 *     �V�������O�����܂���Ƀ��X�g�ɒǉ�����
 * @retval
 *     NGX_CONF_OK: ����
 *     NGX_CONF_ERROR: ���s
 */
char *
ngx_log_set_log(ngx_conf_t *cf, ngx_log_t **head)
{
    ngx_log_t          *new_log;
    ngx_str_t          *value, name;
    ngx_syslog_peer_t  *peer;

    // new_log ���X�g�ɒ��g���Ȃ��Ȃ�V�������蓖�ĂĂ�����ΏۂɑI���A������ new_log ���X�g�̐擪�ɂ���
    // new_log ���X�g�ɒ��g�����邪�A���O���x���� 0 �Ȃ炻����ΏۂɑI��
    // new_log ���X�g�ɒ��g�����邪�A���O���x���� 0 �łȂ��Ȃ�V�������蓖�ĂĂ�����I��
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
        // ngx_conf_open_file() �� name �� NULL ���ƕW���G���[�o�͂�Ԃ��̂�
        ngx_str_null(&name);
        // ���O�͕W���G���[�o�͂ɃG���[����������
        cf->cycle->log_use_stderr = 1;

        /**
         * @brief
         *     ��Q���� name �Ŏw�肳�ꂽ�t�@�C�����J���ĕԂ�
         *     name �� NULL �̏ꍇ�͕W���G���[�o�͂��J���� cycle->open_files ���X�g�Ƀv�b�V������
         *     name �ƊY������t�@�C�����Ȃ������ꍇ�� fd �� NGX_INVALID_FD ���Z�b�g���ꂽ�t�@�C�����Ԃ�
         * @param[in]
         *     cycle: ���̃T�C�N�����ۗL����I�[�v���t�@�C���Q����A���O�p�̏o�͐�t�@�C����T��
         *     name: ���O�p�̏o�͐�t�@�C����
         * @retval
         *     NULL: �v���I�Ȏ��s
         *     otherwise: ���O�p�̏o�͐�t�@�C��
         *                �Ȃ��Afd �� INVALID_FILE �Ƃ��ĕς��邱�Ƃ�����i�T�������ǌ�����Ȃ������ꍇ�j
         * @detail
         *     �����ŁA�T�C�N�������ݒ�t�@�C���p�̃v���t�B�N�X����p���ăp�X���w�肷��
         *     �����Ŏ��ۂɊJ���̂ł͂Ȃ��A���łɊJ����Ă��� cycle->open_files ���̃t�@�C�����疼�O���Y��������̂�Ԃ�����
         */
        new_log->file = ngx_conf_open_file(cf->cycle, &name);
        if (new_log->file == NULL) {
            return NGX_CONF_ERROR;
        }

    } else if (ngx_strncmp(value[1].data, "memory:", 7) == 0) {

        // �f�o�b�O�p�̐ݒ���ۂ�
        // �f�o�b�O�֘A�͂Ƃ肠������΂�
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
        // �����m��񂯂ǎ��ғ��ł͋����G���[
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

        // syslog ���w�肳��Ă����Ƃ��ɂ̂݁Awriter �l���L���������͗l
        // peer �Ƃ́H
        new_log->writer = ngx_syslog_writer;
        new_log->wdata = peer;

    } else {
        // memory: syslog: stderr: �̂ǂ�ł��Ȃ��Ȃ�A����̓t�@�C���̃p�X�ł���
        // �J�����Ƃ���������
        new_log->file = ngx_conf_open_file(cf->cycle, &value[1]);
        if (new_log->file == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    /**
     * @brief
     *     ��P�����g�[�N���Q���烍�O���x������T���āA���O�̃��x����ݒ肷��
     * @param[in]
     *     cf: �g�[�N���Q
     *     log: ���O
     * @retval
     *     NGX_CONF_OK: ����
     *     NGX_CONF_ERROR: ���s
     * @pre
     *     err_levels[] �ɃG���[���x����񂪃Z�b�g����Ă���
     *     degug_levels[] �Ƀf�o�b�O���x����񂪃Z�b�g����Ă���
     * @post
     *     log->log_level �Ƀ��O���x�����Z�b�g����Ă���
     */
    if (ngx_log_set_levels(cf, new_log) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }

    // �K�v�ł���� new_log �ɒǉ�
    if (*head != new_log) {
        ngx_log_insert(*head, new_log);
    }

    return NGX_CONF_OK;
}


/**
 * @brief
 *     �V�������O�\���̂������̃��O�\���̃��X�g�ɑ}������
 *     �Ȃ��A�ً}�x�ɂ����я��ɔ��肪����̂ŁA�ꍇ�ɂ���Ă� log �� new_log �̈ʒu�����ւ���
 *     new_log �ً̋}�x�� log ���Ⴂ�ꍇ�́Anew_log �̎��� log ��z�u����
 *         �����āAlog �� new_log ���w�����̂����ւ���
 *     �����łȂ��Ȃ�A���O���X�g�̂����A�ً}�x���������ɂ��܂����Ԃ悤�ɂ����ʒu�ɑ}������
 * @para[out]
 *     log: �������ێ����郊�X�g�ɑ}������
 * @param[in]
 *     new_log: ������}������
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
   *     �o�b�t�@�̓��e�����O�\���̂̃o�b�t�@�֏�������
   * @param[out]
   *     log: ���̃��O�� wdata �����o�i�������o�b�t�@�j�ɏ�������
   * @param[in]
   *     level: �g�p���Ȃ�
   *     buf: �������݂����f�[�^
   *     len: �������݂����T�C�Y
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

      // �I�t�Z�b�g��i�߂�
      written = ngx_atomic_fetch_add(&mem->written, len);

      // �i����܂łɏ������񂾗� % �o�b�t�@�̎c��e�ʁj
      //  �����_���Ɉʒu�����炵�Ă���H
      p = mem->pos + written % (mem->end - mem->pos);

      avail = mem->end - p;

      // �󂫗e�ʂ�����Ă���Ȃ珑������
      // �����łȂ��Ȃ�A�������߂�Ƃ���܂ŏ�������ŁA�c��� mem->pos �ɏ�������
      if (avail >= len) {
          ngx_memcpy(p, buf, len);

      } else {
          ngx_memcpy(p, buf, avail);
          ngx_memcpy(mem->pos, buf + avail, len - avail);
      }
  }


  /**
   * @brief
   *     wdata �����o�� NULL �ɒu��������i�J���͂��Ȃ��A�����炭�v�[���̊J�����ɂ܂Ƃ߂ĊJ�����邽�߁j
   * @param[in]
   *     data: �����Ώۂ̃��O�\����
   */
  static void
  ngx_log_memory_cleanup(void *data)
  {
      ngx_log_t *log = data;

      ngx_log_debug0(NGX_LOG_DEBUG_CORE, log, 0, "destroy memory log buffer");

      log->wdata = NULL;
  }

#endif
