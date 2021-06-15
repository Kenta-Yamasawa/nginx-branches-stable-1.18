
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef _NGX_RESOLVER_H_INCLUDED_
#define _NGX_RESOLVER_H_INCLUDED_


// A レコード（ドメイン名から ip アドレスを調べる）
#define NGX_RESOLVE_A         1
// CNAME レコード（あるドメイン名やホスト名の別名を定義する）
#define NGX_RESOLVE_CNAME     5
// PTR レコード（ip アドレスからドメイン名を調べる）
#define NGX_RESOLVE_PTR       12
// MX レコード（ドメイン名からメールアドレスを調べる）
#define NGX_RESOLVE_MX        15
// TXT レコード
#define NGX_RESOLVE_TXT       16
#if (NGX_HAVE_INET6)
// A レコードの ipv6 用
#define NGX_RESOLVE_AAAA      28
#endif
// SRV レコード（ドメイン名から、提供するホスト名・プロトコル・ポートを調べる））
#define NGX_RESOLVE_SRV       33
// DNAME レコード
#define NGX_RESOLVE_DNAME     39


// RCODE
// フォーマットエラー（400：Bad Request）
#define NGX_RESOLVE_FORMERR   1
// サーバエラー（500：Internal Server Error）
#define NGX_RESOLVE_SERVFAIL  2
// 問い合わせの名前なし（404：Not Found）
#define NGX_RESOLVE_NXDOMAIN  3
// 未実装（501：Not Impleented）
#define NGX_RESOLVE_NOTIMP    4
// 拒否（403：Forbidden）
#define NGX_RESOLVE_REFUSED   5
// タイムアウト
#define NGX_RESOLVE_TIMEDOUT  NGX_ETIMEDOUT


#define NGX_NO_RESOLVER       (void *) -1

#define NGX_RESOLVER_MAX_RECURSION    50


typedef struct ngx_resolver_s  ngx_resolver_t;


// リゾルバが DNS サーバへリクエストを投げるときに使用するコネクション
typedef struct {
    // udp コネクションそのもの
    ngx_connection_t         *udp;
    // tcp コネクションそのもの
    ngx_connection_t         *tcp;
    // アドレス
    struct sockaddr          *sockaddr;
    // アドレスの長さ
    socklen_t                 socklen;
    // 
    ngx_str_t                 server;
    // ログ出力に使用
    ngx_log_t                 log;
    // 受信バッファ
    ngx_buf_t                *read_buf;
    // 送信バッファ
    ngx_buf_t                *write_buf;
    // 親リゾルバ（このコネクションで通信するリゾルバ）
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

    // 名前解決の際に使用したリゾルバ・コンテキスト構造体
    ngx_resolver_ctx_t       *ctx;
    ngx_int_t                 state;

    ngx_uint_t                naddrs;
    ngx_addr_t               *addrs;
} ngx_resolver_srv_name_t;


// 結果ノード
typedef struct {
    // この赤黒木ノード
    ngx_rbtree_node_t         node;
    // この結果ノードを管理するためのキューの要素
    ngx_queue_t               queue;

    /* PTR: resolved name, A: name to resolve */
    // 解決すべき名前
    u_char                   *name;

#if (NGX_HAVE_INET6)
    /* PTR: IPv6 address to resolve (IPv4 address is in rbtree node key) */
    struct in6_addr           addr6;
#endif

    // 名前解決した名前の長さ
    u_short                   nlen;
    // クエリの長さ
    u_short                   qlen;

    // クエリ（結果ノードごとに異なる固有の値）
    // DNS ポイゾニング対策？
    u_char                   *query;
#if (NGX_HAVE_INET6)
    u_char                   *query6;
#endif

    // マスタリゾルバから帰ってきた名前解決済みアドレス
    union {
        in_addr_t             addr;
        in_addr_t            *addrs;
        u_char               *cname;
        ngx_resolver_srv_t   *srvs;
    } u;

    // マスタリゾルバから返ってきた（ステータス）コード
    u_char                    code;
    // u の数
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

    // 最後に使ったコネクション
    ngx_uint_t                last_connection;

    // この結果ノードに対応するコンテキスト（DNS クライアントを必要としてきたもの）
    ngx_resolver_ctx_t       *waiting;
} ngx_resolver_node_t;


// スタブリゾルバ
struct ngx_resolver_s {
    /* has to be pointer because of "incomplete type" */
    // 再送信イベント
    ngx_event_t              *event;
    void                     *dummy;
    // ログ出力に使用するログ機能
    ngx_log_t                *log;

    /* event ident must be after 3 pointers as in ngx_connection_t */
    ngx_int_t                 ident;

    /* simple round robin DNS peers balancer */
    // DNS サーバへの問い合わせに使用するコネクション群の配列
    ngx_array_t               connections;
    // 次に使用すべきコネクションへのインデックス
    ngx_uint_t                last_connection;

    // このリゾルバが解決した名前群を ngx_resolver_node_t 型で保管（name型）
    // 赤黒木のルート
    ngx_rbtree_t              name_rbtree;
    // 赤黒木の末端
    ngx_rbtree_node_t         name_sentinel;

    // このリゾルバが解決した名前群を ngx_resolver_node_t 型で保管（srv型）
    ngx_rbtree_t              srv_rbtree;
    ngx_rbtree_node_t         srv_sentinel;

    // このリゾルバが解決した名前群を ngx_resolver_node_t 型で保管（addr型）
    ngx_rbtree_t              addr_rbtree;
    ngx_rbtree_node_t         addr_sentinel;

    // 再送信すべき要素を入れるためのキュー
    ngx_queue_t               name_resend_queue;
    ngx_queue_t               srv_resend_queue;
    ngx_queue_t               addr_resend_queue;

    // 失効すべき要素を入れるためのキュー
    ngx_queue_t               name_expire_queue;
    ngx_queue_t               srv_expire_queue;
    ngx_queue_t               addr_expire_queue;

#if (NGX_HAVE_INET6)
    // ipv6 に対応しているかどうか
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

    // このリゾルバ関連のログのレベル
    ngx_uint_t                log_level;
};


// クエリを投げて名前解決をする一連のアクション
struct ngx_resolver_ctx_s {
    // コンテキストはリスト形式でまとめて管理できる
    ngx_resolver_ctx_t       *next;
    // このアクションを投げたリゾルバ
    ngx_resolver_t           *resolver;
    // このアクションで解決したい要求を表す結果ノード
    ngx_resolver_node_t      *node;

    /* event ident must be after 3 pointers as in ngx_connection_t */
    ngx_int_t                 ident;

    // 名前解決した結果（成功なら NGX_OK）
    ngx_int_t                 state;
    // 名前解決する名前
    ngx_str_t                 name;
    ngx_str_t                 service;

    // ↓ngx_resolver_response_a() で更新される
    // ここから
    time_t                    valid;
    // addrs の要素数
    ngx_uint_t                naddrs;
    // ip アドレス
    ngx_resolver_addr_t      *addrs;
    // ip アドレス
    ngx_resolver_addr_t       addr;
    // ip アドレス
    struct sockaddr_in        sin;
    // ここまで

    ngx_uint_t                count;
    ngx_uint_t                nsrvs;
    ngx_resolver_srv_name_t  *srvs;

    // ngx_http_upstream_resolve_handler など
    // 名前解決した直後に実行したい関数
    ngx_resolver_handler_pt   handler;
    // この名前解決を必要とするリクエスト構造体など
    void                     *data;
    // タイムアウト時間
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
// 使わない
ngx_int_t ngx_resolve_addr(ngx_resolver_ctx_t *ctx);
// 使わない
void ngx_resolve_addr_done(ngx_resolver_ctx_t *ctx);
char *ngx_resolver_strerror(ngx_int_t err);


#endif /* _NGX_RESOLVER_H_INCLUDED_ */
