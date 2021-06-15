
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
 *     OS �Ɉˑ����鏉�����������ꋓ�ɍs��
 * @param[in]
 *     log: ���O�̏o�͂Ɏg�p
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
 * @post
 *     �ÓI�ϐ� ngx_linux_kern_ostype �� os �̃^�C�v��񂪊i�[����Ă���
 *     �ÓI�ϐ� ngx_linux_kern_osrelease �� os �̃����[�X��񂪊i�[����Ă���
 *     �ÓI�ϐ� ngx_os_io �� OS �ˑ��̓��o�͏����Q���Z�b�g����Ă���
 *     �ÓI�ϐ� ngx_os_argv_last �� ngx_os_argv �̍Ō�̔Ԓn���w���Ă���
 *     �ÓI�ϐ� environ ���K�v�ł���΃��������蓖�Ē�������Ă���
 *     �ÓI�ϐ� ngx_pagsize �Ƀy�[�W�T�C�Y���i�[����Ă���
 *     �ÓI�ϐ� ngx_cacheline_size �ɃL���b�V�����C���T�C�Y���i�[����Ă���
 *     �ÓI�ϐ� ngx_pagesize_shift �� ngx_pagesize �� 2 �̉��悩�i�[����Ă���
 *     �ÓI�ϐ� ngx_ncpu �Ɍ��ݗ��p�\�ȃv���Z�b�T�̐����i�[����Ă���
 *     �ÓI�ϐ� ngx_max_sockets �ɃI�[�v���\�ȃ\�P�b�g�̍ő吔���i�[����Ă���
 *     �ÓI�ϐ� ngx_inherited_nonblocking �� fcntl() �ł��������m���u���b�L���O���[�h�ɕύX����K�v�����邩�i�[����Ă���
 *     os �̃����_���֐��̎�ɂ�鏉�������������Ă���
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
     *     OS �̃^�C�v�ƃ����[�X�o�[�W������ÓI�ϐ��֑������
     *     �܂��AOS �ˑ��̓��o�͊֐��Q���Z�b�g����
     * @param[in]
     *     log: ���O�o�͂Ɏg�p
     * @retval
     *     NGX_OK: ����I��
     *     NGX_ERROR: �ُ�I��
     */
    if (ngx_os_specific_init(log) != NGX_OK) {
        return NGX_ERROR;
    }
#endif

    /**
     * @brief
     *     ngx_os_argv(argv) �̌�� environ �������Ă��邱�Ƃ��m�F����
     *     �����ł���΁A�v���Z�X�^�C�g�����������炵���Ɗ��ϐ������㏑�����Ă��܂��̂�
     *     ���ϐ������R�s�[���Ă������� environ ���w���悤�ɑ΍􂷂�
     *     �܂��Angx_os_argv_last �� ngx_os_argv �̍Ō���w���悤�C������
     * @param[in]
     *     log: ���O�o�͂Ɏg�p
     * @retval
     *     NGX_OK: ����
     *     NGX_ERROR: ���s
     * @post
     *     environ ���V�X�e���R���̃f�[�^�ł͂Ȃ��Anginx ���ۗL������ϐ�������Q���w���Ă���
     *     ngx_os_argv_last �� ngx_os_argv �̍Ō�̔Ԓn���w���Ă���
     */
    if (ngx_init_setproctitle(log) != NGX_OK) {
        return NGX_ERROR;
    }

    // �y�[�W�T�C�Y���擾
    ngx_pagesize = getpagesize();
    // �L���b�V�����C���T�C�Y���擾
    ngx_cacheline_size = NGX_CPU_CACHE_LINE;

    // �y�[�W�T�C�Y�͂Q�̉��悩�� ngx_pagesize_shift �Ɋi�[����
    for (n = ngx_pagesize; n >>= 1; ngx_pagesize_shift++) { /* void */ }

#if (NGX_HAVE_SC_NPROCESSORS_ONLN)
    if (ngx_ncpu == 0) {
        // sysconf() �̓J�[�l���̃I�v�V�����l���擾���郁�\�b�h
        // _SC_NPROCESSORS_ONLN �ŗ��p�\�ȃv���Z�b�T�����擾�ł���
        // ����������ŃZ�b�g
        ngx_ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    }
#endif

    // ���܂��擾�ł��Ă��Ȃ�������P�������Ƃ������Ƃɂ��Ƃ�
    if (ngx_ncpu < 1) {
        ngx_ncpu = 1;
    }

#if (NGX_HAVE_LEVEL1_DCACHE_LINESIZE)
    // ���x���P�f�[�^�L���b�V���������Ă���
    // ���̃T�C�Y���擾���� ngx_cacheline_size �փZ�b�g
    size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (size > 0) {
        ngx_cacheline_size = size;
    }
#endif

    /**
     * @brief
     *     CPUID �A�Z���u�����߂���āA�x���_�������ƂɃL���b�V�����C���T�C�Y�����肷��
     * @post
     *     ngx_cacheline_size �ɃL���b�V�����C���T�C�Y���i�[����Ă���iCPU �ɂ���Ă͊i�[����Ȃ��j
     * @detail
     *     (( __i386__ || __amd64__ ) && ( __GNUC__ || __INTEL_COMPILER ))
     *     �����������ꍇ�Ɍ���A�L���ȃ��\�b�h
     *     ����ȊO�ł���Ή������Ȃ�
     */
    ngx_cpuinfo();

    // �ő�\�P�b�g�����擾
    if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
        ngx_log_error(NGX_LOG_ALERT, log, errno,
                      "getrlimit(RLIMIT_NOFILE) failed");
        return NGX_ERROR;
    }

    // �ő�\�P�b�g�����Z�b�g
    ngx_max_sockets = (ngx_int_t) rlmt.rlim_cur;

#if (NGX_HAVE_INHERITED_NONBLOCK || NGX_HAVE_ACCEPT4)
    // accept4() �� accept() �̋@�\�g����
    // �t���O�ɒl���Z�b�g���邱�Ƃł��܂��܂ȈقȂ铮�삪�ł���
    // ���̂����̈���A�m���u���b�L���O���[�h�� accept ����Ƃ�������
    // ���̂��߁A�킴�킴 fcntl() �Ńt���O���Z�b�g����K�v���Ȃ�

    // �킴�킴�m���u���b�L���O���[�h�ɕύX���Ȃ��Ă悢���Ƃ�\��
    ngx_inherited_nonblocking = 1;
#else
    // fcntl() �ɂ��m���u���b�L���O���[�h�ւ̕ύX���K�v�ł��邱�Ƃ�\��
    ngx_inherited_nonblocking = 0;
#endif

    // �����_���n�֐��̏������i��ɂ�鏉�����j
    tp = ngx_timeofday();
    srandom(((unsigned) ngx_pid << 16) ^ tp->sec ^ tp->msec);

    return NGX_OK;
}


/**
 * @brief
 *     freebsd �� solaris �Ƃ����� OS ���L�̐ݒ莖�������O�֏o�͂���
 * @param[in]
 *     log: �o�͐�̃��O
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
