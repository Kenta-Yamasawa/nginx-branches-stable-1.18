
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONNECTION_H_INCLUDED_
#define _NGX_CONNECTION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_listening_s  ngx_listening_t;

struct ngx_listening_s {
    // ���b�X������t�@�C���̃f�B�X�N���v�^
    ngx_socket_t        fd;

    // ���̃A�h���X�Ń��b�X������
    struct sockaddr    *sockaddr;
    // �A�h���X�̒���
    socklen_t           socklen;    /* size of sockaddr */
    // �A�h���X�̃e�L�X�g�`���̒���
    size_t              addr_text_max_len;
    // �A�h���X�̃e�L�X�g�`��
    ngx_str_t           addr_text;

    // �\�P�b�g�^�C�v
    int                 type;

    // �\�P�b�g�o�b�N���O�̃T�C�Y
    int                 backlog;
    // ��M�f�[�^�̃o�b�t�@�̃T�C�Y
    int                 rcvbuf;
    // ���M�f�[�^�̃o�b�t�@�̃T�C�Y
    int                 sndbuf;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    // �A�N�e�B�u��Ԃ����b��ɉ������邩�w��iKeep-Alive �v���[�u���M���[�h�܂ł̗P�\�j
    int                 keepidle;
    // Keep-Alive �v���[�u�𑗐M���鎞�ԊԊu
    int                 keepintvl;
    // Keep-Alive �v���[�u������܂ŋN���邩�i���ׂĕԓ����Ȃ���΃R�l�N�V�������h���b�v����j
    int                 keepcnt;
#endif

    /* handler of accepted connection */
    ngx_connection_handler_pt   handler;

    void               *servers;  /* array of ngx_http_in_addr_t, for example */

    ngx_log_t           log;
    ngx_log_t          *logp;

    size_t              pool_size;
    /* should be here because of the AcceptEx() preread */
    size_t              post_accept_buffer_size;
    /* should be here because of the deferred accept */
    ngx_msec_t          post_accept_timeout;

    ngx_listening_t    *previous;
    ngx_connection_t   *connection;

    ngx_rbtree_t        rbtree;
    ngx_rbtree_node_t   sentinel;

    ngx_uint_t          worker;

    // ���̃��b�X���v���͂��łɃI�[�v������܂���
    unsigned            open:1;
    // 
    unsigned            remain:1;
    // ���̃��b�X���v���͖������邱��
    unsigned            ignore:1;

    unsigned            bound:1;       /* already bound */
    // �Â��T�C�N������󂯌p���ꂽ
    unsigned            inherited:1;   /* inherited from previous process */
    unsigned            nonblocking_accept:1;
    // ���Ƀ��b�X���J�n���܂���
    unsigned            listen:1;
    unsigned            nonblocking:1;
    unsigned            shared:1;    /* shared between threads or processes */
    unsigned            addr_ntop:1;
    // UDP �ʐM�̎��Arecvmsg() �ň����IP�����܂߂邩�ǂ������w��
    unsigned            wildcard:1;

#if (NGX_HAVE_INET6)
    // ipv6 �݂̂����b�X������
    unsigned            ipv6only:1;
#endif
    // ���̃��b�X���v���Ɠ����A�h���X�E�|�[�g�łق��̃v���Z�X�����b�X���ł��邱�Ƃ�������
    unsigned            reuseport:1;
    // �i���܂ł͈�������j���̃��b�X���v���Ɠ����A�h���X�E�|�[�g�łق��̃v���Z�X�����b�X���ł��邱�Ƃ�������
    unsigned            add_reuseport:1;
    // Keep-Alive ��L�������邩�ǂ���
    unsigned            keepalive:2;

    // Accept �t�B���^�̐ݒ�
    unsigned            deferred_accept:1;
    unsigned            delete_deferred:1;
    unsigned            add_deferred:1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    // Accept �t�B���^�̐ݒ�
    char               *accept_filter;
#endif
#if (NGX_HAVE_SETFIB)
    // ? �v�׋�
    int                 setfib;
#endif

#if (NGX_HAVE_TCP_FASTOPEN)
    // TCP_FASTOPEN ��L�������邩�ǂ���
    int                 fastopen;
#endif

};


typedef enum {
    NGX_ERROR_ALERT = 0,
    NGX_ERROR_ERR,
    NGX_ERROR_INFO,
    NGX_ERROR_IGNORE_ECONNRESET,
    NGX_ERROR_IGNORE_EINVAL
} ngx_connection_log_error_e;


typedef enum {
    NGX_TCP_NODELAY_UNSET = 0,
    NGX_TCP_NODELAY_SET,
    NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;


typedef enum {
    NGX_TCP_NOPUSH_UNSET = 0,
    NGX_TCP_NOPUSH_SET,
    NGX_TCP_NOPUSH_DISABLED
} ngx_connection_tcp_nopush_e;


#define NGX_LOWLEVEL_BUFFERED  0x0f
#define NGX_SSL_BUFFERED       0x01
#define NGX_HTTP_V2_BUFFERED   0x02


struct ngx_connection_s {
    /**
     * �C�ӂ̃f�[�^���i�[�ł���̈�
     *   
     *   
     */
    void               *data;
    // ��M�C�x���g
    ngx_event_t        *read;
    // ���M�C�x���g
    ngx_event_t        *write;

    // ���̃R�l�N�V�����ɕR�Â����\�P�b�g
    ngx_socket_t        fd;

    // ��M�n���h��
    ngx_recv_pt         recv;
    // ���M�n���h��
    ngx_send_pt         send;
    // �`�F�C����M�n���h��
    ngx_recv_chain_pt   recv_chain;
    // �`�F�C�����M�n���h��
    ngx_send_chain_pt   send_chain;

    // ���̃R�l�N�V�����ɕR�Â������X�j���O
    ngx_listening_t    *listening;

    // ���̃R�l�N�V�����ő��M�ς݂̃f�[�^�o�C�g��
    off_t               sent;

    // ���̃R�l�N�V�������g�p���郍�O�G���W��
    ngx_log_t          *log;

    // ���̃R�l�N�V�����p�̕ʌ̃R�l�N�V�����v�[��
    ngx_pool_t         *pool;

    // ���̃R�l�N�V�����ŗp�������4�w�v���g�R��
    int                 type;

    // ����̃A�h���X���
    struct sockaddr    *sockaddr;
    socklen_t           socklen;
    ngx_str_t           addr_text;

    ngx_proxy_protocol_t  *proxy_protocol;

#if (NGX_SSL || NGX_COMPAT)
    ngx_ssl_connection_t  *ssl;
#endif

    ngx_udp_connection_t  *udp;

    // ������̃A�h���X���
    struct sockaddr    *local_sockaddr;
    socklen_t           local_socklen;

    ngx_buf_t          *buffer;

    ngx_queue_t         queue;

    ngx_atomic_uint_t   number;

    ngx_uint_t          requests;

    unsigned            buffered:8;

    unsigned            log_error:3;     /* ngx_connection_log_error_e */

    unsigned            timedout:1;
    unsigned            error:1;
    unsigned            destroyed:1;

    unsigned            idle:1;
    unsigned            reusable:1;
    unsigned            close:1;
    unsigned            shared:1;

    unsigned            sendfile:1;
    unsigned            sndlowat:1;
    // nodelay ���[�h���ǂ���
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
    // nopush ���[�h���ǂ���
    unsigned            tcp_nopush:2;    /* ngx_connection_tcp_nopush_e */

    unsigned            need_last_buf:1;

#if (NGX_HAVE_AIO_SENDFILE || NGX_COMPAT)
    unsigned            busy_count:2;
#endif

#if (NGX_THREADS || NGX_COMPAT)
    ngx_thread_task_t  *sendfile_task;
#endif
};


#define ngx_set_connection_log(c, l)                                         \
                                                                             \
    c->log->file = l->file;                                                  \
    c->log->next = l->next;                                                  \
    c->log->writer = l->writer;                                              \
    c->log->wdata = l->wdata;                                                \
    if (!(c->log->log_level & NGX_LOG_DEBUG_CONNECTION)) {                   \
        c->log->log_level = l->log_level;                                    \
    }


ngx_listening_t *ngx_create_listening(ngx_conf_t *cf, struct sockaddr *sockaddr,
    socklen_t socklen);
ngx_int_t ngx_clone_listening(ngx_cycle_t *cycle, ngx_listening_t *ls);
ngx_int_t ngx_set_inherited_sockets(ngx_cycle_t *cycle);
ngx_int_t ngx_open_listening_sockets(ngx_cycle_t *cycle);
void ngx_configure_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_connection(ngx_connection_t *c);
void ngx_close_idle_connections(ngx_cycle_t *cycle);
ngx_int_t ngx_connection_local_sockaddr(ngx_connection_t *c, ngx_str_t *s,
    ngx_uint_t port);
ngx_int_t ngx_tcp_nodelay(ngx_connection_t *c);
ngx_int_t ngx_connection_error(ngx_connection_t *c, ngx_err_t err, char *text);

ngx_connection_t *ngx_get_connection(ngx_socket_t s, ngx_log_t *log);
void ngx_free_connection(ngx_connection_t *c);

void ngx_reusable_connection(ngx_connection_t *c, ngx_uint_t reusable);

#endif /* _NGX_CONNECTION_H_INCLUDED_ */
