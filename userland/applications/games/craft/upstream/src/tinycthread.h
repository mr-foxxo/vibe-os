#ifndef VIBE_CRAFT_TINYCTHREAD_H
#define VIBE_CRAFT_TINYCTHREAD_H

#define thrd_error 0
#define thrd_success 1
#define thrd_timeout 2
#define thrd_busy 3
#define thrd_nomem 4

#define mtx_plain 1
#define mtx_timed 2
#define mtx_try 4
#define mtx_recursive 8

typedef int (*thrd_start_t)(void *);
typedef struct { int active; } thrd_t;
typedef struct { int locked; } mtx_t;
typedef struct { int signaled; } cnd_t;

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
int thrd_join(thrd_t thr, int *res);
int mtx_init(mtx_t *mtx, int type);
void mtx_destroy(mtx_t *mtx);
int mtx_lock(mtx_t *mtx);
int mtx_unlock(mtx_t *mtx);
int cnd_init(cnd_t *cond);
void cnd_destroy(cnd_t *cond);
int cnd_signal(cnd_t *cond);
int cnd_wait(cnd_t *cond, mtx_t *mtx);

#endif
