
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @macro
 *     NGX_DEBUG: 
 *     NGX_DEBUG_PALLOC: 
 */


static ngx_inline void *ngx_palloc_small(ngx_pool_t *pool, size_t size,
    ngx_uint_t align);
static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);


/**
 * @brief
 *     1. size �������v�[���̈�𐶐����� NGX_POOL_ALIGNMENT �ŃA���C�������g����
 *     2. �v�[���̐擪�̃w�b�_�̈��������
 * @param[in]
 *     size: �v�[���̃T�C�Y�i�w�b�_���܂ށj
 *     log: �g�p���郍�O�G���W��
 */
ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;

    // ���蓖�ĂăA���C�������g
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
    if (p == NULL) {
        return NULL;
    }

    // ��Ԍ��̋󂫔Ԓn
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    // ���̃v�[���̍Ō�{�P
    p->d.end = (u_char *) p + size;
    p->d.next = NULL;
    p->d.failed = 0;

    // �c��T�C�Y
    size = size - sizeof(ngx_pool_t);
    // �c��g�p�\�T�C�Y�i�y�[�W�T�C�Y�Ɏ��߂����Ƃ��A���������킯�ł͂Ȃ����ۂ��j
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;

    return p;
}


void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;

    // �v�[�����ۗL���邷�ׂẴN���[���A�b�v�֐����Ăяo��
    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "run cleanup: %p", c);
            c->handler(c->data);
        }
    }

#if (NGX_DEBUG)

    /*
     * we could allocate the pool->log from this pool
     * so we cannot use this log while free()ing the pool
     */

    for (l = pool->large; l; l = l->next) {
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);
    }

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: %p, unused: %uz", p, p->d.end - p->d.last);

        if (n == NULL) {
            break;
        }
    }

#endif

    // ���̃v�[�����ۗL���� large �����ׂĊJ������
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    // ���̃v�[�����炽�ǂ��S�Ẵv�[�����J������
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);

        if (n == NULL) {
            break;
        }
    }
}


/**
 * @brief
 *     
 */
void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;

    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
        p->d.failed = 0;
    }

    pool->current = pool;
    pool->chain = NULL;
    pool->large = NULL;
}


/**
 * @brief
 *     �v�[�����烁���������蓖�Ă�
 * @param[in]
 *     pool: ���̃v�[�����犄�蓖�Ă�
 *     size: ���o�C�g���蓖�Ă邩
 * @macro
 *     NGX_DEBUG_PALLOC: ������ OFF ���Ə�� ngx_palloc_large() �ɂ�銄�蓖�Ă��s����
 */
void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
#if !(NGX_DEBUG_PALLOC)
    // �܂����̃v�[���̎g�p�\�T�C�Y�𒴉߂��Ă��Ȃ��Ȃ� ngx_palloc_small() ��
    if (size <= pool->max) {
        return ngx_palloc_small(pool, size, 1);
    }
#endif

    return ngx_palloc_large(pool, size);
}


void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
#if !(NGX_DEBUG_PALLOC)
    if (size <= pool->max) {
        return ngx_palloc_small(pool, size, 0);
    }
#endif

    return ngx_palloc_large(pool, size);
}


/**
 * @macro
 *     NGX_ALIGNMENT: align �� 1 �Ȃ炱�̒l�ŃA���C�������g���Ċ��蓖�Ă�
 */
static ngx_inline void *
ngx_palloc_small(ngx_pool_t *pool, size_t size, ngx_uint_t align)
{
    u_char      *m;
    ngx_pool_t  *p;

    p = pool->current;

    // ���̃v�[���\���̂��ۗL����S�v�[�����Q��
    do {
        // ��ԁA���̋󂫗̈���Q��
        m = p->d.last;

        // �K�v�ł���΃A���C�������g
        if (align) {
            m = ngx_align_ptr(m, NGX_ALIGNMENT);
        }

        // ����A�v�����ꂽ�T�C�Y���A�󂫗̈悪���邩�m�F
        if ((size_t) (p->d.end - m) >= size) {
            // ����Ȃ玟�̋󂫗̈���X�V
            p->d.last = m + size;

            // ���蓖�Ă��̈��Ԃ�
            return m;
        }

        // ���̃v�[���̈��
        p = p->d.next;

    } while (p);

    // �ǂ̃v�[���ɂ��󂫗̈悪�Ȃ���΃v�[���f�[�^�̈�𐶐�
    // �͂��߂Ă��̊֐����Ă΂ꂽ�����A�������A�������Ă΂��
    return ngx_palloc_block(pool, size);
}


static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new;

    // �v�[���̑S�T�C�Y���擾�i�g�p�\�łȂ��������܂ށj
    psize = (size_t) (pool->d.end - (u_char *) pool);

    // 
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
    if (m == NULL) {
        return NULL;
    }

    new = (ngx_pool_t *) m;

    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;

    for (p = pool->current; p->d.next; p = p->d.next) {
        // ���蓖�Ă悤�Ƃ������ǁA�̈悪����Ȃ������񐔂��C���N�������g����
        // 4 ��𒴂�����I�������珜�O����
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }

    p->d.next = new;

    return m;
}


static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;

    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    n = 0;

    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {
            large->alloc = p;
            return p;
        }

        if (n++ > 3) {
            break;
        }
    }

    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    ngx_pool_large_t  *large;

    p = ngx_memalign(alignment, size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;

    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            ngx_free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


/**
 * @brief
 */
void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


/**
 * @brief
 *     �v�[���N���[���A�b�v������������
 * @param[in]
 *     p: �v�[���N���[���A�b�v�����̃v�[�����犄�蓖�Ă�
 *     size: 
 * @retval
 *     ���������v�[���N���[���A�b�v���Ǘ����邽�߂̍\����
 */
ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;

    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;
    }

    if (size) {
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }

    c->handler = NULL;
    c->next = p->cleanup;

    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


/**
 * @brief
 *     �N���[���A�b�v�n���h��
 *     �v�[���ŃI�[�v�������t�@�C�����Ǘ�����ꍇ�A�J�����邾���ł͂Ȃ�
 *     �t�@�C�������K�v������̂ŁA���̂��߂̃n���h��
 * @detail
 *     �t�@�C�����g�p���Ȃ��̂ł���ΕK�v�Ȃ�
 */
void
ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
    ngx_pool_cleanup_t       *c;
    ngx_pool_cleanup_file_t  *cf;

    for (c = p->cleanup; c; c = c->next) {
        if (c->handler == ngx_pool_cleanup_file) {

            cf = c->data;

            if (cf->fd == fd) {
                c->handler(cf);
                c->handler = NULL;
                return;
            }
        }
    }
}


/**
 * @brief
 *     �N���[���A�b�v�n���h��
 *     �v�[���ŃI�[�v�������t�@�C�����Ǘ�����ꍇ�A�J�����邾���ł͂Ȃ�
 *     �t�@�C�������K�v������̂ŁA���̂��߂̃n���h��
 * @detail
 *     �t�@�C�����g�p���Ȃ��̂ł���ΕK�v�Ȃ�
 */
void
ngx_pool_cleanup_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


/**
 * @brief
 *     �N���[���A�b�v�n���h��
 *     �v�[���ŃI�[�v�������t�@�C�����Ǘ�����ꍇ�A�J�����邾���ł͂Ȃ�
 *     �t�@�C������č폜����K�v������̂ŁA���̂��߂̃n���h��
 * @detail
 *     �t�@�C�����g�p���Ȃ��̂ł���ΕK�v�Ȃ�
 */
void
ngx_pool_delete_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_err_t  err;

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                   c->fd, c->name);

    if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
        err = ngx_errno;

        if (err != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_CRIT, c->log, err,
                          ngx_delete_file_n " \"%s\" failed", c->name);
        }
    }

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


#if 0

static void *
ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
