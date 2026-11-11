#ifndef VIBE_LANG_VIBE_APP_RUNTIME_H
#define VIBE_LANG_VIBE_APP_RUNTIME_H

#include <stddef.h>
#include <stdarg.h>
#include <sys/socket.h>

#include <lang/include/vibe_app.h>
#include <lang/include/vibe_audio_client.h>
#include <lang/include/vibe_network_client.h>

struct mk_network_ethernet_config;
struct mk_audio_event;

const struct vibe_app_context *vibe_app_get_context(void);
void vibe_app_console_putc(char c);
void vibe_app_console_write(const char *text);
int vibe_app_poll_key(void);
void vibe_app_yield(void);
int vibe_app_read_file(const char *path, const char **data_out, int *size_out);
int vibe_app_write_file(const char *path, const void *data, int size);
int vibe_app_create_dir(const char *path);
int vibe_app_open(const char *path, int flags);
int vibe_app_read(int fd, void *buf, int size);
int vibe_app_write(int fd, const void *buf, int size);
int vibe_app_close(int fd);
int vibe_app_lseek(int fd, int offset, int whence);
int vibe_app_sync(void);
int vibe_app_stat(const char *path, struct vibe_app_stat *stat_out);
int vibe_app_fstat(int fd, struct vibe_app_stat *stat_out);
const char *vibe_app_getenv(const char *name);
unsigned int vibe_app_ticks(void);
unsigned int vibe_app_clock_hz(void);
unsigned long long vibe_app_millis(void);
int vibe_app_sleep_ms(unsigned int ms);
int vibe_app_getcwd(char *buf, int max_len);
int vibe_app_remove_dir(const char *path);
int vibe_app_keyboard_set_layout(const char *name);
int vibe_app_keyboard_get_layout(char *buf, int max_len);
int vibe_app_keyboard_get_available_layouts(char *buf, int max_len);
int vibe_app_read_line(char *buf, int max_len, const char *prompt);
int vibe_app_audio_get_info(struct mk_audio_info *info);
int vibe_app_audio_get_status(struct audio_status *status);
int vibe_app_audio_set_params(const struct audio_swpar *params);
int vibe_app_audio_start(void);
int vibe_app_audio_stop(void);
int vibe_app_audio_write(const void *data, uint32_t size);
int vibe_app_audio_read(void *data, uint32_t size);
int vibe_app_audio_event_subscribe(void);
int vibe_app_audio_event_receive(struct mk_audio_event *event, uint32_t timeout_ticks);
int vibe_app_audio_get_control_info(uint32_t index, struct mk_audio_control_info *info);
int vibe_app_audio_mixer_read(mixer_ctrl_t *control);
int vibe_app_audio_mixer_write(const mixer_ctrl_t *control);
int vibe_app_network_get_info(struct mk_network_info *info);
int vibe_app_network_get_status(struct mk_network_status *status);
int vibe_app_network_socket_list(uint32_t index,
                                 struct mk_network_socket_list_entry *entry);
int vibe_app_network_scan(uint32_t index, struct mk_network_scan_info *info);
int vibe_app_network_connect_wifi(const struct mk_network_connect_request *request);
int vibe_app_network_connect_ethernet(const char *if_name);
int vibe_app_network_configure_ethernet(const struct mk_network_ethernet_config *config);
int vibe_app_network_disconnect(const char *if_name);
int vibe_app_network_socket(uint32_t domain, uint32_t type, uint32_t protocol);
int vibe_app_network_bind(int handle, const struct sockaddr *address, uint32_t address_length);
int vibe_app_network_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length);
int vibe_app_network_send(int handle, const void *data, uint32_t size);
int vibe_app_network_recv(int handle, void *buffer, uint32_t size);
int vibe_app_network_close(int handle);
int vibe_app_network_listen(int handle, int backlog);
int vibe_app_network_accept(int handle);
int vibe_app_network_event_subscribe(void);
int vibe_app_network_event_receive(struct mk_network_event *event, uint32_t timeout_ticks);

void *vibe_app_malloc(size_t size);
void vibe_app_free(void *ptr);
void *vibe_app_realloc(void *ptr, size_t size);
void vibe_app_runtime_init(const struct vibe_app_context *ctx);
void vibe_app_run_atexit(void);

typedef int (*vibe_app_thread_fn)(void *arg);

typedef struct vibe_app_thread {
    int in_use;
    int started;
    int finished;
    int detached;
    int id;
    vibe_app_thread_fn fn;
    void *arg;
    int result;
} vibe_app_thread_t;

typedef struct vibe_app_mutex {
    int initialized;
    int locked;
    int owner;
} vibe_app_mutex_t;

typedef struct vibe_app_cond {
    int initialized;
    unsigned int sequence;
} vibe_app_cond_t;

int vibe_app_thread_create(vibe_app_thread_t *thread, vibe_app_thread_fn fn, void *arg);
int vibe_app_thread_join(vibe_app_thread_t *thread, int *result_out);
int vibe_app_thread_detach(vibe_app_thread_t *thread);
int vibe_app_thread_yield(void);
int vibe_app_mutex_init(vibe_app_mutex_t *mutex);
int vibe_app_mutex_lock(vibe_app_mutex_t *mutex);
int vibe_app_mutex_trylock(vibe_app_mutex_t *mutex);
int vibe_app_mutex_unlock(vibe_app_mutex_t *mutex);
int vibe_app_cond_init(vibe_app_cond_t *cond);
int vibe_app_cond_signal(vibe_app_cond_t *cond);
int vibe_app_cond_broadcast(vibe_app_cond_t *cond);
int vibe_app_cond_wait(vibe_app_cond_t *cond, vibe_app_mutex_t *mutex);
int vibe_app_cond_timedwait_ms(vibe_app_cond_t *cond, vibe_app_mutex_t *mutex, unsigned int timeout_ms);

/* Memory functions */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
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
int atexit(void (*fn)(void));
void exit(int status);
void abort(void);

int vibe_app_main(int argc, char **argv);

#endif
