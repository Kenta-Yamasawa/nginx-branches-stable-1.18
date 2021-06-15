
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>


static void ngx_show_version_info(void);
static ngx_int_t ngx_add_inherited_sockets(ngx_cycle_t *cycle);
static void ngx_cleanup_environment(void *data);
static ngx_int_t ngx_get_options(int argc, char *const *argv);
static ngx_int_t ngx_process_options(ngx_cycle_t *cycle);
static ngx_int_t ngx_save_argv(ngx_cycle_t *cycle, int argc, char *const *argv);
static void *ngx_core_module_create_conf(ngx_cycle_t *cycle);
static char *ngx_core_module_init_conf(ngx_cycle_t *cycle, void *conf);
static char *ngx_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_set_env(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_set_priority(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_set_cpu_affinity(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_set_worker_processes(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_load_module(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
#if (NGX_HAVE_DLOPEN)
static void ngx_unload_module(void *data);
#endif


static ngx_conf_enum_t  ngx_debug_points[] = {
    { ngx_string("stop"), NGX_DEBUG_POINTS_STOP },
    { ngx_string("abort"), NGX_DEBUG_POINTS_ABORT },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_core_commands[] = {

    // �֌W�Ȃ�
    // nginx ���f�[�����Ƃ��ċN�����邩�ǂ����A��ɊJ���Ҍ���
    { ngx_string("daemon"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_core_conf_t, daemon),
      NULL },

    // �֌W�Ȃ��i��ɃV���O���Ȃ̂Łj
    // ���[�J�v���Z�X���N�������邩�ǂ����A��ɊJ���Ҍ���
    { ngx_string("master_process"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_core_conf_t, master),
      NULL },

    // �Z
    // ���[�J�v���Z�X�̎��Ԑ��x�𗎂Ƃ�����Ɏ��s���ׂ�������
    { ngx_string("timer_resolution"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_core_conf_t, timer_resolution),
      NULL },

    // �֌W�Ȃ��i�t�@�C���͎g��Ȃ��j
    // �Ղ낹��ID ���L�^����t�@�C���̃p�X
    { ngx_string("pid"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, pid),
      NULL },

    // �֌W�Ȃ�
    // �������[�J�v���Z�X�� accept() ���݂��Ƀ��b�N���čs���̂�
    // ���b�N���t�@�C����p���čs���V�X�e���ɂ����Ă����Ńp�X���w�肷��
    { ngx_string("lock_file"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, lock_file),
      NULL },

    // �֌W�Ȃ�
    // ���[�J�v���Z�X�̐����w�肷��
    { ngx_string("worker_processes"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_worker_processes,
      0,
      0,
      NULL },

    // �֌W�Ȃ�
    // �f�o�b�O�Ɏg�p����
    { ngx_string("debug_points"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      0,
      offsetof(ngx_core_conf_t, debug_points),
      &ngx_debug_points },

    // �H
    // ���̃v���Z�X�̃��[�U�����w�肷��
    { ngx_string("user"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE12,
      ngx_set_user,
      0,
      0,
      NULL },

    // �Z
    // nice �R�}���h�̂悤�Ƀ��[�J�v���Z�X�̎��s�D��x���w�肷��
    { ngx_string("worker_priority"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_priority,
      0,
      0,
      NULL },

    // �Z
    // �ǂ�CPU�Ń��[�J�v���Z�X�����s���邩�w�肷��
    { ngx_string("worker_cpu_affinity"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_1MORE,
      ngx_set_cpu_affinity,
      0,
      0,
      NULL },

    { ngx_string("worker_rlimit_nofile"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_core_conf_t, rlimit_nofile),
      NULL },

    { ngx_string("worker_rlimit_core"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_off_slot,
      0,
      offsetof(ngx_core_conf_t, rlimit_core),
      NULL },

    { ngx_string("worker_shutdown_timeout"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_core_conf_t, shutdown_timeout),
      NULL },

    { ngx_string("working_directory"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, working_directory),
      NULL },

    { ngx_string("env"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_env,
      0,
      0,
      NULL },

    { ngx_string("load_module"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_load_module,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_core_module_ctx = {
    ngx_string("core"),
    ngx_core_module_create_conf,
    ngx_core_module_init_conf
};


ngx_module_t  ngx_core_module = {
    NGX_MODULE_V1,
    &ngx_core_module_ctx,                  /* module context */
    ngx_core_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_uint_t   ngx_show_help;
static ngx_uint_t   ngx_show_version;
static ngx_uint_t   ngx_show_configure;
static u_char      *ngx_prefix;
static u_char      *ngx_conf_file;
static u_char      *ngx_conf_params;
static char        *ngx_signal;


static char **ngx_os_environ;


int ngx_cdecl
main(int argc, char *const *argv)
{
    ngx_buf_t        *b;
    ngx_log_t        *log;
    ngx_uint_t        i;
    ngx_cycle_t      *cycle, init_cycle;
    ngx_conf_dump_t  *cd;
    ngx_core_conf_t  *ccf;

    // malloc �̓�����f�o�b�O�p�̂��̂ɐݒ肷��K�v�����邩�ǂ������`�F�b�N
    // FreeBSD �� Darwin ���Ƃ��̂悤�ȓ��삪����A���͋�̃}�N���֐��Ƃ��Ē�`����Ă���
    // �O���ǂݍ��݂���
    //     ���ϐ�
    // �V�X�e���ˑ�����
    //     getenv()
    // �}�N��
    //     NGX_DEBUG_MALLOC
    //         �f�o�b�O�p����ɂ���Ȃ� Yes
    // @post
    //     ngx_debug_malloc �� 0 �� 1�i�L���j���i�[�����
    ngx_debug_init();

    /**
     * @brief
     *     OS ����G���[�����擾���ĐÓI�ϐ��փZ�b�g����
     * @post
     *     ngx_strerror() ���g�p�\�ɂȂ�
     */
    // �O���ǂݍ��݂Ȃ�
    // �V�X�e���ˑ�����
    //     strerror()
    //     strlen()
    //     memcpy()
    //     malloc()
    // �}�N��
    //     NGX_SYS_NERR
    //         nginx ���g�p����G���[�̎��
    //     NGX_MEMCPY_LIMIT
    //         memcpy() �ł���T�C�Y�𐧌�����
    if (ngx_strerror_init() != NGX_OK) {
        return 1;
    }

    /**
     * @brief
     *     �R�}���h���C����������͂��āA���e���󂯂ĐÓI�ϐ��֒l��������
     * @post
     *     ngx_show_version
     *     ngx_show_help
     *     ngx_test_config
     *     ngx_show_configure
     *     ngx_dump_config
     *     ngx_quiet_mode
     *     ngx_prefix
     *     ngx_conf_file
     *     ngx_conf_params
     *     ngx_signal
     *     ngx_process
     *     ���̐ÓI�ϐ��ɁA�R�}���h���C�������ɉ����Đ������l���Z�b�g�����
     */
    // �O���ǂݍ��݂Ȃ�
    // �V�X�e���ˑ�����
    //     strcmp()
    // �}�N���Ȃ�
    if (ngx_get_options(argc, argv) != NGX_OK) {
        return 1;
    }

    // ngx_show_version �́���ngx_get_options() �Őݒ肳���
    // �Ăяo���Ȃ��Ă��悢�̂Ŕ�΂��E�E�E
    if (ngx_show_version) {
        ngx_show_version_info();

        if (!ngx_test_config) {
            return 0;
        }
    }

    // ���̌�Angx_os_init() �ŏ����������
    /* TODO */ ngx_max_sockets = -1;

    /**
     * @brief
     *     5 ��ނ̕����񎞊ԃL���b�V���ƒʏ펞�ԃL���b�V��������������
     * @post
     *     ngx_cached_time �� �ŐV�̎��ԃL���b�V�����w���Ă���
     *
     *     ngx_cached_err_log_time     �����񎞊ԃL���b�V���� "1970/09/28 12:00:00"           �ŃT�C�Y�̂ݏ��������ꂽ
     *     ngx_cached_http_time        �����񎞊ԃL���b�V���� "Mon, 28 Sep 1970 06:00:00 GMT" �ŃT�C�Y�̂ݏ��������ꂽ
     *     ngx_cached_http_log_time    �����񎞊ԃL���b�V���� "28/Sep/1970:12:00:00 +0600"    �ŃT�C�Y�̂ݏ��������ꂽ
     *     ngx_cached_http_log_iso8601 �����񎞊ԃL���b�V���� "1970-09-28T12:00:00+06:00"     �ŃT�C�Y�̂ݏ��������ꂽ
     *     ngx_cached_syslog_time      �����񎞊ԃL���b�V���� "Sep 28 12:00:00"               �ŃT�C�Y�̂ݏ��������ꂽ
     * @syscall
     *     gettimeofday()
     *     va_start()
     *     va_arg()
     *     va_end()
     * @macro
     *     NGX_TIME_SLOT
     *     NGX_HAVE_GETTIMEZONE
     *     NGX_HAVE_GMTOFF
     */
    // �O���ǂݍ��݂Ȃ�
    ngx_time_init();

#if (NGX_PCRE)
    // OK
    ngx_regex_init();
#endif
    // �v���Z�X ID ���擾
    // �O���ǂݍ��݂Ȃ�
    // �V�X�e���ˑ��Ȃ�
    // �}�N���Ȃ�
    ngx_pid = ngx_getpid();
    // �e�v���Z�X ID ���擾
    // �O���ǂݍ��݂Ȃ�
    // �V�X�e���ˑ��Ȃ�
    // �}�N���Ȃ�
    ngx_parent = ngx_getppid();

    /**
     * @brief
     *     �G���g���[���O������������
     *       �t�@�C���f�B�X�N���v�^�F�r���h���̃}�N���Ŏw�肳�ꂽ�t�@�C��
     *                               �����Ȃ�W���o��(ngx_stderr)
     *       ���O���x���FNOTICE
     *     err_log �f�B���N�e�B�u�Ŏw�肳�ꂽ���O�͂��̃G���g���[���O���ۗL���郍�O���X�g�֒ǉ�����Ă���
     * @param[in]
     *     prefix: �����Ńp�X�̎Q�ƂɎg�p����
     * @macro
     *     NGX_ERROR_LOG_PATH: �G���[���O���o�͂���t�@�C���̃p�X
     *     NGX_PREFIX: �n���ꂽ�p�X�����΃p�X�̂Ƃ��Ɏg�p����
     *     ngx_string, os/unix/ngx_files �̕�
     */
    log = ngx_log_init(ngx_prefix);
    if (log == NULL) {
        return 1;
    }

    /* STUB */
#if (NGX_OPENSSL)
    ngx_ssl_init(log);
#endif

    /*
     * init_cycle->log is required for signal handlers and
     * ngx_process_options()
     */

    // �������p�̃T�C�N���𐶐��i�T�C�N���̏������́A�ʂ̃T�C�N�������Ƃɍs����̂Łj
    ngx_memzero(&init_cycle, sizeof(ngx_cycle_t));
    // �T�C�N���̃��O��������
    init_cycle.log = log;
    ngx_cycle = &init_cycle;

    // �T�C�N���̃v�[����������
    init_cycle.pool = ngx_create_pool(1024, log);
    if (init_cycle.pool == NULL) {
        return 1;
    }

    /**
     * @brief �ÓI�ϐ��Q�ɃR�}���h���C�������Ɗ��ϐ��ɂ��Ēl���Z�b�g����
     * @param[in] cycle �T�C�N���i���̍\���̂��ۗL���Ă��郍�O���g�p����j
     * @param[in] argc  �Z�b�g�������u�R�}���h���C�������̐��v
     * @param[in] argv  �Z�b�g�������R�}���h���C������
     * @retval
     *     NGX_OK    ����
     *     NGX_ERROR ���s
     * @pre
     *     environ �Ɋ��ϐ��̏�񂪃Z�b�g����Ă���i�����̓V�X�e���������ŏ�������j
     * @post
     *     ngx_os_argv �̓R�}���h���C�����̂��́iargv�j���w���Ă���i�܂�A����� environ �������\��������j
     *     ngx_argc �ɃR�}���h���C�������̐����Z�b�g����Ă���
     *     ngx_argv �ɃR�}���h���C�������̃n�[�h�R�s�[���i�[����Ă���
     *     ngx_os_environ �Ɋ��ϐ��̏�񂪃Z�b�g����Ă���
     */
    // �ÓI�ϐ��Q�ɃR�}���h���C�������Ɗ��ϐ��ɂ��Ēl���Z�b�g����
    if (ngx_save_argv(&init_cycle, argc, argv) != NGX_OK) {
        return 1;
    }

    // cycle �� conf_prefix, prefix, conf_file, conf_param �ɒl���Z�b�g����
    if (ngx_process_options(&init_cycle) != NGX_OK) {
        return 1;
    }

    /**
     * @brief
     *     OS �Ɉˑ����鏉�����������ꋓ�ɍs��
     * @param[in]
     *     log: ���O�̏o�͂Ɏg�p
     * @retval
     *     NGX_OK: ����
     *     NGX_ERROR: ���s
     * @post
     *     �ÓI�ϐ� ngx_linux_kern_ostype �� os �̃^�C�v��񂪊i�[����Ă���
     *     �ÓI�ϐ� ngx_linux_kern_osrelease �� os �̃����[�X��񂪊i�[����Ă���
     *     �ÓI�ϐ� ngx_os_io �� OS �ˑ��̓��o�͏����Q���Z�b�g����Ă���
     *     �ÓI�ϐ� ngx_os_argv_last �� ngx_os_argv �̍Ō�̔Ԓn���w���Ă���
     *     �ÓI�ϐ� environ ���K�v�ł���΃��������蓖�Ē�������Ă���
     *     �ÓI�ϐ� ngx_pagsize �Ƀy�[�W�T�C�Y���i�[����Ă���
     *     �ÓI�ϐ� ngx_cacheline_size �ɃL���b�V�����C���T�C�Y���i�[����Ă���
     *     �ÓI�ϐ� ngx_pagesize_shift �� ngx_pagesize �� 2 �̉��悩�i�[����Ă���
     *     �ÓI�ϐ� ngx_ncpu �Ɍ��ݗ��p�\�ȃv���Z�b�T�̐����i�[����Ă���
     *     �ÓI�ϐ� ngx_max_sockets �ɃI�[�v���\�ȃ\�P�b�g�̍ő吔���i�[����Ă���
     *     �ÓI�ϐ� ngx_inherited_nonblocking �� fcntl() �ł��������m���u���b�L���O���[�h�ɕύX����K�v�����邩�i�[����Ă���
     *     os �̃����_���֐��̎�ɂ�鏉�������������Ă���
     */
    if (ngx_os_init(log) != NGX_OK) {
        return 1;
    }

    /*
     * ngx_crc32_table_init() requires ngx_cacheline_size set in ngx_os_init()
     */
    /**
     * @brief
     *     crc32 �������Ŏg�p����Z�����̃e�[�u�����y�[�W�T�C�Y�i�L���b�V�����C���T�C�Y�j��ɏ悹��
     * @retval
     *     NGX_OK: ����
     *     NGX_ERROR: ���s
     * @pre
     *     ngx_cacheline_size �Ƀy�[�W�T�C�Y�i�L���b�V�����C���T�C�Y�j���i�[����Ă���
     */
    if (ngx_crc32_table_init() != NGX_OK) {
        return 1;
    }

    /*
     * ngx_slab_sizes_init() requires ngx_pagesize set in ngx_os_init()
     */
    /**
     * @brief
     *     �X���u���g�p����ÓI�ϐ��Q���y�[�W�T�C�Y�����Ƃɏ���������
     * @pre
     *     ngx_pagesize �ɖ{�v���Z�X���s���ɂ�����y�[�W�T�C�Y���i�[����Ă���
     * @post
     *     ngx_slab_max_size �ɒl���i�[����Ă���
     *     ngx_slab_exact_size �ɒl���i�[����Ă���
     *     ngx_slab_exact_shift �ɒl���i�[����Ă���
     */
    ngx_slab_sizes_init();

    /**
     * @brief
     *     ���ϐ� NGINX_VAR �� fd ���Z�b�g����Ă����ꍇ�A�T�C�N���Ɏ󂯌p��
     * @param[in:out]
     *     cycle: ���̃T�C�N���� listening �����o�ɃZ�b�g����
     * @retval
     *     NGX_OK: ����I��
     *     NGX_ERROR: �ُ�I��
     */
    // �V���O���v���Z�X���[�h�ł͉������Ȃ�
    if (ngx_add_inherited_sockets(&init_cycle) != NGX_OK) {
        return 1;
    }

    /**
     * @brief
     *     ngx_modules.c �ɂĒ�`�����ÓI���W���[���Q�ɂ���
     *     �����Œu����Ă��� ngx_modules[] ���̊e�v�f�ɂ��āA���Ԗڂɒu����Ă��邩���e���W���[����
     *     index �����o�Ɋi�[����
     *     �܂��A���t�@�C���Ɋi�[����Ă��� ngx_module_names[] ���Q�Ƃ��� name �����o������������
     *     �Ō�ɁA�ÓI���W���[���ƐÓI���I���킹�Ă̍ő升�v���W���[�����v�Z����
     * @pre
     *     ngx_modules.c �ɂ� ngx_modules[] �� ngx_module_names[] ����`����Ă���
     * @post
     *     ngx_modules[] �̊e�v�f�� index �����o�ɓY�����ingx_modules[] ���ŏォ�牽�Ԗڂɒu����Ă��邩�j���i�[����Ă���
     *     ngx_modules[] �̊e�v�f�� name �����o�Ƀ��W���[�������Z�b�g����Ă���
     *     �ÓI�ϐ� ngx__modules_n �ɐÓI���W���[���̑������i�[����Ă���
     *     �ÓI�ϐ� ngx_max_module �ɐÓI�{���I���W���[���̍ő升�v�����i�[����Ă���
     * @retval
     *     NGX_OK: �K�����ꂪ�Ԃ�
     */
    if (ngx_preinit_modules() != NGX_OK) {
        return 1;
    }

    // �T�C�N���̏�����
    // �����Őݒ�t�@�C�����p�[�X����
    cycle = ngx_init_cycle(&init_cycle);
    if (cycle == NULL) {
        if (ngx_test_config) {
            ngx_log_stderr(0, "configuration file %s test failed",
                           init_cycle.conf_file.data);
        }

        return 1;
    }

    // ���s�I�v�V������ t �� T ���w�肳�ꂽ�ꍇ�� ngx_test_config �� 1�A����ȊO�� 0
    // ���ʂ͎��s����Ȃ�
    if (ngx_test_config) {
        if (!ngx_quiet_mode) {
            ngx_log_stderr(0, "configuration file %s test is successful",
                           cycle->conf_file.data);
        }

        if (ngx_dump_config) {
            cd = cycle->config_dump.elts;

            for (i = 0; i < cycle->config_dump.nelts; i++) {

                ngx_write_stdout("# configuration file ");
                (void) ngx_write_fd(ngx_stdout, cd[i].name.data,
                                    cd[i].name.len);
                ngx_write_stdout(":" NGX_LINEFEED);

                b = cd[i].buffer;

                (void) ngx_write_fd(ngx_stdout, b->pos, b->last - b->pos);
                ngx_write_stdout(NGX_LINEFEED);
            }
        }

        return 0;
    }

    // ���s�I�v�V������ --stop �Ȃǂ̃V�O�i�������񂪎w�肳�ꂽ�ꍇ�́A���̕����� ngx_signal �ɓ����Ă���
    // ���ʂ͎��s����Ȃ�
    if (ngx_signal) {
        return ngx_signal_process(cycle, ngx_signal);
    }

    /**
     * @brief
     *     freebsd �� solaris �Ƃ����� OS ���L�̐ݒ莖�������O�֏o�͂���
     * @param[in]
     *     log: �o�͐�̃��O
     */
    ngx_os_status(cycle->log);

    // ��قǐ��������T�C�N����ÓI�ϐ��Ƃ��ĊǗ�
    ngx_cycle = cycle;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    // �f�B���N�e�B�u�ŕ����v���Z�X���[�h���w�肳��Ă�����ڍs
    // ����͕K�v�Ȃ�
    if (ccf->master && ngx_process == NGX_PROCESS_SINGLE) {
        ngx_process = NGX_PROCESS_MASTER;
    }

#if !(NGX_WIN32)

    if (ngx_init_signals(cycle->log) != NGX_OK) {
        return 1;
    }

    // ����͂����͎��s����Ȃ�
    if (!ngx_inherited && ccf->daemon) {
        if (ngx_daemon(cycle->log) != NGX_OK) {
            return 1;
        }

        ngx_daemonized = 1;
    }

    if (ngx_inherited) {
        ngx_daemonized = 1;
    }

#endif

    if (ngx_create_pidfile(&ccf->pid, cycle->log) != NGX_OK) {
        return 1;
    }

    if (ngx_log_redirect_stderr(cycle) != NGX_OK) {
        return 1;
    }

    if (log->file->fd != ngx_stderr) {
        if (ngx_close_file(log->file->fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_close_file_n " built-in log failed");
        }
    }

    ngx_use_stderr = 0;

    if (ngx_process == NGX_PROCESS_SINGLE) {
        ngx_single_process_cycle(cycle);

    } else {
        ngx_master_process_cycle(cycle);
    }

    return 0;
}


static void
ngx_show_version_info(void)
{
    ngx_write_stderr("nginx version: " NGINX_VER_BUILD NGX_LINEFEED);

    if (ngx_show_help) {
        ngx_write_stderr(
            "Usage: nginx [-?hvVtTq] [-s signal] [-c filename] "
                         "[-p prefix] [-g directives]" NGX_LINEFEED
                         NGX_LINEFEED
            "Options:" NGX_LINEFEED
            "  -?,-h         : this help" NGX_LINEFEED
            "  -v            : show version and exit" NGX_LINEFEED
            "  -V            : show version and configure options then exit"
                               NGX_LINEFEED
            "  -t            : test configuration and exit" NGX_LINEFEED
            "  -T            : test configuration, dump it and exit"
                               NGX_LINEFEED
            "  -q            : suppress non-error messages "
                               "during configuration testing" NGX_LINEFEED
            "  -s signal     : send signal to a master process: "
                               "stop, quit, reopen, reload" NGX_LINEFEED
#ifdef NGX_PREFIX
            "  -p prefix     : set prefix path (default: " NGX_PREFIX ")"
                               NGX_LINEFEED
#else
            "  -p prefix     : set prefix path (default: NONE)" NGX_LINEFEED
#endif
            "  -c filename   : set configuration file (default: " NGX_CONF_PATH
                               ")" NGX_LINEFEED
            "  -g directives : set global directives out of configuration "
                               "file" NGX_LINEFEED NGX_LINEFEED
        );
    }

    if (ngx_show_configure) {

#ifdef NGX_COMPILER
        ngx_write_stderr("built by " NGX_COMPILER NGX_LINEFEED);
#endif

#if (NGX_SSL)
        if (ngx_strcmp(ngx_ssl_version(), OPENSSL_VERSION_TEXT) == 0) {
            ngx_write_stderr("built with " OPENSSL_VERSION_TEXT NGX_LINEFEED);
        } else {
            ngx_write_stderr("built with " OPENSSL_VERSION_TEXT
                             " (running with ");
            ngx_write_stderr((char *) (uintptr_t) ngx_ssl_version());
            ngx_write_stderr(")" NGX_LINEFEED);
        }
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
        ngx_write_stderr("TLS SNI support enabled" NGX_LINEFEED);
#else
        ngx_write_stderr("TLS SNI support disabled" NGX_LINEFEED);
#endif
#endif

        ngx_write_stderr("configure arguments:" NGX_CONFIGURE NGX_LINEFEED);
    }
}


/**
 * @brief
 *     ���ϐ� NGINX_VAR �� fd ���Z�b�g����Ă����ꍇ�A�T�C�N���Ɏ󂯌p��
 * @param[in:out]
 *     cycle: ���̃T�C�N���� listening �����o�ɃZ�b�g����
 * @retval
 *     NGX_OK: ����I��
 *     NGX_ERROR: �ُ�I��
 */
static ngx_int_t
ngx_add_inherited_sockets(ngx_cycle_t *cycle)
{
    u_char           *p, *v, *inherited;
    ngx_int_t         s;
    ngx_listening_t  *ls;

    // ���ϐ� NGINX_VAR �̒l���擾����
    // ���̊��ϐ��͕����v���Z�X���[�h�ł̂݁Angx_exec_new_binary() ���Œ�`�����
    // ����āA�V���O���v���Z�X���[�h�ł͂����ŏI������
    inherited = (u_char *) getenv(NGINX_VAR);

    // ����Ȓl���Ȃ��Ȃ�I��
    if (inherited == NULL) {
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                  "using inherited sockets from \"%s\"", inherited);

    if (ngx_array_init(&cycle->listening, cycle->pool, 10,
                       sizeof(ngx_listening_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (p = inherited, v = p; *p; p++) {
        if (*p == ':' || *p == ';') {
            s = ngx_atoi(v, p - v);
            if (s == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "invalid socket number \"%s\" in " NGINX_VAR
                              " environment variable, ignoring the rest"
                              " of the variable", v);
                break;
            }

            v = p + 1;

            ls = ngx_array_push(&cycle->listening);
            if (ls == NULL) {
                return NGX_ERROR;
            }

            ngx_memzero(ls, sizeof(ngx_listening_t));

            ls->fd = (ngx_socket_t) s;
        }
    }

    if (v != p) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "invalid socket number \"%s\" in " NGINX_VAR
                      " environment variable, ignoring", v);
    }

    ngx_inherited = 1;

    return ngx_set_inherited_sockets(cycle);
}


/**
 * @brief
 *     �ÓI�ϐ� environ �ƃR�A���W���[���ݒ�� environment �� env �f�B���N�e�B�u�ŗL�������ꂽ���̂Ō��I����
 * @param[in]
 *     cycle: �R�A���W���[���̐ݒ���擾����ۂȂǂɎg�p
 *     last: 
 * @pre
 *     ngx_os_environ �ɃV�X�e������n���ꂽ���ϐ��̏�񂪃Z�b�g����Ă���
 * @post
 *     �R�A���W���[���ݒ�� environment �����o�����I�ς�
 *     �ÓI�ϐ� environ �����I�ς�
 * @detail
 *     environ �� env �f�B���N�e�B�u�����Ɍ��I�����
 *     ngx_os_environ �̓V�X�e������n���ꂽ���̂����̂܂ܕێ�����
 */
char **
ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last)
{
    char                **p, **env;
    ngx_str_t            *var;
    ngx_uint_t            i, n;
    ngx_core_conf_t      *ccf;
    ngx_pool_cleanup_t   *cln;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    // ���ɃR�A���W���[���Ɋ��ϐ����Z�b�g����Ă���Ȃ牽�����Ȃ�
    if (last == NULL && ccf->environment) {
        return ccf->environment;
    }

    var = ccf->env.elts;

    // env �f�B���N�e�B�u�œo�^�������ϐ��̒����� TZ= ��T���A���������� tz_found �֔��
    for (i = 0; i < ccf->env.nelts; i++) {
        if (ngx_strcmp(var[i].data, "TZ") == 0
            || ngx_strncmp(var[i].data, "TZ=", 3) == 0)
        {
            goto tz_found;
        }
    }

    var = ngx_array_push(&ccf->env);
    if (var == NULL) {
        return NULL;
    }

    // TZ ���Ȃ��Ȃ疳�������
    var->len = 2;
    var->data = (u_char *) "TZ";

    var = ccf->env.elts;

tz_found:

    n = 0;

    // �R�A���W���[���� env ���X�g�̊e�v�f�ɂ���
    // �Z�Z= �`�����A�V�X�e���̊��ϐ��Ɉ�v������̂������Ă�����ł� �Z�Z= �`���̂��̂̓J�E���g����
    // �܂�A�L���Ȋ��ϐ��̐����J�E���g����
    for (i = 0; i < ccf->env.nelts; i++) {

        // �Z�Z= �`����������J�E���g���Ď���
        if (var[i].data[var[i].len] == '=') {
            n++;
            continue;
        }

        // �V�X�e������R�s�[�������ϐ����Q��
        for (p = ngx_os_environ; *p; p++) {

            if (ngx_strncmp(*p, var[i].data, var[i].len) == 0
                && (*p)[var[i].len] == '=')
            {
                n++;
                break;
            }
        }
    }

    if (last) {
        env = ngx_alloc((*last + n + 1) * sizeof(char *), cycle->log);
        if (env == NULL) {
            return NULL;
        }

        *last = n;

    } else {
        // ���ϐ��Q���i�[���邽�߂ɃV�X�e�����烁�������l�����ăN���[���A�b�v�֐����d�|����
        cln = ngx_pool_cleanup_add(cycle->pool, 0);
        if (cln == NULL) {
            return NULL;
        }

        env = ngx_alloc((n + 1) * sizeof(char *), cycle->log);
        if (env == NULL) {
            return NULL;
        }

        cln->handler = ngx_cleanup_environment;
        cln->data = env;
    }

    n = 0;

    for (i = 0; i < ccf->env.nelts; i++) {

        if (var[i].data[var[i].len] == '=') {
            env[n++] = (char *) var[i].data;
            continue;
        }

        for (p = ngx_os_environ; *p; p++) {

            if (ngx_strncmp(*p, var[i].data, var[i].len) == 0
                && (*p)[var[i].len] == '=')
            {
                env[n++] = *p;
                break;
            }
        }
    }

    // �Ō�� NULL ���Z�b�g
    env[n] = NULL;

    if (last == NULL) {
        // �R�A���W���[���̐ݒ�Ɋ��ϐ��Q���Z�b�g����
        ccf->environment = env;
        // �V�X�e������n���ꂽ���̂ł͂Ȃ��āA���A���������R�s�[�ŏ㏑���inginx ���g�p������̂����j
        environ = env;
    }

    return env;
}


static void
ngx_cleanup_environment(void *data)
{
    char  **env = data;

    if (environ == env) {

        /*
         * if the environment is still used, as it happens on exit,
         * the only option is to leak it
         */

        return;
    }

    ngx_free(env);
}


// ngx_master_process_cycle() ���ł̂ݎ��s�����
// ����āA����͌����Ď��s����Ȃ�
ngx_pid_t
ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv)
{
    char             **env, *var;
    u_char            *p;
    ngx_uint_t         i, n;
    ngx_pid_t          pid;
    ngx_exec_ctx_t     ctx;
    ngx_core_conf_t   *ccf;
    ngx_listening_t   *ls;

    ngx_memzero(&ctx, sizeof(ngx_exec_ctx_t));

    ctx.path = argv[0];
    ctx.name = "new binary process";
    ctx.argv = argv;

    n = 2;
    env = ngx_set_environment(cycle, &n);
    if (env == NULL) {
        return NGX_INVALID_PID;
    }

    var = ngx_alloc(sizeof(NGINX_VAR)
                    + cycle->listening.nelts * (NGX_INT32_LEN + 1) + 2,
                    cycle->log);
    if (var == NULL) {
        ngx_free(env);
        return NGX_INVALID_PID;
    }

    p = ngx_cpymem(var, NGINX_VAR "=", sizeof(NGINX_VAR));

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        p = ngx_sprintf(p, "%ud;", ls[i].fd);
    }

    *p = '\0';

    env[n++] = var;

#if (NGX_SETPROCTITLE_USES_ENV)

    /* allocate the spare 300 bytes for the new binary process title */

    env[n++] = "SPARE=XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

#endif

    env[n] = NULL;

#if (NGX_DEBUG)
    {
    char  **e;
    for (e = env; *e; e++) {
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0, "env: %s", *e);
    }
    }
#endif

    ctx.envp = (char *const *) env;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ngx_rename_file(ccf->pid.data, ccf->oldpid.data) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_rename_file_n " %s to %s failed "
                      "before executing new binary process \"%s\"",
                      ccf->pid.data, ccf->oldpid.data, argv[0]);

        ngx_free(env);
        ngx_free(var);

        return NGX_INVALID_PID;
    }

    pid = ngx_execute(cycle, &ctx);

    if (pid == NGX_INVALID_PID) {
        if (ngx_rename_file(ccf->oldpid.data, ccf->pid.data)
            == NGX_FILE_ERROR)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_rename_file_n " %s back to %s failed after "
                          "an attempt to execute new binary process \"%s\"",
                          ccf->oldpid.data, ccf->pid.data, argv[0]);
        }
    }

    ngx_free(env);
    ngx_free(var);

    return pid;
}


static ngx_int_t
ngx_get_options(int argc, char *const *argv)
{
    u_char     *p;
    ngx_int_t   i;

    for (i = 1; i < argc; i++) {

        p = (u_char *) argv[i];

        if (*p++ != '-') {
            ngx_log_stderr(0, "invalid option: \"%s\"", argv[i]);
            return NGX_ERROR;
        }

        while (*p) {

            switch (*p++) {

            case '?':
            case 'h':
                ngx_show_version = 1;
                ngx_show_help = 1;
                break;

            case 'v':
                ngx_show_version = 1;
                break;

            case 'V':
                ngx_show_version = 1;
                ngx_show_configure = 1;
                break;

            case 't':
                ngx_test_config = 1;
                break;

            case 'T':
                ngx_test_config = 1;
                ngx_dump_config = 1;
                break;

            case 'q':
                ngx_quiet_mode = 1;
                break;

            case 'p':
                if (*p) {
                    ngx_prefix = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_prefix = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-p\" requires directory name");
                return NGX_ERROR;

            case 'c':
                if (*p) {
                    ngx_conf_file = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_conf_file = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-c\" requires file name");
                return NGX_ERROR;

            case 'g':
                if (*p) {
                    ngx_conf_params = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_conf_params = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-g\" requires parameter");
                return NGX_ERROR;

            case 's':
                if (*p) {
                    ngx_signal = (char *) p;

                } else if (argv[++i]) {
                    ngx_signal = argv[i];

                } else {
                    ngx_log_stderr(0, "option \"-s\" requires parameter");
                    return NGX_ERROR;
                }

                if (ngx_strcmp(ngx_signal, "stop") == 0
                    || ngx_strcmp(ngx_signal, "quit") == 0
                    || ngx_strcmp(ngx_signal, "reopen") == 0
                    || ngx_strcmp(ngx_signal, "reload") == 0)
                {
                    ngx_process = NGX_PROCESS_SIGNALLER;
                    goto next;
                }

                ngx_log_stderr(0, "invalid option: \"-s %s\"", ngx_signal);
                return NGX_ERROR;

            default:
                ngx_log_stderr(0, "invalid option: \"%c\"", *(p - 1));
                return NGX_ERROR;
            }
        }

    next:

        continue;
    }

    return NGX_OK;
}


/**
 * @brief �ÓI�ϐ��Q�ɃR�}���h���C�������Ɗ��ϐ��ɂ��Ēl���Z�b�g����
 * @param[in] cycle �T�C�N���i���̍\���̂��ۗL���Ă��郍�O���g�p����j
 * @param[in] argc  �Z�b�g�������u�R�}���h���C�������̐��v
 * @param[in] argv  �Z�b�g�������R�}���h���C������
 * @retval
 *     NGX_OK    ����
 *     NGX_ERROR ���s
 * @pre
 *     environ �Ɋ��ϐ��̏�񂪃Z�b�g����Ă���i�����̓V�X�e���������ŏ�������j
 * @post
 *     ngx_os_argv �̓R�}���h���C�����̂��́iargv�j���w���Ă���i�܂�A����� environ �������\��������j
 *     ngx_argc �ɃR�}���h���C�������̐����Z�b�g����Ă���
 *     ngx_argv �ɃR�}���h���C�������̃n�[�h�R�s�[���i�[����Ă���
 *     ngx_os_environ �͊��ϐ��̏����w���i�V�X�e��������n�������́j
 */
static ngx_int_t
ngx_save_argv(ngx_cycle_t *cycle, int argc, char *const *argv)
{
#if (NGX_FREEBSD)

    ngx_os_argv = (char **) argv;
    ngx_argc = argc;
    ngx_argv = (char **) argv;

#else
    size_t     len;
    ngx_int_t  i;

    ngx_os_argv = (char **) argv;
    ngx_argc = argc;

    ngx_argv = ngx_alloc((argc + 1) * sizeof(char *), cycle->log);
    if (ngx_argv == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < argc; i++) {
        len = ngx_strlen(argv[i]) + 1;

        ngx_argv[i] = ngx_alloc(len, cycle->log);
        if (ngx_argv[i] == NULL) {
            return NGX_ERROR;
        }

        (void) ngx_cpystrn((u_char *) ngx_argv[i], (u_char *) argv[i], len);
    }

    ngx_argv[i] = NULL;

#endif

    ngx_os_environ = environ;

    return NGX_OK;
}


/**
 * @brief
 *     �T�C�N���� conf_prefix, prefix, conf_file, conf_param �ɒl���Z�b�g����
 *     prefix �̓p�X�w��̎��Ɏg�p����p�X
 *     conf_prefix �͐ݒ�t�@�C���ւ̃p�X�������O������
 *     conf_file �͐ݒ�t�@�C���ւ̃t���p�X
 * @retval
 *     NGX_OK:    ����
 *     NGX_ERROR: ���s
 */
static ngx_int_t
ngx_process_options(ngx_cycle_t *cycle)
{
    u_char  *p;
    size_t   len;

    // ngx_prefix �� main() ���� ngx_get_options() �ŃZ�b�g�����
    // �ʏ�̓X���[
    if (ngx_prefix) {
        len = ngx_strlen(ngx_prefix);
        p = ngx_prefix;

        if (len && !ngx_path_separator(p[len - 1])) {
            p = ngx_pnalloc(cycle->pool, len + 1);
            if (p == NULL) {
                return NGX_ERROR;
            }

            ngx_memcpy(p, ngx_prefix, len);
            p[len++] = '/';
        }

        cycle->conf_prefix.len = len;
        cycle->conf_prefix.data = p;
        cycle->prefix.len = len;
        cycle->prefix.data = p;

    } else {

#ifndef NGX_PREFIX

        p = ngx_pnalloc(cycle->pool, NGX_MAX_PATH);
        if (p == NULL) {
            return NGX_ERROR;
        }

        // �J�����g�f�B���N�g�����擾���� p �ɃZ�b�g
        if (ngx_getcwd(p, NGX_MAX_PATH) == 0) {
            ngx_log_stderr(ngx_errno, "[emerg]: " ngx_getcwd_n " failed");
            return NGX_ERROR;
        }

        // �k���������������������擾
        len = ngx_strlen(p);

        // �����̃k�������� '/' �ŏ㏑������
        p[len++] = '/';

        cycle->conf_prefix.len = len;
        cycle->conf_prefix.data = p;
        cycle->prefix.len = len;
        cycle->prefix.data = p;

#else

  #ifdef NGX_CONF_PREFIX
        ngx_str_set(&cycle->conf_prefix, NGX_CONF_PREFIX);
  #else
        ngx_str_set(&cycle->conf_prefix, NGX_PREFIX);
  #endif
        ngx_str_set(&cycle->prefix, NGX_PREFIX);

#endif
    }

    if (ngx_conf_file) {
        cycle->conf_file.len = ngx_strlen(ngx_conf_file);
        cycle->conf_file.data = ngx_conf_file;

    } else {
        // �R�}���h���C�������Őݒ�t�@�C�����w�肵�Ă��Ȃ���΂�����
        // ""
        ngx_str_set(&cycle->conf_file, NGX_CONF_PATH);
    }

    // prefix �ƍ��킹�� "/" 
    // ����� NGX_OK ���Ԃ�
    if (ngx_conf_full_name(cycle, &cycle->conf_file, 0) != NGX_OK) {
        return NGX_ERROR;
    }

    // conf_prefix �͐ݒ�t�@�C�������������p�X
    // �l�ɂ���Ă� prefix �Ŏw�肷��p�X�Ɛݒ�t�@�C���̃t���p�X��
    // �قȂ�ꍇ������̂ŁA���̂Ƃ��̂��߂ɂ��̂悤�ȕ��򂪂���Ă���
    // prefix �� conf_prefix ����v���Ă��āAconf_file ���t�@�C�����̃p�^�[��
    // conf_file ���p�X�ŁAprefix �� conf_prefix ���قȂ�p�^�[��
    // "/" �̏ꍇ�͂��̃��[�v�͉������s����Ȃ�
    for (p = cycle->conf_file.data + cycle->conf_file.len - 1;
         p > cycle->conf_file.data;
         p--)
    {
        if (ngx_path_separator(*p)) {
            cycle->conf_prefix.len = p - cycle->conf_file.data + 1;
            cycle->conf_prefix.data = cycle->conf_file.data;
            break;
        }
    }

    // �R�}���h���C�������œn���ꂽ�p�����[�^���Z�b�g
    if (ngx_conf_params) {
        cycle->conf_param.len = ngx_strlen(ngx_conf_params);
        cycle->conf_param.data = ngx_conf_params;
    }

    // -t �� -T ���w�肳��Ă����ꍇ�̓��O���x����ύX����
    if (ngx_test_config) {
        cycle->log->log_level = NGX_LOG_INFO;
    }

    return NGX_OK;
}


/**
 * @brief
 *     �R�A���W���[�������̐ݒ�t�@�C���𐶐����ĕԂ�
 * @param[in]
 *     cycle: �ݒ�t�@�C�������蓖�Ă�v�[�������̃T�C�N������؂��
 * @retval
 *     ���������ݒ�t�@�C���ւ̃|�C���^
 */
static void *
ngx_core_module_create_conf(ngx_cycle_t *cycle)
{
    ngx_core_conf_t  *ccf;

    ccf = ngx_pcalloc(cycle->pool, sizeof(ngx_core_conf_t));
    if (ccf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc()
     *
     *     ccf->pid = NULL;
     *     ccf->oldpid = NULL;
     *     ccf->priority = 0;
     *     ccf->cpu_affinity_auto = 0;
     *     ccf->cpu_affinity_n = 0;
     *     ccf->cpu_affinity = NULL;
     */

    ccf->daemon = NGX_CONF_UNSET;
    ccf->master = NGX_CONF_UNSET;
    ccf->timer_resolution = NGX_CONF_UNSET_MSEC;
    ccf->shutdown_timeout = NGX_CONF_UNSET_MSEC;

    ccf->worker_processes = NGX_CONF_UNSET;
    ccf->debug_points = NGX_CONF_UNSET;

    ccf->rlimit_nofile = NGX_CONF_UNSET;
    ccf->rlimit_core = NGX_CONF_UNSET;

    ccf->user = (ngx_uid_t) NGX_CONF_UNSET_UINT;
    ccf->group = (ngx_gid_t) NGX_CONF_UNSET_UINT;

    if (ngx_array_init(&ccf->env, cycle->pool, 1, sizeof(ngx_str_t))
        != NGX_OK)
    {
        return NULL;
    }

    return ccf;
}


/**
 * @brief
 *     �ݒ�t�@�C����������
 */
static char *
ngx_core_module_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    /**
     * @brief
     *     �܂� create_conf() �Ăяo������ł���΁A�������̒l�ŏ���������
     */
    ngx_conf_init_value(ccf->daemon, 1);
    ngx_conf_init_value(ccf->master, 1);
    ngx_conf_init_msec_value(ccf->timer_resolution, 0);
    ngx_conf_init_msec_value(ccf->shutdown_timeout, 0);

    ngx_conf_init_value(ccf->worker_processes, 1);
    ngx_conf_init_value(ccf->debug_points, 0);

#if (NGX_HAVE_CPU_AFFINITY)

    if (!ccf->cpu_affinity_auto
        && ccf->cpu_affinity_n
        && ccf->cpu_affinity_n != 1
        && ccf->cpu_affinity_n != (ngx_uint_t) ccf->worker_processes)
    {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                      "the number of \"worker_processes\" is not equal to "
                      "the number of \"worker_cpu_affinity\" masks, "
                      "using last mask for remaining worker processes");
    }

#endif


    if (ccf->pid.len == 0) {
        ngx_str_set(&ccf->pid, NGX_PID_PATH);
    }

    if (ngx_conf_full_name(cycle, &ccf->pid, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    ccf->oldpid.len = ccf->pid.len + sizeof(NGX_OLDPID_EXT);

    ccf->oldpid.data = ngx_pnalloc(cycle->pool, ccf->oldpid.len);
    if (ccf->oldpid.data == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(ngx_cpymem(ccf->oldpid.data, ccf->pid.data, ccf->pid.len),
               NGX_OLDPID_EXT, sizeof(NGX_OLDPID_EXT));


#if !(NGX_WIN32)

    if (ccf->user == (uid_t) NGX_CONF_UNSET_UINT && geteuid() == 0) {
        struct group   *grp;
        struct passwd  *pwd;

        ngx_set_errno(0);
        pwd = getpwnam(NGX_USER);
        if (pwd == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getpwnam(\"" NGX_USER "\") failed");
            return NGX_CONF_ERROR;
        }

        ccf->username = NGX_USER;
        ccf->user = pwd->pw_uid;

        ngx_set_errno(0);
        grp = getgrnam(NGX_GROUP);
        if (grp == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getgrnam(\"" NGX_GROUP "\") failed");
            return NGX_CONF_ERROR;
        }

        ccf->group = grp->gr_gid;
    }


    if (ccf->lock_file.len == 0) {
        ngx_str_set(&ccf->lock_file, NGX_LOCK_PATH);
    }

    if (ngx_conf_full_name(cycle, &ccf->lock_file, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    {
    ngx_str_t  lock_file;

    lock_file = cycle->old_cycle->lock_file;

    if (lock_file.len) {
        lock_file.len--;

        if (ccf->lock_file.len != lock_file.len
            || ngx_strncmp(ccf->lock_file.data, lock_file.data, lock_file.len)
               != 0)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"lock_file\" could not be changed, ignored");
        }

        cycle->lock_file.len = lock_file.len + 1;
        lock_file.len += sizeof(".accept");

        cycle->lock_file.data = ngx_pstrdup(cycle->pool, &lock_file);
        if (cycle->lock_file.data == NULL) {
            return NGX_CONF_ERROR;
        }

    } else {
        cycle->lock_file.len = ccf->lock_file.len + 1;
        cycle->lock_file.data = ngx_pnalloc(cycle->pool,
                                      ccf->lock_file.len + sizeof(".accept"));
        if (cycle->lock_file.data == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memcpy(ngx_cpymem(cycle->lock_file.data, ccf->lock_file.data,
                              ccf->lock_file.len),
                   ".accept", sizeof(".accept"));
    }
    }

#endif

    return NGX_CONF_OK;
}


static char *
ngx_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_WIN32)

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"user\" is not supported, ignored");

    return NGX_CONF_OK;

#else

    ngx_core_conf_t  *ccf = conf;

    char             *group;
    struct passwd    *pwd;
    struct group     *grp;
    ngx_str_t        *value;

    if (ccf->user != (uid_t) NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    if (geteuid() != 0) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"user\" directive makes sense only "
                           "if the master process runs "
                           "with super-user privileges, ignored");
        return NGX_CONF_OK;
    }

    value = cf->args->elts;

    ccf->username = (char *) value[1].data;

    ngx_set_errno(0);
    pwd = getpwnam((const char *) value[1].data);
    if (pwd == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           "getpwnam(\"%s\") failed", value[1].data);
        return NGX_CONF_ERROR;
    }

    ccf->user = pwd->pw_uid;

    group = (char *) ((cf->args->nelts == 2) ? value[1].data : value[2].data);

    ngx_set_errno(0);
    grp = getgrnam(group);
    if (grp == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           "getgrnam(\"%s\") failed", group);
        return NGX_CONF_ERROR;
    }

    ccf->group = grp->gr_gid;

    return NGX_CONF_OK;

#endif
}


static char *
ngx_set_env(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    ngx_str_t   *value, *var;
    ngx_uint_t   i;

    var = ngx_array_push(&ccf->env);
    if (var == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;
    *var = value[1];

    for (i = 0; i < value[1].len; i++) {

        if (value[1].data[i] == '=') {

            var->len = i;

            return NGX_CONF_OK;
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_set_priority(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    ngx_str_t        *value;
    ngx_uint_t        n, minus;

    if (ccf->priority != 0) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (value[1].data[0] == '-') {
        n = 1;
        minus = 1;

    } else if (value[1].data[0] == '+') {
        n = 1;
        minus = 0;

    } else {
        n = 0;
        minus = 0;
    }

    ccf->priority = ngx_atoi(&value[1].data[n], value[1].len - n);
    if (ccf->priority == NGX_ERROR) {
        return "invalid number";
    }

    if (minus) {
        ccf->priority = -ccf->priority;
    }

    return NGX_CONF_OK;
}


static char *
ngx_set_cpu_affinity(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_HAVE_CPU_AFFINITY)
    ngx_core_conf_t  *ccf = conf;

    u_char            ch, *p;
    ngx_str_t        *value;
    ngx_uint_t        i, n;
    ngx_cpuset_t     *mask;

    if (ccf->cpu_affinity) {
        return "is duplicate";
    }

    mask = ngx_palloc(cf->pool, (cf->args->nelts - 1) * sizeof(ngx_cpuset_t));
    if (mask == NULL) {
        return NGX_CONF_ERROR;
    }

    ccf->cpu_affinity_n = cf->args->nelts - 1;
    ccf->cpu_affinity = mask;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "auto") == 0) {

        if (cf->args->nelts > 3) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid number of arguments in "
                               "\"worker_cpu_affinity\" directive");
            return NGX_CONF_ERROR;
        }

        ccf->cpu_affinity_auto = 1;

        CPU_ZERO(&mask[0]);
        for (i = 0; i < (ngx_uint_t) ngx_min(ngx_ncpu, CPU_SETSIZE); i++) {
            CPU_SET(i, &mask[0]);
        }

        n = 2;

    } else {
        n = 1;
    }

    for ( /* void */ ; n < cf->args->nelts; n++) {

        if (value[n].len > CPU_SETSIZE) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                         "\"worker_cpu_affinity\" supports up to %d CPUs only",
                         CPU_SETSIZE);
            return NGX_CONF_ERROR;
        }

        i = 0;
        CPU_ZERO(&mask[n - 1]);

        for (p = value[n].data + value[n].len - 1;
             p >= value[n].data;
             p--)
        {
            ch = *p;

            if (ch == ' ') {
                continue;
            }

            i++;

            if (ch == '0') {
                continue;
            }

            if (ch == '1') {
                CPU_SET(i - 1, &mask[n - 1]);
                continue;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid character \"%c\" in \"worker_cpu_affinity\"",
                          ch);
            return NGX_CONF_ERROR;
        }
    }

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"worker_cpu_affinity\" is not supported "
                       "on this platform, ignored");
#endif

    return NGX_CONF_OK;
}


ngx_cpuset_t *
ngx_get_cpu_affinity(ngx_uint_t n)
{
#if (NGX_HAVE_CPU_AFFINITY)
    ngx_uint_t        i, j;
    ngx_cpuset_t     *mask;
    ngx_core_conf_t  *ccf;

    static ngx_cpuset_t  result;

    ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                           ngx_core_module);

    if (ccf->cpu_affinity == NULL) {
        return NULL;
    }

    if (ccf->cpu_affinity_auto) {
        mask = &ccf->cpu_affinity[ccf->cpu_affinity_n - 1];

        for (i = 0, j = n; /* void */ ; i++) {

            if (CPU_ISSET(i % CPU_SETSIZE, mask) && j-- == 0) {
                break;
            }

            if (i == CPU_SETSIZE && j == n) {
                /* empty mask */
                return NULL;
            }

            /* void */
        }

        CPU_ZERO(&result);
        CPU_SET(i % CPU_SETSIZE, &result);

        return &result;
    }

    if (ccf->cpu_affinity_n > n) {
        return &ccf->cpu_affinity[n];
    }

    return &ccf->cpu_affinity[ccf->cpu_affinity_n - 1];

#else

    return NULL;

#endif
}


static char *
ngx_set_worker_processes(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t        *value;
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) conf;

    if (ccf->worker_processes != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "auto") == 0) {
        ccf->worker_processes = ngx_ncpu;
        return NGX_CONF_OK;
    }

    ccf->worker_processes = ngx_atoi(value[1].data, value[1].len);

    if (ccf->worker_processes == NGX_ERROR) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


static char *
ngx_load_module(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_HAVE_DLOPEN)
    void                *handle;
    char               **names, **order;
    ngx_str_t           *value, file;
    ngx_uint_t           i;
    ngx_module_t        *module, **modules;
    ngx_pool_cleanup_t  *cln;

    if (cf->cycle->modules_used) {
        return "is specified too late";
    }

    value = cf->args->elts;

    file = value[1];

    if (ngx_conf_full_name(cf->cycle, &file, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->cycle->pool, 0);
    if (cln == NULL) {
        return NGX_CONF_ERROR;
    }

    handle = ngx_dlopen(file.data);
    if (handle == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlopen_n " \"%s\" failed (%s)",
                           file.data, ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    cln->handler = ngx_unload_module;
    cln->data = handle;

    modules = ngx_dlsym(handle, "ngx_modules");
    if (modules == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlsym_n " \"%V\", \"%s\" failed (%s)",
                           &value[1], "ngx_modules", ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    names = ngx_dlsym(handle, "ngx_module_names");
    if (names == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlsym_n " \"%V\", \"%s\" failed (%s)",
                           &value[1], "ngx_module_names", ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    order = ngx_dlsym(handle, "ngx_module_order");

    for (i = 0; modules[i]; i++) {
        module = modules[i];
        module->name = names[i];

        if (ngx_add_module(cf, &file, module, order) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cf->log, 0, "module: %s i:%ui",
                       module->name, module->index);
    }

    return NGX_CONF_OK;

#else

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "\"load_module\" is not supported "
                       "on this platform");
    return NGX_CONF_ERROR;

#endif
}


#if (NGX_HAVE_DLOPEN)

static void
ngx_unload_module(void *data)
{
    void  *handle = data;

    if (ngx_dlclose(handle) != 0) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      ngx_dlclose_n " failed (%s)", ngx_dlerror());
    }
}

#endif
