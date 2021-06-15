
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

// �����ŃC�x���g��ێ�����
static struct pollfd  *event_list;
// �C�x���g�̑���
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

// �Ȃ�ƁA�f�B���N�e�B�u�Ȃ�
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
 *     �C�x���g���W���[���A�C�x���g�R�A���W���[���� init_conf() �̌�ɌĂ΂�� init_conf()
 * @param[in]
 *     cycle: 
 *     timer: �g��Ȃ�
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
 * @post
 *     �T�C�N�����ۗL����R�l�N�V�����̐������Aevent_list �� event �����Ă�悤�ɂȂ�
 *     �ÓI�ϐ� ngx_event_actions �� poll �̃R���e�L�X�g�̃A�N�V�����W���i�[����Ă���
 *     �ÓI�ϐ� ngx_io �� OS �ŗL�̑���M�����Q���i�[����Ă���
 *     �ÓI�ϐ� ngx_event_flags �Ńt���O�������Ă���
 */
static ngx_int_t
ngx_poll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    struct pollfd   *list;

    if (event_list == NULL) {
        nevents = 0;
    }

    // NGX_SINGLE_PROCESS �͂��Ă͂܂�i�͂��j
    // event_list �̊��蓖�āA�X�V���s��
    if (ngx_process >= NGX_PROCESS_WORKER
        || cycle->old_cycle == NULL
        || cycle->old_cycle->connection_n < cycle->connection_n)
    {
        // �R�l�N�V�����̐������C�x���g�����Ă�悤�Ɋ��蓖�Ă�
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

    // �e OS �Ɉˑ����鑗��M�p�����������ɂ܂Ƃ߂��Ă���
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

    // �Ȃ񂩃t���O
    ngx_event_flags = NGX_USE_LEVEL_EVENT|NGX_USE_FD_EVENT;

    return NGX_OK;
}


/**
 * @brief
 *     �C�x���g���X�g���J������
 * @param[in]
 *     cycle: ���Ɏg��Ȃ�
 */
static void
ngx_poll_done(ngx_cycle_t *cycle)
{
    ngx_free(event_list);

    event_list = NULL;
}


/**
 * @brief
 *     ��M�E���邢�͑��M�C�x���g���C�x���g���X�g�ɒǉ�����
 * @param[in]
 *     ev: ���̃C�x���g��ǉ�����
 *     event: ��M�C�x���g�����M�C�x���g�����w�肷��
 *     flags: �g��Ȃ�
 * @retval
 *     NGX_OK: �K���Ԃ�
 * @post
 *     ev->active �� ON �ɂȂ�A�L��������Ă���
 *     event_list �Ɏ�M�A���邢�͑��M�C�x���g���ǉ�����Ă���
 *     nevents ���X�V����Ă���
 *     ev->index �� event_list �ɂ����鎩�g�̃C���f�b�N�X���i�[����Ă���
 */
static ngx_int_t
ngx_poll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    ngx_event_t       *e;
    ngx_connection_t  *c;

    // ngx_event_t �\���̂� data �ɂ́A�C�x���g�ŊĎ��������R�l�N�V�������i�[����Ă���
    c = ev->data;

    // ���̃C�x���g���L��������Ă��邱�Ƃ�����
    ev->active = 1;

    // ev->index �������łȂ��Ȃ�A���łɂ��̃C�x���g�̓Z�b�g����Ă���
    if (ev->index != NGX_INVALID_INDEX) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "poll event fd:%d ev:%i is already set", c->fd, event);
        return NGX_OK;
    }

    if (event == NGX_READ_EVENT) {
        // ���łɂ��̃R�l�N�V���������M�C�x���g���Z�b�g���Ă����� NULL �ł͂Ȃ�
        e = c->write;
#if (NGX_READ_EVENT != POLLIN)
        event = POLLIN;
#endif

    } else {
        // ���łɂ��̃R�l�N�V��������M�C�x���g���Z�b�g���Ă����� NULL �ł͂Ȃ�
        e = c->read;
#if (NGX_WRITE_EVENT != POLLOUT)
        event = POLLOUT;
#endif
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "poll add event: fd:%d ev:%i", c->fd, event);

    // �C�x���g���X�g�ɒǉ�
    // ���̃R�l�N�V�����Ɋւ���C�x���g�͏��߂ẴP�[�X
    if (e == NULL || e->index == NGX_INVALID_INDEX) {
        event_list[nevents].fd = c->fd;
        event_list[nevents].events = (short) event;
        event_list[nevents].revents = 0;

        ev->index = nevents;
        nevents++;

    } else {
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "poll add index: %i", e->index);

        // ���߂Ăł͂Ȃ��P�[�X
        // �������łɁA��M�ł���Α��M�A���M�ł���Ύ�M�C�x���g���Z�b�g����Ă���ꍇ�̏���
        event_list[e->index].events |= (short) event;
        ev->index = e->index;
    }

    return NGX_OK;
}


/**
 * @brief
 *     ��M�E���邢�͑��M�C�x���g���C�x���g���X�g����폜����
 * @param[in]
 *     ev: ���̃C�x���g���폜����
 *     event: ��M�C�x���g�����M�C�x���g�����w�肷��
 *     flags: �g��Ȃ�
 * @retval
 *     NGX_OK: �K���Ԃ�
 * @post
 *     ev->active �� OFF �ɂȂ�A����������Ă���
 *     event_list �����M�A���邢�͑��M�C�x���g���폜����Ă���
 *     nevents ���X�V����Ă���
 *     ev->index �� NGX_INVALID_INDEX ���i�[����Ă���
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
 *     �C�x���g��ҋ@���āA������C�x���g���s�L���[�Ƀv�b�V�����Ă���
 * @param[in]
 *     cycle: ���̃T�C�N�����ۗL����R�l�N�V�����ɂ��ď�������
 *     timer: �����ɑ҂��A�����Ŏw�肵�����Ԃ����҂�
 *     flags: NGX_UPDATE_TIME ���w�肳��Ă����玞�ԋ@�\���X�V����
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
 * @post
 *     ngx_posted_accept_events �L���[���邢�� ngx_posted_events�L���[�ɃC�x���g���i�[���ꂽ
 *     ���������C�x���g�ɂ��āA�Y������R�l�N�V�������ۗL����C�x���g�� ready �� 1 �ɍX�V����Ă���
 *     �ǂݍ��݃C�x���g�̏ꍇ�� available �� -1 �ɍX�V����Ă���
 * @detail
 *     �P�Dpoll �ő҂�
 *     �Q�D�G���[�n���h�����O
 *     �R�D�L���ȃC�x���g�ɂ��āA���ׂăL���[�ֈڂ�
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

    // �C�x���g��҂�������

    ready = poll(event_list, (u_int) nevents, (int) timer);

    err = (ready == -1) ? ngx_errno : 0;

    // ���ԍX�V
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {
        ngx_time_update();
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "poll ready %d of %ui", ready, nevents);

    if (err) {
        // �r���ŃV�O�i������M�����̂Œ��f�����P�[�X
        if (err == NGX_EINTR) {

            // �V�O�i����M���Ē��f���������Ȃ̂ŁA���͍X�V���Ȃ��Ă悢
            if (ngx_event_timer_alarm) {
                ngx_event_timer_alarm = 0;
                return NGX_OK;
            }

            // ���̃G���[�ԍ��Ȃ�V�O�i������M���������Ȃ̂ŁA�ً}�I�ł͂Ȃ�
            level = NGX_LOG_INFO;

        } else {
            // �ق��̃G���[�ԍ��������炢�낢��Ƃ܂���
            level = NGX_LOG_ALERT;
        }

        ngx_log_error(level, cycle->log, err, "poll() failed");
        // ���A�C�x���g�p���[�v���甲����i�O�ɃV�O�i�������ӏ�������j
        return NGX_ERROR;
    }

    // �^�C���A�E�g
    if (ready == 0) {
        if (timer != NGX_TIMER_INFINITE) {
            return NGX_OK;
        }

        // ���Ԗ������҂����w�肵���̂ɁA�^�C���A�E�g����������̂͂��邢

        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "poll() returned no events without timeout");
        return NGX_ERROR;
    }

    // �ǂ̃C�x���g�����������̂����`�F�b�N
    // �`�F�b�N���āA���ׂăL���[�ֈڂ�
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

        // revents ���z��O�̒l�������烍�O�\��
        if (revents & ~(POLLIN|POLLOUT|POLLERR|POLLHUP|POLLNVAL)) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "strange poll() events fd:%d ev:%04Xd rev:%04Xd",
                          event_list[i].fd, event_list[i].events, revents);
        }

        // ���̃C�x���g�ł͂Ȃ��̂Ŏ��̃C�x���g��
        if (event_list[i].fd == -1) {
            /*
             * the disabled event, a workaround for our possible bug,
             * see the comment below
             */
            continue;
        }

        // �e�C�x���g�͑Ώۃ\�P�b�g�� fd �������Ă���
        // �T�C�N���\���̂͒S������e�\�P�b�g�� fd �������Ă���
        // �����ŏƍ����邱�ƂŃC�x���g�������\�P�b�g�i�R�l�N�V�����j�������ŕ�����
        c = ngx_cycle->files[event_list[i].fd];

        if (c->fd == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0, "unexpected event");

            /*
             * it is certainly our fault and it should be investigated,
             * in the meantime we disable this event to avoid a CPU spinning
             */

            if (i == nevents - 1) {
                // �Ō�̃C�x���g�������炻�������Q�Ƃ��Ȃ��悤�폜����
                nevents--;
            } else {
                // �C�x���g���x���ł��̃\�P�b�g�𖳌�������i������C�x���g��҂��Ȃ��j
                // �����̌y�ʉ��H
                event_list[i].fd = -1;
            }

            continue;
        }

        if (revents & (POLLERR|POLLHUP|POLLNVAL)) {

            /*
             * if the error events were returned, add POLLIN and POLLOUT
             * to handle the events at least in one active handler
             */
            // �G���[�C�x���g���ǂ݁E�����C�x���g�̂ǂ��炩�ŏ�����������Ȃ̂Łinginx �́j
            revents |= POLLIN|POLLOUT;
        }

        found = 0;

        // ���܁A���̃R�l�N�V�����͓ǂݍ��݂��L��������Ă��āA�����Ă܂��ɓǂݍ��݃C�x���g������
        if ((revents & POLLIN) && c->read->active) {
            found = 1;

            ev = c->read;
            // �C�x���g���m�F�����̂ŁA�ǂݍ��ݏ�����������
            ev->ready = 1;
            // ���łɃC�x���g�����B�����̂ŁA����Ȃ�C�x���g�͂��̃R�l�N�V�����ł͏����ł��Ȃ�
            ev->available = -1;

            // �����炭 POLLIN �Ƃ��̑��̃C�x���g����ʂ��Ă���H
            queue = ev->accept ? &ngx_posted_accept_events
                               : &ngx_posted_events;

            // �C�x���g�����s�L���[�ֈڂ�
            ngx_post_event(ev, queue);
        }

        // ���܁A���̃R�l�N�V�����̏������݃C�x���g���L��������Ă��āA�����Ă܂��ɏ������݃C�x���g������
        if ((revents & POLLOUT) && c->write->active) {
            found = 1;

            // 
            ev = c->write;
            ev->ready = 1;

            ngx_post_event(ev, &ngx_posted_events);
        }

        if (found) {
            // ready �͂����炭�A���������C�x���g�̑����A�����莟��A�f�N�������g���Ă���
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
 *     ���ǁA�������Ă��Ȃ��C������E�E�E
 * @param[in]
 *     cycle: ���̃T�C�N�����ێ�����ݒ�\���̂̂����A�C�x���g�p�̂��̂���舵��
 *     conf: �g��Ȃ�
 * @retval
 *     NGX_CONF_OK: �K���Ԃ�
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
