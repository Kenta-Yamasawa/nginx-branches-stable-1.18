
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @brief
 *     �ꎞ�o�b�t�@�𐶐����ĕԂ�
 * @param[in]
 *     pool: ���̃v�[����p���ăo�b�t�@�𐶐�����
 *     size: ���̃o�b�t�@���ۗL���郁�����e�ʂ̃T�C�Y
 * @retval
 *     ngx_buf_t *: ����
 *     NULL: ���s
 */
ngx_buf_t *
ngx_create_temp_buf(ngx_pool_t *pool, size_t size)
{
    ngx_buf_t *b;

    // ngx_pcalloc() : 0 �Ŗ��߂�
    b = ngx_calloc_buf(pool);
    if (b == NULL) {
        return NULL;
    }

    // 
    b->start = ngx_palloc(pool, size);
    if (b->start == NULL) {
        return NULL;
    }

    /*
     * set by ngx_calloc_buf():
     *
     *     b->file_pos = 0;
     *     b->file_last = 0;
     *     b->file = NULL;
     *     b->shadow = NULL;
     *     b->tag = 0;
     *     and flags
     */

    b->pos = b->start;
    b->last = b->start;
    // �Ō�̃������̈�{�P
    b->end = b->last + size;
    b->temporary = 1;

    return b;
}


/**
 * @brief
 *     �o�b�t�@�`�F�C����V���ɐ�������
 *     �Ȃ��A�o�b�t�@�`�F�C�����w���o�b�t�@�͖��w��i�܂��j
 * @retval
 *     NULL: ���s
 *     otherwise: ���������o�b�t�@�`�F�C��
 */
ngx_chain_t *
ngx_alloc_chain_link(ngx_pool_t *pool)
{
    ngx_chain_t  *cl;

    // �v�[�����o�b�t�@�`�F�C����ێ����Ă��邱�Ƃ�����
    // ���������ꍇ�͂����̂��̂��g��
    cl = pool->chain;

    if (cl) {
        // �v�[���̃`�F�C�����犄�蓖�Ă�ꍇ�́A���̃`�F�C���̓v�[���̂��̂ł͂Ȃ��Ȃ�
        pool->chain = cl->next;
        return cl;
    }

    cl = ngx_palloc(pool, sizeof(ngx_chain_t));
    if (cl == NULL) {
        return NULL;
    }

    return cl;
}


/**
 * @brief
 *     �����̃o�b�t�@���܂Ƃ߂Đ������āA�o�b�t�@�`�F�C���Ƃ��ĕԂ�
 * @param[in]
 *     pool: ���̃v�[�����犄�蓖�Ă�
 *     bufs: ��������o�b�t�@�̌��ƃT�C�Y���܂Ƃ߂�����
 * @retval
 *     NULL: ���s
 *     otherwise: ���������o�b�t�@�`�F�C��
 */
ngx_chain_t *
ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs)
{
    u_char       *p;
    ngx_int_t     i;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, **ll;

    p = ngx_palloc(pool, bufs->num * bufs->size);
    if (p == NULL) {
        return NULL;
    }

    ll = &chain;

    for (i = 0; i < bufs->num; i++) {

        b = ngx_calloc_buf(pool);
        if (b == NULL) {
            return NULL;
        }

        /*
         * set by ngx_calloc_buf():
         *
         *     b->file_pos = 0;
         *     b->file_last = 0;
         *     b->file = NULL;
         *     b->shadow = NULL;
         *     b->tag = 0;
         *     and flags
         *
         */

        b->pos = p;
        b->last = p;
        b->temporary = 1;

        b->start = p;
        p += bufs->size;
        b->end = p;

        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            return NULL;
        }

        cl->buf = b;
        *ll = cl;
        ll = &cl->next;
    }

    *ll = NULL;

    return chain;
}


/**
 * @brief
 *     �������̃o�b�t�@�`�F�C���ɑ�O�����̃o�b�t�@�`�F�C����ǉ�����
 * @param[out]
 *     chain: ���̃o�b�t�@�`�F�C���֒ǉ�����
 * @param[in]
 *     pool: ���̃v�[������o�b�t�@�`�F�C�������蓖�Ă�i�o�b�t�@���̂��͎̂󂯌p���j
 *     in:   ���̃o�b�t�@�`�F�C����ǉ�����
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
 */
ngx_int_t
ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain, ngx_chain_t *in)
{
    ngx_chain_t  *cl, **ll;

    ll = chain;

    // �o�b�t�@�`�F�C���̍Ō���Q��
    for (cl = *chain; cl; cl = cl->next) {
        ll = &cl->next;
    }

    while (in) {
        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            *ll = NULL;
            return NGX_ERROR;
        }

        cl->buf = in->buf;
        *ll = cl;
        ll = &cl->next;
        in = in->next;
    }

    *ll = NULL;

    return NGX_OK;
}


/**
 * @brief
 *     �t���[�ȃo�b�t�@�`�F�C���̐擪����o�b�t�@���ЂƂ�菜���Ė߂�
 *     ���邢�̓o�b�t�@�`�F�C������������ĕԂ�
 * @param[in]
 *     p: ��������ꍇ�Ɏg�p����v�[��
 *     free: ���̃o�b�t�@�`�F�C��������菜���ĕԂ��iNULL���j
 * @retval
 *     NULL: ���s
 *     otherwise: ����
 */
ngx_chain_t *
ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free)
{
    ngx_chain_t  *cl;

    if (*free) {
        cl = *free;
        *free = cl->next;
        cl->next = NULL;
        return cl;
    }

    cl = ngx_alloc_chain_link(p);
    if (cl == NULL) {
        return NULL;
    }

    cl->buf = ngx_calloc_buf(p);
    if (cl->buf == NULL) {
        return NULL;
    }

    cl->next = NULL;

    return cl;
}


/**
 * @brief
 *     ��O���� busy �o�b�t�@�`�F�C�����X�g�̊e�o�b�t�@�`�F�C����
 *     ������ free �o�b�t�@�`�F�C�����X�g�Ƒ����� pool ���ۗL����o�b�t�@�`�F�C�����X�g�Ɉڂ�
 *     �o�b�t�@�`�F�C���̃^�O���Ƒ�T���� tag ����v���Ă����ꍇ�� free �o�b�t�@�`�F�C�����X�g��
 *     �����łȂ��ꍇ�� pool �̃o�b�t�@�`�F�C�����X�g��
 * @param[out]
 *     p: busy �o�b�t�@�`�F�C�����X�g�̉�����
 *     free: busy �o�b�t�@�`�F�C�����X�g�̉�����
 * @param[in]
 *     busy: ���̃o�b�t�@�`�F�C�����X�g�������
 *     out: busy �o�b�t�@�`�F�C�����X�g�ƘA��������o�b�t�@�`�F�C�����X�g�iNULL���j
 *     tag: ������̕���Ɏg�p
 */
void
ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free, ngx_chain_t **busy,
    ngx_chain_t **out, ngx_buf_tag_t tag)
{
    ngx_chain_t  *cl;

    // out �o�b�t�@�`�F�C�����X�g����łȂ�
    if (*out) {
        // busy �o�b�t�@�`�F�C�����X�g����ł���
        if (*busy == NULL) {
            // busy �o�b�t�@�`�F�C�����X�g�� out �o�b�t�@�`�F�C�����X�g�Ɠ����e�ɂ���i�R�s�[�ł͂Ȃ��j
            *busy = *out;

        } else {
            // busy �o�b�t�@�`�F�C�����X�g�̂��Ƃ� out �o�b�t�@�`�F�C�����X�g������
            for (cl = *busy; cl->next; cl = cl->next) { /* void */ }

            cl->next = *out;
        }

        // �ǂ݂̂� out �o�b�t�@�`�F�C�����X�g�͋��
        *out = NULL;
    }

    while (*busy) {
        cl = *busy;

        // �o�b�t�@�̃T�C�Y�� 0 �łȂ��Ȃ珈�����I��
        if (ngx_buf_size(cl->buf) != 0) {
            break;
        }

        // �^�O�Ƃ́H
        if (cl->buf->tag != tag) {
            *busy = cl->next;
            // ���̃o�b�t�@�`�F�C�����v�[���̃o�b�t�@�`�F�C�����X�g�։�����
            ngx_free_chain(p, cl);
            continue;
        }

        // �����o��������
        cl->buf->pos = cl->buf->start;
        cl->buf->last = cl->buf->start;

        // free �o�b�t�@�`�F�C�����X�g�ɉ�����
        *busy = cl->next;
        cl->next = *free;
        *free = cl;
    }
}


/**
 * @brief
 *     �ǂ�����ǂ��܂ł̃o�b�t�@�ɂ��āA�ЂƂ܂Ƃ߂ɂł��邩���؂���
 * @param[in:out]
 *     in: ����f�[�^�ʂ𒴂���ŏ��̃o�b�t�@�`�F�C�����X�g�������Ă����i���m�ł͂Ȃ��A������Ƃ͒����邩���j
 * @param[in]
 *     limit: ����f�[�^�ʁA�o�b�t�@�`�F�C�����X�g�̂����A���̏���f�[�^�ʂ𒴂��Ȃ��͈͂�؂�o��
 * @retval:
 *     ���ۂ̍��v�T�C�Y
 */
off_t
ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit)
{
    off_t         total, size, aligned, fprev;
    ngx_fd_t      fd;
    ngx_chain_t  *cl;

    total = 0;

    cl = *in;
    fd = cl->buf->file->fd;

    /**
     * �ȉ��̏�����������������o�b�t�@�`�F�C�����X�g�̃o�b�t�@�`�F�C�������ɎQ�Ƃ���
     *     �E�o�b�t�@�`�F�C������ł͂Ȃ�
     *     �E�o�b�t�@�� in_file �t���O���Z�b�g����Ă���
     *     �E����܂ł̍��v�T�C�Y������𒴂��Ă��Ȃ�
     *     �E�S������t�@�C�������݂���o�b�t�@�ł���
     *     �E��O�̃��[�v�ŒS�������o�b�t�@�̃t�@�C�����e����i�������͓ǂݏI������j�ł͂Ȃ�
     */
    do {
        size = cl->buf->file_last - cl->buf->file_pos;

        // ����܂ł̍��v�f�[�^�ʂ�����𒴉߂���
        if (size > limit - total) {
            size = limit - total;

            // ngx_pagesize �̔{���o�C�g�̂����Acl->buf->file_last �܂Ŋi�[�ł���T�C�Y
            // ���Ƃ��΁A�y�[�W�T�C�Y�� 16 bit �Ńt�@�C���T�C�Y�� 100 bit ��������
            // aligned �� 112 (16 * 7)
            aligned = (cl->buf->file_pos + size + ngx_pagesize - 1)
                       & ~((off_t) ngx_pagesize - 1);

            // �A���C�������g�T�C�Y���t�@�C���T�C�Y�ȉ��H�i���͂܂��������̃P�[�X���Ă���́I�H�j
            if (aligned <= cl->buf->file_last) {
                // ���łɓǂ񂾃f�[�^��������
                size = aligned - cl->buf->file_pos;
            }

            // �g�[�^���T�C�Y�ɉ����ă��[�v�𔲂���
            total += size;
            break;
        }

        total += size;
        fprev = cl->buf->file_pos + size;
        cl = cl->next;

    } while (cl
             && cl->buf->in_file
             && total < limit
             && fd == cl->buf->file->fd
             && fprev == cl->buf->file_pos);

    *in = cl;

    return total;
}


/**
 * @brief
 *     ���M�����������A�o�b�t�@�`�F�C���̊e�o�b�t�@�̃f�[�^�擪�|�C���^���X�V����B
 * @param[out]
 *     in: �o�b�t�@�`�F�C��
 * @param[in]
 *     sent: ���M�����f�[�^��
 * @detail
 *     in �̒��g�ɕω��͂Ȃ�
 */
ngx_chain_t *
ngx_chain_update_sent(ngx_chain_t *in, off_t sent)
{
    off_t  size;

    for ( /* void */ ; in; in = in->next) {

        if (ngx_buf_special(in->buf)) {
            continue;
        }

        if (sent == 0) {
            break;
        }

        size = ngx_buf_size(in->buf);

        // ���̃o�b�t�@�̂��ׂẴf�[�^�����M��������
        if (sent >= size) {
            sent -= size;

            // �擪�|�C���^���X�V
            if (ngx_buf_in_memory(in->buf)) {
                in->buf->pos = in->buf->last;
            }

            if (in->buf->in_file) {
                in->buf->file_pos = in->buf->file_last;
            }

            continue;
        }

        if (ngx_buf_in_memory(in->buf)) {
            in->buf->pos += (size_t) sent;
        }

        if (in->buf->in_file) {
            in->buf->file_pos += sent;
        }

        break;
    }

    return in;
}
