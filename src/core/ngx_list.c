
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @brief
 *     第一引数プールに n * size バイトまでデータを格納できるリストを生成する。
 *     呼び出し直後、初期化されてすぐに使えるようになる。
 * @param[in]
 *     pool: このプールからリストを割り当てる
 *     n   : リストに格納するデータの数
 *     size: リストに格納する各データのバイト数
 * @retval
 *     生成されたリストへのポインタ
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
 *     第一引数リストに要素をプッシュする
 * @param[in]
 *     pool: このリストに要素をプッシュする
 * @retval
 *     断片リストのお尻アドレス
 */
void *
ngx_list_push(ngx_list_t *l)
{
    // elt: 要素のこと。element の略？
    void             *elt;
    ngx_list_part_t  *last;

    last = l->last;

    // 最後のリストパートが既に一杯なら
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
