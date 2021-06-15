
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef _NGX_RESOLVER_H_INCLUDED_
#define _NGX_RESOLVER_H_INCLUDED_


// A ���R�[�h�i�h���C�������� ip �A�h���X�𒲂ׂ�j
#define NGX_RESOLVE_A         1
// CNAME ���R�[�h�i����h���C������z�X�g���̕ʖ����`����j
#define NGX_RESOLVE_CNAME     5
// PTR ���R�[�h�iip �A�h���X����h���C�����𒲂ׂ�j
#define NGX_RESOLVE_PTR       12
// MX ���R�[�h�i�h���C�������烁�[���A�h���X�𒲂ׂ�j
#define NGX_RESOLVE_MX        15
// TXT ���R�[�h
#define NGX_RESOLVE_TXT       16
#if (NGX_HAVE_INET6)
// A ���R�[�h�� ipv6 �p
#define NGX_RESOLVE_AAAA      28
#endif
// SRV ���R�[�h�i�h���C��������A�񋟂���z�X�g���E�v���g�R���E�|�[�g�𒲂ׂ�j�j
#define NGX_RESOLVE_SRV       33
// DNAME ���R�[�h
#define NGX_RESOLVE_DNAME     39


// RCODE
// �t�H�[�}�b�g�G���[�i400�FBad Request�j
#define NGX_RESOLVE_FORMERR   1
// �T�[�o�G���[�i500�FInternal Server Error�j
#define NGX_RESOLVE_SERVFAIL  2
// �₢���킹�̖��O�Ȃ��i404�FNot Found�j
#define NGX_RESOLVE_NXDOMAIN  3
// �������i501�FNot Impleented�j
#define NGX_RESOLVE_NOTIMP    4
// ���ہi403�FForbidden�j
#define NGX_RESOLVE_REFUSED   5
// �^�C���A�E�g
#define NGX_RESOLVE_TIMEDOUT  NGX_ETIMEDOUT


#define NGX_NO_RESOLVER       (void *) -1

#define NGX_RESOLVER_MAX_RECURSION    50


typedef struct ngx_resolver_s  ngx_resolver_t;


// ���]���o�� DNS �T�[�o�փ��N�G�X�g�𓊂���Ƃ��Ɏg�p����R�l�N�V����
typedef struct {
    // udp �R�l�N�V�������̂���
    ngx_connection_t         *udp;
    // tcp �R�l�N�V�������̂���
    ngx_connection_t         *tcp;
    // �A�h���X
    struct sockaddr          *sockaddr;
    // �A�h���X�̒���
    socklen_t                 socklen;
    // 
    ngx_str_t                 server;
    // ���O�o�͂Ɏg�p
    ngx_log_t                 log;
    // ��M�o�b�t�@
    ngx_buf_t                *read_buf;
    // ���M�o�b�t�@
    ngx_buf_t                *write_buf;
    // �e���]���o�i���̃R�l�N�V�����ŒʐM���郊�]���o�j
    ngx_resolver_t           *resolver;
} ngx_resolver_connection_t;


typedef struct ngx_resolver_ctx_s  ngx_resolver_ctx_t;

typedef void (*ngx_resolver_handler_pt)(ngx_resolver_ctx_t *ctx);


typedef struct {
    struct sockaddr          *sockaddr;
    socklen_t                 socklen;
    ngx_str_t                 name;
    u_short                   priority;
    u_short                   weight;
} ngx_resolver_addr_t;


typedef struct {
    ngx_str_t                 name;
    u_short                   priority;
    u_short                   weight;
    u_short                   port;
} ngx_resolver_srv_t;


typedef struct {
    ngx_str_t                 name;
    u_short                   priority;
    u_short                   weight;
    u_short                   port;

    // ���O�����̍ۂɎg�p�������]���o�E�R���e�L�X�g�\����
    ngx_resolver_ctx_t       *ctx;
    ngx_int_t                 state;

    ngx_uint_t                naddrs;
    ngx_addr_t               *addrs;
} ngx_resolver_srv_name_t;


// ���ʃm�[�h
typedef struct {
    // ���̐ԍ��؃m�[�h
    ngx_rbtree_node_t         node;
    // ���̌��ʃm�[�h���Ǘ����邽�߂̃L���[�̗v�f
    ngx_queue_t               queue;

    /* PTR: resolved name, A: name to resolve */
    // �������ׂ����O
    u_char                   *name;

#if (NGX_HAVE_INET6)
    /* PTR: IPv6 address to resolve (IPv4 address is in rbtree node key) */
    struct in6_addr           addr6;
#endif

    // ���O�����������O�̒���
    u_short                   nlen;
    // �N�G���̒���
    u_short                   qlen;

    // �N�G���i���ʃm�[�h���ƂɈقȂ�ŗL�̒l�j
    // DNS �|�C�]�j���O�΍�H
    u_char                   *query;
#if (NGX_HAVE_INET6)
    u_char                   *query6;
#endif

    // �}�X�^���]���o����A���Ă������O�����ς݃A�h���X
    union {
        in_addr_t             addr;
        in_addr_t            *addrs;
        u_char               *cname;
        ngx_resolver_srv_t   *srvs;
    } u;

    // �}�X�^���]���o����Ԃ��Ă����i�X�e�[�^�X�j�R�[�h
    u_char                    code;
    // u �̐�
    u_short                   naddrs;
    u_short                   nsrvs;
    u_short                   cnlen;

#if (NGX_HAVE_INET6)
    union {
        struct in6_addr       addr6;
        struct in6_addr      *addrs6;
    } u6;

    u_short                   naddrs6;
#endif

    time_t                    expire;
    time_t                    valid;
    uint32_t                  ttl;

    unsigned                  tcp:1;
#if (NGX_HAVE_INET6)
    unsigned                  tcp6:1;
#endif

    // �Ō�Ɏg�����R�l�N�V����
    ngx_uint_t                last_connection;

    // ���̌��ʃm�[�h�ɑΉ�����R���e�L�X�g�iDNS �N���C�A���g��K�v�Ƃ��Ă������́j
    ngx_resolver_ctx_t       *waiting;
} ngx_resolver_node_t;


// �X�^�u���]���o
struct ngx_resolver_s {
    /* has to be pointer because of "incomplete type" */
    // �đ��M�C�x���g
    ngx_event_t              *event;
    void                     *dummy;
    // ���O�o�͂Ɏg�p���郍�O�@�\
    ngx_log_t                *log;

    /* event ident must be after 3 pointers as in ngx_connection_t */
    ngx_int_t                 ident;

    /* simple round robin DNS peers balancer */
    // DNS �T�[�o�ւ̖₢���킹�Ɏg�p����R�l�N�V�����Q�̔z��
    ngx_array_t               connections;
    // ���Ɏg�p���ׂ��R�l�N�V�����ւ̃C���f�b�N�X
    ngx_uint_t                last_connection;

    // ���̃��]���o�������������O�Q�� ngx_resolver_node_t �^�ŕۊǁiname�^�j
    // �ԍ��؂̃��[�g
    ngx_rbtree_t              name_rbtree;
    // �ԍ��؂̖��[
    ngx_rbtree_node_t         name_sentinel;

    // ���̃��]���o�������������O�Q�� ngx_resolver_node_t �^�ŕۊǁisrv�^�j
    ngx_rbtree_t              srv_rbtree;
    ngx_rbtree_node_t         srv_sentinel;

    // ���̃��]���o�������������O�Q�� ngx_resolver_node_t �^�ŕۊǁiaddr�^�j
    ngx_rbtree_t              addr_rbtree;
    ngx_rbtree_node_t         addr_sentinel;

    // �đ��M���ׂ��v�f�����邽�߂̃L���[
    ngx_queue_t               name_resend_queue;
    ngx_queue_t               srv_resend_queue;
    ngx_queue_t               addr_resend_queue;

    // �������ׂ��v�f�����邽�߂̃L���[
    ngx_queue_t               name_expire_queue;
    ngx_queue_t               srv_expire_queue;
    ngx_queue_t               addr_expire_queue;

#if (NGX_HAVE_INET6)
    // ipv6 �ɑΉ����Ă��邩�ǂ���
    ngx_uint_t                ipv6;                 /* unsigned  ipv6:1; */
    ngx_rbtree_t              addr6_rbtree;
    ngx_rbtree_node_t         addr6_sentinel;
    ngx_queue_t               addr6_resend_queue;
    ngx_queue_t               addr6_expire_queue;
#endif

    time_t                    resend_timeout;
    time_t                    tcp_timeout;
    time_t                    expire;
    time_t                    valid;

    // ���̃��]���o�֘A�̃��O�̃��x��
    ngx_uint_t                log_level;
};


// �N�G���𓊂��Ė��O�����������A�̃A�N�V����
struct ngx_resolver_ctx_s {
    // �R���e�L�X�g�̓��X�g�`���ł܂Ƃ߂ĊǗ��ł���
    ngx_resolver_ctx_t       *next;
    // ���̃A�N�V�����𓊂������]���o
    ngx_resolver_t           *resolver;
    // ���̃A�N�V�����ŉ����������v����\�����ʃm�[�h
    ngx_resolver_node_t      *node;

    /* event ident must be after 3 pointers as in ngx_connection_t */
    ngx_int_t                 ident;

    // ���O�����������ʁi�����Ȃ� NGX_OK�j
    ngx_int_t                 state;
    // ���O�������閼�O
    ngx_str_t                 name;
    ngx_str_t                 service;

    // ��ngx_resolver_response_a() �ōX�V�����
    // ��������
    time_t                    valid;
    // addrs �̗v�f��
    ngx_uint_t                naddrs;
    // ip �A�h���X
    ngx_resolver_addr_t      *addrs;
    // ip �A�h���X
    ngx_resolver_addr_t       addr;
    // ip �A�h���X
    struct sockaddr_in        sin;
    // �����܂�

    ngx_uint_t                count;
    ngx_uint_t                nsrvs;
    ngx_resolver_srv_name_t  *srvs;

    // ngx_http_upstream_resolve_handler �Ȃ�
    // ���O������������Ɏ��s�������֐�
    ngx_resolver_handler_pt   handler;
    // ���̖��O������K�v�Ƃ��郊�N�G�X�g�\���̂Ȃ�
    void                     *data;
    // �^�C���A�E�g����
    ngx_msec_t                timeout;

    unsigned                  quick:1;
    unsigned                  async:1;
    unsigned                  cancelable:1;
    ngx_uint_t                recursion;
    ngx_event_t              *event;
};


ngx_resolver_t *ngx_resolver_create(ngx_conf_t *cf, ngx_str_t *names,
    ngx_uint_t n);
ngx_resolver_ctx_t *ngx_resolve_start(ngx_resolver_t *r,
    ngx_resolver_ctx_t *temp);
ngx_int_t ngx_resolve_name(ngx_resolver_ctx_t *ctx);
void ngx_resolve_name_done(ngx_resolver_ctx_t *ctx);
// �g��Ȃ�
ngx_int_t ngx_resolve_addr(ngx_resolver_ctx_t *ctx);
// �g��Ȃ�
void ngx_resolve_addr_done(ngx_resolver_ctx_t *ctx);
char *ngx_resolver_strerror(ngx_int_t err);


#endif /* _NGX_RESOLVER_H_INCLUDED_ */
