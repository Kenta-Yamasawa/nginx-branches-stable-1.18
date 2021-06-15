
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
// �v�[���Ɋ��蓖�Ă邱�Ƃ��ł���f�[�^�̃}�b�N�X�l�i�v�[�����̂��̂̊Ǘ��\���̂̃T�C�Y�͏����j
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)

#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

// �v�[���̃T�C�Y�͂��̃A���C�������g�l�ɂ���ăA���C�������g�����
#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s {
    ngx_pool_cleanup_pt   handler;
    void                 *data;
    // �N���[���A�b�v�͑��̃N���[���A�b�v���w�����Ƃ��ł���
    ngx_pool_cleanup_t   *next;
};


typedef struct ngx_pool_large_s  ngx_pool_large_t;

// �v�[���𒴉߂����T�C�Y�v���ɂ��āA�ʂɊ��蓖�Ă��u���b�N
struct ngx_pool_large_s {
    // ���̃��[�W�u���b�N
    ngx_pool_large_t     *next;
    // ���[�W�u���b�N�̐擪�|�C���^
    void                 *alloc;
};


typedef struct {
    // ��ԁA���̋󂫔Ԓn
    u_char               *last;
    // ���̃v�[���̃f�[�^�̈�̍Ō�{�P
    u_char               *end;
    // ���̃v�[����
    ngx_pool_t           *next;
    ngx_uint_t            failed;
} ngx_pool_data_t;


// �v�[�����̂���
struct ngx_pool_s {
    // �f�[�^���̂��̂ւ̃|�C���^�Q�i�f�t�H���g���ƃy�[�W�T�C�Y���[�P�����ێ��j
    ngx_pool_data_t       d;
    // �c��g�p�\�f�[�^�o�C�g
    size_t                max;
    ngx_pool_t           *current;
    ngx_chain_t          *chain;
    ngx_pool_large_t     *large;
    ngx_pool_cleanup_t   *cleanup;
    // ���O�o�͂Ɏg�p���郍�O
    ngx_log_t            *log;
};


typedef struct {
    ngx_fd_t              fd;
    u_char               *name;
    ngx_log_t            *log;
} ngx_pool_cleanup_file_t;


ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);
void ngx_reset_pool(ngx_pool_t *pool);

void *ngx_palloc(ngx_pool_t *pool, size_t size);
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
void ngx_pool_cleanup_file(void *data);
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
