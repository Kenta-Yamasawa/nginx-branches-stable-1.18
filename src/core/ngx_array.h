
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_ARRAY_H_INCLUDED_
#define _NGX_ARRAY_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


// �z����Ǘ�����\����
typedef struct {
    // ���̔z�񂪕ۗL����e�v�f�ւ̃G���g���|�C���^
    void        *elts;
    // �v�f�̐�
    ngx_uint_t   nelts;
    // �e�v�f�̐�
    size_t       size;
    // ���̗v�f��ۗL�ł��邩
    ngx_uint_t   nalloc;
    // ���蓖�ĂɎg�p����v�[��
    ngx_pool_t  *pool;
} ngx_array_t;


ngx_array_t *ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size);
void ngx_array_destroy(ngx_array_t *a);
void *ngx_array_push(ngx_array_t *a);
void *ngx_array_push_n(ngx_array_t *a, ngx_uint_t n);


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
static ngx_inline ngx_int_t
ngx_array_init(ngx_array_t *array, ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    /*
     * set "array->nelts" before "array->elts", otherwise MSVC thinks
     * that "array->nelts" may be used without having been initialized
     */

    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;

    array->elts = ngx_palloc(pool, n * size);
    if (array->elts == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


#endif /* _NGX_ARRAY_H_INCLUDED_ */
