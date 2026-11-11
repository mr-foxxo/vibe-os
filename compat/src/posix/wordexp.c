#include <wordexp.h>
#include <compat/posix/errno.h>
#include <compat/libc/string.h>
#include <compat/posix/unistd.h>
#include <stdlib.h>
#include <glob.h>

static char *expand_vars(const char *s) {
    char *result;
    size_t cap = 128;
    size_t pos = 0;
    const char *p;

    result = malloc(cap);
    if (!result) return 0;

    for (p = s; *p; p++) {
        if (*p == '~' && (p == s || *(p-1) == ' ')) {
            const char *home = getenv("HOME");
            if (!home) home = "/";
            while (*home) {
                if (pos + 1 >= cap) {
                    cap *= 2;
                    result = realloc(result, cap);
                    if (!result) return 0;
                }
                result[pos++] = *home++;
            }
        } else if (*p == '$') {
            const char *env;
            char varname[64];
            size_t vlen = 0;
            p++;
            if (*p == '{') {
                p++;
                while (*p && *p != '}' && vlen < sizeof(varname) - 1) {
                    varname[vlen++] = *p;
                    p++;
                }
                if (*p == '}') p++;
            } else {
                while (*p && ( (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                              (*p >= '0' && *p <= '9') || *p == '_') && vlen < sizeof(varname) - 1) {
                    varname[vlen++] = *p;
                    p++;
                }
            }
            varname[vlen] = '\0';
            env = getenv(varname);
            if (!env) env = "";
            while (*env) {
                if (pos + 1 >= cap) {
                    cap *= 2;
                    result = realloc(result, cap);
                    if (!result) return 0;
                }
                result[pos++] = *env++;
            }
            if (!*p) break;
        } else {
            if (pos + 1 >= cap) {
                cap *= 2;
                result = realloc(result, cap);
                if (!result) return 0;
            }
            result[pos++] = *p;
        }
    }
    result[pos] = '\0';
    return result;
}

int wordexp(const char *words, wordexp_t *p, int flags) {
    char *expanded;
    glob_t g;
    int rc;
    size_t i;
    int glob_flags = 0;

    if (!words || !p) {
        errno = EINVAL;
        return WRDE_SYNTAX;
    }

    if ((flags & WRDE_NOCMD) == 0) {
    }

    expanded = expand_vars(words);
    if (!expanded) return WRDE_NOSPACE;

    if ((flags & WRDE_DOOFFS) != 0) {
        glob_flags |= GLOB_DOOFFS;
    }

    rc = glob(expanded, glob_flags, 0, &g);
    free(expanded);

    if (rc == GLOB_NOSPACE) {
        return WRDE_NOSPACE;
    }
    if (rc == GLOB_NOMATCH) {
        if ((flags & WRDE_APPEND) != 0 && p->we_wordv != 0) {
        } else {
            p->we_wordc = 0;
            p->we_wordv = 0;
            p->we_offs = 0;
        }
        if ((flags & WRDE_REUSE) == 0) {
            p->we_wordc = 1;
            p->we_wordv = malloc(sizeof(char *));
            if (!p->we_wordv) return WRDE_NOSPACE;
            p->we_wordv[0] = strdup(words);
            if (!p->we_wordv[0]) {
                free(p->we_wordv);
                p->we_wordv = 0;
                return WRDE_NOSPACE;
            }
            p->we_wordc = 1;
        }
        return 0;
    }

    if ((flags & WRDE_APPEND) != 0 && p->we_wordv != 0) {
        size_t oldc = p->we_wordc;
        size_t newc = oldc + g.gl_pathc;
        char **newv = realloc(p->we_wordv, (newc + 1) * sizeof(char *));
        if (!newv) {
            globfree(&g);
            return WRDE_NOSPACE;
        }
        for (i = 0; i < g.gl_pathc; i++) {
          newv[oldc + i] = strdup(g.gl_pathv[i]);
          if (!newv[oldc + i]) {
            globfree(&g);
            return WRDE_NOSPACE;
          }
        }
        newv[newc] = 0;
        p->we_wordv = newv;
        p->we_wordc = newc;
    } else {
        if (p->we_wordv && (flags & WRDE_REUSE) != 0) {
            wordfree(p);
        }
        p->we_wordc = g.gl_pathc;
        p->we_wordv = malloc((g.gl_pathc + 1) * sizeof(char *));
        if (!p->we_wordv) {
            globfree(&g);
            return WRDE_NOSPACE;
        }
        for (i = 0; i < g.gl_pathc; i++) {
            p->we_wordv[i] = strdup(g.gl_pathv[i]);
            if (!p->we_wordv[i]) {
                globfree(&g);
                wordfree(p);
                return WRDE_NOSPACE;
            }
        }
        p->we_wordv[g.gl_pathc] = 0;
    }
    if ((flags & WRDE_DOOFFS) != 0 && p->we_offs > 0) {
        size_t offs = p->we_offs;
        size_t count = p->we_wordc;
        char **nv = malloc((offs + count + 1) * sizeof(char *));
        if (!nv) return WRDE_NOSPACE;
        for (i = 0; i < offs; i++) nv[i] = 0;
        for (i = 0; i < count; i++) nv[offs + i] = p->we_wordv[i];
        nv[offs + count] = 0;
        free(p->we_wordv);
        p->we_wordv = nv;
    }

    globfree(&g);
    return 0;
}

void wordfree(wordexp_t *p) {
    size_t i;
    if (!p || !p->we_wordv) return;
    for (i = 0; i < p->we_wordc; i++) {
        free(p->we_wordv[i]);
    }
    free(p->we_wordv);
    p->we_wordv = 0;
    p->we_wordc = 0;
}
