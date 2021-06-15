
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

    // 送信イベントを取得する
    wev = c->write;

    // まだ準備ができていないなら、まだ書き込めてない分を返す
    if (!wev->ready) {
        return in;
    }

// ここは POLL だと実行されない
#if (NGX_HAVE_KQUEUE)

    if ((ngx_event_flags & NGX_USE_KQUEUE_EVENT) && wev->pending_eof) {
        (void) ngx_connection_error(c, wev->kq_errno,
                               "kevent() reported about an closed connection");
        wev->error = 1;
        return NGX_CHAIN_ERROR;
    }

#endif

    /* the maximum limit size is the maximum size_t value - the page size */

    // 不正な上限サイズだったら修正する
    if (limit == 0 || limit > (off_t) (NGX_MAX_SIZE_T_VALUE - ngx_pagesize)) {
        limit = NGX_MAX_SIZE_T_VALUE - ngx_pagesize;
    }

    // 送信したバイト量
    send = 0;

    vec.iovs = iovs;
    vec.nalloc = NGX_IOVS_PREALLOCATE;

    for ( ;; ) {
        // これまでに送信バッファへ送ったバイト量
        prev_send = send;

        /* create the iovec and coalesce the neighbouring bufs */

        /**
         * @brief
         *     ngx_writev() の呼び出しに必要な ngx_iovec_t 構造体を生成する
         * @param[out]
         *     vec: 生成した ngx_iovec_t 構造体の出力先
         * @param[in]
         *     in: 送信すべきデータ
         *     limit: 送信できる最大量
         *     log: ログ出力用
         * @retval
         *     NGX_CHAIN_ERROR: 異常終了
         *     otherwise: 正常終了
         * @post
         *     vec->iovs[] に書き込むべき各バッファが入っている
         *     vec->iovs[]->base に書き込むべきデータの先頭ポインタが入っている
         *     vec->iovs[]->len  に書き込むべきサイズが入っている
         *     vec->count  に iovs にいくつのバッファが入っているか入っている
         *     vec->size   に合計書き込み量が入っている
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

        // この直後の ngx_writev() 呼び出し成功時、これまでに送信バッファへ書き込んだと思われるバイト量
        send += vec.size;

        n = ngx_writev(c, &vec);

        if (n == NGX_ERROR) {
            return NGX_CHAIN_ERROR;
        }

        sent = (n == NGX_AGAIN) ? 0 : n;

        // このコネクションで送信したデータ量
        c->sent += sent;

        /**
         * @brief
         *     送信した分だけ、バッファチェインの各バッファのデータ先頭ポインタを更新する。
         * @param[out]
         *     in: バッファチェイン
         * @param[in]
         *     sent: 送信したデータ量
         * @detail
         *     in の中身に変化はない
         */
        in = ngx_chain_update_sent(in, sent);

        // 今回、すべてを送信バッファ側へ移せなかった
        if (send - prev_send != sent) {
            // 送信イベントの準備ができていないことにする
            wev->ready = 0;
            // 残りのチェインバッファを返す
            return in;
        }

        // 書き込める量を超過した or すべて書き込んだ
        if (send >= limit || in == NULL) {
            return in;
        }
    }
}


/**
 * @brief
 *     ngx_writev() の呼び出しに必要な ngx_iovec_t 構造体を生成する
 * @param[out]
 *     vec: 生成した ngx_iovec_t 構造体の出力先
 * @param[in]
 *     in: 送信すべきデータ
 *     limit: 送信できる最大量
 *     log: ログ出力用
 * @retval
 *     NGX_CHAIN_ERROR: 異常終了
 *     otherwise: 正常終了
 * @post
 *     vec->iovs[] に書き込むべき各バッファが入っている
 *       vec->iovs[]->base に書き込むべきデータの先頭ポインタが入っている
 *       vec->iovs[]->len  に書き込むべきサイズが入っている
 *     vec->count  に iovs にいくつのバッファが入っているか入っている
 *     vec->size   に合計書き込み量が入っている
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

    // まだ送信できる限り、in からデータを取り出していく
    for ( /* void */ ; in && total < limit; in = in->next) {

        // このバッファに特別なフラグが立っているケース
        if (ngx_buf_special(in->buf)) {
            continue;
        }

        // このバッファに特別なフラグが立っているケース
        if (in->buf->in_file) {
            break;
        }

        // このバッファに特別なフラグが立っているケース
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

        // このバッファについて、書き込むべき容量
        size = in->buf->last - in->buf->pos;

        // 上限を超過していたら修正
        if (size > limit - total) {
            size = limit - total;
        }

        if (prev == in->buf->pos) {
            // バッファ->next == バッファ でループしたケース
            // ここって通るの？
            iov->iov_len += size;

        } else {
            if (n == vec->nalloc) {
                break;
            }

            // iovs に今の buf を移す
            iov = &vec->iovs[n++];

            iov->iov_base = (void *) in->buf->pos;
            iov->iov_len = size;
        }

        // 前のバッファについて、まだ書き込まれていないものへのポインタ
        prev = in->buf->pos + size;
        // トータルで書き込むべき容量を更新
        total += size;
    }

    // iovs におけるインデックス
    vec->count = n;
    // 合計の書き込むべき量
    vec->size = total;

    return in;
}


/**
 * @brief
 *     iovs に書き込まれているデータをすべて送信する
 * @param[in]
 *     c: 使用するコネクション
 *     vec: 送信したいデータ
 * @retval
 *     n: 送信したバイト量（成功）
 *     NGX_ERROR: エラー終了１
 *     NGX_EAGAIN: エラー終了２
 * @detail
 *     writev() を呼び出して成功した（一部 or 全て）場合は、送信バッファに送ったバイト量を返す
 *     writev() を呼び出して、NGX_EINTR  だった場合は、処理を繰り返す
 *     writev() を呼び出して、NGX_ERROR  だった場合は、NGX_ERROR を返す
 *     writev() を呼び出して、NGX_EAGAIN だった場合は、NGX_AGAIN を返す
 */
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
