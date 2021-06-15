
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_channel.h>


// ���̊֐��͌����Ď��s����Ȃ�
// MASTER_PROCESS ���[�h�̂Ƃ��Ƀ��C�����[�v���ŌĂ΂��
ngx_int_t
ngx_write_channel(ngx_socket_t s, ngx_channel_t *ch, size_t size,
    ngx_log_t *log)
{
    ssize_t             n;
    ngx_err_t           err;
    struct iovec        iov[1];
    struct msghdr       msg;

#if (NGX_HAVE_MSGHDR_MSG_CONTROL)

    union {
        struct cmsghdr  cm;
        char            space[CMSG_SPACE(sizeof(int))];
    } cmsg;

    if (ch->fd == -1) {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;

    } else {
        msg.msg_control = (caddr_t) &cmsg;
        msg.msg_controllen = sizeof(cmsg);

        ngx_memzero(&cmsg, sizeof(cmsg));

        cmsg.cm.cmsg_len = CMSG_LEN(sizeof(int));
        cmsg.cm.cmsg_level = SOL_SOCKET;
        cmsg.cm.cmsg_type = SCM_RIGHTS;

        /*
         * We have to use ngx_memcpy() instead of simple
         *   *(int *) CMSG_DATA(&cmsg.cm) = ch->fd;
         * because some gcc 4.4 with -O2/3/s optimization issues the warning:
         *   dereferencing type-punned pointer will break strict-aliasing rules
         *
         * Fortunately, gcc with -O1 compiles this ngx_memcpy()
         * in the same simple assignment as in the code above
         */

        ngx_memcpy(CMSG_DATA(&cmsg.cm), &ch->fd, sizeof(int));
    }

    msg.msg_flags = 0;

#else

    if (ch->fd == -1) {
        msg.msg_accrights = NULL;
        msg.msg_accrightslen = 0;

    } else {
        msg.msg_accrights = (caddr_t) &ch->fd;
        msg.msg_accrightslen = sizeof(int);
    }

#endif

    iov[0].iov_base = (char *) ch;
    iov[0].iov_len = size;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    n = sendmsg(s, &msg, 0);

    if (n == -1) {
        err = ngx_errno;
        if (err == NGX_EAGAIN) {
            return NGX_AGAIN;
        }

        ngx_log_error(NGX_LOG_ALERT, log, err, "sendmsg() failed");
        return NGX_ERROR;
    }

    return NGX_OK;
}


// ���̊֐��͌����Ď��s����Ȃ�
// MASTER_PROCESS ���[�h�̂Ƃ��Ƀ��C�����[�v���ŌĂ΂��
ngx_int_t
ngx_read_channel(ngx_socket_t s, ngx_channel_t *ch, size_t size, ngx_log_t *log)
{
    ssize_t             n;
    ngx_err_t           err;
    struct iovec        iov[1];
    struct msghdr       msg;

#if (NGX_HAVE_MSGHDR_MSG_CONTROL)
    union {
        struct cmsghdr  cm;
        char            space[CMSG_SPACE(sizeof(int))];
    } cmsg;
#else
    int                 fd;
#endif

    iov[0].iov_base = (char *) ch;
    iov[0].iov_len = size;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

#if (NGX_HAVE_MSGHDR_MSG_CONTROL)
    msg.msg_control = (caddr_t) &cmsg;
    msg.msg_controllen = sizeof(cmsg);
#else
    msg.msg_accrights = (caddr_t) &fd;
    msg.msg_accrightslen = sizeof(int);
#endif

    n = recvmsg(s, &msg, 0);

    if (n == -1) {
        err = ngx_errno;
        if (err == NGX_EAGAIN) {
            return NGX_AGAIN;
        }

        ngx_log_error(NGX_LOG_ALERT, log, err, "recvmsg() failed");
        return NGX_ERROR;
    }

    if (n == 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, log, 0, "recvmsg() returned zero");
        return NGX_ERROR;
    }

    if ((size_t) n < sizeof(ngx_channel_t)) {
        ngx_log_error(NGX_LOG_ALERT, log, 0,
                      "recvmsg() returned not enough data: %z", n);
        return NGX_ERROR;
    }

#if (NGX_HAVE_MSGHDR_MSG_CONTROL)

    if (ch->command == NGX_CMD_OPEN_CHANNEL) {

        if (cmsg.cm.cmsg_len < (socklen_t) CMSG_LEN(sizeof(int))) {
            ngx_log_error(NGX_LOG_ALERT, log, 0,
                          "recvmsg() returned too small ancillary data");
            return NGX_ERROR;
        }

        if (cmsg.cm.cmsg_level != SOL_SOCKET || cmsg.cm.cmsg_type != SCM_RIGHTS)
        {
            ngx_log_error(NGX_LOG_ALERT, log, 0,
                          "recvmsg() returned invalid ancillary data "
                          "level %d or type %d",
                          cmsg.cm.cmsg_level, cmsg.cm.cmsg_type);
            return NGX_ERROR;
        }

        /* ch->fd = *(int *) CMSG_DATA(&cmsg.cm); */

        ngx_memcpy(&ch->fd, CMSG_DATA(&cmsg.cm), sizeof(int));
    }

    if (msg.msg_flags & (MSG_TRUNC|MSG_CTRUNC)) {
        ngx_log_error(NGX_LOG_ALERT, log, 0,
                      "recvmsg() truncated data");
    }

#else

    if (ch->command == NGX_CMD_OPEN_CHANNEL) {
        if (msg.msg_accrightslen != sizeof(int)) {
            ngx_log_error(NGX_LOG_ALERT, log, 0,
                          "recvmsg() returned no ancillary data");
            return NGX_ERROR;
        }

        ch->fd = fd;
    }

#endif

    return n;
}


// ���̊֐��͌����Ď��s����Ȃ�
// MASTER_PROCESS ���[�h�̂Ƃ��Ƀ��C�����[�v���ŌĂ΂��
/**
 * @brief
 *     �n���ꂽ�R�l�N�V�����ɃC�x���g���d�|����
 *     �`�����l���F�C�x���g�ƃn���h�����d�|����ꂽ�R�l�N�V�����̂���
 * @param[in]
 *     cycle: ���̃T�C�N���̃v�[�����g�p����
 *     fd: �R�l�N�V����
 *     event: �ǂ� or �������w�肷��
 *     handler: �C�x���g�����������Ƃ��ɌĂяo�����n���h��
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
 */
ngx_int_t
ngx_add_channel_event(ngx_cycle_t *cycle, ngx_fd_t fd, ngx_int_t event,
    ngx_event_handler_pt handler)
{
    ngx_event_t       *ev, *rev, *wev;
    ngx_connection_t  *c;

    // fd ����R�l�N�V���������擾
    c = ngx_get_connection(fd, cycle->log);

    if (c == NULL) {
        return NGX_ERROR;
    }

    // ���̃R�l�N�V�������g�p����v�[���ɃT�C�N���̂��̂����蓖�Ă�
    c->pool = cycle->pool;

    rev = c->read;
    wev = c->write;

    // ���̃R�l�N�V�����̓ǂ݁E�����̗��C�x���g�ɃT�C�N���̃��O���Z�b�g����
    rev->log = cycle->log;
    wev->log = cycle->log;

    // �`�����l���ł��邱�Ƃ��t���O�t������
    rev->channel = 1;
    wev->channel = 1;

    ev = (event == NGX_READ_EVENT) ? rev : wev;

    // �C�x���g�Ƀn���h����ݒ肷��
    ev->handler = handler;

    if (ngx_add_conn && (ngx_event_flags & NGX_USE_EPOLL_EVENT) == 0) {
        // POLL ���� ngx_add_conn() �͒�`����Ă��Ȃ�
        if (ngx_add_conn(c) == NGX_ERROR) {
            ngx_free_connection(c);
            return NGX_ERROR;
        }

    } else {
        // POLL ���Ƃ�����
        // ngx_add_event() �� ngx_poll_add_event() �Ɠ���
        if (ngx_add_event(ev, event, 0) == NGX_ERROR) {
            ngx_free_connection(c);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


// ���̊֐��͌����Ď��s����Ȃ�
// MASTER_PROCESS ���[�h�̂Ƃ��Ƀ��C�����[�v���ŌĂ΂��
void
ngx_close_channel(ngx_fd_t *fd, ngx_log_t *log)
{
    if (close(fd[0]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno, "close() channel failed");
    }

    if (close(fd[1]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno, "close() channel failed");
    }
}
