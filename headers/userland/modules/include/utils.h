#ifndef UTILS_H
#define UTILS_H

#include <include/userland_api.h>
#include <stddef.h>

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
int audio_play_wav_best_effort(const char *path, const char *tag);
const char *audio_last_playback_error(void);
const char *audio_last_playback_detail(void);
int audio_desktop_startup_wav_allowed(void);
int audio_backend_kind(void);

struct audio_async_playback {
    int active;
    int backend_kind;
    int node;
    int waiting_for_idle;
    int finalizing;
    int audio_event_subscription;
    int audio_event_valid;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t streamed;
    uint32_t idle_started;
    uint32_t idle_timeout;
    uint32_t last_chunk_ticks;
    uint32_t params_storage[16];
    struct mk_audio_event last_audio_event;
    int use_kernel_async;
    int builtin_wav;
    char tag[32];
    char path[80];
};

int audio_play_wav_async_start(struct audio_async_playback *playback, const char *path, const char *tag);
int audio_play_wav_async_poll(struct audio_async_playback *playback);

int point_in_rect(const struct rect *r, int x, int y);
struct rect window_close_button(const struct rect *w);
struct rect window_max_button(const struct rect *w);
struct rect window_min_button(const struct rect *w);
struct rect window_resize_grip(const struct rect *w);

#endif // UTILS_H
