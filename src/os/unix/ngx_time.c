
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>

/**
 * @macro
 *     NGX_HAVE_LOCALTIME_R: 
 *     NGX_FREEBSD: 
 *     NGX_LINUX: 
 */


/*
 * FreeBSD does not test /etc/localtime change, however, we can workaround it
 * by calling tzset() with TZ and then without TZ to update timezone.
 * The trick should work since FreeBSD 2.1.0.
 *
 * Linux does not test /etc/localtime change in localtime(),
 * but may stat("/etc/localtime") several times in every strftime(),
 * therefore we use it to update timezone.
 *
 * Solaris does not test /etc/TIMEZONE change too and no workaround available.
 */

void
ngx_timezone_update(void)
{
#if (NGX_FREEBSD)

    if (getenv("TZ")) {
        return;
    }

    putenv("TZ=UTC");

    tzset();

    unsetenv("TZ");

    tzset();

#elif (NGX_LINUX)
    time_t      s;
    struct tm  *t;
    char        buf[4];

    s = time(0);

    t = localtime(&s);

    strftime(buf, 4, "%H", t);

#endif
}


/**
 * @brief
 *     グリニッチ標準時（1970/1/1 00:00:00からの経過秒数）をわかりやすい形式（ローカル時刻）へ変換
 * @param[out]
 *     tm: わかりやすい形式
 * @param[in]
 *     s: グリニッチ標準時
 */
void
ngx_localtime(time_t s, ngx_tm_t *tm)
{
#if (NGX_HAVE_LOCALTIME_R)
    // おとなしくこちらを使うべし
    // localtime() と gmtime() は同じメモリ領域を使っているので、
    // 同じ timer に対して同じ値を返してしまう仕様である（ゴミ）
    // https://yuzu441.hateblo.jp/entry/2016/04/21/223544
    (void) localtime_r(&s, tm);

#else
    ngx_tm_t  *t;

    t = localtime(&s);
    *tm = *t;

#endif

    tm->ngx_tm_mon++;
    tm->ngx_tm_year += 1900;
}


/**
 * @brief
 *     グリニッチ標準時（1970/1/1 00:00:00からの経過秒数）をわかりやすい形式（ローカル時刻）へ変換
 * @param[out]
 *     tm: わかりやすい形式
 * @param[in]
 *     s: グリニッチ標準時
 */
void
ngx_libc_localtime(time_t s, struct tm *tm)
{
#if (NGX_HAVE_LOCALTIME_R)
    (void) localtime_r(&s, tm);

#else
    struct tm  *t;

    // グリニッチ標準時（1970/1/1 00:00:00からの経過秒数）をわかりやすい形式へ変換
    t = localtime(&s);
    *tm = *t;

#endif
}


/**
 * @brief
 *     グリニッチ標準時（1970/1/1 00:00:00からの経過秒数）をわかりやすい形式（世界協定時刻）へ変換
 * @param[out]
 *     tm: わかりやすい形式
 * @param[in]
 *     s: グリニッチ標準時
 */
void
ngx_libc_gmtime(time_t s, struct tm *tm)
{
#if (NGX_HAVE_LOCALTIME_R)
    (void) gmtime_r(&s, tm);

#else
    struct tm  *t;

    t = gmtime(&s);
    *tm = *t;

#endif
}
