
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


ngx_rbtree_t              ngx_event_timer_rbtree;
static ngx_rbtree_node_t  ngx_event_timer_sentinel;

/*
 * the event timer rbtree may contain the duplicate keys, however,
 * it should not be a problem, because we use the rbtree to find
 * a minimum timer value only
 */

/**
 * @brief
 *     イベントタイマ用の赤黒木を初期化する
 */
ngx_int_t
ngx_event_timer_init(ngx_log_t *log)
{
    ngx_rbtree_init(&ngx_event_timer_rbtree, &ngx_event_timer_sentinel,
                    ngx_rbtree_insert_timer_value);

    return NGX_OK;
}


/**
 * @brief
 *     イベントタイマ群の中から最小のものについて、すでに経過しているか調べる
 * @retval
 *     0: すでに経過した
 *     otherwise: 残り時間
 */
ngx_msec_t
ngx_event_find_timer(void)
{
    ngx_msec_int_t      timer;
    ngx_rbtree_node_t  *node, *root, *sentinel;

    // イベントタイマ赤黒木が空なら無限と解釈する
    if (ngx_event_timer_rbtree.root == &ngx_event_timer_sentinel) {
        return NGX_TIMER_INFINITE;
    }

    root = ngx_event_timer_rbtree.root;
    sentinel = ngx_event_timer_rbtree.sentinel;

    // イベントタイマ群の中から最小のキーを持つノードを参照する
    node = ngx_rbtree_min(root, sentinel);

    // もし過ぎていたら 0 を、そうでなければ残り時間を返す
    timer = (ngx_msec_int_t) (node->key - ngx_current_msec);

    return (ngx_msec_t) (timer > 0 ? timer : 0);
}


/**
 * @brief
 *     赤黒木に挿入されたイベントタイマ群のうち、
 *     経過時間を過ぎたものをすべて実行して、赤黒木から取り除く
 */
void
ngx_event_expire_timers(void)
{
    ngx_event_t        *ev;
    ngx_rbtree_node_t  *node, *root, *sentinel;

    sentinel = ngx_event_timer_rbtree.sentinel;

    // 赤黒木に挿入されたイベントタイマ群のうち、
    // 経過時間を過ぎたものをすべて実行して、赤黒木から取り除く
    for ( ;; ) {
        root = ngx_event_timer_rbtree.root;

        if (root == sentinel) {
            return;
        }

        node = ngx_rbtree_min(root, sentinel);

        /* node->key > ngx_current_msec */

        // 現在時刻を参考に、イベントタイマの指定時間を過ぎたかどうかを検証
        if ((ngx_msec_int_t) (node->key - ngx_current_msec) > 0) {
            return;
        }

        ev = (ngx_event_t *) ((char *) node - offsetof(ngx_event_t, timer));

        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "event timer del: %d: %M",
                       ngx_event_ident(ev->data), ev->timer.key);

        // イベントタイマを管理用赤黒木から取り除く
        ngx_rbtree_delete(&ngx_event_timer_rbtree, &ev->timer);

#if (NGX_DEBUG)
        ev->timer.left = NULL;
        ev->timer.right = NULL;
        ev->timer.parent = NULL;
#endif

        // イベントタイマが有効化されていないことをフラグ付けする
        ev->timer_set = 0;

        // イベントタイマが指定時間を過ぎて実行されたことをフラグ付けする
        ev->timedout = 1;

        // イベントタイマに仕掛けられたハンドラを実行する
        ev->handler(ev);
    }
}


ngx_int_t
ngx_event_no_timers_left(void)
{
    ngx_event_t        *ev;
    ngx_rbtree_node_t  *node, *root, *sentinel;

    sentinel = ngx_event_timer_rbtree.sentinel;
    root = ngx_event_timer_rbtree.root;

    if (root == sentinel) {
        return NGX_OK;
    }

    for (node = ngx_rbtree_min(root, sentinel);
         node;
         node = ngx_rbtree_next(&ngx_event_timer_rbtree, node))
    {
        ev = (ngx_event_t *) ((char *) node - offsetof(ngx_event_t, timer));

        if (!ev->cancelable) {
            return NGX_AGAIN;
        }
    }

    /* only cancelable timers left */

    return NGX_OK;
}
