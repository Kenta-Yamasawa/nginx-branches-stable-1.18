
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


// cycle ‚Åg‚¤
void ngx_timezone_update(void);
// ‚½‚¾ˆø”‚ğƒ~ƒŠ•bŒ`®‚É’¼‚·‚¾‚¯
// ‚È‚­‚Ä‚àã‚ÌŠÖ”‚ª‚¿‚á‚ñ‚ÆÀ‘•‚³‚ê‚Ä‚¢‚ê‚Î‚¢‚ç‚È‚¢
void ngx_localtime(time_t s, ngx_tm_t *tm);
// core/time ‚Åg‚¤Assl ‚Å‚µ‚©g‚í‚È‚¢
void ngx_libc_localtime(time_t s, struct tm *tm);
// ssl ‚Å‚µ‚©g‚í‚È‚¢
void ngx_libc_gmtime(time_t s, struct tm *tm);

// ngx_http_userid_filter_module ‚Å‚Ì‚İ—\”õ‚¾‚³‚ê‚é
// ‚±‚Ìã‚ÉÀ‘•‚³‚ê‚½ŠÖ”ŒQ‚ğ€”õ‚·‚ê‚Î‚È‚­‚Ä‚à‚¢‚¢
#define ngx_gettimeofday(tp)  (void) gettimeofday(tp, NULL);
// ˆÚA‚¹‚æ
#define ngx_msleep(ms)        (void) usleep(ms * 1000)
// ˆÚA‚¹‚æ
#define ngx_sleep(s)          (void) sleep(s)


#endif /* _NGX_TIME_H_INCLUDED_ */
