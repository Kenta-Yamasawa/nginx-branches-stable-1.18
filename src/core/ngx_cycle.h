
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CYCLE_H_INCLUDED_
#define _NGX_CYCLE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef NGX_CYCLE_POOL_SIZE
#define NGX_CYCLE_POOL_SIZE     NGX_DEFAULT_POOL_SIZE
#endif


#define NGX_DEBUG_POINTS_STOP   1
#define NGX_DEBUG_POINTS_ABORT  2


typedef struct ngx_shm_zone_s  ngx_shm_zone_t;

typedef ngx_int_t (*ngx_shm_zone_init_pt) (ngx_shm_zone_t *zone, void *data);

struct ngx_shm_zone_s {
    void                     *data;
    ngx_shm_t                 shm;
    ngx_shm_zone_init_pt      init;
    void                     *tag;
    void                     *sync;
    ngx_uint_t                noreuse;  /* unsigned  noreuse:1; */
};


struct ngx_cycle_s {
    // ngx_init_cycle() �ŏ�����
    // �e���W���[���ŗL�̐ݒ��ێ�����\���̂��i�[����
    void                  ****conf_ctx;
    // ngx_init_cycle() �ŏ�����
    // ���̃T�C�N���͂��̃v�[�����g���āA��{�I�Ƀ��������蓖�Ă��s��
    ngx_pool_t               *pool;

    // ngx_init_cycle() �ŏ�����
    // ���O�i�󂯌p�����T�C�N������󂯌p���j
    ngx_log_t                *log;
    ngx_log_t                 new_log;

    ngx_uint_t                log_use_stderr;  /* unsigned  log_use_stderr:1; */

    ngx_connection_t        **files;
    ngx_connection_t         *free_connections;
    ngx_uint_t                free_connection_n;

    // ngx_init_cycle() �ŏ�����
    // ���̃T�C�N�����g�p����S���W���[��
    ngx_module_t            **modules;
    // ngx_init_cycle() �ŏ�����
    // ���̃T�C�N�����g�p����S���W���[���̍��v��
    ngx_uint_t                modules_n;
    ngx_uint_t                modules_used;    /* unsigned  modules_used:1; */

    // ngx_init_cycle() �ŏ�����
    ngx_queue_t               reusable_connections_queue;
    ngx_uint_t                reusable_connections_n;

    // ngx_init_cycle() �ŏ�����
    ngx_array_t               listening;
    // ngx_init_cycle() �ŏ�����
    // 
    ngx_array_t               paths;

    // ngx_init_cycle() �ŏ�����
    // �R���t�B�O����z��`���ł܂Ƃ߂����́i�R���t�B�O�_���v�j
    ngx_array_t               config_dump;
    // ngx_init_cycle() �ŏ�����
    // �R���t�B�O�_���v��ۑ����邽�߂̐ԍ���
    ngx_rbtree_t              config_dump_rbtree;
    // ngx_init_cycle() �ŏ�����
    // �R���t�B�O�_���v��ۑ����邽�߂̐ԍ��؂Ŏg�p����t�m�[�h
    ngx_rbtree_node_t         config_dump_sentinel;

    // ngx_init_cycle() �ŏ�����
    ngx_list_t                open_files;
    // ngx_init_cycle() �ŏ�����
    ngx_list_t                shared_memory;

    ngx_uint_t                connection_n;
    ngx_uint_t                files_n;

    ngx_connection_t         *connections;
    ngx_event_t              *read_events;
    ngx_event_t              *write_events;

    // ngx_init_cycle() �ŏ�����
    // �󂯌p�����T�C�N��
    // ���Ƃ��ŏ��̃T�C�N���ł����Ă�����
    // �ŏ��̃T�C�N���ł���ꍇ�� init_cycle �ƌĂ΂��T�C�N���� main() ���Ő�������ēn����Ă���
    ngx_cycle_t              *old_cycle;

    // ngx_process_options() �� init_cycle �̂����������
    // ngx_init_cycle() ���� init_cycle(old_cycle) ����󂯌p��
    // �ݒ�t�@�C���ւ̃t���p�X
    ngx_str_t                 conf_file;
    // ngx_process_options() �� init_cycle �̂����������
    // ngx_init_cycle() ���� init_cycle(old_cycle) ����󂯌p��
    // �p�����[�^
    ngx_str_t                 conf_param;
    // ngx_process_options() �� init_cycle �̂����������
    // ngx_init_cycle() ���� init_cycle(old_cycle) ����󂯌p��
    // �ݒ�t�@�C���ւ̃p�X�i�t�@�C�����������j
    ngx_str_t                 conf_prefix;
    // ngx_process_options() �� init_cycle �̂����������
    // ngx_init_cycle() ���� init_cycle(old_cycle) ����󂯌p��
    // �z�[���p�X
    ngx_str_t                 prefix;


    ngx_str_t                 lock_file;
    // ngx_init_cycle() �ŏ�����
    // �{�v���Z�X�������Ă���v���Z�b�T�̃z�X�g��
    ngx_str_t                 hostname;
};


// �R�A���W���[�������̐ݒ�t�@�C��
typedef struct {
    // nginx ���f�[�����Ƃ��ċN�����邩�ǂ����A��ɊJ���Ҍ���
    ngx_flag_t                daemon;
    // ���[�J�v���Z�X���N�������邩�ǂ����A��ɊJ���Ҍ���
    ngx_flag_t                master;

    // ���[�J�v���Z�X�̎��Ԑ��x�𗎂Ƃ�����Ɏ��s���ׂ�������
    ngx_msec_t                timer_resolution;
    // 
    ngx_msec_t                shutdown_timeout;

    // ���[�J�v���Z�X�̐����w�肷��
    ngx_int_t                 worker_processes;
    ngx_int_t                 debug_points;

    ngx_int_t                 rlimit_nofile;
    off_t                     rlimit_core;

    int                       priority;

    ngx_uint_t                cpu_affinity_auto;
    ngx_uint_t                cpu_affinity_n;
    ngx_cpuset_t             *cpu_affinity;

    char                     *username;
    ngx_uid_t                 user;
    ngx_gid_t                 group;

    ngx_str_t                 working_directory;
    // �������[�J�v���Z�X�� accept() ���݂��Ƀ��b�N���čs���̂�
    // ���b�N���t�@�C����p���čs���V�X�e���ɂ����Ă����Ńp�X���w�肷��
    ngx_str_t                 lock_file;

    // �Ղ낹��ID ���L�^����t�@�C���̃p�X
    ngx_str_t                 pid;
    ngx_str_t                 oldpid;

    ngx_array_t               env;
    char                    **environment;

    ngx_uint_t                transparent;  /* unsigned  transparent:1; */
} ngx_core_conf_t;


#define ngx_is_init_cycle(cycle)  (cycle->conf_ctx == NULL)


ngx_cycle_t *ngx_init_cycle(ngx_cycle_t *old_cycle);
ngx_int_t ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log);
void ngx_delete_pidfile(ngx_cycle_t *cycle);
ngx_int_t ngx_signal_process(ngx_cycle_t *cycle, char *sig);
void ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user);
char **ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last);
ngx_pid_t ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv);
ngx_cpuset_t *ngx_get_cpu_affinity(ngx_uint_t n);
ngx_shm_zone_t *ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name,
    size_t size, void *tag);
void ngx_set_shutdown_timer(ngx_cycle_t *cycle);


extern volatile ngx_cycle_t  *ngx_cycle;
extern ngx_array_t            ngx_old_cycles;
extern ngx_module_t           ngx_core_module;
extern ngx_uint_t             ngx_test_config;
extern ngx_uint_t             ngx_dump_config;
extern ngx_uint_t             ngx_quiet_mode;


#endif /* _NGX_CYCLE_H_INCLUDED_ */
