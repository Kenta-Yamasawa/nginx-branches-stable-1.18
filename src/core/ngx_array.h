
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_ARRAY_H_INCLUDED_
#define _NGX_ARRAY_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


// 配列を管理する構造体
typedef struct {
    // この配列が保有する各要素へのエントリポインタ
    void        *elts;
    // 要素の数
    ngx_uint_t   nelts;
    // 各要素の数
    size_t       size;
    // 何個の要素を保有できるか
    ngx_uint_t   nalloc;
    // 割り当てに使用するプール
    ngx_pool_t  *pool;
} ngx_array_t;


ngx_array_t *ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size);
void ngx_array_destroy(ngx_array_t *a);
void *ngx_array_push(ngx_array_t *a);
void *ngx_array_push_n(ngx_array_t *a, ngx_uint_t n);


/**
 * @brief
 *     配列を初期化する
 * @param[out]
 *     array: 初期化する配列構造体
 * @param[in]
 *     pool: 割り当てに使用するプール
 *     n: 何個の要素を保有できるか
 *     size: 各要素のサイズはいくつか
 * @retval
 *     NGX_OK: 成功
 *     NGX_ERROR: 失敗
 * @post
 *     array->nelts が 0 個に初期化されている
 *     array->size に各要素のサイズがセットされている
 *     array->nalloc に何個の要素を保有できるかセットされている
 *     arrya->pool に割り当てに使用するプールがセットされている
 *     array->elts が array->size * array->nalloc 個分の空きメモリ領域を指している
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
