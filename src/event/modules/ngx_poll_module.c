
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


static ngx_int_t ngx_poll_init(ngx_cycle_t *cycle, ngx_msec_t timer);
static void ngx_poll_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_poll_add_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_poll_del_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_poll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
    ngx_uint_t flags);
static char *ngx_poll_init_conf(ngx_cycle_t *cycle, void *conf);

// ここでイベントを保持する
static struct pollfd  *event_list;
// イベントの総数
static ngx_uint_t      nevents;


static ngx_str_t           poll_name = ngx_string("poll");

static ngx_event_module_t  ngx_poll_module_ctx = {
    &poll_name,
    NULL,                                  /* create configuration */
    ngx_poll_init_conf,                    /* init configuration */

    {
        ngx_poll_add_event,                /* add an event */
        ngx_poll_del_event,                /* delete an event */
        ngx_poll_add_event,                /* enable an event */
        ngx_poll_del_event,                /* disable an event */
        NULL,                              /* add an connection */
        NULL,                              /* delete an connection */
        NULL,                              /* trigger a notify */
        ngx_poll_process_events,           /* process the events */
        ngx_poll_init,                     /* init the events */
        ngx_poll_done                      /* done the events */
    }

};

// なんと、ディレクティブなし
ngx_module_t  ngx_poll_module = {
    NGX_MODULE_V1,
    &ngx_poll_module_ctx,                  /* module context */
    NULL,                                  /* module directives */
    NGX_EVENT_MODULE,                      /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


/**
 * @brief
 *     イベントモジュール、イベントコアモジュールの init_conf() の後に呼ばれる init_conf()
 * @param[in]
 *     cycle: 
 *     timer: 使わない
 * @retval
 *     NGX_OK: 成功
 *     NGX_ERROR: 失敗
 * @post
 *     サイクルが保有するコネクションの数だけ、event_list で event を持てるようになる
 *     静的変数 ngx_event_actions に poll のコンテキストのアクション集を格納されている
 *     静的変数 ngx_io に OS 固有の送受信処理群が格納されている
 *     静的変数 ngx_event_flags でフラグが立っている
 */
static ngx_int_t
ngx_poll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    struct pollfd   *list;

    if (event_list == NULL) {
        nevents = 0;
    }

    // NGX_SINGLE_PROCESS はあてはまる（はず）
    // event_list の割り当て、更新を行う
    if (ngx_process >= NGX_PROCESS_WORKER
        || cycle->old_cycle == NULL
        || cycle->old_cycle->connection_n < cycle->connection_n)
    {
        // コネクションの数だけイベントを持てるように割り当てる
        list = ngx_alloc(sizeof(struct pollfd) * cycle->connection_n,
                         cycle->log);
        if (list == NULL) {
            return NGX_ERROR;
        }

        // 
        if (event_list) {
            ngx_memcpy(list, event_list, sizeof(struct pollfd) * nevents);
            ngx_free(event_list);
        }

        event_list = list;
    }

    // 各 OS に依存する送受信用処理がここにまとめられている
    ngx_io = ngx_os_io;

    /**
     *  {
     *      ngx_poll_add_event,
     *      ngx_poll_del_event,
     *      ngx_poll_add_event,
     *      ngx_poll_del_event,
     *      NULL,
     *      NULL,
     *      NULL,
     *      ngx_poll_process_events,
     *      ngx_poll_init,
     *      ngx_poll_done
     *  }
     */
    ngx_event_actions = ngx_poll_module_ctx.actions;

    // なんかフラグ
    ngx_event_flags = NGX_USE_LEVEL_EVENT|NGX_USE_FD_EVENT;

    return NGX_OK;
}


/**
 * @brief
 *     イベントリストを開放する
 * @param[in]
 *     cycle: 特に使わない
 */
static void
ngx_poll_done(ngx_cycle_t *cycle)
{
    ngx_free(event_list);

    event_list = NULL;
}


/**
 * @brief
 *     受信・あるいは送信イベントをイベントリストに追加する
 * @param[in]
 *     ev: このイベントを追加する
 *     event: 受信イベントか送信イベントかを指定する
 *     flags: 使わない
 * @retval
 *     NGX_OK: 必ず返る
 * @post
 *     ev->active が ON になり、有効化されている
 *     event_list に受信、あるいは送信イベントが追加されている
 *     nevents が更新されている
 *     ev->index に event_list における自身のインデックスが格納されている
 */
static ngx_int_t
ngx_poll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    ngx_event_t       *e;
    ngx_connection_t  *c;

    // ngx_event_t 構造体の data には、イベントで監視したいコネクションが格納されている
    c = ev->data;

    // そのイベントが有効化されていることを示す
    ev->active = 1;

    // ev->index が無効でないなら、すでにこのイベントはセットされている
    if (ev->index != NGX_INVALID_INDEX) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "poll event fd:%d ev:%i is already set", c->fd, event);
        return NGX_OK;
    }

    if (event == NGX_READ_EVENT) {
        // すでにこのコネクションが送信イベントをセットしていたら NULL ではない
        e = c->write;
#if (NGX_READ_EVENT != POLLIN)
        event = POLLIN;
#endif

    } else {
        // すでにこのコネクションが受信イベントをセットしていたら NULL ではない
        e = c->read;
#if (NGX_WRITE_EVENT != POLLOUT)
        event = POLLOUT;
#endif
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "poll add event: fd:%d ev:%i", c->fd, event);

    // イベントリストに追加
    // このコネクションに関するイベントは初めてのケース
    if (e == NULL || e->index == NGX_INVALID_INDEX) {
        event_list[nevents].fd = c->fd;
        event_list[nevents].events = (short) event;
        event_list[nevents].revents = 0;

        ev->index = nevents;
        nevents++;

    } else {
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "poll add index: %i", e->index);

        // 初めてではないケース
        // もうすでに、受信であれば送信、送信であれば受信イベントがセットされている場合の処理
        event_list[e->index].events |= (short) event;
        ev->index = e->index;
    }

    return NGX_OK;
}


/**
 * @brief
 *     受信・あるいは送信イベントをイベントリストから削除する
 * @param[in]
 *     ev: このイベントを削除する
 *     event: 受信イベントか送信イベントかを指定する
 *     flags: 使わない
 * @retval
 *     NGX_OK: 必ず返る
 * @post
 *     ev->active が OFF になり、無効化されている
 *     event_list から受信、あるいは送信イベントが削除されている
 *     nevents が更新されている
 *     ev->index に NGX_INVALID_INDEX が格納されている
 */
static ngx_int_t
ngx_poll_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    ngx_event_t       *e;
    ngx_connection_t  *c;

    c = ev->data;

    ev->active = 0;

    if (ev->index == NGX_INVALID_INDEX) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "poll event fd:%d ev:%i is already deleted",
                      c->fd, event);
        return NGX_OK;
    }

    if (event == NGX_READ_EVENT) {
        e = c->write;
#if (NGX_READ_EVENT != POLLIN)
        event = POLLIN;
#endif

    } else {
        e = c->read;
#if (NGX_WRITE_EVENT != POLLOUT)
        event = POLLOUT;
#endif
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "poll del event: fd:%d ev:%i", c->fd, event);

    if (e == NULL || e->index == NGX_INVALID_INDEX) {
        nevents--;

        if (ev->index < nevents) {

            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                           "index: copy event %ui to %i", nevents, ev->index);

            event_list[ev->index] = event_list[nevents];

            c = ngx_cycle->files[event_list[nevents].fd];

            if (c->fd == -1) {
                ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                              "unexpected last event");

            } else {
                if (c->read->index == nevents) {
                    c->read->index = ev->index;
                }

                if (c->write->index == nevents) {
                    c->write->index = ev->index;
                }
            }
        }

    } else {
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "poll del index: %i", e->index);

        event_list[e->index].events &= (short) ~event;
    }

    ev->index = NGX_INVALID_INDEX;

    return NGX_OK;
}


/**
 * @brief
 *     イベントを待機して、来たらイベント実行キューにプッシュしていく
 * @param[in]
 *     cycle: このサイクルが保有するコネクションについて処理する
 *     timer: 無限に待つか、ここで指定した時間だけ待つか
 *     flags: NGX_UPDATE_TIME が指定されていたら時間機能を更新する
 * @retval
 *     NGX_OK: 成功
 *     NGX_ERROR: 失敗
 * @post
 *     ngx_posted_accept_events キューあるいは ngx_posted_eventsキューにイベントが格納された
 *     発生したイベントについて、該当するコネクションが保有するイベントの ready が 1 に更新されている
 *     読み込みイベントの場合は available が -1 に更新されている
 * @detail
 *     １．poll で待つ
 *     ２．エラーハンドリング
 *     ３．有効なイベントについて、すべてキューへ移す
 */
static ngx_int_t
ngx_poll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{
    int                 ready, revents;
    ngx_err_t           err;
    ngx_uint_t          i, found, level;
    ngx_event_t        *ev;
    ngx_queue_t        *queue;
    ngx_connection_t   *c;

    /* NGX_TIMER_INFINITE == INFTIM */

#if (NGX_DEBUG0)
    if (cycle->log->log_level & NGX_LOG_DEBUG_ALL) {
        for (i = 0; i < nevents; i++) {
            ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "poll: %ui: fd:%d ev:%04Xd",
                           i, event_list[i].fd, event_list[i].events);
        }
    }
#endif

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "poll timer: %M", timer);

    // イベントを待ち続ける

    ready = poll(event_list, (u_int) nevents, (int) timer);

    err = (ready == -1) ? ngx_errno : 0;

    // 時間更新
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {
        ngx_time_update();
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "poll ready %d of %ui", ready, nevents);

    if (err) {
        // 途中でシグナルを受信したので中断したケース
        if (err == NGX_EINTR) {

            // シグナル受信して中断しただけなので、次は更新しなくてよい
            if (ngx_event_timer_alarm) {
                ngx_event_timer_alarm = 0;
                return NGX_OK;
            }

            // このエラー番号ならシグナルを受信しただけなので、緊急的ではない
            level = NGX_LOG_INFO;

        } else {
            // ほかのエラー番号だったらいろいろとまずい
            level = NGX_LOG_ALERT;
        }

        ngx_log_error(level, cycle->log, err, "poll() failed");
        // 一回、イベント用ループから抜ける（外にシグナル処理箇所がある）
        return NGX_ERROR;
    }

    // タイムアウト
    if (ready == 0) {
        if (timer != NGX_TIMER_INFINITE) {
            return NGX_OK;
        }

        // 時間無制限待ちを指定したのに、タイムアウトが発生するのはずるい

        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "poll() returned no events without timeout");
        return NGX_ERROR;
    }

    // どのイベントが発生したのか一つ一つチェック
    // チェックして、すべてキューへ移す
    for (i = 0; i < nevents && ready; i++) {

        revents = event_list[i].revents;

#if 1
        ngx_log_debug4(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "poll: %ui: fd:%d ev:%04Xd rev:%04Xd",
                       i, event_list[i].fd, event_list[i].events, revents);
#else
        if (revents) {
            ngx_log_debug4(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "poll: %ui: fd:%d ev:%04Xd rev:%04Xd",
                           i, event_list[i].fd, event_list[i].events, revents);
        }
#endif

        if (revents & POLLNVAL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "poll() error fd:%d ev:%04Xd rev:%04Xd",
                          event_list[i].fd, event_list[i].events, revents);
        }

        // revents が想定外の値だったらログ表示
        if (revents & ~(POLLIN|POLLOUT|POLLERR|POLLHUP|POLLNVAL)) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "strange poll() events fd:%d ev:%04Xd rev:%04Xd",
                          event_list[i].fd, event_list[i].events, revents);
        }

        // このイベントではないので次のイベントへ
        if (event_list[i].fd == -1) {
            /*
             * the disabled event, a workaround for our possible bug,
             * see the comment below
             */
            continue;
        }

        // 各イベントは対象ソケットの fd を持っている
        // サイクル構造体は担当する各ソケットの fd を持っている
        // ここで照合することでイベントが来たソケット（コネクション）がここで分かる
        c = ngx_cycle->files[event_list[i].fd];

        if (c->fd == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0, "unexpected event");

            /*
             * it is certainly our fault and it should be investigated,
             * in the meantime we disable this event to avoid a CPU spinning
             */

            if (i == nevents - 1) {
                // 最後のイベントだったらそもそも参照しないよう削除する
                nevents--;
            } else {
                // イベントレベルでこのソケットを無効化する（次からイベントを待たない）
                // 処理の軽量化？
                event_list[i].fd = -1;
            }

            continue;
        }

        if (revents & (POLLERR|POLLHUP|POLLNVAL)) {

            /*
             * if the error events were returned, add POLLIN and POLLOUT
             * to handle the events at least in one active handler
             */
            // エラーイベントも読み・書きイベントのどちらかで処理する実装なので（nginx は）
            revents |= POLLIN|POLLOUT;
        }

        found = 0;

        // いま、そのコネクションは読み込みが有効化されていて、そしてまさに読み込みイベントが来た
        if ((revents & POLLIN) && c->read->active) {
            found = 1;

            ev = c->read;
            // イベントを確認したので、読み込み準備が整った
            ev->ready = 1;
            // すでにイベントが到達したので、さらなるイベントはこのコネクションでは処理できない
            ev->available = -1;

            // おそらく POLLIN とその他のイベントを区別している？
            queue = ev->accept ? &ngx_posted_accept_events
                               : &ngx_posted_events;

            // イベントを実行キューへ移す
            ngx_post_event(ev, queue);
        }

        // いま、そのコネクションの書き込みイベントが有効化されていて、そしてまさに書き込みイベントが来た
        if ((revents & POLLOUT) && c->write->active) {
            found = 1;

            // 
            ev = c->write;
            ev->ready = 1;

            ngx_post_event(ev, &ngx_posted_events);
        }

        if (found) {
            // ready はおそらく、発生したイベントの総数、見つかり次第、デクリメントしていく
            ready--;
            continue;
        }
    }

    if (ready != 0) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0, "poll ready != events");
    }

    return NGX_OK;
}


/**
 * @brief
 *     結局、何もしていない気がする・・・
 * @param[in]
 *     cycle: このサイクルが保持する設定構造体のうち、イベント用のものを取り扱う
 *     conf: 使わない
 * @retval
 *     NGX_CONF_OK: 必ず返る
 */
static char *
ngx_poll_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_event_conf_t  *ecf;

    ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);

    if (ecf->use != ngx_poll_module.ctx_index) {
        return NGX_CONF_OK;
    }

    return NGX_CONF_OK;
}
