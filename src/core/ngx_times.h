
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_TIMES_H_INCLUDED_
#define _NGX_TIMES_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

// nginx における現在時刻を表す構造体
typedef struct {
    time_t      sec;
    ngx_uint_t  msec;
    ngx_int_t   gmtoff;
} ngx_time_t;


/**
 * @brief
 *     5 種類の文字列時間キャッシュと通常時間キャッシュを初期化する
 * @post
 *     ngx_cached_time が 最新の時間キャッシュを指している
 *
 *     ngx_cached_err_log_time     文字列時間キャッシュが "1970/09/28 12:00:00"           でサイズのみ初期化された
 *     ngx_cached_http_time        文字列時間キャッシュが "Mon, 28 Sep 1970 06:00:00 GMT" でサイズのみ初期化された
 *     ngx_cached_http_log_time    文字列時間キャッシュが "28/Sep/1970:12:00:00 +0600"    でサイズのみ初期化された
 *     ngx_cached_http_log_iso8601 文字列時間キャッシュが "1970-09-28T12:00:00+06:00"     でサイズのみ初期化された
 *     ngx_cached_syslog_time      文字列時間キャッシュが "Sep 28 12:00:00"               でサイズのみ初期化された
 */
void ngx_time_init(void);
/**
 * @brief
 *     5 種類の文字列時間キャッシュと通常時間キャッシュが現在時刻で更新される
 *     スレッドセーフ
 * @post
 *     ngx_cached_time             通常時間キャッシュが最新時刻で更新された
 *
 *     ngx_cached_err_log_time     文字列時間キャッシュが最新時刻で更新された
 *     ngx_cached_http_time        文字列時間キャッシュが最新時刻で更新された
 *     ngx_cached_http_log_time    文字列時間キャッシュが最新時刻で更新された
 *     ngx_cached_http_log_iso8601 文字列時間キャッシュが最新時刻で更新された
 *     ngx_cached_syslog_time      文字列時間キャッシュが最新時刻で更新された
 */
void ngx_time_update(void);
/**
 * @brief
 *     ５種類の文字列時間キャッシュの一部と通常時間キャッシュを更新する
 *     スレッドセーフ
 */
void ngx_time_sigsafe_update(void);
/**
 * @brief
 *     1970年１月１日からの経過時間（秒）から、日付・時刻へ文字列形式で変換してバッファに格納
 * @param[out]
 *     buf: バッファ
 * @param[in]
 *     t: 現在時刻（グリニッジ形式）
 * @retval
 *     文字列形式の変換結果の現在時刻
 */
u_char *ngx_http_time(u_char *buf, time_t t);
/**
 * @brief
 *     1970年１月１日からの経過時間（秒）から、日付・時刻へ文字列形式で変換してバッファに格納
 * @param[out]
 *     buf: バッファ
 * @param[in]
 *     t: 現在時刻（グリニッジ形式）
 * @retval
 *     文字列形式の変換結果の現在時刻
 */
u_char *ngx_http_cookie_time(u_char *buf, time_t t);
/**
 * @brief
 *     1970年１月１日からの経過時間（秒）から、日付・時刻形式へ変換する
 * @param[out]
 *     tp: 変換結果の日付・時刻
 * @param[in]
 *     t: 1970年１月１日からの経過時間（秒）
 */
void ngx_gmtime(time_t t, ngx_tm_t *tp);

/**
 * @brief
 *     キャッシュと比較して、現在時刻が日をまたいだかどうかを判定して更新する
 * @param[in]
 *     when: 現在時刻
 * @retval
 *     日をまたいでいたら、when を１日分インクリメントして返す
 *     そうでなければ when そのまま
 */
time_t ngx_next_time(time_t when);
#define ngx_next_time_n      "mktime()"


// 通常時間キャッシュへのアクセサ
extern volatile ngx_time_t  *ngx_cached_time;

/**
 * @brief
 *     通常時間キャッシュから現在時刻を秒単位で取得
 * @retval
 *     現在時刻（秒）
 */
#define ngx_time()           ngx_cached_time->sec
/**
 * @brief
 *     通常時間キャッシュに格納されているオブジェクトそのものを取得
 * @retval
 *     現在時刻（ngx_time_t)
 */
#define ngx_timeofday()      (ngx_time_t *) ngx_cached_time

// ５種類の文字列時間キャッシュへのアクセサ
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
