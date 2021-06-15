
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMEM_H_INCLUDED_
#define _NGX_SHMEM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


// ���L��������\���\����
typedef struct {
    // ���L�������ւ̃A�h���X�i�|�C���^�j
    u_char      *addr;
    // �T�C�Y
    size_t       size;
    // ���̋��L���������L�̖��O
    ngx_str_t    name;
    // �T�C�N������󂯌p�����O
    ngx_log_t   *log;
    // ���݂��Ă��邩�ǂ����H
    ngx_uint_t   exists;   /* unsigned  exists:1;  */
} ngx_shm_t;


ngx_int_t ngx_shm_alloc(ngx_shm_t *shm);
void ngx_shm_free(ngx_shm_t *shm);


#endif /* _NGX_SHMEM_H_INCLUDED_ */
