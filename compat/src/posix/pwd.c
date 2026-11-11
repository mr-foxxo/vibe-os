#include <compat/posix/errno.h>
#include <sys/cdefs.h>
#include <pwd.h>
#include <string.h>
#include <stddef.h>

static char g_pw_name[] = "root";
static char g_pw_passwd[] = "*";
static char g_pw_class[] = "staff";
static char g_pw_gecos[] = "root";
static char g_pw_dir[] = "/";
static char g_pw_shell[] = "/bin/sh";
static struct passwd g_root_pwd = {
    .pw_name = g_pw_name,
    .pw_passwd = g_pw_passwd,
    .pw_uid = 0,
    .pw_gid = 0,
    .pw_change = 0,
    .pw_class = g_pw_class,
    .pw_gecos = g_pw_gecos,
    .pw_dir = g_pw_dir,
    .pw_shell = g_pw_shell,
    .pw_expire = 0
};

static int g_pwd_iterated = 0;
static struct passwd *g_pwd_ent = 0;

struct passwd *getpwuid(uid_t uid) {
    if (uid == 0) return &g_root_pwd;
    errno = ENOENT;
    return 0;
}

struct passwd *getpwnam(const char *name) {
    if (name != 0 && strcmp(name, "root") == 0) return &g_root_pwd;
    errno = ENOENT;
    return 0;
}

struct passwd *getpwuid_shadow(uid_t uid) {
    return getpwuid(uid);
}

struct passwd *getpwnam_shadow(const char *name) {
    return getpwnam(name);
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result) {
    (void)buffer;
    (void)bufsize;
    if (uid == 0) {
        *pwd = g_root_pwd;
        *result = pwd;
        return 0;
    }
    *result = 0;
    return ENOENT;
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result) {
    (void)buffer;
    (void)bufsize;
    if (name != 0 && strcmp(name, "root") == 0) {
        *pwd = g_root_pwd;
        *result = pwd;
        return 0;
    }
    *result = 0;
    return ENOENT;
}

struct passwd *getpwent(void) {
    if (g_pwd_ent == 0 && !g_pwd_iterated) {
        g_pwd_ent = &g_root_pwd;
        return g_pwd_ent;
    }
    g_pwd_iterated = 1;
    g_pwd_ent = 0;
    return 0;
}

void setpwent(void) {
    g_pwd_iterated = 0;
    g_pwd_ent = 0;
}

void endpwent(void) {
    g_pwd_iterated = 0;
    g_pwd_ent = 0;
}
