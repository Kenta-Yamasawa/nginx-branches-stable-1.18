
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @macro
 *     �Ȃ�
 */


/**
 * @brief
 *     �z����쐬����
 *     �z��͕����̕����z�񂩂�\�������
 *     �e�����z��� n �� size �o�C�g�f�[�^���Ǘ��ł���
 * @param[in]
 *     p: ���̃v�[����p���Ĕz��\���̂����蓖�Ă�
 * @retval
 *     ���������z��\���̂�Ԃ�
 */
ngx_array_t *
ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size)
{
    ngx_array_t *a;

    a = ngx_palloc(p, sizeof(ngx_array_t));
    if (a == NULL) {
        return NULL;
    }

    /**
     * @brief
     *     �z�������������
     * @param[out]
     *     array: ����������z��\����
     * @param[in]
     *     pool: ���蓖�ĂɎg�p����v�[��
     *     n: ���̗v�f��ۗL�ł��邩
     *     size: �e�v�f�̃T�C�Y�͂�����
     * @retval
     *     NGX_OK: ����
     *     NGX_ERROR: ���s
     * @post
     *     array->nelts �� 0 �ɏ���������Ă���
     *     array->size �Ɋe�v�f�̃T�C�Y���Z�b�g����Ă���
     *     array->nalloc �ɉ��̗v�f��ۗL�ł��邩�Z�b�g����Ă���
     *     arrya->pool �Ɋ��蓖�ĂɎg�p����v�[�����Z�b�g����Ă���
     *     array->elts �� array->size * array->nalloc ���̋󂫃������̈���w���Ă���
     */
    if (ngx_array_init(a, p, n, size) != NGX_OK) {
        return NULL;
    }

    return a;
}


void
ngx_array_destroy(ngx_array_t *a)
{
    ngx_pool_t  *p;

    p = a->pool;

    if ((u_char *) a->elts + a->size * a->nalloc == p->d.last) {
        p->d.last -= a->size * a->nalloc;
    }

    if ((u_char *) a + sizeof(ngx_array_t) == p->d.last) {
        p->d.last = (u_char *) a;
    }
}


/**
 * @brief
 *     �����œn�����z��̎��̋󂫔Ԓn��Ԃ�
 * @param[in]
 *     a: �Ώ۔z��
 * @retval
 *     NULL: ���s
 *     otherwise: a->size �o�C�g���̋󂫔Ԓn
 * @detail
 *     ���̊֐��Ńf�[�^���v�b�V���ł���킯�ł͂Ȃ�
 *     ���̊֐��Ŏ擾�����󂫔Ԓn�Ɏ蓮�Ńf�[�^��������K�v������
 */
void *
ngx_array_push(ngx_array_t *a)
{
    void        *elt, *new;
    size_t       size;
    ngx_pool_t  *p;

    // ���ł� a->nalloc �Ŏw�肵���������ׂĎg��ꂽ�ꍇ
    if (a->nelts == a->nalloc) {

        /* the array is full */

        size = a->size * a->nalloc;

        p = a->pool;

        if ((u_char *) a->elts + size == p->d.last
            && p->d.last + a->size <= p->d.end)
        {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */
            // �v�[���̈�ԁA���Ɉʒu���Ă���P�[�X

            p->d.last += a->size;
            a->nalloc++;

        } else {
            /* allocate a new array */
            // �{���̔{�̃T�C�Y�̔z����ēx�A�v�[�����犄�蓖�ĂȂ���

            new = ngx_palloc(p, 2 * size);
            if (new == NULL) {
                return NULL;
            }

            // �O�̔z��̗v�f���R�s�[���Ĉڂ�
            ngx_memcpy(new, a->elts, size);
            // �p�����[�^���X�V����
            a->elts = new;
            a->nalloc *= 2;
        }
    }

    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts++;

    return elt;
}


/**
 * @brief
 *     �����œn�����z��̎��̋󂫔Ԓn��Ԃ�
 * @param[in]
 *     a: �Ώ۔z��
 * @retval
 *     a->size �o�C�g���̋󂫔Ԓn
 * @detail
 *     ���̊֐��Ńf�[�^���v�b�V���ł���킯�ł͂Ȃ�
 *     ���̊֐��Ŏ擾�����󂫔Ԓn�Ɏ蓮�Ńf�[�^��������K�v������
 */
void *
ngx_array_push_n(ngx_array_t *a, ngx_uint_t n)
{
    void        *elt, *new;
    size_t       size;
    ngx_uint_t   nalloc;
    ngx_pool_t  *p;

    size = n * a->size;

    if (a->nelts + n > a->nalloc) {

        /* the array is full */

        p = a->pool;

        if ((u_char *) a->elts + a->size * a->nalloc == p->d.last
            && p->d.last + size <= p->d.end)
        {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */

            p->d.last += size;
            a->nalloc += n;

        } else {
            /* allocate a new array */

            nalloc = 2 * ((n >= a->nalloc) ? n : a->nalloc);

            new = ngx_palloc(p, nalloc * a->size);
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, a->nelts * a->size);
            a->elts = new;
            a->nalloc = nalloc;
        }
    }

    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts += n;

    return elt;
}
