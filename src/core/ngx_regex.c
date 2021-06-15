
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>

// �}�N��
//     NGX_HAVE_PCRE_JIT: �T�|�[�g���Ă��邩�ǂ���


// pcre �Ɋւ���ݒ莖���Q
typedef struct {
    // PCRE_JIT ���L�����ǂ���
    ngx_flag_t  pcre_jit;
} ngx_regex_conf_t;


static void * ngx_libc_cdecl ngx_regex_malloc(size_t size);
static void ngx_libc_cdecl ngx_regex_free(void *p);
#if (NGX_HAVE_PCRE_JIT)
static void ngx_pcre_free_studies(void *data);
#endif

static ngx_int_t ngx_regex_module_init(ngx_cycle_t *cycle);

static void *ngx_regex_create_conf(ngx_cycle_t *cycle);
static char *ngx_regex_init_conf(ngx_cycle_t *cycle, void *conf);

static char *ngx_regex_pcre_jit(ngx_conf_t *cf, void *post, void *data);
static ngx_conf_post_t  ngx_regex_pcre_jit_post = { ngx_regex_pcre_jit };


static ngx_command_t  ngx_regex_commands[] = {
    // �Ȃ� pcre �̋@�\���X�s�[�h�A�b�v����炵��
    { ngx_string("pcre_jit"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_regex_conf_t, pcre_jit),
      &ngx_regex_pcre_jit_post },

      ngx_null_command
};


static ngx_core_module_t  ngx_regex_module_ctx = {
    ngx_string("regex"),
    ngx_regex_create_conf,
    ngx_regex_init_conf
};


ngx_module_t  ngx_regex_module = {
    NGX_MODULE_V1,
    &ngx_regex_module_ctx,                 /* module context */
    ngx_regex_commands,                    /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    ngx_regex_module_init,                 /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


// PCRE ���g�p����v�[��
static ngx_pool_t  *ngx_pcre_pool;
static ngx_list_t  *ngx_pcre_studies;


// PCRE �� pcre_compile() �����Ŏg�p���郁�������蓖�Ċ֐��ƊJ���֐����Anginx ���ۗL����v�[�����犄�蓖�Ă���̂֕ύX����
// https://bokko.hatenablog.com/entry/2013/10/13/142154
// OK
void
ngx_regex_init(void)
{
    pcre_malloc = ngx_regex_malloc;
    pcre_free = ngx_regex_free;
}


/**
 * @brief
 *     PCRE ���g�p����v�[����ݒ肷��
 * @param[in]
 *     pool: �ݒ肷��v�[��
 * @post
 *     ngx_pcre_pool �� PCRE ���g�p����v�[�����Z�b�g����Ă���
 */
static ngx_inline void
ngx_regex_malloc_init(ngx_pool_t *pool)
{
    ngx_pcre_pool = pool;
}


/**
 * @brief
 *     PCRE ���g�p����v�[������������
 * @post
 *     ngx_pcre_pool �� PCRE ���g�p����v�[�����Z�b�g����Ă��Ȃ�
 */
static ngx_inline void
ngx_regex_malloc_done(void)
{
    ngx_pcre_pool = NULL;
}


/**
 * pcre_compile() �����s���āA���̌��ʂ̃p�����[�^���Z�b�g����
 */
ngx_int_t
ngx_regex_compile(ngx_regex_compile_t *rc)
{
    int               n, erroff;
    char             *p;
    pcre             *re;
    const char       *errstr;
    ngx_regex_elt_t  *elt;

    ngx_regex_malloc_init(rc->pool);

    // �p�^�[���}�b�`���O�O�̉�����
    re = pcre_compile((const char *) rc->pattern.data, (int) rc->options,
                      &errstr, &erroff, NULL);

    /* ensure that there is no current pool */
    ngx_regex_malloc_done();

    // ���s
    if (re == NULL) {
        if ((size_t) erroff == rc->pattern.len) {
           rc->err.len = ngx_snprintf(rc->err.data, rc->err.len,
                              "pcre_compile() failed: %s in \"%V\"",
                               errstr, &rc->pattern)
                      - rc->err.data;

        } else {
           rc->err.len = ngx_snprintf(rc->err.data, rc->err.len,
                              "pcre_compile() failed: %s in \"%V\" at \"%s\"",
                               errstr, &rc->pattern, rc->pattern.data + erroff)
                      - rc->err.data;
        }

        return NGX_ERROR;
    }

    rc->regex = ngx_pcalloc(rc->pool, sizeof(ngx_regex_t));
    if (rc->regex == NULL) {
        goto nomem;
    }

    rc->regex->code = re;

    /* do not study at runtime */

    // ngx_pcre_studies ����łȂ��Ȃ�A��x�A���������p�^�[���}�b�`���O�̂��L���b�V�����Ă���
    if (ngx_pcre_studies != NULL) {
        elt = ngx_list_push(ngx_pcre_studies);
        if (elt == NULL) {
            goto nomem;
        }

        elt->regex = rc->regex;
        elt->name = rc->pattern.data;
    }

    // pcre �̃p�^�[���� () �ň͂�ꂽ���̂��ꂼ��ɂ��ă}�b�`���O�����؂���
    // https://www.php.net/manual/ja/regexp.reference.subpatterns.php
    // ���̃T�u�p�^�[���������Ă��邩���ׂ�
    n = pcre_fullinfo(re, NULL, PCRE_INFO_CAPTURECOUNT, &rc->captures);
    if (n < 0) {
        p = "pcre_fullinfo(\"%V\", PCRE_INFO_CAPTURECOUNT) failed: %d";
        goto failed;
    }

    if (rc->captures == 0) {
        return NGX_OK;
    }

    // (?P<name>pattern) �Ƃ����L�@��p���� �T�u�p�^�[���ɖ��O�����邱�Ƃ��ł��܂��B
    // https://www.php.net/manual/ja/regexp.reference.subpatterns.php
    // ���̖��O�t���T�u�p�^�[���������Ă��邩���ׂ�
    n = pcre_fullinfo(re, NULL, PCRE_INFO_NAMECOUNT, &rc->named_captures);
    if (n < 0) {
        p = "pcre_fullinfo(\"%V\", PCRE_INFO_NAMECOUNT) failed: %d";
        goto failed;
    }

    if (rc->named_captures == 0) {
        return NGX_OK;
    }

    n = pcre_fullinfo(re, NULL, PCRE_INFO_NAMEENTRYSIZE, &rc->name_size);
    if (n < 0) {
        p = "pcre_fullinfo(\"%V\", PCRE_INFO_NAMEENTRYSIZE) failed: %d";
        goto failed;
    }

    n = pcre_fullinfo(re, NULL, PCRE_INFO_NAMETABLE, &rc->names);
    if (n < 0) {
        p = "pcre_fullinfo(\"%V\", PCRE_INFO_NAMETABLE) failed: %d";
        goto failed;
    }

    return NGX_OK;

failed:

    rc->err.len = ngx_snprintf(rc->err.data, rc->err.len, p, &rc->pattern, n)
                  - rc->err.data;
    return NGX_ERROR;

nomem:

    rc->err.len = ngx_snprintf(rc->err.data, rc->err.len,
                               "regex \"%V\" compilation failed: no memory",
                               &rc->pattern)
                  - rc->err.data;
    return NGX_ERROR;
}


/**
 * @brief
 *     ���K�\���p�^�[���}�b�`���O���s��
 * @param[in]
 *     a: �z��A�����ɓ����Ă��邷�ׂẴp�^�[���}�b�`���O�̂ɂ��āA�s��
 *     s: �}�b�`���O�Ώۂ̕�����
 *     log: ���O�o�͗p
 * @retval
 *     NGX_OK: a �̂Ȃ��Ɉ�v������̂�������
 *     NGX_ERROR: �G���[����������
 *     NGX_DECLINED: a �̂Ȃ��Ɉ�v������̂��Ȃ�����
 */
ngx_int_t
ngx_regex_exec_array(ngx_array_t *a, ngx_str_t *s, ngx_log_t *log)
{
    ngx_int_t         n;
    ngx_uint_t        i;
    ngx_regex_elt_t  *re;

    re = a->elts;

    for (i = 0; i < a->nelts; i++) {

        // �p�^�[���}�b�`���O�ipcre_exec() ���}�N���֐��Œ�`���Ă���j
        n = ngx_regex_exec(re[i].regex, s, NULL, 0);

        // ��v���Ȃ�����
        if (n == NGX_REGEX_NO_MATCHED) {
            continue;
        }

        if (n < 0) {
            ngx_log_error(NGX_LOG_ALERT, log, 0,
                          ngx_regex_exec_n " failed: %i on \"%V\" using \"%s\"",
                          n, s, re[i].name);
            return NGX_ERROR;
        }

        /* match */

        return NGX_OK;
    }

    return NGX_DECLINED;
}


/**
 * @brief
 *     ngx_pcre_pool ���g���ă������̈���l�����ĕԂ�
 * @param[in]
 *     size: �l���������������T�C�Y
 * @retval
 *     NULL: ���s
 *     otherwise: �l�������������̈�ւ̃|�C���^
 */
// OK
static void * ngx_libc_cdecl
ngx_regex_malloc(size_t size)
{
    ngx_pool_t      *pool;
    pool = ngx_pcre_pool;

    if (pool) {
        return ngx_palloc(pool, size);
    }

    return NULL;
}


/**
 * @brief
 *     �������Ȃ�
 */
// OK
static void ngx_libc_cdecl
ngx_regex_free(void *p)
{
    return;
}


#if (NGX_HAVE_PCRE_JIT)

static void
ngx_pcre_free_studies(void *data)
{
    ngx_list_t *studies = data;

    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_regex_elt_t  *elts;

    part = &studies->part;
    elts = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            elts = part->elts;
            i = 0;
        }

        if (elts[i].regex->extra != NULL) {
            pcre_free_study(elts[i].regex->extra);
        }
    }
}

#endif


static ngx_int_t
ngx_regex_module_init(ngx_cycle_t *cycle)
{
    int               opt;
    const char       *errstr;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_regex_elt_t  *elts;

    opt = 0;

#if (NGX_HAVE_PCRE_JIT)
    {
    ngx_regex_conf_t    *rcf;
    ngx_pool_cleanup_t  *cln;

    rcf = (ngx_regex_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_regex_module);

    if (rcf->pcre_jit) {
        opt = PCRE_STUDY_JIT_COMPILE;

        /*
         * The PCRE JIT compiler uses mmap for its executable codes, so we
         * have to explicitly call the pcre_free_study() function to free
         * this memory.
         */

        cln = ngx_pool_cleanup_add(cycle->pool, 0);
        if (cln == NULL) {
            return NGX_ERROR;
        }

        cln->handler = ngx_pcre_free_studies;
        cln->data = ngx_pcre_studies;
    }
    }
#endif

    ngx_regex_malloc_init(cycle->pool);

    part = &ngx_pcre_studies->part;
    elts = part->elts;

    for (i = 0; /* void */ ; i++) {

        // ���̕������X�g��
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            elts = part->elts;
            i = 0;
        }

        elts[i].regex->extra = pcre_study(elts[i].regex->code, opt, &errstr);

        if (errstr != NULL) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "pcre_study() failed: %s in \"%s\"",
                          errstr, elts[i].name);
        }

#if (NGX_HAVE_PCRE_JIT)
        if (opt & PCRE_STUDY_JIT_COMPILE) {
            int jit, n;

            jit = 0;
            n = pcre_fullinfo(elts[i].regex->code, elts[i].regex->extra,
                              PCRE_INFO_JIT, &jit);

            if (n != 0 || jit != 1) {
                ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                              "JIT compiler does not support pattern: \"%s\"",
                              elts[i].name);
            }
        }
#endif
    }

    ngx_regex_malloc_done();

    ngx_pcre_studies = NULL;

    return NGX_OK;
}


/**
 * @brief
 *     �ݒ莖���\���̂𐶐����ĕԂ�
 * @param[in]
 *     cycle: ���̃T�C�N���̃v�[�����g���āAngx_pcre_studies �� �p�^�[���}�b�`���O�̃��X�g�𐶐�����
 * @retval
 *     NULL: ���s
 *     otherwise: ���������ݒ莖���\����
 */
static void *
ngx_regex_create_conf(ngx_cycle_t *cycle)
{
    ngx_regex_conf_t  *rcf;

    rcf = ngx_pcalloc(cycle->pool, sizeof(ngx_regex_conf_t));
    if (rcf == NULL) {
        return NULL;
    }

    rcf->pcre_jit = NGX_CONF_UNSET;

    ngx_pcre_studies = ngx_list_create(cycle->pool, 8, sizeof(ngx_regex_elt_t));
    if (ngx_pcre_studies == NULL) {
        return NULL;
    }

    return rcf;
}


/**
 * @brief
 *     �ݒ莖���\���̂�������
 * @param[in]
 *     cycle: �������Ȃ�
 *     conf: ����������ݒ莖���\����
 * @retval
 *     NGX_CONF_OK: �����i���̊֐��͕K����������j
 */
static char *
ngx_regex_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_regex_conf_t *rcf = conf;

    ngx_conf_init_value(rcf->pcre_jit, 0);

    return NGX_CONF_OK;
}


static char *
ngx_regex_pcre_jit(ngx_conf_t *cf, void *post, void *data)
{
    ngx_flag_t  *fp = data;

    if (*fp == 0) {
        return NGX_CONF_OK;
    }

#if (NGX_HAVE_PCRE_JIT)
    {
    int  jit, r;

    jit = 0;
    r = pcre_config(PCRE_CONFIG_JIT, &jit);

    if (r != 0 || jit != 1) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "PCRE library does not support JIT");
        *fp = 0;
    }
    }
#else
    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "nginx was built without PCRE JIT support");
    *fp = 0;
#endif

    return NGX_CONF_OK;
}
