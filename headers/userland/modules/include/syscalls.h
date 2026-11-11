#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdint.h>
#include <include/userland_api.h>

struct mk_message;
struct audio_swpar;
struct audio_status;
struct mk_audio_info;
struct mk_audio_control_info;
struct mk_network_info;
struct mk_network_status;
struct mk_network_scan_info;
struct mk_network_connect_request;
struct mk_network_socket_list_entry;
struct mk_network_ethernet_config;
typedef struct mixer_ctrl mixer_ctrl_t;

int sys_poll_mouse(struct mouse_state *state);
int sys_poll_key(void);
int sys_poll_key_wait(unsigned int timeout_ticks);
int sys_next_input_event(struct input_event *event);
void sys_clear(uint8_t color);
void sys_rect(int x, int y, int w, int h, uint8_t color);
void sys_text(int x, int y, uint8_t color, const char *text);
void sys_present(void);
void sys_present_dirty(void);
void sys_present_full(void);
int sys_gfx_set_present_policy(uint32_t policy);
int sys_gfx_set_present_copy_override(uint32_t kind);
int sys_video_present_submit(uint32_t mode, uint32_t *sequence_out);
int sys_video_event_subscribe(void);
int sys_video_event_receive(struct mk_video_event *event, uint32_t timeout_ticks);
void sys_leave_graphics(void);
int sys_gfx_set_mode(uint32_t width, uint32_t height, uint8_t bpp);
int sys_gfx_set_palette(const uint8_t *rgb_triplets);
int sys_gfx_get_palette(uint8_t *rgb_triplets);
void sys_gfx_blit8(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale);
void sys_gfx_blit8_present(const uint8_t *src, int src_w, int src_h, int dst_x, int dst_y, int scale);
void sys_gfx_blit8_stretch(const uint8_t *src, int src_w, int src_h,
                           int dst_x, int dst_y, int dst_w, int dst_h);
void sys_gfx_blit8_stretch_present(const uint8_t *src, int src_w, int src_h,
                                   int dst_x, int dst_y, int dst_w, int dst_h);
int sys_storage_load(void *dst, uint32_t size);
int sys_storage_save(const void *src, uint32_t size);
int sys_storage_read_sectors(uint32_t lba, void *dst, uint32_t sector_count);
int sys_storage_write_sectors(uint32_t lba, const void *src, uint32_t sector_count);
uint32_t sys_storage_total_sectors(void);
uint32_t sys_storage_partition_start_lba(void);
int sys_storage_backend_load(void *dst, uint32_t size);
int sys_storage_backend_save(const void *src, uint32_t size);
int sys_storage_backend_read_sectors(uint32_t lba, void *dst, uint32_t sector_count);
int sys_storage_backend_write_sectors(uint32_t lba, const void *src, uint32_t sector_count);
uint32_t sys_storage_backend_total_sectors(void);
uint32_t sys_storage_backend_partition_start_lba(void);
int sys_open(const char *path, int flags);
int sys_read(int fd, void *buf, uint32_t count);
int sys_write(int fd, const void *buf, uint32_t count);
int sys_close(int fd);
off_t sys_lseek(int fd, off_t offset, int whence);
int sys_stat(const char *path, struct stat *buf);
int sys_fstat(int fd, struct stat *buf);
int sys_launch_info(struct userland_launch_info *info);
int sys_launch_builtin_user(uint32_t target);
int sys_task_snapshot(struct task_snapshot_summary *summary,
                      struct task_snapshot_entry *entries,
                      uint32_t max_entries);
int sys_memory_status(struct mk_memory_status *status);
int sys_memory_budget(struct mk_memory_budget *budget);
int sys_task_terminate(uint32_t pid);
int sys_task_create(uintptr_t entry, void *arg, uint32_t stack_size, uint32_t task_class);
int sys_launch_app(const char *name);
int sys_launch_app_argv(int argc, char **argv);
int sys_task_event_subscribe(void);
int sys_task_event_subscribe_mask(uint32_t event_mask, uint32_t task_class_mask);
int sys_task_event_receive(struct mk_task_event *event, uint32_t timeout_ticks);
void sys_sleep(void);
uint32_t sys_ticks(void);
int sys_gfx_info(struct video_mode *mode);
int sys_gfx_caps(struct video_capabilities *caps);
int sys_gfx_bench(struct video_bench_info *bench);
int sys_audio_get_info(struct mk_audio_info *info);
int sys_audio_get_status(struct audio_status *status);
int sys_audio_set_params(const struct audio_swpar *params);
int sys_audio_start(void);
int sys_audio_stop(void);
int sys_audio_write(const void *data, uint32_t size);
int sys_audio_write_async(const void *data, uint32_t size);
int sys_audio_event_subscribe(void);
int sys_audio_event_receive(struct mk_audio_event *event, uint32_t timeout_ticks);
int sys_audio_read(void *data, uint32_t size);
int sys_audio_control_info(uint32_t index, struct mk_audio_control_info *info);
int sys_audio_mixer_read(mixer_ctrl_t *control);
int sys_audio_mixer_write(const mixer_ctrl_t *control);
int sys_audio_play_asset(const char *path);
int sys_network_get_info(struct mk_network_info *info);
int sys_network_get_status(struct mk_network_status *status);
int sys_network_socket_list(uint32_t index, struct mk_network_socket_list_entry *entry);
int sys_network_scan(uint32_t index, struct mk_network_scan_info *info);
int sys_network_connect_wifi(const struct mk_network_connect_request *request);
int sys_network_connect_ethernet(const char *if_name);
int sys_network_configure_ethernet(const struct mk_network_ethernet_config *config);
int sys_network_disconnect(const char *if_name);
int sys_network_socket(uint32_t domain, uint32_t type, uint32_t protocol);
int sys_network_bind(int handle, const struct sockaddr *address, uint32_t address_length);
int sys_network_socket_connect(int handle, const struct sockaddr *address, uint32_t address_length);
int sys_network_send(int handle, const void *data, uint32_t size);
int sys_network_recv(int handle, void *buffer, uint32_t size);
int sys_network_sendto(int handle, const void *data, uint32_t size,
                       const struct sockaddr *dest_addr, uint32_t addrlen);
int sys_network_recvfrom(int handle, void *buffer, uint32_t size,
                         struct sockaddr *src_addr, uint32_t *addrlen);
int sys_network_close(int handle);
int sys_network_listen(int handle, int backlog);
int sys_network_accept(int handle);
int sys_network_event_subscribe(void);
int sys_network_event_receive(struct mk_network_event *event, uint32_t timeout_ticks);
int sys_getpid(void);
void sys_yield(void);
void sys_write_debug(const char *msg);
void sys_text_clear(void);
void sys_text_putc(char c);
int sys_text_write(const char *msg);
int sys_text_move_cursor(int delta);
int sys_keyboard_set_layout(const char *name);
int sys_keyboard_get_layout(char *buffer, int size);
int sys_keyboard_get_available_layouts(char *buffer, int size);
int sys_service_receive(struct mk_message *message);
int sys_service_send(const struct mk_message *message);
int sys_message_post(struct mk_async_message *message);
int sys_message_receive(struct mk_async_message *message, uint32_t timeout_ticks);
int sys_service_subscribe(uint32_t service_type);
int sys_service_pid(uint32_t service_type);
int sys_service_restart(uint32_t service_type);
int sys_service_health(uint32_t service_type, struct mk_service_health_stats *stats);
int sys_service_event_receive(uint32_t service_type,
                              struct mk_service_event *event,
                              uint32_t timeout_ticks);
uint32_t sys_transfer_size(uint32_t transfer_id);
int sys_transfer_read(uint32_t transfer_id, void *dst, uint32_t size);
int sys_transfer_write(uint32_t transfer_id, const void *src, uint32_t size);
void sys_shutdown(void);

#endif // SYSCALLS_H
