
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/**
 * @brief
 *     一時バッファを生成して返す
 * @param[in]
 *     pool: このプールを用いてバッファを生成する
 *     size: このバッファが保有するメモリ容量のサイズ
 * @retval
 *     ngx_buf_t *: 成功
 *     NULL: 失敗
 */
ngx_buf_t *
ngx_create_temp_buf(ngx_pool_t *pool, size_t size)
{
    ngx_buf_t *b;

    // ngx_pcalloc() : 0 で埋める
    b = ngx_calloc_buf(pool);
    if (b == NULL) {
        return NULL;
    }

    // 
    b->start = ngx_palloc(pool, size);
    if (b->start == NULL) {
        return NULL;
    }

    /*
     * set by ngx_calloc_buf():
     *
     *     b->file_pos = 0;
     *     b->file_last = 0;
     *     b->file = NULL;
     *     b->shadow = NULL;
     *     b->tag = 0;
     *     and flags
     */

    b->pos = b->start;
    b->last = b->start;
    // 最後のメモリ領域＋１
    b->end = b->last + size;
    b->temporary = 1;

    return b;
}


/**
 * @brief
 *     バッファチェインを新たに生成する
 *     なお、バッファチェインが指すバッファは未指定（つまり空）
 * @retval
 *     NULL: 失敗
 *     otherwise: 生成したバッファチェイン
 */
ngx_chain_t *
ngx_alloc_chain_link(ngx_pool_t *pool)
{
    ngx_chain_t  *cl;

    // プールがバッファチェインを保持していることがある
    // そういう場合はそこのものを使う
    cl = pool->chain;

    if (cl) {
        // プールのチェインから割り当てる場合は、そのチェインはプールのものではなくなる
        pool->chain = cl->next;
        return cl;
    }

    cl = ngx_palloc(pool, sizeof(ngx_chain_t));
    if (cl == NULL) {
        return NULL;
    }

    return cl;
}


/**
 * @brief
 *     複数のバッファをまとめて生成して、バッファチェインとして返す
 * @param[in]
 *     pool: このプールから割り当てる
 *     bufs: 生成するバッファの個数とサイズをまとめたもの
 * @retval
 *     NULL: 失敗
 *     otherwise: 生成したバッファチェイン
 */
ngx_chain_t *
ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs)
{
    u_char       *p;
    ngx_int_t     i;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, **ll;

    p = ngx_palloc(pool, bufs->num * bufs->size);
    if (p == NULL) {
        return NULL;
    }

    ll = &chain;

    for (i = 0; i < bufs->num; i++) {

        b = ngx_calloc_buf(pool);
        if (b == NULL) {
            return NULL;
        }

        /*
         * set by ngx_calloc_buf():
         *
         *     b->file_pos = 0;
         *     b->file_last = 0;
         *     b->file = NULL;
         *     b->shadow = NULL;
         *     b->tag = 0;
         *     and flags
         *
         */

        b->pos = p;
        b->last = p;
        b->temporary = 1;

        b->start = p;
        p += bufs->size;
        b->end = p;

        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            return NULL;
        }

        cl->buf = b;
        *ll = cl;
        ll = &cl->next;
    }

    *ll = NULL;

    return chain;
}


/**
 * @brief
 *     第二引数のバッファチェインに第三引数のバッファチェインを追加する
 * @param[out]
 *     chain: このバッファチェインへ追加する
 * @param[in]
 *     pool: このプールからバッファチェインを割り当てる（バッファそのものは受け継ぐ）
 *     in:   このバッファチェインを追加する
 * @retval
 *     NGX_OK: 成功
 *     NGX_ERROR: 失敗
 */
ngx_int_t
ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain, ngx_chain_t *in)
{
    ngx_chain_t  *cl, **ll;

    ll = chain;

    // バッファチェインの最後を参照
    for (cl = *chain; cl; cl = cl->next) {
        ll = &cl->next;
    }

    while (in) {
        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            *ll = NULL;
            return NGX_ERROR;
        }

        cl->buf = in->buf;
        *ll = cl;
        ll = &cl->next;
        in = in->next;
    }

    *ll = NULL;

    return NGX_OK;
}


/**
 * @brief
 *     フリーなバッファチェインの先頭からバッファをひとつ取り除いて戻す
 *     あるいはバッファチェインを一つ生成して返す
 * @param[in]
 *     p: 生成する場合に使用するプール
 *     free: このバッファチェインから一つ取り除いて返す（NULLも可）
 * @retval
 *     NULL: 失敗
 *     otherwise: 成功
 */
ngx_chain_t *
ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free)
{
    ngx_chain_t  *cl;

    if (*free) {
        cl = *free;
        *free = cl->next;
        cl->next = NULL;
        return cl;
    }

    cl = ngx_alloc_chain_link(p);
    if (cl == NULL) {
        return NULL;
    }

    cl->buf = ngx_calloc_buf(p);
    if (cl->buf == NULL) {
        return NULL;
    }

    cl->next = NULL;

    return cl;
}


/**
 * @brief
 *     第三引数 busy バッファチェインリストの各バッファチェインを
 *     第二引数 free バッファチェインリストと第一引数 pool が保有するバッファチェインリストに移す
 *     バッファチェインのタグ名と第５引数 tag が一致していた場合は free バッファチェインリストへ
 *     そうでない場合は pool のバッファチェインリストへ
 * @param[out]
 *     p: busy バッファチェインリストの加わり先
 *     free: busy バッファチェインリストの加わり先
 * @param[in]
 *     busy: このバッファチェインリストが加わる
 *     out: busy バッファチェインリストと連結させるバッファチェインリスト（NULLも可）
 *     tag: 加わり先の分岐に使用
 */
void
ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free, ngx_chain_t **busy,
    ngx_chain_t **out, ngx_buf_tag_t tag)
{
    ngx_chain_t  *cl;

    // out バッファチェインリストが空でない
    if (*out) {
        // busy バッファチェインリストが空である
        if (*busy == NULL) {
            // busy バッファチェインリストを out バッファチェインリストと同内容にする（コピーではない）
            *busy = *out;

        } else {
            // busy バッファチェインリストのあとに out バッファチェインリストが続く
            for (cl = *busy; cl->next; cl = cl->next) { /* void */ }

            cl->next = *out;
        }

        // どのみち out バッファチェインリストは空に
        *out = NULL;
    }

    while (*busy) {
        cl = *busy;

        // バッファのサイズが 0 でないなら処理を終了
        if (ngx_buf_size(cl->buf) != 0) {
            break;
        }

        // タグとは？
        if (cl->buf->tag != tag) {
            *busy = cl->next;
            // このバッファチェインをプールのバッファチェインリストへ加える
            ngx_free_chain(p, cl);
            continue;
        }

        // メンバを初期化
        cl->buf->pos = cl->buf->start;
        cl->buf->last = cl->buf->start;

        // free バッファチェインリストに加える
        *busy = cl->next;
        cl->next = *free;
        *free = cl;
    }
}


/**
 * @brief
 *     どこからどこまでのバッファについて、ひとまとめにできるか検証する
 * @param[in:out]
 *     in: 上限データ量を超える最初のバッファチェインリストを教えてくれる（正確ではない、ちょっとは超えるかも）
 * @param[in]
 *     limit: 上限データ量、バッファチェインリストのうち、この上限データ量を超えない範囲を切り出す
 * @retval:
 *     実際の合計サイズ
 */
off_t
ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit)
{
    off_t         total, size, aligned, fprev;
    ngx_fd_t      fd;
    ngx_chain_t  *cl;

    total = 0;

    cl = *in;
    fd = cl->buf->file->fd;

    /**
     * 以下の条件が満たされる限りバッファチェインリストのバッファチェインを順に参照する
     *     ・バッファチェインが空ではない
     *     ・バッファに in_file フラグがセットされている
     *     ・これまでの合計サイズが上限を超えていない
     *     ・担当するファイルが存在するバッファである
     *     ・一個前のループで担当したバッファのファイル内容が空（もしくは読み終わった）ではない
     */
    do {
        size = cl->buf->file_last - cl->buf->file_pos;

        // これまでの合計データ量が上限を超過した
        if (size > limit - total) {
            size = limit - total;

            // ngx_pagesize の倍数バイトのうち、cl->buf->file_last まで格納できるサイズ
            // たとえば、ページサイズが 16 bit でファイルサイズが 100 bit だったら
            // aligned は 112 (16 * 7)
            aligned = (cl->buf->file_pos + size + ngx_pagesize - 1)
                       & ~((off_t) ngx_pagesize - 1);

            // アラインメントサイズがファイルサイズ以下？（＝はまだしも＜のケースってあるの！？）
            if (aligned <= cl->buf->file_last) {
                // すでに読んだデータ分を除く
                size = aligned - cl->buf->file_pos;
            }

            // トータルサイズに加えてループを抜ける
            total += size;
            break;
        }

        total += size;
        fprev = cl->buf->file_pos + size;
        cl = cl->next;

    } while (cl
             && cl->buf->in_file
             && total < limit
             && fd == cl->buf->file->fd
             && fprev == cl->buf->file_pos);

    *in = cl;

    return total;
}


/**
 * @brief
 *     送信した分だけ、バッファチェインの各バッファのデータ先頭ポインタを更新する。
 * @param[out]
 *     in: バッファチェイン
 * @param[in]
 *     sent: 送信したデータ量
 * @detail
 *     in の中身に変化はない
 */
ngx_chain_t *
ngx_chain_update_sent(ngx_chain_t *in, off_t sent)
{
    off_t  size;

    for ( /* void */ ; in; in = in->next) {

        if (ngx_buf_special(in->buf)) {
            continue;
        }

        if (sent == 0) {
            break;
        }

        size = ngx_buf_size(in->buf);

        // このバッファのすべてのデータが送信完了した
        if (sent >= size) {
            sent -= size;

            // 先頭ポインタを更新
            if (ngx_buf_in_memory(in->buf)) {
                in->buf->pos = in->buf->last;
            }

            if (in->buf->in_file) {
                in->buf->file_pos = in->buf->file_last;
            }

            continue;
        }

        if (ngx_buf_in_memory(in->buf)) {
            in->buf->pos += (size_t) sent;
        }

        if (in->buf->in_file) {
            in->buf->file_pos += sent;
        }

        break;
    }

    return in;
}
