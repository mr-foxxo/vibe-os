#ifndef UTILS_H
#define UTILS_H

#include "userland_api.h"

/* simple rectangle type used by various UI routines */
struct rect {
    int x;
    int y;
    int w;
    int h;
};

int str_len(const char *s);
int str_eq(const char *a, const char *b);
int str_eq_ci(const char *a, const char *b);
void str_copy_limited(char *dst, const char *src, int max_len);
void str_append(char *dst, const char *src, int max_len);
char *skip_spaces(char *s);
char *next_token(char **cursor);
int to_upper(int c);
int point_in_rect(const struct rect *r, int x, int y);
struct rect window_close_button(const struct rect *w);

#endif // UTILS_H