
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_REGEX_H_INCLUDED_
#define _NGX_REGEX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <pcre.h>

// pcre_exec() �̌��ʁA�}�b�`���Ȃ������Ƃ��ɕԂ�}�N��
#define NGX_REGEX_NO_MATCHED  PCRE_ERROR_NOMATCH   /* -1 */

// ���̃I�v�V�������w�肷��ƁA�p�^�[���̒��̕����͑啶���ɂ��������ɂ��}�b�`����
#define NGX_REGEX_CASELESS    PCRE_CASELESS


typedef struct {
    pcre        *code;
    pcre_extra  *extra;
} ngx_regex_t;


typedef struct {
    // ���K�\���p�^�[��
    ngx_str_t     pattern;
    // PCRE ���g�p����v�[��
    ngx_pool_t   *pool;
    // NGX_REGEX_CASELESS�iPCRE_CASELESS �̂ݑΉ����Ă�����ۂ��H���j
    ngx_int_t     options;

    // pattern �������� pcre_compile() �������������K�p�^�[���}�b�`���O�́i����ɖ��������j
    // ������������ pcre_exec() �����s����ă}�b�`���O���s����
    ngx_regex_t  *regex;
    // �}�b�`�����i�T�u�j�p�^�[���̐�
    int           captures;
    // �}�b�`�������O�t���i�T�u�j�p�^�[���̐�
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
