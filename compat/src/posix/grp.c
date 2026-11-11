#include <compat/posix/errno.h>
#include <sys/cdefs.h>
#include <grp.h>
#include <string.h>

static char g_root_name[] = "root";
static char g_root_passwd[] = "*";
static char *g_root_mem[] = { "root", 0 };
static struct group g_root_group = {
    .gr_name = g_root_name,
    .gr_passwd = g_root_passwd,
    .gr_gid = 0,
    .gr_mem = g_root_mem
};

static int g_grp_iterated = 0;
static struct group *g_grp_ent = 0;

struct group *getgrgid(gid_t gid) {
    if (gid == 0) return &g_root_group;
    errno = ENOENT;
    return 0;
}

struct group *getgrnam(const char *name) {
    if (name != 0 && strcmp(name, "root") == 0) return &g_root_group;
    errno = ENOENT;
    return 0;
}

struct group *getgrent(void) {
    if (g_grp_ent == 0 && !g_grp_iterated) {
        g_grp_ent = &g_root_group;
        return g_grp_ent;
    }
    g_grp_iterated = 1;
    g_grp_ent = 0;
    return 0;
}

void setgrent(void) {
    g_grp_iterated = 0;
    g_grp_ent = 0;
}

void endgrent(void) {
    g_grp_iterated = 0;
    g_grp_ent = 0;
}

int getgrgid_r(gid_t gid, struct group *grp, char *buffer, size_t bufsize, struct group **result) {
    (void)buffer;
    (void)bufsize;
    if (gid == 0) {
        *grp = g_root_group;
        *result = grp;
        return 0;
    }
    *result = 0;
    return ENOENT;
}

int getgrnam_r(const char *name, struct group *grp, char *buffer, size_t bufsize, struct group **result) {
    (void)buffer;
    (void)bufsize;
    if (name != 0 && strcmp(name, "root") == 0) {
        *grp = g_root_group;
        *result = grp;
        return 0;
    }
    *result = 0;
    return ENOENT;
}
