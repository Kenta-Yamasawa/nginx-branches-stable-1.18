
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


ngx_chain_t *
ngx_writev_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{
    ssize_t        n, sent;
    off_t          send, prev_send;
    ngx_chain_t   *cl;
    ngx_event_t   *wev;
    ngx_iovec_t    vec;
    struct iovec   iovs[NGX_IOVS_PREALLOCATE];

    // ���M�C�x���g���擾����
    wev = c->write;

    // �܂��������ł��Ă��Ȃ��Ȃ�A�܂��������߂ĂȂ�����Ԃ�
    if (!wev->ready) {
        return in;
    }

// ������ POLL ���Ǝ��s����Ȃ�
#if (NGX_HAVE_KQUEUE)

    if ((ngx_event_flags & NGX_USE_KQUEUE_EVENT) && wev->pending_eof) {
        (void) ngx_connection_error(c, wev->kq_errno,
                               "kevent() reported about an closed connection");
        wev->error = 1;
        return NGX_CHAIN_ERROR;
    }

#endif

    /* the maximum limit size is the maximum size_t value - the page size */

    // �s���ȏ���T�C�Y��������C������
    if (limit == 0 || limit > (off_t) (NGX_MAX_SIZE_T_VALUE - ngx_pagesize)) {
        limit = NGX_MAX_SIZE_T_VALUE - ngx_pagesize;
    }

    // ���M�����o�C�g��
    send = 0;

    vec.iovs = iovs;
    vec.nalloc = NGX_IOVS_PREALLOCATE;

    for ( ;; ) {
        prev_send = send;

        /* create the iovec and coalesce the neighbouring bufs */

        /**
         * @brief
         *     ngx_writev() �̌Ăяo���ɕK�v�� ngx_iovec_t �\���̂𐶐�����
         * @param[out]
         *     vec: �������� ngx_iovec_t �\���̂̏o�͐�
         * @param[in]
         *     in: ���M���ׂ��f�[�^
         *     limit: ���M�ł���ő��
         *     log: ���O�o�͗p
         * @retval
         *     NGX_CHAIN_ERROR: �ُ�I��
         *     otherwise: ����I��
         * @post
         *     vec->iovs[] �ɏ������ނׂ��e�o�b�t�@�������Ă���
         *     vec->iovs[]->base �ɏ������ނׂ��f�[�^�̐擪�|�C���^�������Ă���
         *     vec->iovs[]->len  �ɏ������ނׂ��T�C�Y�������Ă���
         *     vec->count  �� iovs �ɂ����̃o�b�t�@�������Ă��邩�����Ă���
         *     vec->size   �ɍ��v�������ݗʂ������Ă���
         */
        cl = ngx_output_chain_to_iovec(&vec, in, limit - send, c->log);

        if (cl == NGX_CHAIN_ERROR) {
            return NGX_CHAIN_ERROR;
        }

        if (cl && cl->buf->in_file) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "file buf in writev "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();

            return NGX_CHAIN_ERROR;
        }

        send += vec.size;

        n = ngx_writev(c, &vec);

        if (n == NGX_ERROR) {
            return NGX_CHAIN_ERROR;
        }

        sent = (n == NGX_AGAIN) ? 0 : n;

        // ���̃R�l�N�V�����ő��M�����f�[�^��
        c->sent += sent;

        /**
         * @brief
         *     ���M�����������A�o�b�t�@�`�F�C���̊e�o�b�t�@�̃f�[�^�擪�|�C���^���X�V����B
         * @param[out]
         *     in: �o�b�t�@�`�F�C��
         * @param[in]
         *     sent: ���M�����f�[�^��
         * @detail
         *     in �̒��g�ɕω��͂Ȃ�
         */
        in = ngx_chain_update_sent(in, sent);

        // ����A�S�����M�ł��Ȃ�����
        if (send - prev_send != sent) {
            wev->ready = 0;
            return in;
        }

        // �������߂�ʂ𒴉߂��� or ���ׂď�������
        if (send >= limit || in == NULL) {
            return in;
        }
    }
}


/**
 * @brief
 *     ngx_writev() �̌Ăяo���ɕK�v�� ngx_iovec_t �\���̂𐶐�����
 * @param[out]
 *     vec: �������� ngx_iovec_t �\���̂̏o�͐�
 * @param[in]
 *     in: ���M���ׂ��f�[�^
 *     limit: ���M�ł���ő��
 *     log: ���O�o�͗p
 * @retval
 *     NGX_CHAIN_ERROR: �ُ�I��
 *     otherwise: ����I��
 * @post
 *     vec->iovs[] �ɏ������ނׂ��e�o�b�t�@�������Ă���
 *       vec->iovs[]->base �ɏ������ނׂ��f�[�^�̐擪�|�C���^�������Ă���
 *       vec->iovs[]->len  �ɏ������ނׂ��T�C�Y�������Ă���
 *     vec->count  �� iovs �ɂ����̃o�b�t�@�������Ă��邩�����Ă���
 *     vec->size   �ɍ��v�������ݗʂ������Ă���
 */
ngx_chain_t *
ngx_output_chain_to_iovec(ngx_iovec_t *vec, ngx_chain_t *in, size_t limit,
    ngx_log_t *log)
{
    size_t         total, size;
    u_char        *prev;
    ngx_uint_t     n;
    struct iovec  *iov;

    iov = NULL;
    prev = NULL;
    total = 0;
    n = 0;

    // �܂����M�ł������Ain ����f�[�^�����o���Ă���
    for ( /* void */ ; in && total < limit; in = in->next) {

        // ���̃o�b�t�@�ɓ��ʂȃt���O�������Ă���P�[�X
        if (ngx_buf_special(in->buf)) {
            continue;
        }

        // ���̃o�b�t�@�ɓ��ʂȃt���O�������Ă���P�[�X
        if (in->buf->in_file) {
            break;
        }

        // ���̃o�b�t�@�ɓ��ʂȃt���O�������Ă���P�[�X
        if (!ngx_buf_in_memory(in->buf)) {
            ngx_log_error(NGX_LOG_ALERT, log, 0,
                          "bad buf in output chain "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          in->buf->temporary,
                          in->buf->recycled,
                          in->buf->in_file,
                          in->buf->start,
                          in->buf->pos,
                          in->buf->last,
                          in->buf->file,
                          in->buf->file_pos,
                          in->buf->file_last);

            ngx_debug_point();

            return NGX_CHAIN_ERROR;
        }

        // ���̃o�b�t�@�ɂ��āA�������ނׂ��e��
        size = in->buf->last - in->buf->pos;

        // ����𒴉߂��Ă�����C��
        if (size > limit - total) {
            size = limit - total;
        }

        if (prev == in->buf->pos) {
            // �o�b�t�@->next == �o�b�t�@ �Ń��[�v�����P�[�X
            // �������Ēʂ�́H
            iov->iov_len += size;

        } else {
            if (n == vec->nalloc) {
                break;
            }

            // iovs �ɍ��� buf ���ڂ�
            iov = &vec->iovs[n++];

            iov->iov_base = (void *) in->buf->pos;
            iov->iov_len = size;
        }

        // �O�̃o�b�t�@�ɂ��āA�܂��������܂�Ă��Ȃ����̂ւ̃|�C���^
        prev = in->buf->pos + size;
        // �g�[�^���ŏ������ނׂ��e�ʂ��X�V
        total += size;
    }

    // iovs �ɂ�����C���f�b�N�X
    vec->count = n;
    // ���v�̏������ނׂ���
    vec->size = total;

    return in;
}


ssize_t
ngx_writev(ngx_connection_t *c, ngx_iovec_t *vec)
{
    ssize_t    n;
    ngx_err_t  err;

eintr:

    n = writev(c->fd, vec->iovs, vec->count);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "writev: %z of %uz", n, vec->size);

    if (n == -1) {
        switch (err) {
        case NGX_EAGAIN:
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,
                           "writev() not ready");
            return NGX_AGAIN;

        case NGX_EINTR:
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,
                           "writev() was interrupted");
            goto eintr;

        default:
            c->write->error = 1;
            ngx_connection_error(c, err, "writev() failed");
            return NGX_ERROR;
        }
    }

    return n;
}
