
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


/**
 * @brief
 *     バッファチェインリストが保有するすべてのバッファが一杯になるまで受信することを試みる
 *     ただし、limit バイト以上は受信しない
 *     ただし、IOV_MAX までしかバッファチェインを使用しない（それ以上は無視）
 * @param[in]
 *     c: コネクション
 *     chain: 受信に使用するバッファチェインリスト
 *     limit: 限界受信バイト量
 * @retval
 *     n: 受信バイト量
 *     0: これ以上、受信できない
 *     NGX_ERROR: エラー終了1
 *     NGX_AGAIN: エラー終了2
 * @detail
 *     readv() に成功して、すべてを受信した（バッファ一杯 or limit バイト）ら、受信したバイト量を返す
 *     readv() に成功したが、すべてを受信できなかったら ready フラグを 0 に、受信したバイト量を返す
 *     readv() を呼び出して、これ以上、読み出せないときは ready フラグを 0 に、eof フラグを 1 にして 0 を返す
 *     readv() を呼び出して E_INTR が返るたびに処理を繰り返す
 *     readv() を呼び出して E_AGAIN が返ったら ready フラグを 0 に、NGX_AGAIN を返す
 *     readv() を呼び出して E_ERROR が返ったら ready フラグを 0 に、error フラグを 1 にして NGX_ERROR を返す
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

    // このコネクションの受信イベントを取得する
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
    vec.nalloc = NGX_IOVS_PREALLOCATE; // IOV_MAX と基本的には同じ
    vec.pool = c->pool;

    /* coalesce the neighbouring bufs */

    // バッファチェインリストの先頭バッファチェインから参照していく
    while (chain) {
        // n をこのバッファチェインからの受信容量に設定する
        n = chain->buf->end - chain->buf->last;

        if (limit) {
            // 受信限界量を超過したらループを抜ける
            if (size >= limit) {
                // これ以上は受信できない
                break;
            }

            // 受信限界量的に、すべてを受信できないなら、n を限界量に設定する
            if (size + n > limit) {
                n = (ssize_t) (limit - size);
            }
        }

        // ここは？
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

            // iov へ移す
            iov->iov_base = (void *) chain->buf->last;
            iov->iov_len = n;
        }

        // 合計の受信量を更新
        size += n;
        // 
        prev = chain->buf->end;
        // 次のバッファチェインへ
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

            // すべてを受信バッファから受け取れなかった時は、受信イベントの ready フラグを 0 に
            if (n < size && !(ngx_event_flags & NGX_USE_GREEDY_EVENT)) {
                rev->ready = 0;
            }

            // 受信した合計バイト量を受信する
            return n;
        }

        // エラー番号を取得
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
