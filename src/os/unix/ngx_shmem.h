
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMEM_H_INCLUDED_
#define _NGX_SHMEM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


// 共有メモリを表す構造体
typedef struct {
    // 共有メモリへのアドレス（ポインタ）
    u_char      *addr;
    // サイズ
    size_t       size;
    // この共有メモリ特有の名前
    ngx_str_t    name;
    // サイクルから受け継ぐログ
    ngx_log_t   *log;
    // 存在しているかどうか？
    ngx_uint_t   exists;   /* unsigned  exists:1;  */
} ngx_shm_t;


ngx_int_t ngx_shm_alloc(ngx_shm_t *shm);
void ngx_shm_free(ngx_shm_t *shm);


#endif /* _NGX_SHMEM_H_INCLUDED_ */
