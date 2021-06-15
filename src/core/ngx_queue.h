
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
 *     �L���[������������
 * @param[out:in]
 *     q: ����������L���[
 */
#define ngx_queue_init(q)                                                     \
    (q)->prev = q;                                                            \
    (q)->next = q


/**
 * @brief
 *     �L���[���󂩂ǂ�����Ԃ�
 * @param[in]
 *     h: ���؂���L���[
 * @retval
 *     �L���[���󂩂ǂ���
 */
#define ngx_queue_empty(h)                                                    \
    (h == (h)->prev)


/**
 * @brief
 *     �L���[�̐擪�ɗv�f��ǉ�����i�G���L���[�j
 * @param[out]
 *     h: �L���[
 * @param[in]
 *     x: �v�f
 */
#define ngx_queue_insert_head(h, x)                                           \
    (x)->next = (h)->next;                                                    \
    (x)->next->prev = x;                                                      \
    (x)->prev = h;                                                            \
    (h)->next = x


/**
 * @brief
 *     �L���[�̐擪�ɗv�f��ǉ�����i�G���L���[�j
 * @param[out]
 *     h: �L���[
 * @param[in]
 *     x: �v�f
 */
#define ngx_queue_insert_after   ngx_queue_insert_head


/**
 * @brief
 *     �L���[�̍Ō�ɗv�f��ǉ�����
 * @param[out]
 *     h: �L���[
 * @param[in]
 *     x: �v�f
 */
#define ngx_queue_insert_tail(h, x)                                           \
    (x)->prev = (h)->prev;                                                    \
    (x)->prev->next = x;                                                      \
    (x)->next = h;                                                            \
    (h)->prev = x


/**
 * @brief
 *     �L���[�̐擪�̗v�f��Ԃ�
 * @param[in]
 *     h: �L���[
 * @retval
 *     �v�f
 */
#define ngx_queue_head(h)                                                     \
    (h)->next


/**
 * @brief
 *     �L���[�̍Ō�̗v�f��Ԃ�
 * @param[in]
 *     h: �L���[
 * @retval
 *     �v�f
 */
#define ngx_queue_last(h)                                                     \
    (h)->prev


/**
 * @brief
 *     �L���[�̃w�b�h��Ԃ�
 * @param[in]
 *     h: �L���[
 * @retval
 *     �w�b�h
 */
#define ngx_queue_sentinel(h)                                                 \
    (h)


/**
 * @brief
 *     �v�f�̎��̗v�f��Ԃ�
 * @param[in]
 *     h: �v�f
 * @retval
 *     ���̗v�f
 */
#define ngx_queue_next(q)                                                     \
    (q)->next


/**
 * @brief
 *     �v�f�̑O�̗v�f��Ԃ�
 * @param[in]
 *     h: �v�f
 * @retval
 *     �O�̗v�f
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
 *     �L���[����v�f����菜��
 * @param[in]
 *     x: ��菜�������v�f
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
 *     �L���[�̗v�f����f�[�^�����o��
 * @param[in]
 *     q: ��菜�������v�f
 *     type: ���o�������f�[�^�̌^
 *     link: ���o�������f�[�^�̃����o
 * @retval
 *     ���o�����f�[�^
 */
#define ngx_queue_data(q, type, link)                                         \
    (type *) ((u_char *) q - offsetof(type, link))


ngx_queue_t *ngx_queue_middle(ngx_queue_t *queue);
void ngx_queue_sort(ngx_queue_t *queue,
    ngx_int_t (*cmp)(const ngx_queue_t *, const ngx_queue_t *));


#endif /* _NGX_QUEUE_H_INCLUDED_ */
