
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HASH_H_INCLUDED_
#define _NGX_HASH_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * データ値とハッシュ値がまとまったもの
 * つまり、ハッシュそのもの
 */
typedef struct {
    // 値
    void             *value;
    // 値のサイズ
    u_short           len;
    // ハッシュ？
    u_char            name[1];
} ngx_hash_elt_t;


/**
 * ハッシュテーブル
 */
typedef struct {
    // 各ハッシュ値の数だけある配列のようなもの（ここに要素を入れる）
    ngx_hash_elt_t  **buckets;
    // ハッシュをいくつ所持しているか
    ngx_uint_t        size;
} ngx_hash_t;


typedef struct {
    // ハッシュをまとめたもの
    ngx_hash_t        hash;
    void             *value;
} ngx_hash_wildcard_t;


typedef struct {
    // キー
    ngx_str_t         key;
    // ハッシュ関数（キー）
    ngx_uint_t        key_hash;
    // 値
    void             *value;
} ngx_hash_key_t;


typedef ngx_uint_t (*ngx_hash_key_pt) (u_char *data, size_t len);


typedef struct {
    ngx_hash_t            hash;
    ngx_hash_wildcard_t  *wc_head;
    ngx_hash_wildcard_t  *wc_tail;
} ngx_hash_combined_t;


typedef struct {
    ngx_hash_t       *hash;
    ngx_hash_key_pt   key;

    // 
    ngx_uint_t        max_size;
    // ハッシュテーブルのインデックスのサイズ
    ngx_uint_t        bucket_size;

    char             *name;
    ngx_pool_t       *pool;
    ngx_pool_t       *temp_pool;
} ngx_hash_init_t;


#define NGX_HASH_SMALL            1
#define NGX_HASH_LARGE            2

#define NGX_HASH_LARGE_ASIZE      16384
#define NGX_HASH_LARGE_HSIZE      10007

#define NGX_HASH_WILDCARD_KEY     1
#define NGX_HASH_READONLY_KEY     2


/**
 * @brief
 *     これまでに使用されたキーを格納するためのもの
 *     つまり衝突判定のためだけに使用する
 */
typedef struct {
    // keys_hash, dns_wc_head_hash, dns_wc_tail_hash は hsize 個分の配列を格納する
    // つまりハッシュテーブルの大きさ
    ngx_uint_t        hsize;

    ngx_pool_t       *pool;
    ngx_pool_t       *temp_pool;

    // キー、ハッシュ関数（キー）、データをひとまとまりにしたものが順次、ここにプッシュされていく
    ngx_array_t       keys;
    // ハッシュテーブル、keys_hash[ハッシュ関数（キー）] でキー群が取得できる（衝突回避目的に使用）
    ngx_array_t      *keys_hash;

    ngx_array_t       dns_wc_head;
    ngx_array_t      *dns_wc_head_hash;

    ngx_array_t       dns_wc_tail;
    ngx_array_t      *dns_wc_tail_hash;
} ngx_hash_keys_arrays_t;


typedef struct {
    ngx_uint_t        hash;
    ngx_str_t         key;
    ngx_str_t         value;
    u_char           *lowcase_key;
} ngx_table_elt_t;


void *ngx_hash_find(ngx_hash_t *hash, ngx_uint_t key, u_char *name, size_t len);
void *ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_wc_tail(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_combined(ngx_hash_combined_t *hash, ngx_uint_t key,
    u_char *name, size_t len);

ngx_int_t ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);
ngx_int_t ngx_hash_wildcard_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);

#define ngx_hash(key, c)   ((ngx_uint_t) key * 31 + c)
ngx_uint_t ngx_hash_key(u_char *data, size_t len);
ngx_uint_t ngx_hash_key_lc(u_char *data, size_t len);
ngx_uint_t ngx_hash_strlow(u_char *dst, u_char *src, size_t n);


ngx_int_t ngx_hash_keys_array_init(ngx_hash_keys_arrays_t *ha, ngx_uint_t type);
ngx_int_t ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key,
    void *value, ngx_uint_t flags);


#endif /* _NGX_HASH_H_INCLUDED_ */
