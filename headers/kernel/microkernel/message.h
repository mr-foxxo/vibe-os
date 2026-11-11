#ifndef KERNEL_MICROKERNEL_MESSAGE_H
#define KERNEL_MICROKERNEL_MESSAGE_H

#include <stddef.h>
#include <stdint.h>
#include <include/userland_api.h>

#define MK_MESSAGE_ABI_VERSION 1u
#define MK_MESSAGE_PAYLOAD_MAX 256u

enum mk_message_type {
    MK_MSG_NONE = 0,
    MK_MSG_HELLO = 1,
    MK_MSG_SERVICE_REGISTER = 2,
    MK_MSG_SERVICE_LOOKUP = 3,
    MK_MSG_BLOCK_READ = 16,
    MK_MSG_BLOCK_WRITE = 17,
    MK_MSG_BLOCK_INFO = 18,
    MK_MSG_FS_OPEN = 32,
    MK_MSG_FS_READ = 33,
    MK_MSG_FS_WRITE = 34,
    MK_MSG_FS_CLOSE = 35,
    MK_MSG_FS_LSEEK = 36,
    MK_MSG_FS_STAT = 37,
    MK_MSG_FS_FSTAT = 38,
    MK_MSG_CONSOLE_WRITE_DEBUG = 40,
    MK_MSG_CONSOLE_TEXT_CLEAR = 41,
    MK_MSG_CONSOLE_TEXT_PUTC = 42,
    MK_MSG_CONSOLE_CURSOR_MOVE = 43,
    MK_MSG_CONSOLE_TEXT_WRITE = 44,
    MK_MSG_CONSOLE_VT_SWITCH = 45,
    MK_MSG_VIDEO_MODE_SET = 48,
    MK_MSG_VIDEO_CLEAR = 49,
    MK_MSG_VIDEO_RECT = 50,
    MK_MSG_VIDEO_TEXT = 51,
    MK_MSG_VIDEO_FLIP = 52,
    MK_MSG_VIDEO_LEAVE = 53,
    MK_MSG_VIDEO_BLIT8 = 54,
    MK_MSG_VIDEO_SET_PALETTE = 55,
    MK_MSG_VIDEO_GET_PALETTE = 56,
    MK_MSG_VIDEO_GET_INFO = 57,
    MK_MSG_VIDEO_GET_CAPS = 58,
    MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT = 59,
    MK_MSG_VIDEO_BLIT8_STRETCH = 60,
    MK_MSG_VIDEO_TEXT_INLINE = 61,
    MK_MSG_VIDEO_BLIT8_INLINE = 62,
    MK_MSG_VIDEO_BLIT8_STRETCH_INLINE = 63,
    MK_MSG_VIDEO_BLIT8_PRESENT = 64,
    MK_MSG_VIDEO_BLIT8_PRESENT_INLINE = 65,
    MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT_INLINE = 66,
    MK_MSG_INPUT_EVENT = 67,
    MK_MSG_INPUT_MOUSE_POLL = 68,
    MK_MSG_INPUT_KEY_READ = 69,
    MK_MSG_INPUT_SET_LAYOUT = 70,
    MK_MSG_INPUT_GET_LAYOUT = 71,
    MK_MSG_INPUT_GET_AVAILABLE_LAYOUTS = 72,
    MK_MSG_VIDEO_PRESENT_SUBMIT = 73,
    MK_MSG_VIDEO_SET_PRESENT_POLICY = 74,
    MK_MSG_VIDEO_SET_PRESENT_COPY_OVERRIDE = 75,
    MK_MSG_NET_SOCKET = 80,
    MK_MSG_NET_BIND = 81,
    MK_MSG_NET_CONNECT = 82,
    MK_MSG_NET_SEND = 83,
    MK_MSG_NET_RECV = 84,
    MK_MSG_NET_SETSOCKOPT = 85,
    MK_MSG_NET_GETSOCKOPT = 86,
    MK_MSG_NET_GETINFO = 87,
    MK_MSG_NET_GET_STATUS = 88,
    MK_MSG_NET_SCAN = 89,
    MK_MSG_NET_CONNECT_WIFI = 90,
    MK_MSG_NET_DISCONNECT = 91,
    MK_MSG_NET_CONNECT_ETHERNET = 92,
    MK_MSG_NET_CONFIGURE_ETHERNET = 93,
    MK_MSG_AUDIO_GETINFO = 96,
    MK_MSG_AUDIO_GET_STATUS = 97,
    MK_MSG_AUDIO_GET_PARAMS = 98,
    MK_MSG_AUDIO_SET_PARAMS = 99,
    MK_MSG_AUDIO_START = 100,
    MK_MSG_AUDIO_STOP = 101,
    MK_MSG_AUDIO_WRITE = 102,
    MK_MSG_AUDIO_READ = 103,
    MK_MSG_AUDIO_MIXER_READ = 104,
    MK_MSG_AUDIO_MIXER_WRITE = 105,
    MK_MSG_AUDIO_CONTROL_INFO = 106,
    MK_MSG_AUDIO_WRITE_ASYNC = 107,
    MK_MSG_AUDIO_PLAY_ASSET = 108,
    /* Bug #19 item 0.12 (01/11): blocking key read served BY THE INPUT
     * HOST (payload uint32 timeout ticks; 0 = indefinite). The host is
     * the ONLY consumer of the driver key mailbox, so a key can never be
     * dequeued into a reply that a timed-out client abandons — the mixed
     * direct-mailbox / poll consumer model lost the `status` suffix of
     * `netmgrd status` exactly that way (key-cons trace, gate 19_ns9:
     * shell received `netmgrd ` + Enter, argc=1). Additive message type;
     * KEY_READ (69) is unchanged for every existing caller. */
    MK_MSG_INPUT_KEY_READ_WAIT = 109,

    /* Live socket-table export (closure item 1.5): index -> entry, like the
     * SCAN pattern. Additive; no existing message or struct is touched. */
    MK_MSG_NET_SOCKET_LIST = 110
};

struct mk_message {
    uint32_t abi_version;
    uint32_t type;
    uint32_t source_pid;
    uint32_t target_pid;
    uint32_t payload_size;
    uint8_t payload[MK_MESSAGE_PAYLOAD_MAX];
};

void mk_message_init(struct mk_message *message, uint32_t type);
int mk_message_validate(const struct mk_message *message);
int mk_message_set_payload(struct mk_message *message, const void *payload, size_t payload_size);
void mk_async_message_init(struct mk_async_message *message, uint32_t type);
int mk_async_message_validate(const struct mk_async_message *message);

#endif
