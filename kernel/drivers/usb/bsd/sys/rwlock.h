#ifndef _SYS_RWLOCK_H_
#define _SYS_RWLOCK_H_

struct rwlock { int dummy; };

void rw_init(struct rwlock *rw, const char *name);
void rw_enter_read(struct rwlock *rw);
void rw_enter_write(struct rwlock *rw);
void rw_exit(struct rwlock *rw);
void rw_assert_anylock(struct rwlock *rw);
void rw_assert_wrlock(struct rwlock *rw);

#endif
