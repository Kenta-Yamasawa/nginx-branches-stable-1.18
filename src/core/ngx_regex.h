
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_REGEX_H_INCLUDED_
#define _NGX_REGEX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <pcre.h>

// pcre_exec() の結果、マッチしなかったときに返るマクロ
#define NGX_REGEX_NO_MATCHED  PCRE_ERROR_NOMATCH   /* -1 */

// このオプションを指定すると、パターンの中の文字は大文字にも小文字にもマッチする
#define NGX_REGEX_CASELESS    PCRE_CASELESS


typedef struct {
    pcre        *code;
    pcre_extra  *extra;
} ngx_regex_t;


typedef struct {
    // 正規表現パターン
    ngx_str_t     pattern;
    // PCRE が使用するプール
    ngx_pool_t   *pool;
    // NGX_REGEX_CASELESS（PCRE_CASELESS のみ対応しているっぽい？↑）
    ngx_int_t     options;

    // pattern を引数に pcre_compile() が生成した正規パターンマッチング体（勝手に命名した）
    // こいつを引数に pcre_exec() が実行されてマッチングが行われる
    ngx_regex_t  *regex;
    // マッチした（サブ）パターンの数
    int           captures;
    // マッチした名前付き（サブ）パターンの数
    int           named_captures;
    int           name_size;
    u_char       *names;
    ngx_str_t     err;
} ngx_regex_compile_t;


typedef struct {
    ngx_regex_t  *regex;
    u_char       *name;
} ngx_regex_elt_t;


void ngx_regex_init(void);
ngx_int_t ngx_regex_compile(ngx_regex_compile_t *rc);

#define ngx_regex_exec(re, s, captures, size)                                \
    pcre_exec(re->code, re->extra, (const char *) (s)->data, (s)->len, 0, 0, \
              captures, size)
#define ngx_regex_exec_n      "pcre_exec()"

ngx_int_t ngx_regex_exec_array(ngx_array_t *a, ngx_str_t *s, ngx_log_t *log);


#endif /* _NGX_REGEX_H_INCLUDED_ */
