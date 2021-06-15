
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_RBTREE_H_INCLUDED_
#define _NGX_RBTREE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef ngx_uint_t  ngx_rbtree_key_t;
typedef ngx_int_t   ngx_rbtree_key_int_t;


typedef struct ngx_rbtree_node_s  ngx_rbtree_node_t;

/**
 * @brief
 *     赤黒木のノード
 */
struct ngx_rbtree_node_s {
    ngx_rbtree_key_t       key;
    ngx_rbtree_node_t     *left;
    ngx_rbtree_node_t     *right;
    ngx_rbtree_node_t     *parent;
    u_char                 color;
    u_char                 data;
};


typedef struct ngx_rbtree_s  ngx_rbtree_t;

typedef void (*ngx_rbtree_insert_pt) (ngx_rbtree_node_t *root,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);

/**
 * @brief
 *     赤黒木そのもの
 */
struct ngx_rbtree_s {
    // 根
    ngx_rbtree_node_t     *root;
    // 葉
    ngx_rbtree_node_t     *sentinel;
    ngx_rbtree_insert_pt   insert;
};

/**
 * @brief 第二引数ノードを黒色にして、第一引数赤黒木のルート、監視員にする。
 *        そして、第三引数で指定した関数をこの赤黒木のノード挿入用関数として登録する。
 *        必ず成功する。
 * @param[in]
 *     tree: 初期化対象赤黒木
 *     s   : ルート、監視員として使用するノード
 *     i   : ノード挿入用関数
 */
#define ngx_rbtree_init(tree, s, i)                                           \
    ngx_rbtree_sentinel_init(s);                                              \
    (tree)->root = s;                                                         \
    (tree)->sentinel = s;                                                     \
    (tree)->insert = i


void ngx_rbtree_insert(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);
void ngx_rbtree_delete(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);
void ngx_rbtree_insert_value(ngx_rbtree_node_t *root, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel);
void ngx_rbtree_insert_timer_value(ngx_rbtree_node_t *root,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);
ngx_rbtree_node_t *ngx_rbtree_next(ngx_rbtree_t *tree,
    ngx_rbtree_node_t *node);

/**
 * @brief ノードを赤色にする。必ず成功する。
 * @param[in]
 *     node: 対象ノード
 */
#define ngx_rbt_red(node)               ((node)->color = 1)
/**
 * @brief ノードを黒色にする。必ず成功する。
 * @param[in]
 *     node: 対象ノード
 */
#define ngx_rbt_black(node)             ((node)->color = 0)
/**
 * @brief ノードが赤色なら 1 を返す
 * @param[in]
 *     node: 対象ノード
 * @retval
 *     赤色である：　1
 *     黒色である：  0
 */
#define ngx_rbt_is_red(node)            ((node)->color)
/**
 * @brief ノードが黒色なら 1 を返す
 * @param[in]
 *     node: 対象ノード
 * @retval
 *     赤色である：　0
 *     黒色である：  1
 */
#define ngx_rbt_is_black(node)          (!ngx_rbt_is_red(node))
/**
 * @brief 第二引数ノードの色を第一引数ノードへコピーする。必ず成功する。
 * @param[in]
 *     n2: コピー元ノード
 * @param[out]
 *     n1: コピー先ノード
 */
#define ngx_rbt_copy_color(n1, n2)      (n1->color = n2->color)


/* a sentinel must be black */

/**
 * @brief ノードを黒色にする。必ず成功する。
 * @param[in]
 *     node: 黒色にしたいノード
 */
#define ngx_rbtree_sentinel_init(node)  ngx_rbt_black(node)

/**
 * @brief このノード下における、最小のキーを返す
 * @param[in]
 *     node: 調査対象のノード
 *     sentinel: そのノードの最下層にいる葉ノード
 */
static ngx_inline ngx_rbtree_node_t *
ngx_rbtree_min(ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    while (node->left != sentinel) {
        node = node->left;
    }

    return node;
}


#endif /* _NGX_RBTREE_H_INCLUDED_ */
