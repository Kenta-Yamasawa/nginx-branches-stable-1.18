
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


static void ngx_destroy_cycle_pools(ngx_conf_t *conf);
static ngx_int_t ngx_init_zone_pool(ngx_cycle_t *cycle,
    ngx_shm_zone_t *shm_zone);
static ngx_int_t ngx_test_lockfile(u_char *file, ngx_log_t *log);
static void ngx_clean_old_cycles(ngx_event_t *ev);
static void ngx_shutdown_timer_handler(ngx_event_t *ev);


volatile ngx_cycle_t  *ngx_cycle;
ngx_array_t            ngx_old_cycles;

static ngx_pool_t     *ngx_temp_pool;
static ngx_event_t     ngx_cleaner_event;
static ngx_event_t     ngx_shutdown_event;

ngx_uint_t             ngx_test_config;
ngx_uint_t             ngx_dump_config;
ngx_uint_t             ngx_quiet_mode;


/* STUB NAME */
static ngx_connection_t  dumb;
/* STUB */


/**
 * @brief
 *     �T�C�N�����������E�������ĕԂ�
 *     old_cycle ����ꕔ�̃p�����[�^�������p��
 * @param[in]
 *     old_cycle: �����p�����T�C�N��
 * @retval
 *     �������E���������T�C�N��
 * @macro
 *     NGX_CYCLE_POOL_SIZE: ���̃T�C�N���p�̃v�[���̃T�C�Y��ݒ�
 *     
 * @sub
 *     array
 *     pool
 *     rbtree
 *     list
 */
ngx_cycle_t *
ngx_init_cycle(ngx_cycle_t *old_cycle)
{
    void                *rv;
    char               **senv;
    ngx_uint_t           i, n;
    ngx_log_t           *log;
    ngx_time_t          *tp;
    ngx_conf_t           conf;
    ngx_pool_t          *pool;
    ngx_cycle_t         *cycle, **old;
    ngx_shm_zone_t      *shm_zone, *oshm_zone;
    ngx_list_part_t     *part, *opart;
    ngx_open_file_t     *file;
    ngx_listening_t     *ls, *nls;
    ngx_core_conf_t     *ccf, *old_ccf;
    ngx_core_module_t   *module;
    char                 hostname[NGX_MAXHOSTNAMELEN];

    ngx_timezone_update();

    /* force localtime update with a new timezone */

    tp = ngx_timeofday();
    tp->sec = 0;

    ngx_time_update();

    // ���O�͑����Ŏ蓮�ŃZ�b�g���邱��
    // �iinit_cycle ����󂯌p���j
    log = old_cycle->log;

    // ���̃T�C�N���p�̃v�[���𐶐�
    pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);
    if (pool == NULL) {
        return NULL;
    }
    // �v�[���Ƀ��O���Z�b�g
    pool->log = log;

    // ���̃T�C�N���p�̃v�[���ɃT�C�N���̈�����蓖�Ă�
    cycle = ngx_pcalloc(pool, sizeof(ngx_cycle_t));
    if (cycle == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // �v�[���Z�b�g
    cycle->pool = pool;
    // ���O�Z�b�g
    cycle->log = log;
    // �󂯌p�����T�C�N�����Z�b�g
    cycle->old_cycle = old_cycle;

    // conf_prefix ���Â��T�C�N�����玝���Ă���
    cycle->conf_prefix.len = old_cycle->conf_prefix.len;
    cycle->conf_prefix.data = ngx_pstrdup(pool, &old_cycle->conf_prefix);
    if (cycle->conf_prefix.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // prefix ���Â��T�C�N�����玝���Ă���
    cycle->prefix.len = old_cycle->prefix.len;
    cycle->prefix.data = ngx_pstrdup(pool, &old_cycle->prefix);
    if (cycle->prefix.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // conf_file ���Â��T�C�N�����玝���Ă���
    cycle->conf_file.len = old_cycle->conf_file.len;
    cycle->conf_file.data = ngx_pnalloc(pool, old_cycle->conf_file.len + 1);
    if (cycle->conf_file.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    ngx_cpystrn(cycle->conf_file.data, old_cycle->conf_file.data,
                old_cycle->conf_file.len + 1);

    // conf_params ���Â��T�C�N�����玝���Ă���
    cycle->conf_param.len = old_cycle->conf_param.len;
    cycle->conf_param.data = ngx_pstrdup(pool, &old_cycle->conf_param);
    if (cycle->conf_param.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // �������� 10�iinit_cycle->paths �͋�j
    n = old_cycle->paths.nelts ? old_cycle->paths.nelts : 10;

    // paths �����Z�b�g
    // n = 10
    if (ngx_array_init(&cycle->paths, pool, n, sizeof(ngx_path_t *))
        != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    ngx_memzero(cycle->paths.elts, n * sizeof(ngx_path_t *));

    if (ngx_array_init(&cycle->config_dump, pool, 1, sizeof(ngx_conf_dump_t))
        != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    /**
     * @brief 
     *     �������m�[�h�����F�ɂ��āA�������ԍ��؂̃��[�g�A�Ď����ɂ���B
     *     �����āA��O�����Ŏw�肵���֐������̐ԍ��؂̃m�[�h�}���p�֐��Ƃ��ēo�^����B
     *     �K����������B
     * @param[in]
     *     tree: �������Ώېԍ���
     *     s   : ���[�g�A�Ď����Ƃ��Ďg�p����m�[�h
     *     i   : �m�[�h�}���p�֐�
     */
    // �����炭�A���݂Ɖߋ��̐ݒ莖����ۑ����Ă����Ă����ɕ����ł���悤�ɂ��邽�߂̂��́H
    ngx_rbtree_init(&cycle->config_dump_rbtree, &cycle->config_dump_sentinel,
                    ngx_str_rbtree_insert_value);

    // �e���X�g�p�[�g�ɉ��̗v�f���i�[���邩�ݒ�i�K�v�ł���ΌÂ��T�C�N������󂯌p���j
    // init_cycle->open_files �͋�Ȃ̂ł����� n = 20
    if (old_cycle->open_files.part.nelts) {
        n = old_cycle->open_files.part.nelts;
        for (part = old_cycle->open_files.part.next; part; part = part->next) {
            n += part->nelts;
        }

    } else {
        n = 20;
    }

    // ���̃T�C�N�����ۗL����v�[�����g���āA�J�����t�@�C�����Ǘ����邽�߂̃��X�g�����蓖�Ă�
    // n = 20
    if (ngx_list_init(&cycle->open_files, pool, n, sizeof(ngx_open_file_t))
        != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // �e���X�g�p�[�g�ɉ��̗v�f���i�[���邩�ݒ�i�K�v�ł���ΌÂ��T�C�N������󂯌p��
    // init_cycle->shared_memory �͋�
    // �Ȃ̂� n = 1 �ŌŒ�
    if (old_cycle->shared_memory.part.nelts) {
        // �����͎��s����Ȃ�
        n = old_cycle->shared_memory.part.nelts;
        for (part = old_cycle->shared_memory.part.next; part; part = part->next)
        {
            n += part->nelts;
        }

    } else {
        n = 1;
    }

    // ���̃T�C�N�����ۗL����v�[�����g���āA���L���������Ǘ����邽�߂̃��X�g�����蓖�Ă�
    // n = 1
    if (ngx_list_init(&cycle->shared_memory, pool, n, sizeof(ngx_shm_zone_t))
        != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // ���������͂P�O
    // ���ϐ��� fd ���Z�b�g����Ă���ƁAinit_cycle �͂����� listening �ɕۗL���Ă���i����͖����j
    n = old_cycle->listening.nelts ? old_cycle->listening.nelts : 10;

    if (ngx_array_init(&cycle->listening, pool, n, sizeof(ngx_listening_t))
        != NGX_OK)
    {
        ngx_destroy_pool(pool);
        return NULL;
    }

    ngx_memzero(cycle->listening.elts, n * sizeof(ngx_listening_t));


    ngx_queue_init(&cycle->reusable_connections_queue);


    cycle->conf_ctx = ngx_pcalloc(pool, ngx_max_module * sizeof(void *));
    if (cycle->conf_ctx == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // �v���Z�b�T�̃z�X�g�����擾����
    // https://kazmax.zpp.jp/cmd/g/gethostname.2.html
    /** 
     * @post
     *     cycle �� hostname �����o�Ƀv���Z�b�T�̃z�X�g�����Z�b�g����Ă���
     */
    if (gethostname(hostname, NGX_MAXHOSTNAMELEN) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "gethostname() failed");
        ngx_destroy_pool(pool);
        return NULL;
    }

    /* on Linux gethostname() silently truncates name that does not fit */

    // �T�C�N���Ƀv���Z�b�T�̃z�X�g�����i�[����
    hostname[NGX_MAXHOSTNAMELEN - 1] = '\0';
    cycle->hostname.len = ngx_strlen(hostname);

    cycle->hostname.data = ngx_pnalloc(pool, cycle->hostname.len);
    if (cycle->hostname.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // �����炭�������ɂ��Ă���H
    ngx_strlow(cycle->hostname.data, (u_char *) hostname, cycle->hostname.len);

    /**
     * @brief
     *     �ÓI�ɒǉ��������W���[�������̃T�C�N���փR�s�[����B
     * @param[in]
     *     cycle: ���W���[�����R�s�[�i�C���X�g�[���j�������T�C�N��
     * @retval
     *     NGX_ERROR: ���s
     *     NGX_OK   : ����
     * @pre
     *     ngx_modules �Ƀ��W���[����񂪃Z�b�g����Ă���
     * @post
     *     cycle �� modules �����o�Ƀ��W���[����񂪃Z�b�g����Ă���
     *     cycle �� modules_n �����o�Ƀ��W���[���̐����Z�b�g����Ă���
     */
    if (ngx_cycle_modules(cycle) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NULL;
    }


    /**
     * @post
     *     cycle �� conf_ctx �����o�Ɋe���W���[���ŗL�̐ݒ莖����
     *     �Ǘ��\���̂ւ̃|�C���^���Z�b�g����Ă���
     */
    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->type != NGX_CORE_MODULE) {
            continue;
        }

        // �R���e�L�X�g�F�֐��e�[�u����\��
        module = cycle->modules[i]->ctx;

        // �e���W���[���ŗL�̐ݒ莖����ێ����邽�߂̍\���̂���`����Ă���Ȃ�
        if (module->create_conf) {
            // �e���W���[���ŗL�̐ݒ莖����ێ����邽�߂̍\���̂��������i���������蓖�āj���Ď擾����
            rv = module->create_conf(cycle);
            if (rv == NULL) {
                ngx_destroy_pool(pool);
                return NULL;
            }
            // �e���W���[�����ƂɌŗL�̔Ԓn�����蓖�Ă��Ă���
            // conf_ctx[�Ԓn] �͊e���W���[���ŗL�̐ݒ��ێ�����\���̂��i�[����
            cycle->conf_ctx[cycle->modules[i]->index] = rv;
        }
    }


    senv = environ;


    ngx_memzero(&conf, sizeof(ngx_conf_t));
    /* STUB: init array ? */
    conf.args = ngx_array_create(pool, 10, sizeof(ngx_str_t));
    if (conf.args == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // �ݒ�̈ꎞ�v�[�������蓖��
    conf.temp_pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);
    if (conf.temp_pool == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    // �ݒ�p�̍\���̂� ctx �̓T�C�N���� conf_ctx ���w���i�܂�`�����W���[���̐ݒ�\���̂������Ă���j
    conf.ctx = cycle->conf_ctx;
    conf.cycle = cycle;
    conf.pool = pool;
    conf.log = log;
    conf.module_type = NGX_CORE_MODULE;
    conf.cmd_type = NGX_MAIN_CONF;

#if 0
    log->log_level = NGX_LOG_DEBUG_ALL;
#endif

    /**
     * @brief
     *     �T�C�N���� conf_param �ɒl�������Ă�����A
     *     �������o�b�t�@�Ɉڂ��ăp�[�X����
     *     �i�ݒ�t�@�C���̃f�B���N�e�B�u���p�[�X������̗v�̂Łj
     * @param[in]
     *     cf: �����Ώۂ̐ݒ�
     * @retval
     *     NGX_CONF_OK: ����
     *     otherwise: ���s
     */
    if (ngx_conf_param(&conf) != NGX_CONF_OK) {
        environ = senv;
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }

    // cycle->conf_file �������ݒ�t�@�C���̑S�f�B���N�e�B�u��K�p����
    if (ngx_conf_parse(&conf, &cycle->conf_file) != NGX_CONF_OK) {
        environ = senv;
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }

    // �e�X�g���̕���Ȃ̂œǂݔ�΂�
    if (ngx_test_config && !ngx_quiet_mode) {
        ngx_log_stderr(0, "the configuration file %s syntax is ok",
                       cycle->conf_file.data);
    }

    // �e�R�A���W���[���ɂ��� init_conf() ���Ăяo���H
    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->type != NGX_CORE_MODULE) {
            continue;
        }

        module = cycle->modules[i]->ctx;

        if (module->init_conf) {
            if (module->init_conf(cycle,
                                  cycle->conf_ctx[cycle->modules[i]->index])
                == NGX_CONF_ERROR)
            {
                environ = senv;
                ngx_destroy_cycle_pools(&conf);
                return NULL;
            }
        }
    }

    // ���̃v���Z�X���V�O�i�����[�h�Ȃ�I���i�ʏ�͂����͔�΂����j
    if (ngx_process == NGX_PROCESS_SIGNALLER) {
        return cycle;
    }

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    // -t �܂��� -T �Ŏ��s���ꂽ
    if (ngx_test_config) {
        // pid �t�@�C���𐶐�
        if (ngx_create_pidfile(&ccf->pid, log) != NGX_OK) {
            goto failed;
        }

    } else if (!ngx_is_init_cycle(old_cycle)) {

        /*
         * we do not create the pid file in the first ngx_init_cycle() call
         * because we need to write the demonized process pid
         */

        old_ccf = (ngx_core_conf_t *) ngx_get_conf(old_cycle->conf_ctx,
                                                   ngx_core_module);
        if (ccf->pid.len != old_ccf->pid.len
            || ngx_strcmp(ccf->pid.data, old_ccf->pid.data) != 0)
        {
            /* new pid file name */

            if (ngx_create_pidfile(&ccf->pid, log) != NGX_OK) {
                goto failed;
            }

            ngx_delete_pidfile(old_cycle);
        }
    }

    if (ngx_test_lockfile(cycle->lock_file.data, log) != NGX_OK) {
        goto failed;
    }

    // �p�X���Ă����̂� "/aaa/bbb" �Ƃ��̂���
    // ����� cycle->paths �ɕۗL����Ă���e�p�X�������ƂɃf�B���N�g���𐶐�����
    // ccf->user ���A���Z�b�g�i����͂����j�Auser �f�B���N�e�B�u�Őݒ肵�Ă��Ȃ��ꍇ�A�͉������Ȃ�
    // old ����̈��p���͂Ȃ���
    // �ȉ��̃��W���[���Q���p�X�� merge_loc �Ő�������
    //   http_scgi
    //   http_fastcgi
    //   http_core
    //   http_uwsgi
    //   http_proxy
    /**
     * @brief
     *     �p�X�ɋL���ꂽ�f�B���N�g���𐶐�����
     *     ���̂��ƁA���̃f�B���N�g���̏��L�҂ƃA�N�Z�X���ɂ��Đݒ肷��
     * @param[in]
     *     cycle: ���̃T�C�N������������ paths �ɂ��āA�f�B���N�g���𐶐�����
     *     user: �p�X�̏��L�҂ƈ�v���邩�ǂ����̌��؂Ɏg�p����
     * @retval
     *     NGX_OK: ����
     *     NGX_ERROR: ���s
     */
    if (ngx_create_paths(cycle, ccf->user) != NGX_OK) {
        goto failed;
    }

    // 
    if (ngx_log_open_default(cycle) != NGX_OK) {
        goto failed;
    }

    /* open the new files */

    /**
     * cycle->open_files ���̃t�@�C���Q���I�[�v�����邾��
     */

    // �I�[�v���t�@�C���Ɋւ��郊�X�g���T�C�N������擾
    // ���O
    part = &cycle->open_files.part;
    file = part->elts;

    // old ����̈��p���͂Ȃ���
    // �ȉ��̃��W���[���Q���p�X�� merge_loc �Ő�������
    //   ngx_log
    //   ngx_http_log
    for (i = 0; /* void */ ; i++) {

        // ���̕������X�g��
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        // ���O����Ȃ玟��
        if (file[i].name.len == 0) {
            continue;
        }

        // �V�X�e���R�[���ɂȂ���(os/unix/ngx_files.h)
        /**
         * @brief
         *     �t�@�C�����J��
         * @param[in]
         *     name: �t�@�C����
         *     mode: �ǂ�ȃ��[�h�ŊJ����
         *     create: �����ō쐬���邩�ǂ���
         *     access: ����
         * @detail
         *     ��Q����
         *         �ǉ��������݃��[�h�iwrite() �̂��тɃI�t�Z�b�g�𖖔��ցj
         *     ��R����
         *         �t�@�C�������݂��Ȃ��ꍇ�Ɏ����ō쐬���郂�[�h
         *     ��S����
         *         �t�@�C���쐬�҂݂̂��ǂݏ����\�A���̑��͓ǂݍ��݂̂݉�
         */
        file[i].fd = ngx_open_file(file[i].name.data,
                                   NGX_FILE_APPEND,
                                   NGX_FILE_CREATE_OR_OPEN,
                                   NGX_FILE_DEFAULT_ACCESS);

        ngx_log_debug3(NGX_LOG_DEBUG_CORE, log, 0,
                       "log: %p %d \"%s\"",
                       &file[i], file[i].fd, file[i].name.data);

        if (file[i].fd == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_open_file_n " \"%s\" failed",
                          file[i].name.data);
            goto failed;
        }

#if !(NGX_WIN32)
        // �t�@�C���f�B�X�N���v�^��ݒ肷�邽�߂̃V�X�e���R�[��
        // �������� F_SETFD, F_GETFD �̏ꍇ�́i�ݒ肩�擾���j
        // ��O�����Ɏw��ł���̂� FD_CLOEXEC �t���O�̂�
        // ������ 1 �Ȃ�Afd �� exec�n�֐�����������Ǝ����ŃN���[�Y�����
        if (fcntl(file[i].fd, F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          "fcntl(FD_CLOEXEC) \"%s\" failed",
                          file[i].name.data);
            goto failed;
        }
#endif
    }

    cycle->log = &cycle->new_log;
    pool->log = &cycle->new_log;


    /* create shared memory */

    /**
     * ���L�������F�Â��T�C�N������󂯌p���������̂��ƁH
     */

    // ���L���������Ǘ����Ă��郊�X�g���T�C�N������擾
    part = &cycle->shared_memory.part;
    shm_zone = part->elts;

    // �����ł��������Ȃ�
    // old ����̈��p���͂Ȃ����A���W���[�����������邱�Ƃ��Ȃ�
    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        if (shm_zone[i].shm.size == 0) {
            ngx_log_error(NGX_LOG_EMERG, log, 0,
                          "zero size shared memory zone \"%V\"",
                          &shm_zone[i].shm.name);
            goto failed;
        }

        // �e���L�������ɃT�C�N���̃��O���Z�b�g
        shm_zone[i].shm.log = cycle->log;

        opart = &old_cycle->shared_memory.part;
        oshm_zone = opart->elts;

        // ���ׂĂ̌Â����L�������ƏƂ炵���킹�āA��v���Ă���������p��
        for (n = 0; /* void */ ; n++) {

            if (n >= opart->nelts) {
                if (opart->next == NULL) {
                    break;
                }
                opart = opart->next;
                oshm_zone = opart->elts;
                n = 0;
            }

            if (shm_zone[i].shm.name.len != oshm_zone[n].shm.name.len) {
                continue;
            }

            if (ngx_strncmp(shm_zone[i].shm.name.data,
                            oshm_zone[n].shm.name.data,
                            shm_zone[i].shm.name.len)
                != 0)
            {
                continue;
            }

            // �Â����L��������
            // �@�E�^�O����v���Ă���
            //   �E�T�C�Y����v���Ă���
            // �@�E���L�������̍ė��p�t���O�������Ă���
            if (shm_zone[i].tag == oshm_zone[n].tag
                && shm_zone[i].shm.size == oshm_zone[n].shm.size
                && !shm_zone[i].noreuse)
            {
                // �Â����L�������̃A�h���X���󂯌p��
                shm_zone[i].shm.addr = oshm_zone[n].shm.addr;
#if (NGX_WIN32)
                // �Â����L�������̃n���h�����󂯌p��
                shm_zone[i].shm.handle = oshm_zone[n].shm.handle;
#endif
                // �Â����L��������
                if (shm_zone[i].init(&shm_zone[i], oshm_zone[n].data)
                    != NGX_OK)
                {
                    goto failed;
                }

                goto shm_zone_found;
            }

            break;
        }

        // �ǂ̌Â����L�������Ƃ���v���Ă��Ȃ������̂ŐV�K�쐬
        if (ngx_shm_alloc(&shm_zone[i].shm) != NGX_OK) {
            goto failed;
        }

        // �v�[�����蓖��
        if (ngx_init_zone_pool(cycle, &shm_zone[i]) != NGX_OK) {
            goto failed;
        }

        // ������
        if (shm_zone[i].init(&shm_zone[i], NULL) != NGX_OK) {
            goto failed;
        }

    shm_zone_found:

        continue;
    }


    /* handle the listening sockets */

    // listen �f�B���N�e�B�u�̃p�[�X�Ő��������P�̃��b�X���������Ώ� "3128"
    if (old_cycle->listening.nelts) {
        ls = old_cycle->listening.elts;
        for (i = 0; i < old_cycle->listening.nelts; i++) {
            ls[i].remain = 0;
        }

        nls = cycle->listening.elts;
        for (n = 0; n < cycle->listening.nelts; n++) {

            for (i = 0; i < old_cycle->listening.nelts; i++) {
                // ��������t���O�������Ă����玟��
                if (ls[i].ignore) {
                    continue;
                }

                // ���̂܂܂ɂ���t���O�������Ă����玟��
                if (ls[i].remain) {
                    continue;
                }

                // �^�C�v����v���Ă��Ȃ��Ȃ玟��
                if (ls[i].type != nls[n].type) {
                    continue;
                }

                // �A�h���X����v���Ă����珈����
                if (ngx_cmp_sockaddr(nls[n].sockaddr, nls[n].socklen,
                                     ls[i].sockaddr, ls[i].socklen, 1)
                    == NGX_OK)
                {
                    nls[n].fd = ls[i].fd;
                    // ���g���ǂ̃\�P�b�g����󂯌p�������̂Ȃ̂��L�^����
                    nls[n].previous = &ls[i];
                    // �󂯌p�����\�P�b�g�́A����Ɏ󂯌p�����邱�Ƃ͂ł��Ȃ�
                    ls[i].remain = 1;

                    // ���b�X���o�b�N���O�̃T�C�Y���قȂ�Ȃ�ēx�A���b�X�������蒼��
                    if (ls[i].backlog != nls[n].backlog) {
                        nls[n].listen = 1;
                    }

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)

                    /*
                     * FreeBSD, except the most recent versions,
                     * could not remove accept filter
                     */
                    nls[n].deferred_accept = ls[i].deferred_accept;

                    if (ls[i].accept_filter && nls[n].accept_filter) {
                        if (ngx_strcmp(ls[i].accept_filter,
                                       nls[n].accept_filter)
                            != 0)
                        {
                            nls[n].delete_deferred = 1;
                            nls[n].add_deferred = 1;
                        }

                    } else if (ls[i].accept_filter) {
                        nls[n].delete_deferred = 1;

                    } else if (nls[n].accept_filter) {
                        nls[n].add_deferred = 1;
                    }
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)

                    if (ls[i].deferred_accept && !nls[n].deferred_accept) {
                        nls[n].delete_deferred = 1;

                    } else if (ls[i].deferred_accept != nls[n].deferred_accept)
                    {
                        nls[n].add_deferred = 1;
                    }
#endif

#if (NGX_HAVE_REUSEPORT)
                    if (nls[n].reuseport && !ls[i].reuseport) {
                        // �Â��T�C�N���Ƃ͈قȂ�A�����[�X�|�[�g��ON�ɂȂ������Ƃ��L�^
                        nls[n].add_reuseport = 1;
                    }
#endif

                    break;
                }
            }

            if (nls[n].fd == (ngx_socket_t) -1) {
                nls[n].open = 1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
                if (nls[n].accept_filter) {
                    nls[n].add_deferred = 1;
                }
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
                if (nls[n].deferred_accept) {
                    nls[n].add_deferred = 1;
                }
#endif
            }
        }

    } else {
        ls = cycle->listening.elts;
        for (i = 0; i < cycle->listening.nelts; i++) {
            ls[i].open = 1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
            if (ls[i].accept_filter) {
                ls[i].add_deferred = 1;
            }
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
            if (ls[i].deferred_accept) {
                ls[i].add_deferred = 1;
            }
#endif
        }
    }

    /**
     * @brief
     *     cycle ���������� listening ���̑S�t�@�C���ɂ��āA���b�X�������݂�
     * @param[in]
     *     cycle: ���̃T�C�N�����ۗL����\�P�b�g�i�t�@�C���j�ɂ��ď������s��
     * @retval
     *     NGX_OK: ����
     *     NGX_ERROR: ���s
     * @detail
     *     �ȉ��̃\�P�b�g�ɂ��Ă͉������Ȃ�
     *     �@�E�����t���O ignore �������Ă���
     *       �E���p���t���O inherited �������Ă���
     *       �Efd �������ł���
     * @OS �ˑ�
     *     ���肾������
     */
    if (ngx_open_listening_sockets(cycle) != NGX_OK) {
        goto failed;
    }

    // �R�}���h���C�������ɂ���� ngx_test_config �� ON �ɂȂ��Ă��Ȃ��ꍇ�i���ғ��j�͎��s
    if (!ngx_test_config) {
        // �e listening ���ۗL����\�P�b�g�ɂ��āA�ݒ���s��
        ngx_configure_listening_sockets(cycle);
    }

    /* commit the new cycle configuration */

    if (!ngx_use_stderr) {
        (void) ngx_log_redirect_stderr(cycle);
    }

    pool->log = cycle->log;

    /**
     * @brief
     *     ���̃T�C�N�����ۗL���邷�ׂẴ��W���[���ɂ��āA�������֐���ۗL���Ă�������s����
     * @param[in]
     *     cycle: ���̃T�C�N���ۗ̕L���郂�W���[���Q���Ώ�
     * @retval
     *     NGX_OK: ����
     *     NGX_ERROR: ���s
     */
    if (ngx_init_modules(cycle) != NGX_OK) {
        /* fatal */
        exit(1);
    }


    /* close and delete stuff that lefts from an old cycle */

    /* free the unnecessary shared memory */

    opart = &old_cycle->shared_memory.part;
    oshm_zone = opart->elts;

    // �����͉������Ȃ�
    for (i = 0; /* void */ ; i++) {

        if (i >= opart->nelts) {
            if (opart->next == NULL) {
                goto old_shm_zone_done;
            }
            opart = opart->next;
            oshm_zone = opart->elts;
            i = 0;
        }

        part = &cycle->shared_memory.part;
        shm_zone = part->elts;

        for (n = 0; /* void */ ; n++) {

            if (n >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                shm_zone = part->elts;
                n = 0;
            }

            if (oshm_zone[i].shm.name.len != shm_zone[n].shm.name.len) {
                continue;
            }

            if (ngx_strncmp(oshm_zone[i].shm.name.data,
                            shm_zone[n].shm.name.data,
                            oshm_zone[i].shm.name.len)
                != 0)
            {
                continue;
            }

            if (oshm_zone[i].tag == shm_zone[n].tag
                && oshm_zone[i].shm.size == shm_zone[n].shm.size
                && !oshm_zone[i].noreuse)
            {
                goto live_shm_zone;
            }

            break;
        }

        ngx_shm_free(&oshm_zone[i].shm);

    live_shm_zone:

        continue;
    }

old_shm_zone_done:


    /* close the unnecessary listening sockets */

    // �������i���ϐ��� fd ���w�肵�Ă��Ȃ���΁j�������Ȃ�
    ls = old_cycle->listening.elts;
    for (i = 0; i < old_cycle->listening.nelts; i++) {

        if (ls[i].remain || ls[i].fd == (ngx_socket_t) -1) {
            continue;
        }

        if (ngx_close_socket(ls[i].fd) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,
                          ngx_close_socket_n " listening socket on %V failed",
                          &ls[i].addr_text);
        }

#if (NGX_HAVE_UNIX_DOMAIN)

        if (ls[i].sockaddr->sa_family == AF_UNIX) {
            u_char  *name;

            name = ls[i].addr_text.data + sizeof("unix:") - 1;

            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                          "deleting socket %s", name);

            if (ngx_delete_file(name) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,
                              ngx_delete_file_n " %s failed", name);
            }
        }

#endif
    }


    /* close the unnecessary open files */

    part = &old_cycle->open_files.part;
    file = part->elts;

    // �������������Ȃ�
    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].fd == NGX_INVALID_FILE || file[i].fd == ngx_stderr) {
            continue;
        }

        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }
    }

    // �ݒ�p�̈ꎞ�v�[�����J��
    ngx_destroy_pool(conf.temp_pool);

    // init_cycle �ɂ��󂯌p���ł������ꍇ�͂����ŏI���I
    if (ngx_process == NGX_PROCESS_MASTER || ngx_is_init_cycle(old_cycle)) {

        // init_cycle �̃v�[�����J���i������ init_cycle ���g�����蓖�Ă��Ă����̂ŁAinit_cycle �������I�ɏ��Łj
        ngx_destroy_pool(old_cycle->pool);
        // init_cycle �ɂ�鏉�����E�����̏ꍇ�� old_cycle �� NULL
        cycle->old_cycle = NULL;

        return cycle;
    }


    if (ngx_temp_pool == NULL) {
        ngx_temp_pool = ngx_create_pool(128, cycle->log);
        if (ngx_temp_pool == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "could not create ngx_temp_pool");
            exit(1);
        }

        n = 10;

        if (ngx_array_init(&ngx_old_cycles, ngx_temp_pool, n,
                           sizeof(ngx_cycle_t *))
            != NGX_OK)
        {
            exit(1);
        }

        ngx_memzero(ngx_old_cycles.elts, n * sizeof(ngx_cycle_t *));

        ngx_cleaner_event.handler = ngx_clean_old_cycles;
        ngx_cleaner_event.log = cycle->log;
        ngx_cleaner_event.data = &dumb;
        dumb.fd = (ngx_socket_t) -1;
    }

    ngx_temp_pool->log = cycle->log;

    old = ngx_array_push(&ngx_old_cycles);
    if (old == NULL) {
        exit(1);
    }
    *old = old_cycle;

    if (!ngx_cleaner_event.timer_set) {
        ngx_add_timer(&ngx_cleaner_event, 30000);
        ngx_cleaner_event.timer_set = 1;
    }

    return cycle;


failed:

    if (!ngx_is_init_cycle(old_cycle)) {
        old_ccf = (ngx_core_conf_t *) ngx_get_conf(old_cycle->conf_ctx,
                                                   ngx_core_module);
        if (old_ccf->environment) {
            environ = old_ccf->environment;
        }
    }

    /* rollback the new cycle configuration */

    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].fd == NGX_INVALID_FILE || file[i].fd == ngx_stderr) {
            continue;
        }

        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }
    }

    /* free the newly created shared memory */

    part = &cycle->shared_memory.part;
    shm_zone = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        if (shm_zone[i].shm.addr == NULL) {
            continue;
        }

        opart = &old_cycle->shared_memory.part;
        oshm_zone = opart->elts;

        for (n = 0; /* void */ ; n++) {

            if (n >= opart->nelts) {
                if (opart->next == NULL) {
                    break;
                }
                opart = opart->next;
                oshm_zone = opart->elts;
                n = 0;
            }

            if (shm_zone[i].shm.name.len != oshm_zone[n].shm.name.len) {
                continue;
            }

            if (ngx_strncmp(shm_zone[i].shm.name.data,
                            oshm_zone[n].shm.name.data,
                            shm_zone[i].shm.name.len)
                != 0)
            {
                continue;
            }

            if (shm_zone[i].tag == oshm_zone[n].tag
                && shm_zone[i].shm.size == oshm_zone[n].shm.size
                && !shm_zone[i].noreuse)
            {
                goto old_shm_zone_found;
            }

            break;
        }

        ngx_shm_free(&shm_zone[i].shm);

    old_shm_zone_found:

        continue;
    }

    if (ngx_test_config) {
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        if (ls[i].fd == (ngx_socket_t) -1 || !ls[i].open) {
            continue;
        }

        if (ngx_close_socket(ls[i].fd) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,
                          ngx_close_socket_n " %V failed",
                          &ls[i].addr_text);
        }
    }

    ngx_destroy_cycle_pools(&conf);

    return NULL;
}


static void
ngx_destroy_cycle_pools(ngx_conf_t *conf)
{
    ngx_destroy_pool(conf->temp_pool);
    ngx_destroy_pool(conf->pool);
}


static ngx_int_t
ngx_init_zone_pool(ngx_cycle_t *cycle, ngx_shm_zone_t *zn)
{
    u_char           *file;
    ngx_slab_pool_t  *sp;

    sp = (ngx_slab_pool_t *) zn->shm.addr;

    if (zn->shm.exists) {

        if (sp == sp->addr) {
            return NGX_OK;
        }

#if (NGX_WIN32)

        /* remap at the required address */

        if (ngx_shm_remap(&zn->shm, sp->addr) != NGX_OK) {
            return NGX_ERROR;
        }

        sp = (ngx_slab_pool_t *) zn->shm.addr;

        if (sp == sp->addr) {
            return NGX_OK;
        }

#endif

        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "shared zone \"%V\" has no equal addresses: %p vs %p",
                      &zn->shm.name, sp->addr, sp);
        return NGX_ERROR;
    }

    sp->end = zn->shm.addr + zn->shm.size;
    sp->min_shift = 3;
    sp->addr = zn->shm.addr;

#if (NGX_HAVE_ATOMIC_OPS)

    file = NULL;

#else

    file = ngx_pnalloc(cycle->pool,
                       cycle->lock_file.len + zn->shm.name.len + 1);
    if (file == NULL) {
        return NGX_ERROR;
    }

    (void) ngx_sprintf(file, "%V%V%Z", &cycle->lock_file, &zn->shm.name);

#endif

    if (ngx_shmtx_create(&sp->mutex, &sp->lock, file) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_slab_init(sp);

    return NGX_OK;
}


ngx_int_t
ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log)
{
    size_t      len;
    ngx_uint_t  create;
    ngx_file_t  file;
    u_char      pid[NGX_INT64_LEN + 2];

    // NGX_PROCESS_SINGLE �܂��� NGX_PROCESS_MASTER �ӊO�Ȃ炱���ŏI��
    if (ngx_process > NGX_PROCESS_MASTER) {
        return NGX_OK;
    }

    ngx_memzero(&file, sizeof(ngx_file_t));

    file.name = *name;
    file.log = log;

    create = ngx_test_config ? NGX_FILE_CREATE_OR_OPEN : NGX_FILE_TRUNCATE;

    // �t�@�C�����J��
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDWR,
                            create, NGX_FILE_DEFAULT_ACCESS);

    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return NGX_ERROR;
    }

    // �t�@�C���� pid ����������
    if (!ngx_test_config) {
        len = ngx_snprintf(pid, NGX_INT64_LEN + 2, "%P%N", ngx_pid) - pid;

        if (ngx_write_file(&file, pid, len, 0) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file.name.data);
    }

    return NGX_OK;
}


void
ngx_delete_pidfile(ngx_cycle_t *cycle)
{
    u_char           *name;
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    name = ngx_new_binary ? ccf->oldpid.data : ccf->pid.data;

    if (ngx_delete_file(name) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", name);
    }
}


ngx_int_t
ngx_signal_process(ngx_cycle_t *cycle, char *sig)
{
    ssize_t           n;
    ngx_pid_t         pid;
    ngx_file_t        file;
    ngx_core_conf_t  *ccf;
    u_char            buf[NGX_INT64_LEN + 2];

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "signal process started");

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    ngx_memzero(&file, sizeof(ngx_file_t));

    file.name = ccf->pid;
    file.log = cycle->log;

    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY,
                            NGX_FILE_OPEN, NGX_FILE_DEFAULT_ACCESS);

    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return 1;
    }

    n = ngx_read_file(&file, buf, NGX_INT64_LEN + 2, 0);

    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file.name.data);
    }

    if (n == NGX_ERROR) {
        return 1;
    }

    while (n-- && (buf[n] == CR || buf[n] == LF)) { /* void */ }

    pid = ngx_atoi(buf, ++n);

    if (pid == (ngx_pid_t) NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "invalid PID number \"%*s\" in \"%s\"",
                      n, buf, file.name.data);
        return 1;
    }

    return ngx_os_signal_process(cycle, sig, pid);

}


/**
 * @brief
 *     �A�g�~�b�N���삪�ł��Ȃ��Ƃ��̂��߂Ƀ��b�N�t�@�C���ɂ��ăe�X�g����
 *     (GCC �̃A�g�~�b�N���삪�g����Ζ��Ȃ��̂ŁA���̏����͊�{�͎g��Ȃ�)
 * @macro
 *     NGX_HAVE_ATOMIC_OPS: ON �Ȃ烍�b�N�t�@�C�����g���K�v���Ȃ��̂ŉ������Ȃ�
 */
static ngx_int_t
ngx_test_lockfile(u_char *file, ngx_log_t *log)
{
#if !(NGX_HAVE_ATOMIC_OPS)
    ngx_fd_t  fd;

    fd = ngx_open_file(file, NGX_FILE_RDWR, NGX_FILE_CREATE_OR_OPEN,
                       NGX_FILE_DEFAULT_ACCESS);

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file);
        return NGX_ERROR;
    }

    if (ngx_close_file(fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file);
    }

    if (ngx_delete_file(file) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", file);
    }

#endif

    return NGX_OK;
}


void
ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user)
{
    ngx_fd_t          fd;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].name.len == 0) {
            continue;
        }

        if (file[i].flush) {
            file[i].flush(&file[i], cycle->log);
        }

        fd = ngx_open_file(file[i].name.data, NGX_FILE_APPEND,
                           NGX_FILE_CREATE_OR_OPEN, NGX_FILE_DEFAULT_ACCESS);

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "reopen file \"%s\", old:%d new:%d",
                       file[i].name.data, file[i].fd, fd);

        if (fd == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          ngx_open_file_n " \"%s\" failed", file[i].name.data);
            continue;
        }

#if !(NGX_WIN32)
        if (user != (ngx_uid_t) NGX_CONF_UNSET_UINT) {
            ngx_file_info_t  fi;

            if (ngx_file_info(file[i].name.data, &fi) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              ngx_file_info_n " \"%s\" failed",
                              file[i].name.data);

                if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                  ngx_close_file_n " \"%s\" failed",
                                  file[i].name.data);
                }

                continue;
            }

            if (fi.st_uid != user) {
                if (chown((const char *) file[i].name.data, user, -1) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                  "chown(\"%s\", %d) failed",
                                  file[i].name.data, user);

                    if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                      ngx_close_file_n " \"%s\" failed",
                                      file[i].name.data);
                    }

                    continue;
                }
            }

            if ((fi.st_mode & (S_IRUSR|S_IWUSR)) != (S_IRUSR|S_IWUSR)) {

                fi.st_mode |= (S_IRUSR|S_IWUSR);

                if (chmod((const char *) file[i].name.data, fi.st_mode) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                  "chmod() \"%s\" failed", file[i].name.data);

                    if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                                      ngx_close_file_n " \"%s\" failed",
                                      file[i].name.data);
                    }

                    continue;
                }
            }
        }

        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "fcntl(FD_CLOEXEC) \"%s\" failed",
                          file[i].name.data);

            if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              ngx_close_file_n " \"%s\" failed",
                              file[i].name.data);
            }

            continue;
        }
#endif

        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }

        file[i].fd = fd;
    }

    (void) ngx_log_redirect_stderr(cycle);
}


ngx_shm_zone_t *
ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name, size_t size, void *tag)
{
    ngx_uint_t        i;
    ngx_shm_zone_t   *shm_zone;
    ngx_list_part_t  *part;

    part = &cf->cycle->shared_memory.part;
    shm_zone = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        if (name->len != shm_zone[i].shm.name.len) {
            continue;
        }

        if (ngx_strncmp(name->data, shm_zone[i].shm.name.data, name->len)
            != 0)
        {
            continue;
        }

        if (tag != shm_zone[i].tag) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                            "the shared memory zone \"%V\" is "
                            "already declared for a different use",
                            &shm_zone[i].shm.name);
            return NULL;
        }

        if (shm_zone[i].shm.size == 0) {
            shm_zone[i].shm.size = size;
        }

        if (size && size != shm_zone[i].shm.size) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                            "the size %uz of shared memory zone \"%V\" "
                            "conflicts with already declared size %uz",
                            size, &shm_zone[i].shm.name, shm_zone[i].shm.size);
            return NULL;
        }

        return &shm_zone[i];
    }

    shm_zone = ngx_list_push(&cf->cycle->shared_memory);

    if (shm_zone == NULL) {
        return NULL;
    }

    shm_zone->data = NULL;
    shm_zone->shm.log = cf->cycle->log;
    shm_zone->shm.addr = NULL;
    shm_zone->shm.size = size;
    shm_zone->shm.name = *name;
    shm_zone->shm.exists = 0;
    shm_zone->init = NULL;
    shm_zone->tag = tag;
    shm_zone->noreuse = 0;

    return shm_zone;
}


static void
ngx_clean_old_cycles(ngx_event_t *ev)
{
    ngx_uint_t     i, n, found, live;
    ngx_log_t     *log;
    ngx_cycle_t  **cycle;

    log = ngx_cycle->log;
    ngx_temp_pool->log = log;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, log, 0, "clean old cycles");

    live = 0;

    cycle = ngx_old_cycles.elts;
    for (i = 0; i < ngx_old_cycles.nelts; i++) {

        if (cycle[i] == NULL) {
            continue;
        }

        found = 0;

        for (n = 0; n < cycle[i]->connection_n; n++) {
            if (cycle[i]->connections[n].fd != (ngx_socket_t) -1) {
                found = 1;

                ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "live fd:%ui", n);

                break;
            }
        }

        if (found) {
            live = 1;
            continue;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "clean old cycle: %ui", i);

        ngx_destroy_pool(cycle[i]->pool);
        cycle[i] = NULL;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "old cycles status: %ui", live);

    if (live) {
        ngx_add_timer(ev, 30000);

    } else {
        ngx_destroy_pool(ngx_temp_pool);
        ngx_temp_pool = NULL;
        ngx_old_cycles.nelts = 0;
    }
}


void
ngx_set_shutdown_timer(ngx_cycle_t *cycle)
{
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ccf->shutdown_timeout) {
        ngx_shutdown_event.handler = ngx_shutdown_timer_handler;
        ngx_shutdown_event.data = cycle;
        ngx_shutdown_event.log = cycle->log;
        ngx_shutdown_event.cancelable = 1;

        ngx_add_timer(&ngx_shutdown_event, ccf->shutdown_timeout);
    }
}


static void
ngx_shutdown_timer_handler(ngx_event_t *ev)
{
    ngx_uint_t         i;
    ngx_cycle_t       *cycle;
    ngx_connection_t  *c;

    cycle = ev->data;

    c = cycle->connections;

    for (i = 0; i < cycle->connection_n; i++) {

        if (c[i].fd == (ngx_socket_t) -1
            || c[i].read == NULL
            || c[i].read->accept
            || c[i].read->channel
            || c[i].read->resolver)
        {
            continue;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                       "*%uA shutdown timeout", c[i].number);

        c[i].close = 1;
        c[i].error = 1;

        c[i].read->handler(c[i].read);
    }
}
