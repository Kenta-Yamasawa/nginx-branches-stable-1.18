
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_TIME_H_INCLUDED_
#define _NGX_TIME_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef ngx_rbtree_key_t      ngx_msec_t;
typedef ngx_rbtree_key_int_t  ngx_msec_int_t;

typedef struct tm             ngx_tm_t;

#define ngx_tm_sec            tm_sec
#define ngx_tm_min            tm_min
#define ngx_tm_hour           tm_hour
#define ngx_tm_mday           tm_mday
#define ngx_tm_mon            tm_mon
#define ngx_tm_year           tm_year
#define ngx_tm_wday           tm_wday
#define ngx_tm_isdst          tm_isdst

#define ngx_tm_sec_t          int
#define ngx_tm_min_t          int
#define ngx_tm_hour_t         int
#define ngx_tm_mday_t         int
#define ngx_tm_mon_t          int
#define ngx_tm_year_t         int
#define ngx_tm_wday_t         int


#if (NGX_HAVE_GMTOFF)
#define ngx_tm_gmtoff         tm_gmtoff
#define ngx_tm_zone           tm_zone
#endif


#if (NGX_SOLARIS)

#define ngx_timezone(isdst) (- (isdst ? altzone : timezone) / 60)

#else

#define ngx_timezone(isdst) (- (isdst ? timezone + 3600 : timezone) / 60)

#endif


// cycle で使う
void ngx_timezone_update(void);
// ただ引数をミリ秒形式に直すだけ
// なくても上の関数がちゃんと実装されていればいらない
void ngx_localtime(time_t s, ngx_tm_t *tm);
// core/time で使う、ssl でしか使わない
void ngx_libc_localtime(time_t s, struct tm *tm);
// ssl でしか使わない
void ngx_libc_gmtime(time_t s, struct tm *tm);

// ngx_http_userid_filter_module でのみ予備だされる
// この上に実装された関数群を準備すればなくてもいい
#define ngx_gettimeofday(tp)  (void) gettimeofday(tp, NULL);
// 移植せよ
#define ngx_msleep(ms)        (void) usleep(ms * 1000)
// 移植せよ
#define ngx_sleep(s)          (void) sleep(s)


#endif /* _NGX_TIME_H_INCLUDED_ */
