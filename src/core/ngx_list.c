
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @brief
 *     �������v�[���� n * size �o�C�g�܂Ńf�[�^���i�[�ł��郊�X�g�𐶐�����B
 *     �Ăяo������A����������Ă����Ɏg����悤�ɂȂ�B
 * @param[in]
 *     pool: ���̃v�[�����烊�X�g�����蓖�Ă�
 *     n   : ���X�g�Ɋi�[����f�[�^�̐�
 *     size: ���X�g�Ɋi�[����e�f�[�^�̃o�C�g��
 * @retval
 *     �������ꂽ���X�g�ւ̃|�C���^
 */
ngx_list_t *
ngx_list_create(ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    ngx_list_t  *list;

    list = ngx_palloc(pool, sizeof(ngx_list_t));
    if (list == NULL) {
        return NULL;
    }

    if (ngx_list_init(list, pool, n, size) != NGX_OK) {
        return NULL;
    }

    return list;
}


/**
 * @brief
 *     ���������X�g�ɗv�f���v�b�V������
 * @param[in]
 *     pool: ���̃��X�g�ɗv�f���v�b�V������
 * @retval
 *     �f�Ѓ��X�g�̂��K�A�h���X
 */
void *
ngx_list_push(ngx_list_t *l)
{
    // elt: �v�f�̂��ƁBelement �̗��H
    void             *elt;
    ngx_list_part_t  *last;

    last = l->last;

    // �Ō�̃��X�g�p�[�g�����Ɉ�t�Ȃ�
    if (last->nelts == l->nalloc) {

        /* the last part is full, allocate a new list part */

        last = ngx_palloc(l->pool, sizeof(ngx_list_part_t));
        if (last == NULL) {
            return NULL;
        }

        last->elts = ngx_palloc(l->pool, l->nalloc * l->size);
        if (last->elts == NULL) {
            return NULL;
        }

        last->nelts = 0;
        last->next = NULL;

        l->last->next = last;
        l->last = last;
    }

    elt = (char *) last->elts + l->size * last->nelts;
    last->nelts++;

    return elt;
}
