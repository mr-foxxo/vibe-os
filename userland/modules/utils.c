#include <userland/modules/include/utils.h>

int str_len(const char *s) {
    int n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

__attribute__((noinline, optimize("O0")))
int str_eq(const char *a, const char *b) {
    for (;;) {
        char ca = *a;
        char cb = *b;

        if (ca != cb) {
            return 0;
        }
        if (ca == '\0') {
            return 1;
        }

        ++a;
        ++b;
    }
}

int to_upper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    return c;
}

int str_eq_ci(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (to_upper(*a) != to_upper(*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

void str_copy_limited(char *dst, const char *src, int max_len) {
    int i = 0;
    while (src[i] != '\0' && i < (max_len - 1)) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

void str_append(char *dst, const char *src, int max_len) {
    int len = str_len(dst);
    int i = 0;

    while (src[i] != '\0' && (len + i) < (max_len - 1)) {
        dst[len + i] = src[i];
        ++i;
    }
    dst[len + i] = '\0';
}

char *skip_spaces(char *s) {
    while (*s == ' ') {
        ++s;
    }
    return s;
}

char *next_token(char **cursor) {
    char *start = skip_spaces(*cursor);
    char *p;

    if (*start == '\0') {
        *cursor = start;
        return 0;
    }

    p = start;
    while (*p != '\0' && *p != ' ') {
        ++p;
    }

    if (*p != '\0') {
        *p = '\0';
        ++p;
    }

    *cursor = p;
    return start;
}

int point_in_rect(const struct rect *r, int x, int y) {
    return x >= r->x && x < (r->x + r->w) && y >= r->y && y < (r->y + r->h);
}

struct rect window_close_button(const struct rect *w) {
    struct rect close = {w->x + w->w - 14, w->y + 2, 10, 10};
    return close;
}

struct rect window_max_button(const struct rect *w) {
    struct rect max = {w->x + w->w - 26, w->y + 2, 10, 10};
    return max;
}

struct rect window_min_button(const struct rect *w) {
    struct rect min = {w->x + w->w - 38, w->y + 2, 10, 10};
    return min;
}

struct rect window_resize_grip(const struct rect *w) {
    struct rect grip = {w->x + w->w - 12, w->y + w->h - 12, 12, 12};
    return grip;
}
