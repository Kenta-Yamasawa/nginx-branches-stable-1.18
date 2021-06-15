
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>

#define NGX_CONF_BUFFER  4096

static ngx_int_t ngx_conf_add_dump(ngx_conf_t *cf, ngx_str_t *filename);
static ngx_int_t ngx_conf_handler(ngx_conf_t *cf, ngx_int_t last);
static ngx_int_t ngx_conf_read_token(ngx_conf_t *cf);
static void ngx_conf_flush_files(ngx_cycle_t *cycle);


static ngx_command_t  ngx_conf_commands[] = {

    // �֌W�Ȃ�
    // �ݒ�t�@�C���Ƃ͕ʂ̐ݒ�t�@�C�����C���N���[�h���邽�߂̃f�B���N�e�B�u
    { ngx_string("include"),
      NGX_ANY_CONF|NGX_CONF_TAKE1,
      ngx_conf_include,
      0,
      0,
      NULL },

      ngx_null_command
};


ngx_module_t  ngx_conf_module = {
    NGX_MODULE_V1,
    NULL,                                  /* module context */
    ngx_conf_commands,                     /* module directives */
    NGX_CONF_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_conf_flush_files,                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


/* The eight fixed arguments */

static ngx_uint_t argument_number[] = {
    NGX_CONF_NOARGS,
    NGX_CONF_TAKE1,
    NGX_CONF_TAKE2,
    NGX_CONF_TAKE3,
    NGX_CONF_TAKE4,
    NGX_CONF_TAKE5,
    NGX_CONF_TAKE6,
    NGX_CONF_TAKE7
};


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
char *
ngx_conf_param(ngx_conf_t *cf)
{
    char             *rv;
    ngx_str_t        *param;
    ngx_buf_t         b;
    ngx_conf_file_t   conf_file;

    param = &cf->cycle->conf_param;

    if (param->len == 0) {
        return NGX_CONF_OK;
    }

    ngx_memzero(&conf_file, sizeof(ngx_conf_file_t));

    ngx_memzero(&b, sizeof(ngx_buf_t));

    b.start = param->data;
    b.pos = param->data;
    b.last = param->data + param->len;
    b.end = b.last;
    b.temporary = 1;

    conf_file.file.fd = NGX_INVALID_FILE;
    conf_file.file.name.data = NULL;
    conf_file.line = 0;

    cf->conf_file = &conf_file;
    cf->conf_file->buffer = &b;

    // cf->cycle->conf_param ���p�[�X����
    rv = ngx_conf_parse(cf, NULL);

    cf->conf_file = NULL;

    return rv;
}


static ngx_int_t
ngx_conf_add_dump(ngx_conf_t *cf, ngx_str_t *filename)
{
    off_t             size;
    u_char           *p;
    uint32_t          hash;
    ngx_buf_t        *buf;
    ngx_str_node_t   *sn;
    ngx_conf_dump_t  *cd;

    // 32bit �̃n�b�V���l�𐶐�
    hash = ngx_crc32_long(filename->data, filename->len);

    // �T�C�N�����ۗL����ݒ�_���v�ԍ��؂���n�b�V���Ō���
    sn = ngx_str_rbtree_lookup(&cf->cycle->config_dump_rbtree, filename, hash);

    // ���������̂Ȃ���Ƀ_���v�����݂���̂ŁA�����_���v������K�v�͂Ȃ�
    if (sn) {
        cf->conf_file->dump = NULL;
        return NGX_OK;
    }

    p = ngx_pstrdup(cf->cycle->pool, filename);
    if (p == NULL) {
        return NGX_ERROR;
    }

    cd = ngx_array_push(&cf->cycle->config_dump);
    if (cd == NULL) {
        return NGX_ERROR;
    }

    // �t�@�C���T�C�Y���擾
    size = ngx_file_size(&cf->conf_file->file.info);

    // �ꎞ�o�b�t�@�𐶐�
    buf = ngx_create_temp_buf(cf->cycle->pool, (size_t) size);
    if (buf == NULL) {
        return NGX_ERROR;
    }

    cd->name.data = p;
    cd->name.len = filename->len;
    cd->buffer = buf;

    cf->conf_file->dump = buf;

    sn = ngx_palloc(cf->temp_pool, sizeof(ngx_str_node_t));
    if (sn == NULL) {
        return NGX_ERROR;
    }

    sn->node.key = hash;
    sn->str = cd->name;

    ngx_rbtree_insert(&cf->cycle->config_dump_rbtree, &sn->node);

    return NGX_OK;
}


/**
 * @brief
 *     �i�߂��������j
 *      �ݒ�t�@�C������͂��āA�Y�����邷�ׂẴR�}���h�����s����
 * @param[in]
 *      cf: �ݒ�������ǂ�\����
 *      filename: �ݒ�t�@�C���̃p�X
 * @retval
 *      NGX_OK: ����
 *      NGX_ERROR: �p�[�X���s
 * @detail
 *      cf ��̃p�����[�^�Ɋi�[�������̂ƁA����Ȃ����̂�����ierror_log �Ȃǁj
 *
 *      type ���[�J���ϐ����J�ڂ��Ȃ��̂ŁA�ꌩ����ƕςȃR�[�h�����A
 *      ���� handler() ���� ngx_conf_parse(cf, NULL) �Ńu���b�N��̓��[�h�Ŗ{���\�b�h���ċA�I�ɓǂ�ł���Ă���
 */
char *
ngx_conf_parse(ngx_conf_t *cf, ngx_str_t *filename)
{
    char             *rv;
    ngx_fd_t          fd;
    ngx_int_t         rc;
    ngx_buf_t         buf;
    ngx_conf_file_t  *prev, conf_file;
    enum {
        parse_file = 0,
        parse_block,
        parse_param
    } type;

#if (NGX_SUPPRESS_WARN)
    fd = NGX_INVALID_FILE;
    prev = NULL;
#endif

    // filename ���w�肳�ꂽ�ꍇ�͐ݒ�t�@�C���̍\�����p�[�X����
    if (filename) {

        /* open configuration file */

        fd = ngx_open_file(filename->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);

        if (fd == NGX_INVALID_FILE) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                               ngx_open_file_n " \"%s\" failed",
                               filename->data);
            return 

NGX_CONF_ERROR;
        }

        // �{�֐��̌Ăяo�����I������ۂɁAcf->conf_file �� prev �Ŗ߂�
        prev = cf->conf_file;

        // �܂��ʂɊJ���Ƃ������ƁH
        cf->conf_file = &conf_file;

        if (ngx_fd_info(fd, &cf->conf_file->file.info) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno,
                          ngx_fd_info_n " \"%s\" failed", filename->data);
        }

        cf->conf_file->buffer = &buf;

        buf.start = ngx_alloc(NGX_CONF_BUFFER, cf->log);
        if (buf.start == NULL) {
            goto failed;
        }

        // ���ׂău���b�N�I���E�t�@�C���I���ŉ�������
        buf.pos = buf.start;
        buf.last = buf.start;
        buf.end = buf.last + NGX_CONF_BUFFER;
        buf.temporary = 1;

        // ���̃t�@�C�����u���b�N�I���E�t�@�C���I���ŕ�����
        cf->conf_file->file.fd = fd;
        cf->conf_file->file.name.len = filename->len;
        cf->conf_file->file.name.data = filename->data;
        cf->conf_file->file.offset = 0;
        cf->conf_file->file.log = cf->log;
        cf->conf_file->line = 1;

        type = parse_file;

        // �_���v�R���t�B�O���L���Ȃ��
        if (ngx_dump_config
#if (NGX_DEBUG)
            || 1
#endif
           )
        {
            if (ngx_conf_add_dump(cf, filename) != NGX_OK) {
                goto failed;
            }

        } else {
            // �L���łȂ��Ȃ�_���v�����
            cf->conf_file->dump = NULL;
        }

    } else if (cf->conf_file->file.fd != NGX_INVALID_FILE) {
        // �ݒ�t�@�C�����w�肳��Ă��Ȃ�
        // and
        // �ݒ�\���̂Ƀt�@�C���f�B�X�N���v�^���o�^����Ă���Ȃ�u���b�N��̓t�F�[�Y
        type = parse_block;

    } else {
        // �ݒ�t�@�C�������w�肳��Ă��Ȃ�
        // and
        // �ݒ�\���̂ɐݒ�t�@�C�����o�^������Ă��Ȃ��Ȃ�p�����[�^��̓t�F�[�Y
        type = parse_param;
    }

    // ���̃��[�v�Őݒ�t�@�C����ǂݐ؂�
    for ( ;; ) {
        // �g�[�N���Q�� cf->args �Ƀv�b�V������
        rc = ngx_conf_read_token(cf);

        /*
         * ngx_conf_read_token() may return
         *
         *    NGX_ERROR             there is error
         *    NGX_OK                the token terminated by ";" was found
         *    NGX_CONF_BLOCK_START  the token terminated by "{" was found
         *    NGX_CONF_BLOCK_DONE   the "}" was found
         *    NGX_CONF_FILE_DONE    the configuration file is done
         */

        if (rc == NGX_ERROR) {
            goto done;
        }

        // BLOCK �̏I���ɓ��B����Ƃ�������A�I������
        if (rc == NGX_CONF_BLOCK_DONE) {

            if (type != parse_block) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"}\"");
                goto failed;
            }

            goto done;
        }

        // �ݒ�t�@�C�����Ō�܂œǂނƏI������
        if (rc == NGX_CONF_FILE_DONE) {

            if (type == parse_block) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "unexpected end of file, expecting \"}\"");
                goto failed;
            }

            goto done;
        }

        if (rc == NGX_CONF_BLOCK_START) {

            if (type == parse_param) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "block directives are not supported "
                                   "in -g option");
                goto failed;
            }
        }

        /* rc == NGX_OK || rc == NGX_CONF_BLOCK_START */

        // NGX_CORE_MODULE �̏ꍇ�͊֌W�Ȃ����ۂ��H
        if (cf->handler) {

            /*
             * the custom handler, i.e., that is used in the http's
             * "types { ... }" directive
             */

            if (rc == NGX_CONF_BLOCK_START) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"{\"");
                goto failed;
            }

            rv = (*cf->handler)(cf, NULL, cf->handler_conf);
            if (rv == NGX_CONF_OK) {
                continue;
            }

            if (rv == NGX_CONF_ERROR) {
                goto failed;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%s", rv);

            goto failed;
        }

        // HTTP ���W���[���ȊO�͂�����
        /**
         * @brief
         *     �T�C�N�����ۗL����S���W���[���̃R�}���h�ɂ��āA���O�� cf->args �̐擪�ƈ�v������̂���������
         *     ��v������̂���������A�`��������ɂ��Č��؂�����
         *     ���Ȃ���΁Acf->ctx[���W���[��ID]�Ɋi�[����Ă��郂�W���[���ŗL�̐ݒ�\���̂����o���āA
         *     �R�}���h�̓o�^���ꂽ set() ���Ăяo���ďI������
         * @param[in]
         *     cf: �ݒ�Ǘ��\���́A��������T�C�N�������ǂ�Aargs�i�g�[�N�����j������������
         *     last: ���̃g�[�N����͂̏��
         *         NGX_ERROR             there is error
         *         NGX_OK                the token terminated by ";" was found
         *         NGX_CONF_BLOCK_START  the token terminated by "{" was found
         *         NGX_CONF_BLOCK_DONE   the "}" was found
         *         NGX_CONF_FILE_DONE    the configuration file is done
         * @retval
         *     NGX_OK: ����
         *     NGX_ERROR: ���s
         */
        rc = ngx_conf_handler(cf, rc);

        if (rc == NGX_ERROR) {
            goto failed;
        }
    }

failed:

    rc = NGX_ERROR;

done:
    // ��n��

    // �����A�V�����ݒ�t�@�C����ǂ�ł����Ȃ�
    if (filename) {
        if (cf->conf_file->buffer->start) {
            ngx_free(cf->conf_file->buffer->start);
        }

        if (ngx_close_file(fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                          ngx_close_file_n " %s failed",
                          filename->data);
            rc = NGX_ERROR;
        }

        // ���Ƃ��ƕێ����Ă������֖̂߂�
        cf->conf_file = prev;
    }

    if (rc == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/**
 * @brief
 *     �T�C�N�����ۗL����S���W���[���̃R�}���h�ɂ��āA���O�� cf->args �̐擪�ƈ�v������̂���������
 *     ��v������̂���������A�`��������ɂ��Č��؂�����
 *     ���Ȃ���΁Acf->ctx[���W���[��ID]�Ɋi�[����Ă��郂�W���[���ŗL�̐ݒ�\���̂����o���āA
 *     �R�}���h�̓o�^���ꂽ set() ���Ăяo���ďI������
 * @param[in]
 *     cf: �ݒ�Ǘ��\���́A��������T�C�N�������ǂ�Aargs�i�g�[�N�����j������������
 *     last: ���̃g�[�N����͂̏��
 *         NGX_ERROR             there is error
 *         NGX_OK                the token terminated by ";" was found
 *         NGX_CONF_BLOCK_START  the token terminated by "{" was found
 *         NGX_CONF_BLOCK_DONE   the "}" was found
 *         NGX_CONF_FILE_DONE    the configuration file is done
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
 */
static ngx_int_t
ngx_conf_handler(ngx_conf_t *cf, ngx_int_t last)
{
    char           *rv;
    void           *conf, **confp;
    ngx_uint_t      i, found;
    ngx_str_t      *name;
    ngx_command_t  *cmd;

    // args �̐擪�v�f���擾
    name = cf->args->elts;

    found = 0;

    // �e���W���[���̓R�}���h�i������j�Őݒ�t�@�C���ɂ�����ݒ莯�ʎq��ۗL����
    // ��v���Ă����珈���ɂ�����
    // (�S���W���[���̑S�R�}���h���璀��A������������̂��E�E�E���j
    for (i = 0; cf->cycle->modules[i]; i++) {

        cmd = cf->cycle->modules[i]->commands;
        // ���̃��W���[���̃R�}���h���Ȃ��A���̃��W���[����
        if (cmd == NULL) {
            continue;
        }

        // �e�R�}���h�ɂ���
        for ( /* void */ ; cmd->name.len; cmd++) {

            if (name->len != cmd->name.len) {
                continue;
            }

            if (ngx_strcmp(name->data, cmd->name.data) != 0) {
                continue;
            }

            // ���������i��v���Ă����j
            found = 1;

            /**
             * ���@NGX_CONF_MODULE �� include �f�B���N�e�B�u�̂ݑΉ�
             *     �܂��A�v�͂ǂ̃��W���[���^�C�v�ł������Ƃ��Ă��A��邱�Ƃ͕ς��Ȃ��̂ŁA
             *     �܂�Ahttp���W���[���� include �f�B���N�e�B�u�ł����Ă��A�ق��̃��W���[�������Ƀf�B���N�e�B�u���������Ƃ��ł���̂ł��낤?
             */
            // NGX_CONF_MODULE �p�̐ݒ莖���ł͂Ȃ���
            //   ����
            // ���������͂������ǁA�z��^�C�v����v���Ȃ������ihttp���W���[����include �ƃR�A���W���[����include �Ȃǁj
            // �Ƃ΂�
            if (cf->cycle->modules[i]->type != NGX_CONF_MODULE
                && cf->cycle->modules[i]->type != cf->module_type)
            {
                continue;
            }

            /* is the directive's location right ? */

            // �R�}���h�̃^�C�v�ɂ��Ă����S��v�����ׂ�
            if (!(cmd->type & cf->cmd_type)) {
                continue;
            }

            // ���̃R�}���h�̓p�����^�`���Ȃ̂ɁA�p�����^��͏�Ԃł͂Ȃ�����
            if (!(cmd->type & NGX_CONF_BLOCK) && last != NGX_OK) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                  "directive \"%s\" is not terminated by \";\"",
                                  name->data);
                return NGX_ERROR;
            }

            // ���̃R�}���h�̓u���b�N�`���Ȃ̂ɁA�u���b�N��͏�Ԃł͂Ȃ�����
            if ((cmd->type & NGX_CONF_BLOCK) && last != NGX_CONF_BLOCK_START) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "directive \"%s\" has no opening \"{\"",
                                   name->data);
                return NGX_ERROR;
            }

            /* is the directive's argument count right ? */

            // �R�}���h���Ƃɒ�`���ꂽ�����̐������ƂɁA�����������؂���
            if (!(cmd->type & NGX_CONF_ANY)) {

                if (cmd->type & NGX_CONF_FLAG) {

                    if (cf->args->nelts != 2) {
                        goto invalid;
                    }

                } else if (cmd->type & NGX_CONF_1MORE) {

                    if (cf->args->nelts < 2) {
                        goto invalid;
                    }

                } else if (cmd->type & NGX_CONF_2MORE) {

                    if (cf->args->nelts < 3) {
                        goto invalid;
                    }

                } else if (cf->args->nelts > NGX_CONF_MAX_ARGS) {

                    goto invalid;

                } else if (!(cmd->type & argument_number[cf->args->nelts - 1]))
                {
                    goto invalid;
                }
            }

            /* set up the directive's configuration context */

            conf = NULL;

            if (cmd->type & NGX_DIRECT_CONF) {
                conf = ((void **) cf->ctx)[cf->cycle->modules[i]->index];

            } else if (cmd->type & NGX_MAIN_CONF) {
                conf = &(((void **) cf->ctx)[cf->cycle->modules[i]->index]);

            } else if (cf->ctx) {
                confp = *(void **) ((char *) cf->ctx + cmd->conf);

                if (confp) {
                    conf = confp[cf->cycle->modules[i]->ctx_index];
                }
            }

            // �����ŃR�}���h���L�̃Z�b�g�R�}���h���Ăяo��
            rv = cmd->set(cf, cmd, conf);

            if (rv == NGX_CONF_OK) {
                return NGX_OK;
            }

            if (rv == NGX_CONF_ERROR) {
                return NGX_ERROR;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"%s\" directive %s", name->data, rv);

            return NGX_ERROR;
        }
    }

    if (found) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"%s\" directive is not allowed here", name->data);

        return NGX_ERROR;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "unknown directive \"%s\"", name->data);

    return NGX_ERROR;

invalid:

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid number of arguments in \"%s\" directive",
                       name->data);

    return NGX_ERROR;
}


/**
 * @brief
 *     �g�[�N���P�ʂŐ؂�o���� cf->args �Ƀv�b�V������
 * @detail
 *     worker_processes  1;
 *
 *     events {
 *         worker_connections  1024;
 *     }
 *
 *     http {
 *         include       mime.types;
 *         default_type  application/octet-stream;
 *
 *         sendfile        on;
 *
 *         keepalive_timeout  65;
 *
 *         server {
 *             listen       80;
 *             server_name  localhost;
 *
 *             location / {
 *                 root   html;
 *                 index  index.html index.htm;
 *             }
 *
 *             error_page   500 502 503 504  /50x.html;
 *             location = /50x.html {
 *                 root   html;
 *             }
 *         }
 *     }
 *
 *
 *     �Ăяo�����тɁ��̂悤�� args �֓˂����܂��
 *       worker_processes 1 (NGX_OK)
 *       events (NGX_CONF_BLOCK_START)
 *       worker_connections 1024 (NGX_OK)
 *       (NGX_CONF_BLOCK_DONE)
 *       http (NGX_CONF_BLOCK_START)
 *       include mime.types (NGX_OK)
 *       default_type application/octet-stream (NGX_OK)
 *       sendfile on (NGX_OK)
 *       keepalive_timeout 65 (NGX_OK)
 *       server (NGX_CONF_BLOCK_START)
 *       listen 80 (NGX_OK)
 *       server_name localhost (NGX_OK)
 *       location / (NGX_CONF_BLOCK_START)
 *       root html (NGX_OK)
 *       index index.html index.htm (NGX_OK)
 *       (NGX_CONF_BLOCK_DONE)
 *       error_page 500 502 503 504 /50x.html
 *       location = /50x.html (NGX_CONF_BLOCK_START)
 *       root html (NGX_OK
 *       (NGX_CONF_BLOCK_DONE)
 *       (NGX_CONF_BLOCK_DONE)
 *       (NGX_CONF_BLOCK_DONE)
 */
static ngx_int_t
ngx_conf_read_token(ngx_conf_t *cf)
{
    u_char      *start, ch, *src, *dst;
    off_t        file_size;
    size_t       len;
    ssize_t      n, size;
    ngx_uint_t   found, need_space, last_space, sharp_comment, variable;
    ngx_uint_t   quoted, s_quoted, d_quoted, start_line;
    ngx_str_t   *word;
    ngx_buf_t   *b, *dump;

    found = 0;
    need_space = 0;
    last_space = 1;
    sharp_comment = 0;
    variable = 0;
    quoted = 0;
    s_quoted = 0;
    d_quoted = 0;

    // �ĂԂ��т� nelts �� 0 �ɐݒ肳���
    cf->args->nelts = 0;
    b = cf->conf_file->buffer;
    dump = cf->conf_file->dump;
    start = b->pos;
    start_line = cf->conf_file->line;

    // �ݒ�t�@�C���̃T�C�Y���擾
    file_size = ngx_file_size(&cf->conf_file->file.info);

    for ( ;; ) {

        // �o�b�t�@�̍Ō�ɓ��B����
        if (b->pos >= b->last) {

            // �ݒ�t�@�C���̓ǂݍ��݂��������Ă���
            // �p�[�X�p�����[�^�̉�͂��ƕK�� true �i�ǂ�����O�ŏ���������Ă���j
            if (cf->conf_file->file.offset >= file_size) {

                // �܂��v�f���c���Ă��� or ��͓r���Ȃ̂Ƀo�b�t�@���I���̂͂�������
                if (cf->args->nelts > 0 || !last_space) {

                    if (cf->conf_file->file.fd == NGX_INVALID_FILE) {
                        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                           "unexpected end of parameter, "
                                           "expecting \";\"");
                        return NGX_ERROR;
                    }

                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                  "unexpected end of file, "
                                  "expecting \";\" or \"}\"");
                    return NGX_ERROR;
                }

                // �����p�[�X�ł����̂ŁA�P�ɓǂݏI��������̂Ƃ��Đ���I��
                return NGX_CONF_FILE_DONE;
            }

            len = b->pos - start;

            if (len == NGX_CONF_BUFFER) {
                cf->conf_file->line = start_line;

                if (d_quoted) {
                    ch = '"';

                } else if (s_quoted) {
                    ch = '\'';

                } else {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "too long parameter \"%*s...\" started",
                                       10, start);
                    return NGX_ERROR;
                }

                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "too long parameter, probably "
                                   "missing terminating \"%c\" character", ch);
                return NGX_ERROR;
            }

            if (len) {
                ngx_memmove(b->start, start, len);
            }

            size = (ssize_t) (file_size - cf->conf_file->file.offset);

            if (size > b->end - (b->start + len)) {
                size = b->end - (b->start + len);
            }

            n = ngx_read_file(&cf->conf_file->file, b->start + len, size,
                              cf->conf_file->file.offset);

            if (n == NGX_ERROR) {
                return NGX_ERROR;
            }

            if (n != size) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   ngx_read_file_n " returned "
                                   "only %z bytes instead of %z",
                                   n, size);
                return NGX_ERROR;
            }

            b->pos = b->start + len;
            b->last = b->pos + n;
            start = b->start;

            if (dump) {
                dump->last = ngx_cpymem(dump->last, b->pos, size);
            }
        }

        // ���A�w���Ă��镶�����擾���āA pos �͎��̕������w���悤�C���N�������g����
        ch = *b->pos++;

        if (ch == LF) {
            // ���s���J�E���g����
            cf->conf_file->line++;

            // �R�����g�s���I������̂ŁA�R�����g�s�t���O��߂�
            if (sharp_comment) {
                sharp_comment = 0;
            }
        }

        // ���̓R�����g�s��ǂ�ł���̂Ŗ������Ď���
        if (sharp_comment) {
            continue;
        }

        // �f�B���N�g���N�I�[�g�t���O�������Ă����烊�Z�b�g���Ď���
        if (quoted) {
            quoted = 0;
            continue;
        }

        // �V���O���N�I�[�g��_�u���N�I�[�g�̏I���i�X�y�[�X���K�v�j
        // ""       
        // "":
        // ""{
        // "")
        // ���ȊO�̃p�^�[�����G���[�Ƃ��Ă͂���
        if (need_space) {
            // �X�y�[�X���������̂ŉ���
            if (ch == ' ' || ch == '\t' || ch == CR || ch == LF) {
                last_space = 1;
                need_space = 0;
                continue;
            }

            // �Z�~�R�����i�s���j
            if (ch == ';') {
                return NGX_OK;
            }

            // ��͂����g�[�N���̓u���b�N���ł���
            if (ch == '{') {
                return NGX_CONF_BLOCK_START;
            }

            // �Ȃ񂩒m��񂯂ǁA()�̒���"" �� '' �������Ă���P�[�X���F�߂ėv����ۂ�
            if (ch == ')') {
                last_space = 1;
                need_space = 0;

            } else {
                // �X�y�[�X������ׂ������ɃX�y�[�X���Ȃ�����
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "unexpected \"%c\"", ch);
                return NGX_ERROR;
            }
        }

        // ���O���X�y�[�X�������ꍇ�i�ǂݎn�߂���������X�^�[�g�j
        // �܂�Alast_space == 0 �͉����̃p�^�[���ɑ������ĉ�͓r���ł��邱�Ƃ�����
        if (last_space) {
            // �O�ɃC���N�������g�����̂ŁA���A�w���Ă��镶���ɂ��Ă� -1 ����K�v����
            start = b->pos - 1;
            // ���s�ڂ�
            start_line = cf->conf_file->line;

            // �X�y�[�X������
            if (ch == ' ' || ch == '\t' || ch == CR || ch == LF) {
                continue;
            }

            switch (ch) {

            // �܂��X�y�[�X���
            case ';':
            case '{':
                // �܂������g�[�N�����o�ꂵ�Ă��Ȃ��̂ɁA�u���b�N���n�܂�����s���I�����N����̂͂�������
                if (cf->args->nelts == 0) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "unexpected \"%c\"", ch);
                    return NGX_ERROR;
                }

                // ����̓u���b�N�̃X�^�[�g�ł���
                if (ch == '{') {
                    return NGX_CONF_BLOCK_START;
                }

                // �s������������
                return NGX_OK;

            // �܂��X�y�[�X���
            case '}':
                // nelts != 0 �i�Ȃ񂩃g�[�N�����������Ă܂��I����Ă��Ȃ��j�̂� } ���o�ꂷ��̂͂�������
                if (cf->args->nelts != 0) {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                       "unexpected \"}\"");
                    return NGX_ERROR;
                }

                return NGX_CONF_BLOCK_DONE;

            // �R�����g�s�t���O�𗧂Ă�i�ȍ~�A���s�܂ŃX�L�b�v����j
            case '#':
                sharp_comment = 1;
                continue;

            // �f�B���N�g���N�I�[�g�iC://home/kenta �Ƃ��� / �̂���)
            case '\\':
                quoted = 1;
                last_space = 0;
                continue;

            // �_�u�������[�Ƃɂ��͂��J�n�t���O�𗧂Ă�
            case '"':
                start++;
                d_quoted = 1;
                last_space = 0;
                continue;

            // �V���O�������[�Ƃɂ��͂��J�n�t���O�𗧂Ă�
            case '\'':
                start++;
                s_quoted = 1;
                last_space = 0;
                continue;

            // �ϐ��錾�ӂ炮�𗧂Ă�
            case '$':
                variable = 1;
                last_space = 0;
                continue;

            // ���ɂ���X�y�[�X�ł͂Ȃ�����
            default:
                last_space = 0;
            }

        } else {
            // �ϐ��錾�t���O�������Ă��āi$�̌�j�A������ '{' �������ꍇ�̓G���[�ł͂Ȃ�
            // (�ϐ��錾�ł͂Ȃ��̂� { �����邱�Ƃ͂Ȃ��̂ŕ��@�G���[)
            if (ch == '{' && variable) {
                continue;
            }

            variable = 0;

            if (ch == '\\') {
                quoted = 1;
                continue;
            }

            if (ch == '$') {
                variable = 1;
                continue;
            }

            if (d_quoted) {
                if (ch == '"') {
                    d_quoted = 0;
                    need_space = 1;
                    found = 1;
                }

            } else if (s_quoted) {
                if (ch == '\'') {
                    s_quoted = 0;
                    need_space = 1;
                    found = 1;
                }

            } else if (ch == ' ' || ch == '\t' || ch == CR || ch == LF
                       || ch == ';' || ch == '{')
            {
                last_space = 1;
                found = 1;
            }

            // ���A��͂��Ă���g�[�N���̏I��肪��������
            if (found) {
                // cf->args �z��Ŏ��Ɋ��蓖�Ă�ׂ��̈���擾����
                word = ngx_array_push(cf->args);
                if (word == NULL) {
                    return NGX_ERROR;
                }

                // �g�[�N���̃T�C�Y�����v�[�����犄�蓖�Ă�i�����̒����{�k�������j
                word->data = ngx_pnalloc(cf->pool, b->pos - 1 - start + 1);
                if (word->data == NULL) {
                    return NGX_ERROR;
                }

                // �ݒ�t�@�C������ cf->args �ֈڂ�
                for (dst = word->data, src = start, len = 0;
                     src < b->pos - 1;
                     len++)
                {
                    // \" \\ \\\ \t \r \n �ɂ��čl��
                    if (*src == '\\') {
                        switch (src[1]) {
                        case '"':
                        case '\'':
                        case '\\':
                            src++;
                            break;

                        case 't':
                            *dst++ = '\t';
                            src += 2;
                            continue;

                        case 'r':
                            *dst++ = '\r';
                            src += 2;
                            continue;

                        case 'n':
                            *dst++ = '\n';
                            src += 2;
                            continue;
                        }

                    }
                    *dst++ = *src++;
                }
                // �k�������ƒ�����ݒ�
                *dst = '\0';
                word->len = len;

                // �Z�~�R�����ōs�����I������Ƃ�����
                if (ch == ';') {
                    return NGX_OK;
                }

                // ��������u���b�N�������i�܂荡�A�p�[�X�����g�[�N���̓u���b�N����\���j
                if (ch == '{') {
                    return NGX_CONF_BLOCK_START;
                }

                found = 0;
            }
        }
    }
}


/**
 * @brief
 *     �i�p�X�� * ? [ ���܂܂�Ă����ꍇ�̏����͔�΂����j
 *      �ʂ̐ݒ�t�@�C���������Ŏw��ł���
 *      include �f�B���N�e�B�u�̃R�}���h����
 * @param[in:out]
 *     cf: �ݒ�t�@�C����\���\���́i�g�[�N����ۗL����j
 * @param[in]
 *     cmd: �R�}���h
 *     conf: �ݒ�l
 */
char *
ngx_conf_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char        *rv;
    ngx_int_t    n;
    ngx_str_t   *value, file, name;
    ngx_glob_t   gl;

    value = cf->args->elts;
    file = value[1];

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

    /**
     * @brief
     *     �K�v�ł���΁A�v���t�B�b�N�X�ƌ��������p�X����������֕Ԃ�
     */
    if (ngx_conf_full_name(cf->cycle, &file, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    // �p�X������ * ? [ �̂�����̕����������ł��Ȃ������ꍇ
    if (strpbrk((char *) file.data, "*?[") == NULL) {

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        // �ςȃp�X�ł͂Ȃ��̂ŁA�p�X�Ŏw�肳�ꂽ�ʐݒ�t�@�C���ɂ��āA��͂��ēK�p
        /**
         * @brief
         *     �i�߂��������j
         *      �ݒ�t�@�C������͂��āA�Y�����邷�ׂẴR�}���h�����s����
         * @param[in]
         *      cf: �ݒ�������ǂ�\����
         *      filename: �ݒ�t�@�C���̃p�X
         * @retval
         *      NGX_OK: ����
         *      NGX_ERROR: �p�[�X���s
         * @detail
         *      cf ��̃p�����[�^�Ɋi�[�������̂ƁA����Ȃ����̂�����ierror_log �Ȃǁj
         *
         *      type ���[�J���ϐ����J�ڂ��Ȃ��̂ŁA�ꌩ����ƕςȃR�[�h�����A
         *      ���� handler() ���� ngx_conf_parse(cf, NULL) �Ńu���b�N��̓��[�h�Ŗ{���\�b�h���ċA�I�ɓǂ�ł���Ă���
         */
        return ngx_conf_parse(cf, &file);
    }

    // glob �̓p�^�[���}�b�`���O�֘A
    ngx_memzero(&gl, sizeof(ngx_glob_t));

    gl.pattern = file.data;
    gl.log = cf->log;
    gl.test = 1;

    if (ngx_open_glob(&gl) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_open_glob_n " \"%s\" failed", file.data);
        return NGX_CONF_ERROR;
    }

    rv = NGX_CONF_OK;

    for ( ;; ) {
        n = ngx_read_glob(&gl, &name);

        if (n != NGX_OK) {
            break;
        }

        file.len = name.len++;
        file.data = ngx_pstrdup(cf->pool, &name);
        if (file.data == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        rv = ngx_conf_parse(cf, &file);

        if (rv != NGX_CONF_OK) {
            break;
        }
    }

    ngx_close_glob(&gl);

    return rv;
}


/**
 * @brief
 *     �K�v�ł���΁A�v���t�B�b�N�X�ƌ��������p�X����Ԃ�
 */
ngx_int_t
ngx_conf_full_name(ngx_cycle_t *cycle, ngx_str_t *name, ngx_uint_t conf_prefix)
{
    ngx_str_t  *prefix;

    // �ݒ�t�@�C���p�̃v���t�B�N�X�����݂���Ȃ炻������g��
    prefix = conf_prefix ? &cycle->conf_prefix : &cycle->prefix;

    return ngx_get_full_name(cycle->pool, prefix, name);
}


/**
 * @brief
 *     ��Q���� name �Ŏw�肳�ꂽ�t�@�C���� cycle->open_files �Ƀv�b�V������
 *     name �� NULL �̏ꍇ�͕W���G���[�o�͂��J���� cycle->open_files ���X�g�Ƀv�b�V������
 *     name �ƊY������t�@�C�����Ȃ������ꍇ�� fd �� NGX_INVALID_FD ���Z�b�g���ꂽ�t�@�C�����Ԃ�
 *     cycle->open_files �ɂ���t�@�C���Q�� init_cycle() ���ŃI�[�v�������
 * @param[in]
 *     cycle: ���̃T�C�N�����ۗL����I�[�v���t�@�C���Q����A���O�p�̏o�͐�t�@�C����T��
 *     name: ���O�p�̏o�͐�t�@�C����
 * @retval
 *     NULL: �v���I�Ȏ��s
 *     otherwise: ���O�p�̏o�͐�t�@�C��
 *                �Ȃ��Afd �� INVALID_FILE �Ƃ��ĕς��邱�Ƃ�����i�T�������ǌ�����Ȃ������ꍇ�j
 * @detail
 *     �����ŁA�T�C�N�������ݒ�t�@�C���p�̃v���t�B�N�X����p���ăp�X���w�肷��
 *     �����Ŏ��ۂɊJ���̂ł͂Ȃ��A���łɊJ����Ă��� cycle->open_files ���̃t�@�C�����疼�O���Y��������̂�Ԃ�����
 */
ngx_open_file_t *
ngx_conf_open_file(ngx_cycle_t *cycle, ngx_str_t *name)
{
    ngx_str_t         full;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

#if (NGX_SUPPRESS_WARN)
    ngx_str_null(&full);
#endif

    if (name->len) {
        full = *name;

        /**
         * @brief
         *     ��3�����p�X����΃p�X���ǂ��������؂��āA�����łȂ��Ȃ��Q�����̃v���t�B�N�X�ƌ�������
         * @param[in:out]
         *     name: �����Ώۂ̃p�X��
         * @param[in]
         *     pool: �V���������p�X����ۑ������̃v�[��
         *     prefix: ��������v���t�B�b�N�X
         * @retval
         *     NGX_OK: ����
         *     NGX_ERROR: ���s
         * @detail
         *     ���ł� name ����΃p�X�Ȃ牽�������ɏI������
         *     name ����΃p�X�ł͂Ȃ��Ȃ� prefix �ƌ�������
         */
        if (ngx_conf_full_name(cycle, &full, 0) != NGX_OK) {
            return NULL;
        }

        // �擪�̕������X�g���擾
        part = &cycle->open_files.part;
        file = part->elts;

        // �T�C�N�����ۗL����t�@�C���̂����A��v������̂�����΂����Ԃ�
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

            if (full.len != file[i].name.len) {
                continue;
            }

            if (ngx_strcmp(full.data, file[i].name.data) == 0) {
                return &file[i];
            }
        }
    }

    // ���Ƀv�b�V�����ׂ��A�h���X���擾
    file = ngx_list_push(&cycle->open_files);
    if (file == NULL) {
        return NULL;
    }

    if (name->len) {
        file->fd = NGX_INVALID_FILE;
        file->name = full;

    } else {
        file->fd = ngx_stderr;
        file->name = *name;
    }

    file->flush = NULL;
    file->data = NULL;

    return file;
}


/**
 * @brief
 *     �T�C�N�����ۗL���� open_files ���X�g�̂��ׂẴt�@�C���ɂ��āA�ۗL���Ă���Ȃ� flush() ���Ăяo��
 *     ���Ԃ�A���ׂẴt�@�C���ɂ��āA�o�b�t�@�Ɏc���Ă�����̂�����΁A�����ɂ��ׂď����o�����鏈���H
 * @param[in]
 *     cycle: �ΏۃT�C�N��
 */
static void
ngx_conf_flush_files(ngx_cycle_t *cycle)
{
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "flush files");

    // �I�[�v���t�@�C�����X�g�̐擪�������X�g���擾
    part = &cycle->open_files.part;
    file = part->elts;

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

        // �t�@�C���I�u�W�F�N�g�Ƀt���b�V�����\�b�h����`����Ă�����A��ĂɌĂяo��
        if (file[i].flush) {
            file[i].flush(&file[i], cycle->log);
        }
    }
}


void ngx_cdecl
ngx_conf_log_error(ngx_uint_t level, ngx_conf_t *cf, ngx_err_t err,
    const char *fmt, ...)
{
    u_char   errstr[NGX_MAX_CONF_ERRSTR], *p, *last;
    va_list  args;

    last = errstr + NGX_MAX_CONF_ERRSTR;

    va_start(args, fmt);
    p = ngx_vslprintf(errstr, last, fmt, args);
    va_end(args);

    if (err) {
        p = ngx_log_errno(p, last, err);
    }

    if (cf->conf_file == NULL) {
        ngx_log_error(level, cf->log, 0, "%*s", p - errstr, errstr);
        return;
    }

    if (cf->conf_file->file.fd == NGX_INVALID_FILE) {
        ngx_log_error(level, cf->log, 0, "%*s in command line",
                      p - errstr, errstr);
        return;
    }

    ngx_log_error(level, cf->log, 0, "%*s in %s:%ui",
                  p - errstr, errstr,
                  cf->conf_file->file.name.data, cf->conf_file->line);
}


char *
ngx_conf_set_flag_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t        *value;
    ngx_flag_t       *fp;
    ngx_conf_post_t  *post;

    fp = (ngx_flag_t *) (p + cmd->offset);

    if (*fp != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        *fp = 1;

    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
        *fp = 0;

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                     "invalid value \"%s\" in \"%s\" directive, "
                     "it must be \"on\" or \"off\"",
                     value[1].data, cmd->name.data);
        return NGX_CONF_ERROR;
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, fp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_str_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t        *field, *value;
    ngx_conf_post_t  *post;

    field = (ngx_str_t *) (p + cmd->offset);

    if (field->data) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *field = value[1];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, field);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_str_array_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t         *value, *s;
    ngx_array_t      **a;
    ngx_conf_post_t   *post;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NGX_CONF_UNSET_PTR) {
        *a = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    s = ngx_array_push(*a);
    if (s == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    *s = value[1];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, s);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_keyval_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t         *value;
    ngx_array_t      **a;
    ngx_keyval_t      *kv;
    ngx_conf_post_t   *post;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NULL) {
        *a = ngx_array_create(cf->pool, 4, sizeof(ngx_keyval_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    kv = ngx_array_push(*a);
    if (kv == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    kv->key = value[1];
    kv->value = value[2];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, kv);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_num_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_int_t        *np;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    np = (ngx_int_t *) (p + cmd->offset);

    if (*np != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;
    *np = ngx_atoi(value[1].data, value[1].len);
    if (*np == NGX_ERROR) {
        return "invalid number";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, np);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_size_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    size_t           *sp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    sp = (size_t *) (p + cmd->offset);
    if (*sp != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *sp = ngx_parse_size(&value[1]);
    if (*sp == (size_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, sp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_off_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    off_t            *op;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    op = (off_t *) (p + cmd->offset);
    if (*op != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *op = ngx_parse_offset(&value[1]);
    if (*op == (off_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, op);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_msec_t       *msp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    msp = (ngx_msec_t *) (p + cmd->offset);
    if (*msp != NGX_CONF_UNSET_MSEC) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *msp = ngx_parse_time(&value[1], 0);
    if (*msp == (ngx_msec_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, msp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_sec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    time_t           *sp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    sp = (time_t *) (p + cmd->offset);
    if (*sp != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *sp = ngx_parse_time(&value[1], 1);
    if (*sp == (time_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, sp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_bufs_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *p = conf;

    ngx_str_t   *value;
    ngx_bufs_t  *bufs;


    bufs = (ngx_bufs_t *) (p + cmd->offset);
    if (bufs->num) {
        return "is duplicate";
    }

    value = cf->args->elts;

    bufs->num = ngx_atoi(value[1].data, value[1].len);
    if (bufs->num == NGX_ERROR || bufs->num == 0) {
        return "invalid value";
    }

    bufs->size = ngx_parse_size(&value[2]);
    if (bufs->size == (size_t) NGX_ERROR || bufs->size == 0) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_enum_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_uint_t       *np, i;
    ngx_str_t        *value;
    ngx_conf_enum_t  *e;

    np = (ngx_uint_t *) (p + cmd->offset);

    if (*np != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;
    e = cmd->post;

    for (i = 0; e[i].name.len != 0; i++) {
        if (e[i].name.len != value[1].len
            || ngx_strcasecmp(e[i].name.data, value[1].data) != 0)
        {
            continue;
        }

        *np = e[i].value;

        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid value \"%s\"", value[1].data);

    return NGX_CONF_ERROR;
}


char *
ngx_conf_set_bitmask_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_uint_t          *np, i, m;
    ngx_str_t           *value;
    ngx_conf_bitmask_t  *mask;


    np = (ngx_uint_t *) (p + cmd->offset);
    value = cf->args->elts;
    mask = cmd->post;

    for (i = 1; i < cf->args->nelts; i++) {
        for (m = 0; mask[m].name.len != 0; m++) {

            if (mask[m].name.len != value[i].len
                || ngx_strcasecmp(mask[m].name.data, value[i].data) != 0)
            {
                continue;
            }

            if (*np & mask[m].mask) {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "duplicate value \"%s\"", value[i].data);

            } else {
                *np |= mask[m].mask;
            }

            break;
        }

        if (mask[m].name.len == 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid value \"%s\"", value[i].data);

            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


#if 0

char *
ngx_conf_unsupported(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    return "unsupported on this platform";
}

#endif


char *
ngx_conf_deprecated(ngx_conf_t *cf, void *post, void *data)
{
    ngx_conf_deprecated_t  *d = post;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "the \"%s\" directive is deprecated, "
                       "use the \"%s\" directive instead",
                       d->old_name, d->new_name);

    return NGX_CONF_OK;
}


char *
ngx_conf_check_num_bounds(ngx_conf_t *cf, void *post, void *data)
{
    ngx_conf_num_bounds_t  *bounds = post;
    ngx_int_t  *np = data;

    if (bounds->high == -1) {
        if (*np >= bounds->low) {
            return NGX_CONF_OK;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "value must be equal to or greater than %i",
                           bounds->low);

        return NGX_CONF_ERROR;
    }

    if (*np >= bounds->low && *np <= bounds->high) {
        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "value must be between %i and %i",
                       bounds->low, bounds->high);

    return NGX_CONF_ERROR;
}
