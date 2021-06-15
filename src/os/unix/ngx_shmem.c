
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


// マクロ：
//     NGX_HAVE_MAP_ANON
//     NGX_HAVE_MAP_DEVZERO
//     NGX_HAVE_SYSVSHM
// 少なくとも、上記のどれかひとつがオッケーでないとダメ
// 無名マッピングの方法３種類を↑のマクロで指定する
// https://ja.wikipedia.org/wiki/Mmap#%E3%83%95%E3%82%A1%E3%82%A4%E3%83%AB%E3%83%9E%E3%83%83%E3%83%94%E3%83%B3%E3%82%B0


#if (NGX_HAVE_MAP_ANON)


/**
 * @brief
 *     共有メモリを割り当てる
 *     ここでいう共有メモリは以下の特徴を持つ
 *         ・読み書き可能
 *         ・どの特定のファイルとも紐づいていない
 *         ・サイズは shm->size 分
 *         ・複数プロセス間で変更が共有される
 * @param[in:out]
 *     shm: 共有メモリ（あらかじめ shm->size に値をセットしておくこと）
 * @retval
 *     NGX_OK: 成功
 *     NGX_ERROR: 失敗
 */
ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    // mmap() でマッピングした領域はプロセスをまたいで共有できるという特徴がある
    // もう一つ、パイプによる方法もあるが、mmap() の方が高速処理ができる
    // https://www.atmarkit.co.jp/ait/articles/1205/28/news125.html
    /**
     * @brief
     *     ファイルを仮想アドレス空間へマッピングする
     * @param[in]
     *     addr: 割り当ての際のヒントに使用する（NULL でも可）
     *     len: 割り当てたいサイズ（ページサイズの倍数でなくてもよい）（その方が好ましいけど）
     *     prot: 保護領域へのアクセス許可を指定する
     *         デフォルト: ページはアクセスできない
     *         PROT_READ: ページは読み取り可能
     *         PROT_WRITE: ページは書き込み可能
     *         PROT_EXEC: ページは実行可能
     *     flags: マップした領域のタイプ
     *         MAP_ANON: どのファイルとも対応しない匿名メモリ領域
     *         MAP_FIXED: 第一引数で指定したアドレスが取得できないなら失敗する
     *         MAP_HASSEMAPHORE: 領域にセマフォが含まれていることをカーネルへ通知する
     *         MAP_INHERIT: サポートされていない（無視してオッケー）
     *         MAP_PRIVATE: 修正はプロセスごとに固有で行われる(単一プロセスで使う用）
     *         MAP_SHARED:   修正はプロセスで共有に行われる（複数プロセスで共有する用）
     *         などなど・・・
     *     fd: 紐づけたいファイルのディスクリプタ
     *     offset: 第５引数で指定したファイルの offset から終わりまでの内容をマッピング
     * @retval
     *     取得した共有メモリへのアドレス
     * @detail
     *     読み書き可能で、どの特定のファイルとも紐づいていない
     */
    // http://kaworu.jpn.org/doc/FreeBSD/jman/man2/mmap.2.php
    shm->addr = (u_char *) mmap(NULL, shm->size,
                                PROT_READ|PROT_WRITE,
                                MAP_ANON|MAP_SHARED, -1, 0);

    if (shm->addr == MAP_FAILED) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "mmap(MAP_ANON|MAP_SHARED, %uz) failed", shm->size);
        return NGX_ERROR;
    }

    return NGX_OK;
}


/**
 * @brief
 *     共有メモリを開放する。必ず成功する。
 * @param[in]
 *     shm: 開放したい共有メモリ
 */
void
ngx_shm_free(ngx_shm_t *shm)
{
    if (munmap((void *) shm->addr, shm->size) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "munmap(%p, %uz) failed", shm->addr, shm->size);
    }
}

#elif (NGX_HAVE_MAP_DEVZERO)

ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    ngx_fd_t  fd;

    fd = open("/dev/zero", O_RDWR);

    if (fd == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "open(\"/dev/zero\") failed");
        return NGX_ERROR;
    }

    shm->addr = (u_char *) mmap(NULL, shm->size, PROT_READ|PROT_WRITE,
                                MAP_SHARED, fd, 0);

    if (shm->addr == MAP_FAILED) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "mmap(/dev/zero, MAP_SHARED, %uz) failed", shm->size);
    }

    if (close(fd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "close(\"/dev/zero\") failed");
    }

    return (shm->addr == MAP_FAILED) ? NGX_ERROR : NGX_OK;
}


/**
 * @brief
 *     共有メモリを開放する。必ず成功する。
 * @param[in]
 *     shm: 開放したい共有メモリ
 */
void
ngx_shm_free(ngx_shm_t *shm)
{
    if (munmap((void *) shm->addr, shm->size) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "munmap(%p, %uz) failed", shm->addr, shm->size);
    }
}

#elif (NGX_HAVE_SYSVSHM)

#include <sys/ipc.h>
#include <sys/shm.h>


ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    int  id;

    id = shmget(IPC_PRIVATE, shm->size, (SHM_R|SHM_W|IPC_CREAT));

    if (id == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "shmget(%uz) failed", shm->size);
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, shm->log, 0, "shmget id: %d", id);

    shm->addr = shmat(id, NULL, 0);

    if (shm->addr == (void *) -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno, "shmat() failed");
    }

    if (shmctl(id, IPC_RMID, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "shmctl(IPC_RMID) failed");
    }

    return (shm->addr == (void *) -1) ? NGX_ERROR : NGX_OK;
}


void
ngx_shm_free(ngx_shm_t *shm)
{
    if (shmdt(shm->addr) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "shmdt(%p) failed", shm->addr);
    }
}

#endif
