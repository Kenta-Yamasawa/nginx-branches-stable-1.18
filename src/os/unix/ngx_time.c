
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
 *     �O���j�b�`�W�����i1970/1/1 00:00:00����̌o�ߕb���j���킩��₷���`���i���[�J�������j�֕ϊ�
 * @param[out]
 *     tm: �킩��₷���`��
 * @param[in]
 *     s: �O���j�b�`�W����
 */
void
ngx_localtime(time_t s, ngx_tm_t *tm)
{
#if (NGX_HAVE_LOCALTIME_R)
    // ���ƂȂ�����������g���ׂ�
    // localtime() �� gmtime() �͓����������̈���g���Ă���̂ŁA
    // ���� timer �ɑ΂��ē����l��Ԃ��Ă��܂��d�l�ł���i�S�~�j
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
 *     �O���j�b�`�W�����i1970/1/1 00:00:00����̌o�ߕb���j���킩��₷���`���i���[�J�������j�֕ϊ�
 * @param[out]
 *     tm: �킩��₷���`��
 * @param[in]
 *     s: �O���j�b�`�W����
 */
void
ngx_libc_localtime(time_t s, struct tm *tm)
{
#if (NGX_HAVE_LOCALTIME_R)
    (void) localtime_r(&s, tm);

#else
    struct tm  *t;

    // �O���j�b�`�W�����i1970/1/1 00:00:00����̌o�ߕb���j���킩��₷���`���֕ϊ�
    t = localtime(&s);
    *tm = *t;

#endif
}


/**
 * @brief
 *     �O���j�b�`�W�����i1970/1/1 00:00:00����̌o�ߕb���j���킩��₷���`���i���E���莞���j�֕ϊ�
 * @param[out]
 *     tm: �킩��₷���`��
 * @param[in]
 *     s: �O���j�b�`�W����
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
