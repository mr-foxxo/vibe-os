#ifndef SECTORC_INTERNAL_H
#define SECTORC_INTERNAL_H

#include <userland/sectorc/include/sectorc.h>

void sectorc_write(const char *text);
void sectorc_write_line(const char *text);
void sectorc_putc(char c);
void sectorc_write_int(int value);
int sectorc_read_source(const char *path, const char **out_source);
void sectorc_copy_string(char *dst, const char *src, int max_len);
int sectorc_string_equal(const char *a, const char *b);
int sectorc_string_length(const char *text);
void sectorc_set_error(char *dst, int max_len, const char *message, int line);

#endif // SECTORC_INTERNAL_H
