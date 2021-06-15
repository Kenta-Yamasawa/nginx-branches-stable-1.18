
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


// 実行キュー
ngx_queue_t  ngx_posted_accept_events;
ngx_queue_t  ngx_posted_next_events;
ngx_queue_t  ngx_posted_events;


/**
 * @brief 実行キュー内の全イベントについて実行ハンドラを呼び出す
 * @param[in]
 *     cycle : ログ用
 *     posted: 対象の実行キュー
 */
void
ngx_event_process_posted(ngx_cycle_t *cycle, ngx_queue_t *posted)
{
    ngx_queue_t  *q;
    ngx_event_t  *ev;

    // キューが空じゃない限り繰り返す
    while (!ngx_queue_empty(posted)) {

        q = ngx_queue_head(posted);
        ev = ngx_queue_data(q, ngx_event_t, queue);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                      "posted event %p", ev);

        // キューから取り除く（イベントそのものを失効するわけではない）
        ngx_delete_posted_event(ev);

        // イベントのハンドラを実行する
        ev->handler(ev);
    }
}


/**
 * @brief
 *     ngx_posted_next_events キューの中身をすべて ngx_posted_events キューへ移す。
 *     そして中身のイベントを有効化する。
 *     ngx_posted_next_events キューは初期化する。
 */
void
ngx_event_move_posted_next(ngx_cycle_t *cycle)
{
    ngx_queue_t  *q;
    ngx_event_t  *ev;

    for (q = ngx_queue_head(&ngx_posted_next_events);
         q != ngx_queue_sentinel(&ngx_posted_next_events);
         q = ngx_queue_next(q))
    {
        ev = ngx_queue_data(q, ngx_event_t, queue);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                      "posted next event %p", ev);

        ev->ready = 1;
        ev->available = -1;
    }

    ngx_queue_add(&ngx_posted_events, &ngx_posted_next_events);
    ngx_queue_init(&ngx_posted_next_events);
}
