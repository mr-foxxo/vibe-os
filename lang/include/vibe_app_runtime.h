#ifndef VIBE_LANG_VIBE_APP_RUNTIME_H
#define VIBE_LANG_VIBE_APP_RUNTIME_H

#include <stddef.h>
#include <stdarg.h>

#include <lang/include/vibe_app.h>

const struct vibe_app_context *vibe_app_get_context(void);
void vibe_app_console_putc(char c);
void vibe_app_console_write(const char *text);
int vibe_app_poll_key(void);
void vibe_app_yield(void);
int vibe_app_read_file(const char *path, const char **data_out, int *size_out);
int vibe_app_getcwd(char *buf, int max_len);
int vibe_app_remove_dir(const char *path);
int vibe_app_keyboard_set_layout(const char *name);
int vibe_app_keyboard_get_layout(char *buf, int max_len);
int vibe_app_keyboard_get_available_layouts(char *buf, int max_len);
int vibe_app_read_line(char *buf, int max_len, const char *prompt);

void *vibe_app_malloc(size_t size);
void vibe_app_free(void *ptr);
void *vibe_app_realloc(void *ptr, size_t size);
void vibe_app_runtime_init(const struct vibe_app_context *ctx);

/* Memory functions */
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *memcpy(void *dst, const void *src, size_t size);
void *memmove(void *dst, const void *src, size_t size);
void *memset(void *dst, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);

/* String functions */
size_t strlen(const char *text);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t size);
char *strcpy(char *dst, const char *src);
char *strchr(const char *text, int c);

/* FILE I/O */
typedef struct FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);

int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

FILE *fopen(const char *filename, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputs(const char *s, FILE *stream);
int puts(const char *s);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);

int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);

int getchar(void);
int putchar(int c);
int getc(FILE *stream);
int putc(int c, FILE *stream);

int vibe_app_main(int argc, char **argv);

#endif
