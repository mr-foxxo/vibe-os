#include <fnmatch.h>
#include <string.h>

#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0x10
#endif

int fnmatch(const char *pattern, const char *string, int flags) {
    const char *p = pattern;
    const char *s = string;
    char c;

    if (p == 0 || s == 0) return FNM_NOMATCH;

    while (*p != '\0') {
        c = *p;

        if ((flags & FNM_NOESCAPE) == 0 && c == '\\') {
            ++p;
            c = *p;
            if (c == '\0') return FNM_NOMATCH;
            if (*s == '\0') return FNM_NOMATCH;
            if (*s != c) return FNM_NOMATCH;
            ++p;
            ++s;
            continue;
        }

        if (c == '*') {
            ++p;
            if (*p == '\0') return 0;
            while (*s != '\0') {
                if (fnmatch(p, s, flags) == 0) return 0;
                ++s;
            }
            return FNM_NOMATCH;
        }

        if (c == '?') {
            if (*s == '\0') return FNM_NOMATCH;
            ++p;
            ++s;
            continue;
        }

        if (c == '[') {
            int invert = 0;
            int matched = 0;
            ++p;
            if (*p == '!') {
                invert = 1;
                ++p;
            }
            if (*p == '\0') return FNM_NOMATCH;
            if (*s == '\0') return FNM_NOMATCH;

            while (*p != ']' && *p != '\0') {
                char range_start;
                char range_end;

                if ((flags & FNM_NOESCAPE) == 0 && *p == '\\') {
                    ++p;
                    if (*p == '\0') break;
                }

                range_start = *p;
                ++p;
                if (*p == '-' && *(p + 1) != ']' && *(p + 1) != '\0') {
                    ++p;
                    if ((flags & FNM_NOESCAPE) == 0 && *p == '\\') {
                        ++p;
                        if (*p == '\0') break;
                    }
                    range_end = *p;
                    ++p;
                    {
                        char sc = *s;
                        char rs = range_start;
                        char re = range_end;
                        if ((flags & FNM_CASEFOLD) != 0) {
                            if (sc >= 'A' && sc <= 'Z') sc += 32;
                            if (rs >= 'A' && rs <= 'Z') rs += 32;
                            if (re >= 'A' && re <= 'Z') re += 32;
                        }
                        if (sc >= rs && sc <= re) matched = 1;
                    }
                } else {
                    char sc = *s;
                    char pc = range_start;
                    if ((flags & FNM_CASEFOLD) != 0) {
                        if (sc >= 'A' && sc <= 'Z') sc += 32;
                        if (pc >= 'A' && pc <= 'Z') pc += 32;
                    }
                    if (sc == pc) matched = 1;
                }
            }
            if (*p == ']') ++p;
            if ((invert && matched) || (!invert && !matched)) return FNM_NOMATCH;
            ++s;
            continue;
        }

        if (*s != c) {
            if ((flags & FNM_CASEFOLD) != 0) {
                char sc = *s;
                char pc = c;
                if (sc >= 'A' && sc <= 'Z') sc += 32;
                if (pc >= 'A' && pc <= 'Z') pc += 32;
                if (sc != pc) return FNM_NOMATCH;
            } else {
                return FNM_NOMATCH;
            }
        }
        ++p;
        ++s;
    }

    return (*s == '\0') ? 0 : FNM_NOMATCH;
}
