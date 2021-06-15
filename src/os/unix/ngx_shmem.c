
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


// �}�N���F
//     NGX_HAVE_MAP_ANON
//     NGX_HAVE_MAP_DEVZERO
//     NGX_HAVE_SYSVSHM
// ���Ȃ��Ƃ��A��L�̂ǂꂩ�ЂƂ��I�b�P�[�łȂ��ƃ_��
// �����}�b�s���O�̕��@�R��ނ����̃}�N���Ŏw�肷��
// https://ja.wikipedia.org/wiki/Mmap#%E3%83%95%E3%82%A1%E3%82%A4%E3%83%AB%E3%83%9E%E3%83%83%E3%83%94%E3%83%B3%E3%82%B0


#if (NGX_HAVE_MAP_ANON)


/**
 * @brief
 *     ���L�����������蓖�Ă�
 *     �����ł������L�������͈ȉ��̓���������
 *         �E�ǂݏ����\
 *         �E�ǂ̓���̃t�@�C���Ƃ��R�Â��Ă��Ȃ�
 *         �E�T�C�Y�� shm->size ��
 *         �E�����v���Z�X�ԂŕύX�����L�����
 * @param[in:out]
 *     shm: ���L�������i���炩���� shm->size �ɒl���Z�b�g���Ă������Ɓj
 * @retval
 *     NGX_OK: ����
 *     NGX_ERROR: ���s
 */
ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    // mmap() �Ń}�b�s���O�����̈�̓v���Z�X���܂����ŋ��L�ł���Ƃ�������������
    // ������A�p�C�v�ɂ����@�����邪�Ammap() �̕��������������ł���
    // https://www.atmarkit.co.jp/ait/articles/1205/28/news125.html
    /**
     * @brief
     *     �t�@�C�������z�A�h���X��Ԃփ}�b�s���O����
     * @param[in]
     *     addr: ���蓖�Ă̍ۂ̃q���g�Ɏg�p����iNULL �ł��j
     *     len: ���蓖�Ă����T�C�Y�i�y�[�W�T�C�Y�̔{���łȂ��Ă��悢�j�i���̕����D�܂������ǁj
     *     prot: �ی�̈�ւ̃A�N�Z�X�����w�肷��
     *         �f�t�H���g: �y�[�W�̓A�N�Z�X�ł��Ȃ�
     *         PROT_READ: �y�[�W�͓ǂݎ��\
     *         PROT_WRITE: �y�[�W�͏������݉\
     *         PROT_EXEC: �y�[�W�͎��s�\
     *     flags: �}�b�v�����̈�̃^�C�v
     *         MAP_ANON: �ǂ̃t�@�C���Ƃ��Ή����Ȃ������������̈�
     *         MAP_FIXED: �������Ŏw�肵���A�h���X���擾�ł��Ȃ��Ȃ玸�s����
     *         MAP_HASSEMAPHORE: �̈�ɃZ�}�t�H���܂܂�Ă��邱�Ƃ��J�[�l���֒ʒm����
     *         MAP_INHERIT: �T�|�[�g����Ă��Ȃ��i�������ăI�b�P�[�j
     *         MAP_PRIVATE: �C���̓v���Z�X���ƂɌŗL�ōs����(�P��v���Z�X�Ŏg���p�j
     *         MAP_SHARED:   �C���̓v���Z�X�ŋ��L�ɍs����i�����v���Z�X�ŋ��L����p�j
     *         �ȂǂȂǁE�E�E
     *     fd: �R�Â������t�@�C���̃f�B�X�N���v�^
     *     offset: ��T�����Ŏw�肵���t�@�C���� offset ����I���܂ł̓��e���}�b�s���O
     * @retval
     *     �擾�������L�������ւ̃A�h���X
     * @detail
     *     �ǂݏ����\�ŁA�ǂ̓���̃t�@�C���Ƃ��R�Â��Ă��Ȃ�
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
 *     ���L���������J������B�K����������B
 * @param[in]
 *     shm: �J�����������L������
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
 *     ���L���������J������B�K����������B
 * @param[in]
 *     shm: �J�����������L������
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
