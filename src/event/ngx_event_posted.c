
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


// ���s�L���[
ngx_queue_t  ngx_posted_accept_events;
ngx_queue_t  ngx_posted_next_events;
ngx_queue_t  ngx_posted_events;


/**
 * @brief ���s�L���[���̑S�C�x���g�ɂ��Ď��s�n���h�����Ăяo��
 * @param[in]
 *     cycle : ���O�p
 *     posted: �Ώۂ̎��s�L���[
 */
void
ngx_event_process_posted(ngx_cycle_t *cycle, ngx_queue_t *posted)
{
    ngx_queue_t  *q;
    ngx_event_t  *ev;

    // �L���[���󂶂�Ȃ�����J��Ԃ�
    while (!ngx_queue_empty(posted)) {

        q = ngx_queue_head(posted);
        ev = ngx_queue_data(q, ngx_event_t, queue);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                      "posted event %p", ev);

        // �L���[�����菜���i�C�x���g���̂��̂���������킯�ł͂Ȃ��j
        ngx_delete_posted_event(ev);

        // �C�x���g�̃n���h�������s����
        ev->handler(ev);
    }
}


/**
 * @brief
 *     ngx_posted_next_events �L���[�̒��g�����ׂ� ngx_posted_events �L���[�ֈڂ��B
 *     �����Ē��g�̃C�x���g��L��������B
 *     ngx_posted_next_events �L���[�͏���������B
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
