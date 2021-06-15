
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @macro
 *     なし
 */


/**
 * @brief
 *     配列を作成する
 *     配列は複数の部分配列から構成される
 *     各部分配列は n 個の size バイトデータを管理できる
 * @param[in]
 *     p: このプールを用いて配列構造体を割り当てる
 * @retval
 *     生成した配列構造体を返す
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
 *     引数で渡した配列の次の空き番地を返す
 * @param[in]
 *     a: 対象配列
 * @retval
 *     NULL: 失敗
 *     otherwise: a->size バイト分の空き番地
 * @detail
 *     この関数でデータをプッシュできるわけではない
 *     この関数で取得した空き番地に手動でデータを代入する必要がある
 */
void *
ngx_array_push(ngx_array_t *a)
{
    void        *elt, *new;
    size_t       size;
    ngx_pool_t  *p;

    // すでに a->nalloc で指定した個数がすべて使われた場合
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
            // プールの一番、後ろに位置しているケース

            p->d.last += a->size;
            a->nalloc++;

        } else {
            /* allocate a new array */
            // 本来の倍のサイズの配列を再度、プールから割り当てなおす

            new = ngx_palloc(p, 2 * size);
            if (new == NULL) {
                return NULL;
            }

            // 前の配列の要素をコピーして移す
            ngx_memcpy(new, a->elts, size);
            // パラメータを更新する
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
 *     引数で渡した配列の次の空き番地を返す
 * @param[in]
 *     a: 対象配列
 * @retval
 *     a->size バイト分の空き番地
 * @detail
 *     この関数でデータをプッシュできるわけではない
 *     この関数で取得した空き番地に手動でデータを代入する必要がある
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
