#include <compat/posix/errno.h>
#include <ndbm.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int dummy;
} DBM_impl;

DBM *dbm_open(const char *name, int flags, mode_t mode) {
    (void)name;
    (void)flags;
    (void)mode;
    errno = ENOSYS;
    return 0;
}

void dbm_close(DBM *db) {
    (void)db;
}

int dbm_delete(DBM *db, datum key) {
    (void)db;
    (void)key;
    errno = ENOSYS;
    return -1;
}

int dbm_error(DBM *db) {
    (void)db;
    return 0;
}

datum dbm_fetch(DBM *db, datum key) {
    datum d;
    (void)db;
    (void)key;
    d.dptr = 0;
    d.dsize = 0;
    return d;
}

datum dbm_firstkey(DBM *db) {
    datum d;
    (void)db;
    d.dptr = 0;
    d.dsize = 0;
    return d;
}

datum dbm_nextkey(DBM *db) {
    datum d;
    (void)db;
    d.dptr = 0;
    d.dsize = 0;
    return d;
}

int dbm_store(DBM *db, datum key, datum content, int flags) {
    (void)db;
    (void)key;
    (void)content;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int dbm_clearerr(DBM *db) {
    (void)db;
    return 0;
}

int dbm_dirfno(DBM *db) {
    (void)db;
    return -1;
}

int dbm_rdonly(DBM *db) {
    (void)db;
    return 0;
}
