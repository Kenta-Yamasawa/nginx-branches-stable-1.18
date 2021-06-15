
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_LIST_H_INCLUDED_
#define _NGX_LIST_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_list_part_s  ngx_list_part_t;

// part
struct ngx_list_part_s {
    // このリストがもつ要素のうち、先頭の要素へのポインタ
    void             *elts;
    // このリストがもつ要素数
    ngx_uint_t        nelts;
    // 次の part
    ngx_list_part_t  *next;
};

// リスト
typedef struct {
    // 最後に生成した part
    ngx_list_part_t  *last;
    // リストの一部、リストそのものは自身の断片を大量にもち、その断片が実データを持つ↑
    ngx_list_part_t   part;
    // 各要素のバイト数
    size_t            size;
    // 各 part に要素をいくつまで格納できるか
    ngx_uint_t        nalloc;
    // このリストが割り当てられているプールへのポインタ
    ngx_pool_t       *pool;
} ngx_list_t;


ngx_list_t *ngx_list_create(ngx_pool_t *pool, ngx_uint_t n, size_t size);

static ngx_inline ngx_int_t
ngx_list_init(ngx_list_t *list, ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    list->part.elts = ngx_palloc(pool, n * size);
    if (list->part.elts == NULL) {
        return NGX_ERROR;
    }

    list->part.nelts = 0;
    list->part.next = NULL;
    list->last = &list->part;
    list->size = size;
    list->nalloc = n;
    list->pool = pool;

    return NGX_OK;
}


/*
 *
 *  the iteration through the list:
 *
 *  part = &list.part;
 *  data = part->elts;
 *
 *  for (i = 0 ;; i++) {
 *
 *      if (i >= part->nelts) {
 *          if (part->next == NULL) {
 *              break;
 *          }
 *
 *          part = part->next;
 *          data = part->elts;
 *          i = 0;
 *      }
 *
 *      ...  data[i] ...
 *
 *  }
 */


void *ngx_list_push(ngx_list_t *list);


#endif /* _NGX_LIST_H_INCLUDED_ */
