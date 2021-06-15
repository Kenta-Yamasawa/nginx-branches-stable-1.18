
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_TIMES_H_INCLUDED_
#define _NGX_TIMES_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

// nginx �ɂ����錻�ݎ�����\���\����
typedef struct {
    time_t      sec;
    ngx_uint_t  msec;
    ngx_int_t   gmtoff;
} ngx_time_t;


/**
 * @brief
 *     5 ��ނ̕����񎞊ԃL���b�V���ƒʏ펞�ԃL���b�V��������������
 * @post
 *     ngx_cached_time �� �ŐV�̎��ԃL���b�V�����w���Ă���
 *
 *     ngx_cached_err_log_time     �����񎞊ԃL���b�V���� "1970/09/28 12:00:00"           �ŃT�C�Y�̂ݏ��������ꂽ
 *     ngx_cached_http_time        �����񎞊ԃL���b�V���� "Mon, 28 Sep 1970 06:00:00 GMT" �ŃT�C�Y�̂ݏ��������ꂽ
 *     ngx_cached_http_log_time    �����񎞊ԃL���b�V���� "28/Sep/1970:12:00:00 +0600"    �ŃT�C�Y�̂ݏ��������ꂽ
 *     ngx_cached_http_log_iso8601 �����񎞊ԃL���b�V���� "1970-09-28T12:00:00+06:00"     �ŃT�C�Y�̂ݏ��������ꂽ
 *     ngx_cached_syslog_time      �����񎞊ԃL���b�V���� "Sep 28 12:00:00"               �ŃT�C�Y�̂ݏ��������ꂽ
 */
void ngx_time_init(void);
/**
 * @brief
 *     5 ��ނ̕����񎞊ԃL���b�V���ƒʏ펞�ԃL���b�V�������ݎ����ōX�V�����
 *     �X���b�h�Z�[�t
 * @post
 *     ngx_cached_time             �ʏ펞�ԃL���b�V�����ŐV�����ōX�V���ꂽ
 *
 *     ngx_cached_err_log_time     �����񎞊ԃL���b�V�����ŐV�����ōX�V���ꂽ
 *     ngx_cached_http_time        �����񎞊ԃL���b�V�����ŐV�����ōX�V���ꂽ
 *     ngx_cached_http_log_time    �����񎞊ԃL���b�V�����ŐV�����ōX�V���ꂽ
 *     ngx_cached_http_log_iso8601 �����񎞊ԃL���b�V�����ŐV�����ōX�V���ꂽ
 *     ngx_cached_syslog_time      �����񎞊ԃL���b�V�����ŐV�����ōX�V���ꂽ
 */
void ngx_time_update(void);
/**
 * @brief
 *     �T��ނ̕����񎞊ԃL���b�V���̈ꕔ�ƒʏ펞�ԃL���b�V�����X�V����
 *     �X���b�h�Z�[�t
 */
void ngx_time_sigsafe_update(void);
/**
 * @brief
 *     1970�N�P���P������̌o�ߎ��ԁi�b�j����A���t�E�����֕�����`���ŕϊ����ăo�b�t�@�Ɋi�[
 * @param[out]
 *     buf: �o�b�t�@
 * @param[in]
 *     t: ���ݎ����i�O���j�b�W�`���j
 * @retval
 *     ������`���̕ϊ����ʂ̌��ݎ���
 */
u_char *ngx_http_time(u_char *buf, time_t t);
/**
 * @brief
 *     1970�N�P���P������̌o�ߎ��ԁi�b�j����A���t�E�����֕�����`���ŕϊ����ăo�b�t�@�Ɋi�[
 * @param[out]
 *     buf: �o�b�t�@
 * @param[in]
 *     t: ���ݎ����i�O���j�b�W�`���j
 * @retval
 *     ������`���̕ϊ����ʂ̌��ݎ���
 */
u_char *ngx_http_cookie_time(u_char *buf, time_t t);
/**
 * @brief
 *     1970�N�P���P������̌o�ߎ��ԁi�b�j����A���t�E�����`���֕ϊ�����
 * @param[out]
 *     tp: �ϊ����ʂ̓��t�E����
 * @param[in]
 *     t: 1970�N�P���P������̌o�ߎ��ԁi�b�j
 */
void ngx_gmtime(time_t t, ngx_tm_t *tp);

/**
 * @brief
 *     �L���b�V���Ɣ�r���āA���ݎ����������܂��������ǂ����𔻒肵�čX�V����
 * @param[in]
 *     when: ���ݎ���
 * @retval
 *     �����܂����ł�����Awhen ���P�����C���N�������g���ĕԂ�
 *     �����łȂ���� when ���̂܂�
 */
time_t ngx_next_time(time_t when);
#define ngx_next_time_n      "mktime()"


// �ʏ펞�ԃL���b�V���ւ̃A�N�Z�T
extern volatile ngx_time_t  *ngx_cached_time;

/**
 * @brief
 *     �ʏ펞�ԃL���b�V�����猻�ݎ�����b�P�ʂŎ擾
 * @retval
 *     ���ݎ����i�b�j
 */
#define ngx_time()           ngx_cached_time->sec
/**
 * @brief
 *     �ʏ펞�ԃL���b�V���Ɋi�[����Ă���I�u�W�F�N�g���̂��̂��擾
 * @retval
 *     ���ݎ����ingx_time_t)
 */
#define ngx_timeofday()      (ngx_time_t *) ngx_cached_time

// �T��ނ̕����񎞊ԃL���b�V���ւ̃A�N�Z�T
extern volatile ngx_str_t    ngx_cached_err_log_time;
extern volatile ngx_str_t    ngx_cached_http_time;
extern volatile ngx_str_t    ngx_cached_http_log_time;
extern volatile ngx_str_t    ngx_cached_http_log_iso8601;
extern volatile ngx_str_t    ngx_cached_syslog_time;

/*
 * milliseconds elapsed since some unspecified point in the past
 * and truncated to ngx_msec_t, used in event timers
 */
extern volatile ngx_msec_t  ngx_current_msec;


#endif /* _NGX_TIMES_H_INCLUDED_ */
