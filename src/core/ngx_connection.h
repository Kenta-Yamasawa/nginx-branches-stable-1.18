
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
    // リッスンするファイルのディスクリプタ
    ngx_socket_t        fd;

    // このアドレスでリッスンする
    struct sockaddr    *sockaddr;
    // アドレスの長さ
    socklen_t           socklen;    /* size of sockaddr */
    // アドレスのテキスト形式の長さ
    size_t              addr_text_max_len;
    // アドレスのテキスト形式
    ngx_str_t           addr_text;

    // ソケットタイプ
    int                 type;

    // ソケットバックログのサイズ
    int                 backlog;
    // 受信データのバッファのサイズ
    int                 rcvbuf;
    // 送信データのバッファのサイズ
    int                 sndbuf;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    // アクティブ状態を何秒後に解除するか指定（Keep-Alive プルーブ送信モードまでの猶予）
    int                 keepidle;
    // Keep-Alive プルーブを送信する時間間隔
    int                 keepintvl;
    // Keep-Alive プルーブを何回まで起きるか（すべて返答がなければコネクションをドロップする）
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

    // このリッスン要求はすでにオープンされました
    unsigned            open:1;
    // 
    unsigned            remain:1;
    // このリッスン要求は無視すること
    unsigned            ignore:1;

    unsigned            bound:1;       /* already bound */
    // 古いサイクルから受け継がれた
    unsigned            inherited:1;   /* inherited from previous process */
    unsigned            nonblocking_accept:1;
    // 既にリッスン開始しました
    unsigned            listen:1;
    unsigned            nonblocking:1;
    unsigned            shared:1;    /* shared between threads or processes */
    unsigned            addr_ntop:1;
    // UDP 通信の時、recvmsg() で宛先のIP情報を含めるかどうかを指定
    unsigned            wildcard:1;

#if (NGX_HAVE_INET6)
    // ipv6 のみをリッスンする
    unsigned            ipv6only:1;
#endif
    // このリッスン要求と同じアドレス・ポートでほかのプロセスがリッスンできることを許可する
    unsigned            reuseport:1;
    // （今までは違ったが）このリッスン要求と同じアドレス・ポートでほかのプロセスがリッスンできることを許可する
    unsigned            add_reuseport:1;
    // Keep-Alive を有効化するかどうか
    unsigned            keepalive:2;

    // Accept フィルタの設定
    unsigned            deferred_accept:1;
    unsigned            delete_deferred:1;
    unsigned            add_deferred:1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    // Accept フィルタの設定
    char               *accept_filter;
#endif
#if (NGX_HAVE_SETFIB)
    // ? 要勉強
    int                 setfib;
#endif

#if (NGX_HAVE_TCP_FASTOPEN)
    // TCP_FASTOPEN を有効化するかどうか
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
     * 任意のデータを格納できる領域
     *   
     *   
     */
    void               *data;
    // 受信イベント
    ngx_event_t        *read;
    // 送信イベント
    ngx_event_t        *write;

    // このコネクションに紐づいたソケット
    ngx_socket_t        fd;

    // 受信ハンドラ
    ngx_recv_pt         recv;
    // 送信ハンドラ
    ngx_send_pt         send;
    // チェイン受信ハンドラ
    ngx_recv_chain_pt   recv_chain;
    // チェイン送信ハンドラ
    ngx_send_chain_pt   send_chain;

    // このコネクションに紐づいたリスニング
    ngx_listening_t    *listening;

    // このコネクションで送信済みのデータバイト量
    off_t               sent;

    // このコネクションが使用するログエンジン
    ngx_log_t          *log;

    // このコネクション用の別個のコネクションプール
    ngx_pool_t         *pool;

    // このコネクションで用いられる第4層プロトコル
    int                 type;

    // 相手のアドレス情報
    struct sockaddr    *sockaddr;
    socklen_t           socklen;
    ngx_str_t           addr_text;

    ngx_proxy_protocol_t  *proxy_protocol;

#if (NGX_SSL || NGX_COMPAT)
    ngx_ssl_connection_t  *ssl;
#endif

    ngx_udp_connection_t  *udp;

    // こちらのアドレス情報
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
    // nodelay モードかどうか
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
    // nopush モードかどうか
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
