
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


/**
 * @brief
 *     �o�b�t�@�`�F�C�����X�g���ۗL���邷�ׂẴo�b�t�@����t�ɂȂ�܂Ŏ�M���邱�Ƃ����݂�
 *     �������Alimit �o�C�g�ȏ�͎�M���Ȃ�
 *     �������AIOV_MAX �܂ł����o�b�t�@�`�F�C�����g�p���Ȃ��i����ȏ�͖����j
 * @param[in]
 *     c: �R�l�N�V����
 *     chain: ��M�Ɏg�p����o�b�t�@�`�F�C�����X�g
 *     limit: ���E��M�o�C�g��
 * @retval
 *     n: ��M�o�C�g��
 *     0: ����ȏ�A��M�ł��Ȃ�
 *     NGX_ERROR: �G���[�I��1
 *     NGX_AGAIN: �G���[�I��2
 * @detail
 *     readv() �ɐ������āA���ׂĂ���M�����i�o�b�t�@��t or limit �o�C�g�j��A��M�����o�C�g�ʂ�Ԃ�
 *     readv() �ɐ����������A���ׂĂ���M�ł��Ȃ������� ready �t���O�� 0 �ɁA��M�����o�C�g�ʂ�Ԃ�
 *     readv() ���Ăяo���āA����ȏ�A�ǂݏo���Ȃ��Ƃ��� ready �t���O�� 0 �ɁAeof �t���O�� 1 �ɂ��� 0 ��Ԃ�
 *     readv() ���Ăяo���� E_INTR ���Ԃ邽�тɏ������J��Ԃ�
 *     readv() ���Ăяo���� E_AGAIN ���Ԃ����� ready �t���O�� 0 �ɁANGX_AGAIN ��Ԃ�
 *     readv() ���Ăяo���� E_ERROR ���Ԃ����� ready �t���O�� 0 �ɁAerror �t���O�� 1 �ɂ��� NGX_ERROR ��Ԃ�
 */
ssize_t
ngx_readv_chain(ngx_connection_t *c, ngx_chain_t *chain, off_t limit)
{
    u_char        *prev;
    ssize_t        n, size;
    ngx_err_t      err;
    ngx_array_t    vec;
    ngx_event_t   *rev;
    struct iovec  *iov, iovs[NGX_IOVS_PREALLOCATE];

    // ���̃R�l�N�V�����̎�M�C�x���g���擾����
    rev = c->read;

#if (NGX_HAVE_KQUEUE)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "readv: eof:%d, avail:%d, err:%d",
                       rev->pending_eof, rev->available, rev->kq_errno);

        if (rev->available == 0) {
            if (rev->pending_eof) {
                rev->ready = 0;
                rev->eof = 1;

                ngx_log_error(NGX_LOG_INFO, c->log, rev->kq_errno,
                              "kevent() reported about an closed connection");

                if (rev->kq_errno) {
                    rev->error = 1;
                    ngx_set_socket_errno(rev->kq_errno);
                    return NGX_ERROR;
                }

                return 0;

            } else {
                return NGX_AGAIN;
            }
        }
    }

#endif

#if (NGX_HAVE_EPOLLRDHUP)

    if (ngx_event_flags & NGX_USE_EPOLL_EVENT) {
        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "readv: eof:%d, avail:%d",
                       rev->pending_eof, rev->available);

        if (rev->available == 0 && !rev->pending_eof) {
            return NGX_AGAIN;
        }
    }

#endif

    prev = NULL;
    iov = NULL;
    size = 0;

    vec.elts = iovs;
    vec.nelts = 0;
    vec.size = sizeof(struct iovec);
    vec.nalloc = NGX_IOVS_PREALLOCATE; // IOV_MAX �Ɗ�{�I�ɂ͓���
    vec.pool = c->pool;

    /* coalesce the neighbouring bufs */

    // �o�b�t�@�`�F�C�����X�g�̐擪�o�b�t�@�`�F�C������Q�Ƃ��Ă���
    while (chain) {
        // n �����̃o�b�t�@�`�F�C������̎�M�e�ʂɐݒ肷��
        n = chain->buf->end - chain->buf->last;

        if (limit) {
            // ��M���E�ʂ𒴉߂����烋�[�v�𔲂���
            if (size >= limit) {
                // ����ȏ�͎�M�ł��Ȃ�
                break;
            }

            // ��M���E�ʓI�ɁA���ׂĂ���M�ł��Ȃ��Ȃ�An �����E�ʂɐݒ肷��
            if (size + n > limit) {
                n = (ssize_t) (limit - size);
            }
        }

        // �����́H
        if (prev == chain->buf->last) {
            iov->iov_len += n;

        } else {
            if (vec.nelts >= IOV_MAX) {
                break;
            }

            iov = ngx_array_push(&vec);
            if (iov == NULL) {
                return NGX_ERROR;
            }

            // iov �ֈڂ�
            iov->iov_base = (void *) chain->buf->last;
            iov->iov_len = n;
        }

        // ���v�̎�M�ʂ��X�V
        size += n;
        // 
        prev = chain->buf->end;
        // ���̃o�b�t�@�`�F�C����
        chain = chain->next;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "readv: %ui, last:%uz", vec.nelts, iov->iov_len);

    do {
        n = readv(c->fd, (struct iovec *) vec.elts, vec.nelts);

        if (n == 0) {
            rev->ready = 0;
            rev->eof = 1;

#if (NGX_HAVE_KQUEUE)

            /*
             * on FreeBSD readv() may return 0 on closed socket
             * even if kqueue reported about available data
             */

            if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
                rev->available = 0;
            }

#endif

            return 0;
        }

        if (n > 0) {

#if (NGX_HAVE_KQUEUE)

            if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
                rev->available -= n;

                /*
                 * rev->available may be negative here because some additional
                 * bytes may be received between kevent() and readv()
                 */

                if (rev->available <= 0) {
                    if (!rev->pending_eof) {
                        rev->ready = 0;
                    }

                    rev->available = 0;
                }

                return n;
            }

#endif

#if (NGX_HAVE_FIONREAD)

            if (rev->available >= 0) {
                rev->available -= n;

                /*
                 * negative rev->available means some additional bytes
                 * were received between kernel notification and readv(),
                 * and therefore ev->ready can be safely reset even for
                 * edge-triggered event methods
                 */

                if (rev->available < 0) {
                    rev->available = 0;
                    rev->ready = 0;
                }

                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                               "readv: avail:%d", rev->available);

            } else if (n == size) {

                if (ngx_socket_nread(c->fd, &rev->available) == -1) {
                    n = ngx_connection_error(c, ngx_socket_errno,
                                             ngx_socket_nread_n " failed");
                    break;
                }

                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                               "readv: avail:%d", rev->available);
            }

#endif

#if (NGX_HAVE_EPOLLRDHUP)

            if ((ngx_event_flags & NGX_USE_EPOLL_EVENT)
                && ngx_use_epoll_rdhup)
            {
                if (n < size) {
                    if (!rev->pending_eof) {
                        rev->ready = 0;
                    }

                    rev->available = 0;
                }

                return n;
            }

#endif

            // ���ׂĂ���M�o�b�t�@����󂯎��Ȃ��������́A��M�C�x���g�� ready �t���O�� 0 ��
            if (n < size && !(ngx_event_flags & NGX_USE_GREEDY_EVENT)) {
                rev->ready = 0;
            }

            // ��M�������v�o�C�g�ʂ���M����
            return n;
        }

        // �G���[�ԍ����擾
        err = ngx_socket_errno;

        if (err == NGX_EAGAIN || err == NGX_EINTR) {
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,
                           "readv() not ready");
            n = NGX_AGAIN;

        } else {
            n = ngx_connection_error(c, err, "readv() failed");
            break;
        }

    } while (err == NGX_EINTR);

    rev->ready = 0;

    if (n == NGX_ERROR) {
        c->read->error = 1;
    }

    return n;
}
