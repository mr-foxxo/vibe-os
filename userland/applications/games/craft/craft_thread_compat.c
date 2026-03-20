#include <userland/applications/games/craft/upstream/src/tinycthread.h>

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    if (thr) {
        thr->active = 1;
    }
    if (func) {
        (void)func(arg);
    }
    return thrd_success;
}

int thrd_join(thrd_t thr, int *res) {
    (void)thr;
    if (res) {
        *res = 0;
    }
    return thrd_success;
}

int mtx_init(mtx_t *mtx, int type) {
    (void)type;
    if (mtx) {
        mtx->locked = 0;
    }
    return thrd_success;
}

void mtx_destroy(mtx_t *mtx) {
    (void)mtx;
}

int mtx_lock(mtx_t *mtx) {
    if (mtx) {
        mtx->locked = 1;
    }
    return thrd_success;
}

int mtx_unlock(mtx_t *mtx) {
    if (mtx) {
        mtx->locked = 0;
    }
    return thrd_success;
}

int cnd_init(cnd_t *cond) {
    if (cond) {
        cond->signaled = 0;
    }
    return thrd_success;
}

void cnd_destroy(cnd_t *cond) {
    (void)cond;
}

int cnd_signal(cnd_t *cond) {
    if (cond) {
        cond->signaled = 1;
    }
    return thrd_success;
}

int cnd_wait(cnd_t *cond, mtx_t *mtx) {
    (void)mtx;
    if (cond) {
        cond->signaled = 0;
    }
    return thrd_success;
}
