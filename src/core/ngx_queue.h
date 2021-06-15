
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef _NGX_QUEUE_H_INCLUDED_
#define _NGX_QUEUE_H_INCLUDED_


typedef struct ngx_queue_s  ngx_queue_t;

struct ngx_queue_s {
    ngx_queue_t  *prev;
    ngx_queue_t  *next;
};


/**
 * @brief
 *     キューを初期化する
 * @param[out:in]
 *     q: 初期化するキュー
 */
#define ngx_queue_init(q)                                                     \
    (q)->prev = q;                                                            \
    (q)->next = q


/**
 * @brief
 *     キューが空かどうかを返す
 * @param[in]
 *     h: 検証するキュー
 * @retval
 *     キューが空かどうか
 */
#define ngx_queue_empty(h)                                                    \
    (h == (h)->prev)


/**
 * @brief
 *     キューの先頭に要素を追加する（エンキュー）
 * @param[out]
 *     h: キュー
 * @param[in]
 *     x: 要素
 */
#define ngx_queue_insert_head(h, x)                                           \
    (x)->next = (h)->next;                                                    \
    (x)->next->prev = x;                                                      \
    (x)->prev = h;                                                            \
    (h)->next = x


/**
 * @brief
 *     キューの先頭に要素を追加する（エンキュー）
 * @param[out]
 *     h: キュー
 * @param[in]
 *     x: 要素
 */
#define ngx_queue_insert_after   ngx_queue_insert_head


/**
 * @brief
 *     キューの最後に要素を追加する
 * @param[out]
 *     h: キュー
 * @param[in]
 *     x: 要素
 */
#define ngx_queue_insert_tail(h, x)                                           \
    (x)->prev = (h)->prev;                                                    \
    (x)->prev->next = x;                                                      \
    (x)->next = h;                                                            \
    (h)->prev = x


/**
 * @brief
 *     キューの先頭の要素を返す
 * @param[in]
 *     h: キュー
 * @retval
 *     要素
 */
#define ngx_queue_head(h)                                                     \
    (h)->next


/**
 * @brief
 *     キューの最後の要素を返す
 * @param[in]
 *     h: キュー
 * @retval
 *     要素
 */
#define ngx_queue_last(h)                                                     \
    (h)->prev


/**
 * @brief
 *     キューのヘッドを返す
 * @param[in]
 *     h: キュー
 * @retval
 *     ヘッド
 */
#define ngx_queue_sentinel(h)                                                 \
    (h)


/**
 * @brief
 *     要素の次の要素を返す
 * @param[in]
 *     h: 要素
 * @retval
 *     次の要素
 */
#define ngx_queue_next(q)                                                     \
    (q)->next


/**
 * @brief
 *     要素の前の要素を返す
 * @param[in]
 *     h: 要素
 * @retval
 *     前の要素
 */
#define ngx_queue_prev(q)                                                     \
    (q)->prev


#if (NGX_DEBUG)

#define ngx_queue_remove(x)                                                   \
    (x)->next->prev = (x)->prev;                                              \
    (x)->prev->next = (x)->next;                                              \
    (x)->prev = NULL;                                                         \
    (x)->next = NULL

#else

/**
 * @brief
 *     キューから要素を取り除く
 * @param[in]
 *     x: 取り除きたい要素
 */
#define ngx_queue_remove(x)                                                   \
    (x)->next->prev = (x)->prev;                                              \
    (x)->prev->next = (x)->next

#endif


#define ngx_queue_split(h, q, n)                                              \
    (n)->prev = (h)->prev;                                                    \
    (n)->prev->next = n;                                                      \
    (n)->next = q;                                                            \
    (h)->prev = (q)->prev;                                                    \
    (h)->prev->next = h;                                                      \
    (q)->prev = n;


#define ngx_queue_add(h, n)                                                   \
    (h)->prev->next = (n)->next;                                              \
    (n)->next->prev = (h)->prev;                                              \
    (h)->prev = (n)->prev;                                                    \
    (h)->prev->next = h;


/**
 * @brief
 *     キューの要素からデータを取り出す
 * @param[in]
 *     q: 取り除きたい要素
 *     type: 取り出したいデータの型
 *     link: 取り出したいデータのメンバ
 * @retval
 *     取り出したデータ
 */
#define ngx_queue_data(q, type, link)                                         \
    (type *) ((u_char *) q - offsetof(type, link))


ngx_queue_t *ngx_queue_middle(ngx_queue_t *queue);
void ngx_queue_sort(ngx_queue_t *queue,
    ngx_int_t (*cmp)(const ngx_queue_t *, const ngx_queue_t *));


#endif /* _NGX_QUEUE_H_INCLUDED_ */
