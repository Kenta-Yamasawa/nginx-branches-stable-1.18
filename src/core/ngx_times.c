
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


static ngx_msec_t ngx_monotonic_time(time_t sec, ngx_uint_t msec);


/*
 * The time may be updated by signal handler or by several threads.
 * The time update operations are rare and require to hold the ngx_time_lock.
 * The time read operations are frequent, so they are lock-free and get time
 * values and strings from the current slot.  Thus thread may get the corrupted
 * values only if it is preempted while copying and then it is not scheduled
 * to run more than NGX_TIME_SLOTS seconds.
 */

// 時間キャッシュの数
#define NGX_TIME_SLOTS   64

// キャッシュで取り扱うインデックス
static ngx_uint_t        slot;
// 時間コア機能が取り扱うロック
static ngx_atomic_t      ngx_time_lock;

// 現在時刻（ミリ秒単位）
volatile ngx_msec_t      ngx_current_msec;
// それぞれ最新のキャッシュを指す
volatile ngx_time_t     *ngx_cached_time;
volatile ngx_str_t       ngx_cached_err_log_time;
volatile ngx_str_t       ngx_cached_http_time;
volatile ngx_str_t       ngx_cached_http_log_time;
volatile ngx_str_t       ngx_cached_http_log_iso8601;
volatile ngx_str_t       ngx_cached_syslog_time;

#if !(NGX_WIN32)

/*
 * localtime() and localtime_r() are not Async-Signal-Safe functions, therefore,
 * they must not be called by a signal handler, so we use the cached
 * GMT offset value. Fortunately the value is changed only two times a year.
 */

static ngx_int_t         cached_gmtoff;
#endif

// 時間のキャッシュ（ngx_time_t 構造体形式）
static ngx_time_t        cached_time[NGX_TIME_SLOTS];
// 文字列形式の 5 種類の時間キャッシュ（フォーマットが異なる）
static u_char            cached_err_log_time[NGX_TIME_SLOTS]
                                    [sizeof("1970/09/28 12:00:00")];
static u_char            cached_http_time[NGX_TIME_SLOTS]
                                    [sizeof("Mon, 28 Sep 1970 06:00:00 GMT")];
static u_char            cached_http_log_time[NGX_TIME_SLOTS]
                                    [sizeof("28/Sep/1970:12:00:00 +0600")];
static u_char            cached_http_log_iso8601[NGX_TIME_SLOTS]
                                    [sizeof("1970-09-28T12:00:00+06:00")];
static u_char            cached_syslog_time[NGX_TIME_SLOTS]
                                    [sizeof("Sep 28 12:00:00")];


// 各曜日を表す文字列テーブル
static char  *week[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
// 各月を表す文字列テーブル
static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

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
// OK
void
ngx_time_init(void)
{
    ngx_cached_err_log_time.len = sizeof("1970/09/28 12:00:00") - 1;
    ngx_cached_http_time.len = sizeof("Mon, 28 Sep 1970 06:00:00 GMT") - 1;
    ngx_cached_http_log_time.len = sizeof("28/Sep/1970:12:00:00 +0600") - 1;
    ngx_cached_http_log_iso8601.len = sizeof("1970-09-28T12:00:00+06:00") - 1;
    ngx_cached_syslog_time.len = sizeof("Sep 28 12:00:00") - 1;

    ngx_cached_time = &cached_time[0];

    ngx_time_update();
}


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
void
ngx_time_update(void)
{
    u_char          *p0, *p1, *p2, *p3, *p4;
    ngx_tm_t         tm, gmt;
    time_t           sec;
    ngx_uint_t       msec;
    ngx_time_t      *tp;
    struct timeval   tv;

    // ロック(GCC のビルトイン関数によるもの)
    if (!ngx_trylock(&ngx_time_lock)) {
        return;
    }

    // 現在時刻を取得する
    ngx_gettimeofday(&tv);

    sec = tv.tv_sec;
    msec = tv.tv_usec / 1000;

    /**
     * @brief
     *     より精密な現在時刻を取得する機構があれば、そちらを利用してその日の時刻をミリ秒単位で返す
     *     そのような機構がなければ、ただ単純に引数で渡した秒数とミリ秒をミリ秒で表して返す
     * @param[in]
     *     sec: 機構がない際に用いられる秒数
     *     msec: 機構がない際に用いられるミリ秒
     * @retval
     *     ミリ秒を返す
     * @macro
     *     NGX_HAVE_CLOCK_MONOTONIC: そのような機構を持っていれば ON
     */
    ngx_current_msec = ngx_monotonic_time(sec, msec);

    // 前にキャッシュされていた時刻を取得
    tp = &cached_time[slot];

    // 秒数が同じだった場合、前の呼び出しからほとんど経過していないので何もせずに終了
    if (tp->sec == sec) {
        tp->msec = msec;
        ngx_unlock(&ngx_time_lock);
        return;
    }

    // スロットを一つ進める
    if (slot == NGX_TIME_SLOTS - 1) {
        slot = 0;
    } else {
        slot++;
    }

    tp = &cached_time[slot];

    tp->sec = sec;
    tp->msec = msec;

    ngx_gmtime(sec, &gmt);


    p0 = &cached_http_time[slot][0];

    (void) ngx_sprintf(p0, "%s, %02d %s %4d %02d:%02d:%02d GMT",
                       week[gmt.ngx_tm_wday], gmt.ngx_tm_mday,
                       months[gmt.ngx_tm_mon - 1], gmt.ngx_tm_year,
                       gmt.ngx_tm_hour, gmt.ngx_tm_min, gmt.ngx_tm_sec);

#if (NGX_HAVE_GETTIMEZONE)

    tp->gmtoff = ngx_gettimezone();
    ngx_gmtime(sec + tp->gmtoff * 60, &tm);

#elif (NGX_HAVE_GMTOFF)

    ngx_localtime(sec, &tm);
    cached_gmtoff = (ngx_int_t) (tm.ngx_tm_gmtoff / 60);
    tp->gmtoff = cached_gmtoff;

#else

    ngx_localtime(sec, &tm);
    cached_gmtoff = ngx_timezone(tm.ngx_tm_isdst);
    tp->gmtoff = cached_gmtoff;

#endif


    p1 = &cached_err_log_time[slot][0];

    (void) ngx_sprintf(p1, "%4d/%02d/%02d %02d:%02d:%02d",
                       tm.ngx_tm_year, tm.ngx_tm_mon,
                       tm.ngx_tm_mday, tm.ngx_tm_hour,
                       tm.ngx_tm_min, tm.ngx_tm_sec);


    p2 = &cached_http_log_time[slot][0];

    (void) ngx_sprintf(p2, "%02d/%s/%d:%02d:%02d:%02d %c%02i%02i",
                       tm.ngx_tm_mday, months[tm.ngx_tm_mon - 1],
                       tm.ngx_tm_year, tm.ngx_tm_hour,
                       tm.ngx_tm_min, tm.ngx_tm_sec,
                       tp->gmtoff < 0 ? '-' : '+',
                       ngx_abs(tp->gmtoff / 60), ngx_abs(tp->gmtoff % 60));

    p3 = &cached_http_log_iso8601[slot][0];

    (void) ngx_sprintf(p3, "%4d-%02d-%02dT%02d:%02d:%02d%c%02i:%02i",
                       tm.ngx_tm_year, tm.ngx_tm_mon,
                       tm.ngx_tm_mday, tm.ngx_tm_hour,
                       tm.ngx_tm_min, tm.ngx_tm_sec,
                       tp->gmtoff < 0 ? '-' : '+',
                       ngx_abs(tp->gmtoff / 60), ngx_abs(tp->gmtoff % 60));

    p4 = &cached_syslog_time[slot][0];

    (void) ngx_sprintf(p4, "%s %2d %02d:%02d:%02d",
                       months[tm.ngx_tm_mon - 1], tm.ngx_tm_mday,
                       tm.ngx_tm_hour, tm.ngx_tm_min, tm.ngx_tm_sec);

    // 時間関連は順序が非常に重要なので
    ngx_memory_barrier();

    ngx_cached_time = tp;
    ngx_cached_http_time.data = p0;
    ngx_cached_err_log_time.data = p1;
    ngx_cached_http_log_time.data = p2;
    ngx_cached_http_log_iso8601.data = p3;
    ngx_cached_syslog_time.data = p4;

    // アンロック
    ngx_unlock(&ngx_time_lock);
}


/**
 * @brief
 *     より精密な現在時刻を取得する機構があれば、そちらを利用してその日の時刻をミリ秒単位で返す
 *     そのような機構がなければ、ただ単純に引数で渡した秒数とミリ秒をミリ秒で表して返す
 * @param[in]
 *     sec: 機構がない際に用いられる秒数
 *     msec: 機構がない際に用いられるミリ秒
 * @retval
 *     ミリ秒を返す
 * @macro
 *     NGX_HAVE_CLOCK_MONOTONIC: そのような機構を持っていれば ON
 */
// OK
static ngx_msec_t
ngx_monotonic_time(time_t sec, ngx_uint_t msec)
{
#if (NGX_HAVE_CLOCK_MONOTONIC)
    struct timespec  ts;

#if defined(CLOCK_MONOTONIC_FAST)
    clock_gettime(CLOCK_MONOTONIC_FAST, &ts);

#elif defined(CLOCK_MONOTONIC_COARSE)
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);

#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif

    sec = ts.tv_sec;
    msec = ts.tv_nsec / 1000000;

#endif

    return (ngx_msec_t) sec * 1000 + msec;
}


#if !(NGX_WIN32)

/**
 * @brief
 *     ５種類の文字列時間キャッシュの一部と通常時間キャッシュを更新する
 *     スレッドセーフ
 */
void
ngx_time_sigsafe_update(void)
{
    u_char          *p, *p2;
    ngx_tm_t         tm;
    time_t           sec;
    ngx_time_t      *tp;
    struct timeval   tv;

    // ロックを掛ける
    if (!ngx_trylock(&ngx_time_lock)) {
        return;
    }

    // 現在時刻を取得
    ngx_gettimeofday(&tv);

    sec = tv.tv_sec;

    tp = &cached_time[slot];

    if (tp->sec == sec) {
        ngx_unlock(&ngx_time_lock);
        return;
    }

    if (slot == NGX_TIME_SLOTS - 1) {
        slot = 0;
    } else {
        slot++;
    }

    tp = &cached_time[slot];

    tp->sec = 0;

    ngx_gmtime(sec + cached_gmtoff * 60, &tm);

    p = &cached_err_log_time[slot][0];

    (void) ngx_sprintf(p, "%4d/%02d/%02d %02d:%02d:%02d",
                       tm.ngx_tm_year, tm.ngx_tm_mon,
                       tm.ngx_tm_mday, tm.ngx_tm_hour,
                       tm.ngx_tm_min, tm.ngx_tm_sec);

    p2 = &cached_syslog_time[slot][0];

    (void) ngx_sprintf(p2, "%s %2d %02d:%02d:%02d",
                       months[tm.ngx_tm_mon - 1], tm.ngx_tm_mday,
                       tm.ngx_tm_hour, tm.ngx_tm_min, tm.ngx_tm_sec);

    // キャッシュ更新との間にメモリバリアを置く
    ngx_memory_barrier();

    // 理由は不明だが、一部の文字列時間キャッシュのみを更新する
    ngx_cached_err_log_time.data = p;
    ngx_cached_syslog_time.data = p2;

    // アンロックする
    ngx_unlock(&ngx_time_lock);
}

#endif


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
u_char *
ngx_http_time(u_char *buf, time_t t)
{
    ngx_tm_t  tm;

    /**
     * @brief
     *     1970年１月１日からの経過時間（秒）から、日付・時刻へ変換する
     * @param[out]
     *     tp: 変換結果の日付・時刻
     * @param[in]
     *     t: 1970年１月１日からの経過時間（秒）
     */
    ngx_gmtime(t, &tm);

    return ngx_sprintf(buf, "%s, %02d %s %4d %02d:%02d:%02d GMT",
                       week[tm.ngx_tm_wday],
                       tm.ngx_tm_mday,
                       months[tm.ngx_tm_mon - 1],
                       tm.ngx_tm_year,
                       tm.ngx_tm_hour,
                       tm.ngx_tm_min,
                       tm.ngx_tm_sec);
}


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
u_char *
ngx_http_cookie_time(u_char *buf, time_t t)
{
    ngx_tm_t  tm;

    /**
     * @brief
     *     1970年１月１日からの経過時間（秒）から、現在の日付・時刻を取得する
     * @param[out]
     *     tp: 現在の日付・時刻
     * @param[in]
     *     t: 1970年１月１日からの経過時間（秒）
     */
    ngx_gmtime(t, &tm);

    /*
     * Netscape 3.x does not understand 4-digit years at all and
     * 2-digit years more than "37"
     */

    return ngx_sprintf(buf,
                       (tm.ngx_tm_year > 2037) ?
                                         "%s, %02d-%s-%d %02d:%02d:%02d GMT":
                                         "%s, %02d-%s-%02d %02d:%02d:%02d GMT",
                       week[tm.ngx_tm_wday],
                       tm.ngx_tm_mday,
                       months[tm.ngx_tm_mon - 1],
                       (tm.ngx_tm_year > 2037) ? tm.ngx_tm_year:
                                                 tm.ngx_tm_year % 100,
                       tm.ngx_tm_hour,
                       tm.ngx_tm_min,
                       tm.ngx_tm_sec);
}


/**
 * @brief
 *     1970年１月１日からの経過時間（秒）から、日付・時刻形式へ変換する
 * @param[out]
 *     tp: 変換結果の日付・時刻
 * @param[in]
 *     t: 1970年１月１日からの経過時間（秒）
 */
// OK
void
ngx_gmtime(time_t t, ngx_tm_t *tp)
{
    ngx_int_t   yday;
    ngx_uint_t  sec, min, hour, mday, mon, year, wday, days, leap;

    /* the calculation is valid for positive time_t only */

    if (t < 0) {
        t = 0;
    }

    days = t / 86400;
    sec = t % 86400;

    /*
     * no more than 4 year digits supported,
     * truncate to December 31, 9999, 23:59:59
     */

    if (days > 2932896) {
        days = 2932896;
        sec = 86399;
    }

    /* January 1, 1970 was Thursday */

    wday = (4 + days) % 7;

    hour = sec / 3600;
    sec %= 3600;
    min = sec / 60;
    sec %= 60;

    /*
     * the algorithm based on Gauss' formula,
     * see src/core/ngx_parse_time.c
     */

    /* days since March 1, 1 BC */
    days = days - (31 + 28) + 719527;

    /*
     * The "days" should be adjusted to 1 only, however, some March 1st's go
     * to previous year, so we adjust them to 2.  This causes also shift of the
     * last February days to next year, but we catch the case when "yday"
     * becomes negative.
     */

    year = (days + 2) * 400 / (365 * 400 + 100 - 4 + 1);

    yday = days - (365 * year + year / 4 - year / 100 + year / 400);

    if (yday < 0) {
        leap = (year % 4 == 0) && (year % 100 || (year % 400 == 0));
        yday = 365 + leap + yday;
        year--;
    }

    /*
     * The empirical formula that maps "yday" to month.
     * There are at least 10 variants, some of them are:
     *     mon = (yday + 31) * 15 / 459
     *     mon = (yday + 31) * 17 / 520
     *     mon = (yday + 31) * 20 / 612
     */

    mon = (yday + 31) * 10 / 306;

    /* the Gauss' formula that evaluates days before the month */

    mday = yday - (367 * mon / 12 - 30) + 1;

    if (yday >= 306) {

        year++;
        mon -= 10;

        /*
         * there is no "yday" in Win32 SYSTEMTIME
         *
         * yday -= 306;
         */

    } else {

        mon += 2;

        /*
         * there is no "yday" in Win32 SYSTEMTIME
         *
         * yday += 31 + 28 + leap;
         */
    }

    tp->ngx_tm_sec = (ngx_tm_sec_t) sec;
    tp->ngx_tm_min = (ngx_tm_min_t) min;
    tp->ngx_tm_hour = (ngx_tm_hour_t) hour;
    tp->ngx_tm_mday = (ngx_tm_mday_t) mday;
    tp->ngx_tm_mon = (ngx_tm_mon_t) mon;
    tp->ngx_tm_year = (ngx_tm_year_t) year;
    tp->ngx_tm_wday = (ngx_tm_wday_t) wday;
}


/**
 * @brief
 *     キャッシュと比較して、現在時刻が日をまたいだかどうかを判定して更新する
 * @param[in]
 *     when: 現在時刻
 * @retval
 *     
 */
time_t
ngx_next_time(time_t when)
{
    time_t     now, next;
    struct tm  tm;

    /**
     * @brief
     *     通常時間キャッシュから現在時刻を秒単位で取得
     * @retval
     *     現在時刻（秒）
     */
    now = ngx_time();

    /**
     * @brief
     *     グリニッチ標準時（1970/1/1 00:00:00からの経過秒数）をわかりやすい形式（ローカル時刻）へ変換
     * @param[out]
     *     tm: わかりやすい形式
     * @param[in]
     *     s: グリニッチ標準時
     */
    ngx_libc_localtime(now, &tm);

    tm.tm_hour = (int) (when / 3600);
    when %= 3600;
    tm.tm_min = (int) (when / 60);
    tm.tm_sec = (int) (when % 60);

    // グリニッジへ変換？（カレンダータイム）
    next = mktime(&tm);

    if (next == -1) {
        return -1;
    }

    // next の方が大きいなら日をまたいでいないはず
    if (next - now > 0) {
        return next;
    }

    // 負ということは日をまたいだ
    tm.tm_mday++;

    /* mktime() should normalize a date (Jan 32, etc) */

    next = mktime(&tm);

    if (next != -1) {
        return next;
    }

    return -1;
}
