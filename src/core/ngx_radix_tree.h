
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_RADIX_TREE_H_INCLUDED_
#define _NGX_RADIX_TREE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


#define NGX_RADIX_NO_VALUE   (uintptr_t) -1

typedef struct ngx_radix_node_s  ngx_radix_node_t;

struct ngx_radix_node_s {
    // �p�g���V�A�؂ɂ�����E���̎q�m�[�h
    ngx_radix_node_t  *right;
    // �p�g���V�A�؂ɂ����鍶���̎q�m�[�h
    ngx_radix_node_t  *left;
    // �e�m�[�h
    ngx_radix_node_t  *parent;
    // ���̃m�[�h�̒l
    uintptr_t          value;
};


typedef struct {
    // �p�g���V�A�؂̍�
    ngx_radix_node_t  *root;
    // ���̃p�g���V�A�؂Ƃ��̃m�[�h�̐����Ɏg�p����v�[��
    ngx_pool_t        *pool;
    // �J���n���h��
    ngx_radix_node_t  *free;
    // 
    char              *start;
    // 
    size_t             size;
} ngx_radix_tree_t;


ngx_radix_tree_t *ngx_radix_tree_create(ngx_pool_t *pool,
    ngx_int_t preallocate);

ngx_int_t ngx_radix32tree_insert(ngx_radix_tree_t *tree,
    uint32_t key, uint32_t mask, uintptr_t value);
ngx_int_t ngx_radix32tree_delete(ngx_radix_tree_t *tree,
    uint32_t key, uint32_t mask);
uintptr_t ngx_radix32tree_find(ngx_radix_tree_t *tree, uint32_t key);

#if (NGX_HAVE_INET6)
ngx_int_t ngx_radix128tree_insert(ngx_radix_tree_t *tree,
    u_char *key, u_char *mask, uintptr_t value);
ngx_int_t ngx_radix128tree_delete(ngx_radix_tree_t *tree,
    u_char *key, u_char *mask);
uintptr_t ngx_radix128tree_find(ngx_radix_tree_t *tree, u_char *key);
#endif


#endif /* _NGX_RADIX_TREE_H_INCLUDED_ */
