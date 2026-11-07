#include <kernel/microkernel/audio.h>
#include <kernel/microkernel/console.h>
#include <kernel/microkernel/filesystem.h>
#include <kernel/microkernel/input.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/network.h>
#include <kernel/microkernel/storage.h>
#include <kernel/microkernel/video.h>
#include <kernel/drivers/storage/ata.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>
#include <string.h>

#define INPUT_SERVICE_LAYOUT_NAME_MAX 64u
#define INPUT_SERVICE_LAYOUTS_TEXT_MAX 256u
#define CONSOLE_SERVICE_TEXT_MAX 4096u
#define CONSOLE_SERVICE_VT_COUNT 4u
#define CONSOLE_SERVICE_VT_ROWS 25u
#define CONSOLE_SERVICE_VT_COLS 80u
#define CONSOLE_SERVICE_VT_CELLS (CONSOLE_SERVICE_VT_ROWS * CONSOLE_SERVICE_VT_COLS)
#define CONSOLE_SERVICE_SCREEN_BUF_SIZE (CONSOLE_SERVICE_VT_CELLS * 2u)
#define STORAGE_SERVICE_BUFFER_MAX (1024u * 1024u)
#define FILESYSTEM_SERVICE_BUFFER_MAX (1024u * 1024u)
#define VIDEO_SERVICE_TEXT_MAX 4096u
#define VIDEO_SERVICE_BLIT_BUFFER_MAX (1024u * 1024u)
#define MK_NETWORK_SOCKET_ADDRESS_MAX 108

#define NETWORK_SERVICE_MAX_SOCKETS 32

struct network_service_socket {
    int fd;
    uint32_t domain;
    uint32_t type;
    uint32_t protocol;
    uint32_t flags;
    char address[MK_NETWORK_SOCKET_ADDRESS_MAX];
    uint32_t address_length;
    uint32_t rx_queue_size;
    uint32_t tx_queue_size;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t errors;
};

static struct network_service_socket g_network_service_sockets[NETWORK_SERVICE_MAX_SOCKETS];
static uint32_t g_network_service_socket_count = 0;

struct console_vt_state {
    uint8_t screen[CONSOLE_SERVICE_SCREEN_BUF_SIZE];
    uint8_t dirty;
    uint8_t cursor_x;
    uint8_t cursor_y;
};

static struct console_vt_state g_console_vt[CONSOLE_SERVICE_VT_COUNT];
static uint32_t g_console_active_vt = 0;
static uint8_t g_console_vt_initialized = 0;

static char g_console_service_text_buffer[CONSOLE_SERVICE_TEXT_MAX];
static uint8_t g_storage_service_buffer[STORAGE_SERVICE_BUFFER_MAX];
static uint8_t g_filesystem_service_buffer[FILESYSTEM_SERVICE_BUFFER_MAX];
static char g_video_service_text_buffer[VIDEO_SERVICE_TEXT_MAX];
static uint8_t g_video_service_blit_buffer[VIDEO_SERVICE_BLIT_BUFFER_MAX];
static uint8_t g_video_service_palette_buffer[MK_VIDEO_PALETTE_BYTES];

static void service_log_online(const struct userland_launch_info *info) {
    char buffer[64];

    if (info == 0) {
        return;
    }

    buffer[0] = '\0';
    str_append(buffer, "service-host: online ", (int)sizeof(buffer));
    str_append(buffer, info->name, (int)sizeof(buffer));
    str_append(buffer, "\n", (int)sizeof(buffer));
    sys_write_debug(buffer);
}

static int network_service_socket_valid_domain(uint32_t domain);
static int network_service_socket_valid_type(uint32_t type);
static int network_service_find_free_socket(void);
static int network_service_get_socket(int fd);

static void service_prepare_reply(struct mk_message *reply,
                                  const struct mk_message *request,
                                  uint32_t source_pid) {
    int i;

    if (reply == 0 || request == 0) {
        return;
    }

    reply->abi_version = MK_MESSAGE_ABI_VERSION;
    reply->type = request->type;
    reply->source_pid = source_pid;
    reply->target_pid = request->source_pid;
    reply->payload_size = 0u;
    for (i = 0; i < (int)sizeof(reply->payload); ++i) {
        reply->payload[i] = 0u;
    }
}

static int service_set_payload(struct mk_message *reply, const void *payload, uint32_t payload_size) {
    if (reply == 0 || payload_size > sizeof(reply->payload)) {
        return -1;
    }
    if (payload != 0 && payload_size != 0u) {
        memcpy(reply->payload, payload, payload_size);
    }
    reply->payload_size = payload_size;
    return 0;
}

static void service_prepare_error_reply(struct mk_message *reply,
                                        const struct mk_message *request,
                                        uint32_t source_pid) {
    service_prepare_reply(reply, request, source_pid);
}

static void service_log_mode(const char *name) {
    char buffer[64];

    if (name == 0 || name[0] == '\0') {
        return;
    }

    buffer[0] = '\0';
    str_append(buffer, "service-host: direct ", (int)sizeof(buffer));
    str_append(buffer, name, (int)sizeof(buffer));
    str_append(buffer, " loop\n", (int)sizeof(buffer));
    sys_write_debug(buffer);
}

static int console_service_reply_result(struct mk_message *reply,
                                        const struct mk_message *request,
                                        uint32_t source_pid,
                                        int value) {
    struct mk_console_result payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int console_service_load_text(uint32_t transfer_id,
                                     uint32_t length,
                                     char **text_out) {
    if (text_out == 0 || length == 0u || length + 1u > sizeof(g_console_service_text_buffer)) {
        return -1;
    }
    if (sys_transfer_size(transfer_id) < length + 1u) {
        return -1;
    }

    memset(g_console_service_text_buffer, 0, length + 1u);
    if (sys_transfer_read(transfer_id, g_console_service_text_buffer, length + 1u) != 0) {
        return -1;
    }
    if (g_console_service_text_buffer[length] != '\0') {
        return -1;
    }

    *text_out = g_console_service_text_buffer;
    return 0;
}

static void console_vt_init(void) {
    if (g_console_vt_initialized) return;
    for (uint32_t i = 0; i < CONSOLE_SERVICE_VT_COUNT; ++i) {
        memset(g_console_vt[i].screen, 0, CONSOLE_SERVICE_SCREEN_BUF_SIZE);
        g_console_vt[i].dirty = 0;
        g_console_vt[i].cursor_x = 0;
        g_console_vt[i].cursor_y = 0;
    }
    g_console_active_vt = 0;
    g_console_vt_initialized = 1;
}

static void console_vt_switch_to(uint32_t vt) {
    struct console_vt_state *old_vt;
    struct console_vt_state *new_vt;
    uint32_t old_active;

    if (vt >= CONSOLE_SERVICE_VT_COUNT || vt == g_console_active_vt) return;
    old_active = g_console_active_vt;
    old_vt = &g_console_vt[old_active];
    new_vt = &g_console_vt[vt];
    old_vt->dirty = 1;
    g_console_active_vt = vt;
    new_vt->dirty = 1;
    sys_text_clear();
    sys_text_move_cursor(0);
    {
        char buf[16];
        int len = 0;
        buf[len++] = '\r';
        for (uint32_t r = 0; r < CONSOLE_SERVICE_VT_ROWS && len < 14; ++r) {
            for (uint32_t c = 0; c < CONSOLE_SERVICE_VT_COLS && len < 14; ++c) {
                uint32_t off = (r * CONSOLE_SERVICE_VT_COLS + c) * 2;
                uint8_t ch = new_vt->screen[off];
                if (ch == 0) ch = ' ';
                buf[len++] = (char)ch;
            }
            if (r < CONSOLE_SERVICE_VT_ROWS - 1 && len < 14) {
                buf[len++] = '\n';
            }
        }
        buf[len] = '\0';
        sys_text_write(buf);
    }
}

static int console_service_handle_request(const struct mk_message *request,
                                          struct mk_message *reply,
                                          uint32_t source_pid) {
    if (request == 0 || reply == 0) {
        return -1;
    }

    switch (request->type) {
    case MK_MSG_CONSOLE_VT_SWITCH: {
        const struct mk_console_vt_switch_request *payload;
        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_console_vt_switch_request *)request->payload;
        console_vt_switch_to(payload->vt);
        return console_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_CONSOLE_WRITE_DEBUG:
    case MK_MSG_CONSOLE_TEXT_WRITE: {
        const struct mk_console_text_request *payload;
        char *text = 0;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_console_text_request *)request->payload;
        if (console_service_load_text(payload->transfer_id, payload->length, &text) != 0) {
            return console_service_reply_result(reply, request, source_pid, -1);
        }
        if (request->type == MK_MSG_CONSOLE_WRITE_DEBUG) {
            sys_write_debug(text);
            rc = 0;
        } else {
            rc = sys_text_write(text);
        }
        return console_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_CONSOLE_TEXT_CLEAR:
        if (request->payload_size != 0u) {
            return -1;
        }
        sys_text_clear();
        return console_service_reply_result(reply, request, source_pid, 0);
    case MK_MSG_CONSOLE_TEXT_PUTC: {
        const struct mk_console_putc_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_console_putc_request *)request->payload;
        sys_text_putc((char)(payload->character & 0xFFu));
        return console_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_CONSOLE_CURSOR_MOVE: {
        const struct mk_console_cursor_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_console_cursor_request *)request->payload;
        return console_service_reply_result(reply,
                                            request,
                                            source_pid,
                                            sys_text_move_cursor(payload->delta));
    }
    default:
        return -1;
    }
}

static int network_service_reply_result(struct mk_message *reply,
                                        const struct mk_message *request,
                                        uint32_t source_pid,
                                        int value) {
    struct mk_network_result payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int network_service_handle_request(const struct mk_message *request,
                                          struct mk_message *reply,
                                          uint32_t source_pid) {
    if (request == 0 || reply == 0) {
        return -1;
    }

    switch (request->type) {
    case MK_MSG_HELLO:
    case MK_MSG_NET_GETINFO: {
        struct mk_network_info info;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&info, 0, sizeof(info));
        if (sys_network_get_info(&info) != 0) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &info, sizeof(info));
    }
    case MK_MSG_NET_GET_STATUS: {
        struct mk_network_status status;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&status, 0, sizeof(status));
        if (sys_network_get_status(&status) != 0) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &status, sizeof(status));
    }
    case MK_MSG_NET_SCAN: {
        struct mk_network_scan_request scan_request;
        struct mk_network_scan_info info;

        if (request->payload_size != sizeof(scan_request)) {
            return -1;
        }
        memcpy(&scan_request, request->payload, sizeof(scan_request));
        scan_request.if_name[sizeof(scan_request.if_name) - 1u] = '\0';
        if (strcmp(scan_request.if_name, "wlan0") != 0) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }
        memset(&info, 0, sizeof(info));
        if (sys_network_scan(scan_request.index, &info) != 0) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &info, sizeof(info));
    }
    case MK_MSG_NET_CONNECT_WIFI:
        if (request->payload_size != sizeof(struct mk_network_connect_request)) {
            return -1;
        }
        return network_service_reply_result(reply,
                                            request,
                                            source_pid,
                                            sys_network_connect_wifi(
                                                (const struct mk_network_connect_request *)request->payload));
    case MK_MSG_NET_DISCONNECT: {
        struct mk_network_disconnect_request disconnect_request;

        if (request->payload_size != sizeof(disconnect_request)) {
            return -1;
        }
        memcpy(&disconnect_request, request->payload, sizeof(disconnect_request));
        disconnect_request.if_name[sizeof(disconnect_request.if_name) - 1u] = '\0';
        return network_service_reply_result(reply,
                                            request,
                                            source_pid,
                                            sys_network_disconnect(disconnect_request.if_name));
    }
    case MK_MSG_NET_CONNECT_ETHERNET: {
        struct mk_network_disconnect_request connect_request;

        if (request->payload_size != sizeof(connect_request)) {
            return -1;
        }
        memcpy(&connect_request, request->payload, sizeof(connect_request));
        connect_request.if_name[sizeof(connect_request.if_name) - 1u] = '\0';
        return network_service_reply_result(reply,
                                            request,
                                            source_pid,
                                            sys_network_connect_ethernet(connect_request.if_name));
    }
    case MK_MSG_NET_CONFIGURE_ETHERNET: {
        struct mk_network_ethernet_config config;

        if (request->payload_size != sizeof(config)) {
            return -1;
        }
        memcpy(&config, request->payload, sizeof(config));
        config.if_name[sizeof(config.if_name) - 1u] = '\0';
        config.ip_address[sizeof(config.ip_address) - 1u] = '\0';
        config.gateway[sizeof(config.gateway) - 1u] = '\0';
        config.dns_server[sizeof(config.dns_server) - 1u] = '\0';
        return network_service_reply_result(reply,
                                            request,
                                            source_pid,
                                            sys_network_configure_ethernet(&config));
    }
    case MK_MSG_NET_SOCKET: {
        const struct mk_network_socket_request *payload;
        int socket_index;
        int fd;

        if (request->payload_size != sizeof(struct mk_network_socket_request)) {
            return -1;
        }
        payload = (const struct mk_network_socket_request *)request->payload;

        if (!network_service_socket_valid_domain(payload->domain) ||
            !network_service_socket_valid_type(payload->type)) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        socket_index = network_service_find_free_socket();
        if (socket_index == -1) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        fd = sys_network_socket(payload->domain, payload->type, payload->protocol);
        if (fd == -1) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        g_network_service_sockets[socket_index].fd = fd;
        g_network_service_sockets[socket_index].domain = payload->domain;
        g_network_service_sockets[socket_index].type = payload->type;
        g_network_service_sockets[socket_index].protocol = payload->protocol;
        g_network_service_sockets[socket_index].flags = 0;
        g_network_service_socket_count++;

        return network_service_reply_result(reply, request, source_pid, fd);
    }
    case MK_MSG_NET_BIND: {
        const struct mk_network_name_request *payload;
        int socket_index;
        int rc;
        uint32_t address_length;
        struct sockaddr_storage address;

        if (request->payload_size != sizeof(struct mk_network_name_request)) {
            return -1;
        }
        payload = (const struct mk_network_name_request *)request->payload;

        socket_index = network_service_get_socket(payload->handle);
        if (socket_index == -1) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        address_length = payload->address_length;
        if (address_length == 0u || address_length > MK_NETWORK_SOCKET_ADDRESS_MAX) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }
        if (sys_transfer_size(payload->transfer_id) < address_length) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        if (sys_transfer_read(payload->transfer_id, &address, address_length) != 0) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        rc = sys_network_bind(payload->handle, (const struct sockaddr *)&address, address_length);
        if (rc == 0) {
            memcpy(g_network_service_sockets[socket_index].address, &address,
                   address_length < MK_NETWORK_SOCKET_ADDRESS_MAX ?
                       address_length : MK_NETWORK_SOCKET_ADDRESS_MAX);
            g_network_service_sockets[socket_index].address_length = address_length;
        }

        return network_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_NET_CONNECT: {
        const struct mk_network_name_request *payload;
        int socket_index;
        int rc;
        uint32_t address_length;
        struct sockaddr_storage address;

        if (request->payload_size != sizeof(struct mk_network_name_request)) {
            return -1;
        }
        payload = (const struct mk_network_name_request *)request->payload;

        socket_index = network_service_get_socket(payload->handle);
        if (socket_index == -1) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        address_length = payload->address_length;
        if (address_length == 0u || address_length > MK_NETWORK_SOCKET_ADDRESS_MAX) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }
        if (sys_transfer_size(payload->transfer_id) < address_length) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        if (sys_transfer_read(payload->transfer_id, &address, address_length) != 0) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        rc = sys_network_socket_connect(payload->handle, (const struct sockaddr *)&address, address_length);
        if (rc == 0) {
            memcpy(g_network_service_sockets[socket_index].address, &address,
                   address_length < MK_NETWORK_SOCKET_ADDRESS_MAX ?
                       address_length : MK_NETWORK_SOCKET_ADDRESS_MAX);
            g_network_service_sockets[socket_index].address_length = address_length;
        }

        return network_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_NET_SEND: {
        const struct mk_network_io_request *payload;
        int socket_index;
        int rc;
        uint32_t data_size;
        uint8_t transfer_buffer[MK_NETWORK_SOCKET_ADDRESS_MAX];

        if (request->payload_size != sizeof(struct mk_network_io_request)) {
            return -1;
        }
        payload = (const struct mk_network_io_request *)request->payload;

        socket_index = network_service_get_socket(payload->handle);
        if (socket_index == -1) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        data_size = payload->size;
        if (data_size > sizeof(transfer_buffer)) {
            /* Cap native send size to internal buffer for now */
            data_size = sizeof(transfer_buffer);
        }

        if (sys_transfer_size(payload->transfer_id) < data_size) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }
        if (sys_transfer_read(payload->transfer_id, transfer_buffer, data_size) != 0) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        rc = sys_network_send(payload->handle, transfer_buffer, data_size);
        if (rc > 0) {
            g_network_service_sockets[socket_index].tx_bytes += rc;
            g_network_service_sockets[socket_index].tx_packets++;
        }

        return network_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_NET_RECV: {
        const struct mk_network_io_request *payload;
        int socket_index;
        int rc;
        uint32_t max_size;
        uint8_t transfer_buffer[MK_NETWORK_SOCKET_ADDRESS_MAX];

        if (request->payload_size != sizeof(struct mk_network_io_request)) {
            return -1;
        }
        payload = (const struct mk_network_io_request *)request->payload;

        socket_index = network_service_get_socket(payload->handle);
        if (socket_index == -1) {
            return network_service_reply_result(reply, request, source_pid, -1);
        }

        max_size = payload->size;
        if (max_size > sizeof(transfer_buffer)) {
            max_size = sizeof(transfer_buffer);
        }

        rc = sys_network_recv(payload->handle, transfer_buffer, max_size);
        if (rc > 0) {
            g_network_service_sockets[socket_index].rx_bytes += rc;
            g_network_service_sockets[socket_index].rx_packets++;
            if (sys_transfer_write(payload->transfer_id, transfer_buffer, (uint32_t)rc) != 0) {
                return network_service_reply_result(reply, request, source_pid, -1);
            }
        }

        return network_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_NET_SETSOCKOPT:
    case MK_MSG_NET_GETSOCKOPT:
        if (request->payload_size != sizeof(struct mk_network_option_request)) {
            return -1;
        }
        return network_service_reply_result(reply, request, source_pid, -1);
    default:
        return -1;
    }
}

static int storage_service_reply_result(struct mk_message *reply,
                                        const struct mk_message *request,
                                        uint32_t source_pid,
                                        int value) {
    struct mk_storage_result payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int storage_service_handle_request(const struct mk_message *request,
                                          struct mk_message *reply,
                                          uint32_t source_pid) {
    if (request == 0 || reply == 0) {
        return -1;
    }

    switch (request->type) {
    case MK_MSG_BLOCK_READ: {
        if (request->payload_size == sizeof(struct mk_storage_sectors_request)) {
            const struct mk_storage_sectors_request *payload;
            uint32_t byte_count;
            int rc;

            payload = (const struct mk_storage_sectors_request *)request->payload;
            if (payload->sector_count == 0u ||
                payload->sector_count > (STORAGE_SERVICE_BUFFER_MAX / KERNEL_PERSIST_SECTOR_SIZE)) {
                return storage_service_reply_result(reply, request, source_pid, -1);
            }
            byte_count = payload->sector_count * KERNEL_PERSIST_SECTOR_SIZE;
            if (sys_transfer_size(payload->transfer_id) < byte_count) {
                return storage_service_reply_result(reply, request, source_pid, -1);
            }
            rc = sys_storage_backend_read_sectors(payload->lba,
                                                  g_storage_service_buffer,
                                                  payload->sector_count);
            if (rc == 0 &&
                sys_transfer_write(payload->transfer_id, g_storage_service_buffer, byte_count) != 0) {
                rc = -1;
            }
            return storage_service_reply_result(reply, request, source_pid, rc);
        }

        if (request->payload_size == sizeof(struct mk_storage_persist_request)) {
            const struct mk_storage_persist_request *payload;
            int rc;

            payload = (const struct mk_storage_persist_request *)request->payload;
            if (payload->size == 0u || payload->size > sizeof(g_storage_service_buffer) ||
                sys_transfer_size(payload->transfer_id) < payload->size) {
                return storage_service_reply_result(reply, request, source_pid, -1);
            }
            rc = sys_storage_backend_load(g_storage_service_buffer, payload->size);
            if (rc == 0 &&
                sys_transfer_write(payload->transfer_id, g_storage_service_buffer, payload->size) != 0) {
                rc = -1;
            }
            return storage_service_reply_result(reply, request, source_pid, rc);
        }

        return -1;
    }
    case MK_MSG_BLOCK_WRITE: {
        if (request->payload_size == sizeof(struct mk_storage_sectors_request)) {
            const struct mk_storage_sectors_request *payload;
            uint32_t byte_count;
            int rc;

            payload = (const struct mk_storage_sectors_request *)request->payload;
            if (payload->sector_count == 0u ||
                payload->sector_count > (STORAGE_SERVICE_BUFFER_MAX / KERNEL_PERSIST_SECTOR_SIZE)) {
                return storage_service_reply_result(reply, request, source_pid, -1);
            }
            byte_count = payload->sector_count * KERNEL_PERSIST_SECTOR_SIZE;
            if (sys_transfer_size(payload->transfer_id) < byte_count ||
                sys_transfer_read(payload->transfer_id, g_storage_service_buffer, byte_count) != 0) {
                return storage_service_reply_result(reply, request, source_pid, -1);
            }
            rc = sys_storage_backend_write_sectors(payload->lba,
                                                   g_storage_service_buffer,
                                                   payload->sector_count);
            return storage_service_reply_result(reply, request, source_pid, rc);
        }

        if (request->payload_size == sizeof(struct mk_storage_persist_request)) {
            const struct mk_storage_persist_request *payload;
            int rc;

            payload = (const struct mk_storage_persist_request *)request->payload;
            if (payload->size == 0u || payload->size > sizeof(g_storage_service_buffer) ||
                sys_transfer_size(payload->transfer_id) < payload->size ||
                sys_transfer_read(payload->transfer_id, g_storage_service_buffer, payload->size) != 0) {
                return storage_service_reply_result(reply, request, source_pid, -1);
            }
            rc = sys_storage_backend_save(g_storage_service_buffer, payload->size);
            return storage_service_reply_result(reply, request, source_pid, rc);
        }

        return -1;
    }
    case MK_MSG_HELLO:
    case MK_MSG_BLOCK_INFO: {
        struct mk_storage_info info;

        memset(&info, 0, sizeof(info));
        info.total_sectors = sys_storage_backend_total_sectors();
        info.partition_start_lba = sys_storage_backend_partition_start_lba();
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &info, sizeof(info));
    }
    default:
        return -1;
    }
}

static int filesystem_service_reply_result(struct mk_message *reply,
                                           const struct mk_message *request,
                                           uint32_t source_pid,
                                           int value) {
    struct mk_fs_result payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int filesystem_service_load_path(uint32_t transfer_id,
                                        uint32_t path_length,
                                        char **path_out) {
    char *path;

    if (path_out == 0 || path_length == 0u) {
        return -1;
    }
    if (path_length + 1u > sizeof(g_filesystem_service_buffer) ||
        sys_transfer_size(transfer_id) < path_length + 1u) {
        return -1;
    }

    path = (char *)g_filesystem_service_buffer;
    memset(path, 0, path_length + 1u);
    if (sys_transfer_read(transfer_id, path, path_length + 1u) != 0) {
        return -1;
    }
    if (path[path_length] != '\0') {
        return -1;
    }

    *path_out = path;
    return 0;
}

static int filesystem_service_handle_request(const struct mk_message *request,
                                             struct mk_message *reply,
                                             uint32_t source_pid) {
    if (request == 0 || reply == 0) {
        return -1;
    }

    switch (request->type) {
    case MK_MSG_FS_OPEN: {
        const struct mk_fs_open_request *payload;
        char *path = 0;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_open_request *)request->payload;
        if (filesystem_service_load_path(payload->path_transfer_id, payload->path_length, &path) != 0) {
            return filesystem_service_reply_result(reply, request, source_pid, -1);
        }
        rc = sys_open(path, (int)payload->flags);
        return filesystem_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_FS_READ: {
        const struct mk_fs_io_request *payload;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_io_request *)request->payload;
        if (payload->size > sizeof(g_filesystem_service_buffer) ||
            sys_transfer_size(payload->transfer_id) < payload->size) {
            return filesystem_service_reply_result(reply, request, source_pid, -1);
        }
        rc = sys_read(payload->fd, g_filesystem_service_buffer, payload->size);
        if (rc > 0 &&
            sys_transfer_write(payload->transfer_id, g_filesystem_service_buffer, (uint32_t)rc) != 0) {
            rc = -1;
        }
        return filesystem_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_FS_WRITE: {
        const struct mk_fs_io_request *payload;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_io_request *)request->payload;
        if (payload->size > sizeof(g_filesystem_service_buffer) ||
            sys_transfer_size(payload->transfer_id) < payload->size ||
            sys_transfer_read(payload->transfer_id, g_filesystem_service_buffer, payload->size) != 0) {
            return filesystem_service_reply_result(reply, request, source_pid, -1);
        }
        rc = sys_write(payload->fd, g_filesystem_service_buffer, payload->size);
        return filesystem_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_FS_CLOSE: {
        const struct mk_fs_close_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_close_request *)request->payload;
        return filesystem_service_reply_result(reply,
                                               request,
                                               source_pid,
                                               sys_close(payload->fd));
    }
    case MK_MSG_FS_LSEEK: {
        const struct mk_fs_seek_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_seek_request *)request->payload;
        return filesystem_service_reply_result(reply,
                                               request,
                                               source_pid,
                                               (int)sys_lseek(payload->fd,
                                                              (off_t)payload->offset,
                                                              payload->whence));
    }
    case MK_MSG_FS_STAT: {
        const struct mk_fs_stat_request *payload;
        char *path = 0;
        struct stat st;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_stat_request *)request->payload;
        if (filesystem_service_load_path(payload->path_transfer_id, payload->path_length, &path) != 0 ||
            sys_transfer_size(payload->stat_transfer_id) < sizeof(st)) {
            return filesystem_service_reply_result(reply, request, source_pid, -1);
        }
        memset(&st, 0, sizeof(st));
        rc = sys_stat(path, &st);
        if (rc == 0 &&
            sys_transfer_write(payload->stat_transfer_id, &st, (uint32_t)sizeof(st)) != 0) {
            rc = -1;
        }
        return filesystem_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_FS_FSTAT: {
        const struct mk_fs_fstat_request *payload;
        struct stat st;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_fstat_request *)request->payload;
        if (sys_transfer_size(payload->stat_transfer_id) < sizeof(st)) {
            return filesystem_service_reply_result(reply, request, source_pid, -1);
        }
        memset(&st, 0, sizeof(st));
        rc = sys_fstat(payload->fd, &st);
        if (rc == 0 &&
            sys_transfer_write(payload->stat_transfer_id, &st, (uint32_t)sizeof(st)) != 0) {
            rc = -1;
        }
        return filesystem_service_reply_result(reply, request, source_pid, rc);
    }
    default:
        return -1;
    }
}

static int input_service_reply_result(struct mk_message *reply,
                                      const struct mk_message *request,
                                      uint32_t source_pid,
                                      int value) {
    struct mk_input_result payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int input_service_reply_mouse(struct mk_message *reply,
                                     const struct mk_message *request,
                                     uint32_t source_pid,
                                     int value,
                                     const struct mouse_state *state) {
    struct mk_input_mouse_reply payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    memset(&payload.state, 0, sizeof(payload.state));
    if (state != 0) {
        payload.state = *state;
    }
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int input_service_reply_event(struct mk_message *reply,
                                     const struct mk_message *request,
                                     uint32_t source_pid,
                                     int value,
                                     const struct input_event *event) {
    struct mk_input_event_reply payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    memset(&payload.event, 0, sizeof(payload.event));
    if (event != 0) {
        payload.event = *event;
    }
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int input_service_handle_request(const struct mk_message *request,
                                        struct mk_message *reply,
                                        uint32_t source_pid) {
    if (request == 0 || reply == 0) {
        return -1;
    }

    switch (request->type) {
    case MK_MSG_INPUT_EVENT: {
        struct input_event event;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&event, 0, sizeof(event));
        if (!sys_next_input_event(&event)) {
            return input_service_reply_event(reply, request, source_pid, 0, 0);
        }
        return input_service_reply_event(reply, request, source_pid, 1, &event);
    }
    case MK_MSG_INPUT_MOUSE_POLL: {
        struct mouse_state state;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&state, 0, sizeof(state));
        if (!sys_poll_mouse(&state)) {
            return input_service_reply_mouse(reply, request, source_pid, 0, 0);
        }
        return input_service_reply_mouse(reply, request, source_pid, 1, &state);
    }
    case MK_MSG_INPUT_KEY_READ:
        if (request->payload_size != 0u) {
            return -1;
        }
        return input_service_reply_result(reply, request, source_pid, sys_poll_key());
    case MK_MSG_INPUT_KEY_READ_WAIT: {
        /* Bug #19 item 0.12 (01/11): blocking key read. This runs in the
         * INPUT host, so the syscall's service-worker branch waits on the
         * driver mailbox here and the host stays the only consumer of key
         * events. sys_poll_key_wait(timeout) returns the key or -1. */
        uint32_t timeout_ticks;

        if (request->payload_size != sizeof(timeout_ticks)) {
            return -1;
        }
        memcpy(&timeout_ticks, request->payload, sizeof(timeout_ticks));
        return input_service_reply_result(reply, request, source_pid,
                                          sys_poll_key_wait(timeout_ticks));
    }
    case MK_MSG_INPUT_SET_LAYOUT: {
        struct mk_input_layout_set_request payload;
        char name[INPUT_SERVICE_LAYOUT_NAME_MAX];

        if (request->payload_size != sizeof(payload)) {
            return -1;
        }
        memcpy(&payload, request->payload, sizeof(payload));
        if (payload.name_length == 0u || payload.name_length >= sizeof(name)) {
            return -1;
        }
        if (sys_transfer_size(payload.transfer_id) < payload.name_length + 1u) {
            return -1;
        }
        memset(name, 0, sizeof(name));
        if (sys_transfer_read(payload.transfer_id, name, payload.name_length + 1u) != 0) {
            return -1;
        }
        if (name[payload.name_length] != '\0') {
            return -1;
        }
        return input_service_reply_result(reply,
                                          request,
                                          source_pid,
                                          sys_keyboard_set_layout(name));
    }
    case MK_MSG_INPUT_GET_LAYOUT: {
        struct mk_input_transfer_request payload;
        char name[INPUT_SERVICE_LAYOUT_NAME_MAX];
        int length;

        if (request->payload_size != sizeof(payload)) {
            return -1;
        }
        memcpy(&payload, request->payload, sizeof(payload));
        if (payload.buffer_size == 0u) {
            return -1;
        }
        memset(name, 0, sizeof(name));
        length = sys_keyboard_get_layout(name, (int)sizeof(name));
        if (length < 0) {
            return -1;
        }
        if (payload.buffer_size <= (uint32_t)length ||
            sys_transfer_size(payload.transfer_id) < payload.buffer_size) {
            return input_service_reply_result(reply, request, source_pid, 0);
        }
        if (sys_transfer_write(payload.transfer_id, name, (uint32_t)length + 1u) != 0) {
            return -1;
        }
        return input_service_reply_result(reply, request, source_pid, length);
    }
    case MK_MSG_INPUT_GET_AVAILABLE_LAYOUTS: {
        struct mk_input_transfer_request payload;
        char buffer[INPUT_SERVICE_LAYOUTS_TEXT_MAX];
        uint32_t buffer_size;
        uint32_t copy_size;

        if (request->payload_size != sizeof(payload)) {
            return -1;
        }
        memcpy(&payload, request->payload, sizeof(payload));
        if (payload.buffer_size == 0u ||
            sys_transfer_size(payload.transfer_id) < payload.buffer_size) {
            return -1;
        }
        memset(buffer, 0, sizeof(buffer));
        buffer_size = payload.buffer_size;
        if (buffer_size > sizeof(buffer)) {
            buffer_size = sizeof(buffer);
        }
        sys_keyboard_get_available_layouts(buffer, (int)buffer_size);
        copy_size = (uint32_t)str_len(buffer) + 1u;
        if (copy_size > payload.buffer_size) {
            copy_size = payload.buffer_size;
        }
        if (sys_transfer_write(payload.transfer_id, buffer, copy_size) != 0) {
            return -1;
        }
        return input_service_reply_result(reply, request, source_pid, 0);
    }
    default:
        return -1;
    }
}

static int audio_service_reply_result(struct mk_message *reply,
                                      const struct mk_message *request,
                                      uint32_t source_pid,
                                      int value) {
    struct mk_audio_result payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int audio_service_ensure_fs_ready(void) {
    static int g_audio_service_fs_ready = 0;

    if (g_audio_service_fs_ready) {
        return 0;
    }

    sys_text_write("audiosvc: lazy fs_init begin\n");
    sys_write_debug("audiosvc: lazy fs_init begin\n");
    fs_init();
    g_audio_service_fs_ready = 1;
    sys_text_write("audiosvc: lazy fs_init done\n");
    sys_write_debug("audiosvc: lazy fs_init done\n");
    return 0;
}

static int audio_service_handle_request(const struct mk_message *request,
                                        struct mk_message *reply,
                                        uint32_t source_pid) {
    if (request == 0 || reply == 0) {
        return -1;
    }

    switch (request->type) {
    case MK_MSG_AUDIO_GETINFO: {
        struct mk_audio_info info;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&info, 0, sizeof(info));
        if (sys_audio_get_info(&info) != 0) {
            return -1;
        }
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &info, sizeof(info));
    }
    case MK_MSG_AUDIO_GET_STATUS: {
        struct audio_status status;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&status, 0, sizeof(status));
        if (sys_audio_get_status(&status) != 0) {
            return -1;
        }
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &status, sizeof(status));
    }
    case MK_MSG_AUDIO_SET_PARAMS:
        if (request->payload_size != sizeof(struct audio_swpar)) {
            return -1;
        }
        return audio_service_reply_result(reply,
                                          request,
                                          source_pid,
                                          sys_audio_set_params((const struct audio_swpar *)request->payload));
    case MK_MSG_AUDIO_START:
        if (request->payload_size != 0u) {
            return -1;
        }
        return audio_service_reply_result(reply, request, source_pid, sys_audio_start());
    case MK_MSG_AUDIO_STOP:
        if (request->payload_size != 0u) {
            return -1;
        }
        return audio_service_reply_result(reply, request, source_pid, sys_audio_stop());
    case MK_MSG_AUDIO_WRITE: {
        const struct mk_audio_write_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_audio_write_request *)request->payload;
        if (payload->size > MK_AUDIO_INLINE_WRITE_MAX) {
            return -1;
        }
        return audio_service_reply_result(reply,
                                          request,
                                          source_pid,
                                          sys_audio_write(payload->data, payload->size));
    }
    case MK_MSG_AUDIO_WRITE_ASYNC: {
        const struct mk_audio_write_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_audio_write_request *)request->payload;
        if (payload->size > MK_AUDIO_INLINE_WRITE_MAX) {
            return -1;
        }
        return audio_service_reply_result(reply,
                                          request,
                                          source_pid,
                                          sys_audio_write_async(payload->data, payload->size));
    }
    case MK_MSG_AUDIO_PLAY_ASSET: {
        const struct mk_audio_play_asset_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_audio_play_asset_request *)request->payload;
        if (payload->path[0] == '\0') {
            return audio_service_reply_result(reply, request, source_pid, -1);
        }
        if (audio_service_ensure_fs_ready() != 0) {
            return audio_service_reply_result(reply, request, source_pid, -1);
        }
        return audio_service_reply_result(reply,
                                          request,
                                          source_pid,
                                          audio_play_wav_best_effort(payload->path, "audio-service"));
    }
    case MK_MSG_AUDIO_READ: {
        const struct mk_audio_transfer_request *payload;
        struct mk_audio_read_reply read_reply;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_audio_transfer_request *)request->payload;
        if (payload->size > MK_AUDIO_INLINE_READ_MAX) {
            return -1;
        }
        memset(&read_reply, 0, sizeof(read_reply));
        rc = sys_audio_read(read_reply.data, payload->size);
        if (rc < 0) {
            return audio_service_reply_result(reply, request, source_pid, rc);
        }
        read_reply.size = (uint32_t)rc;
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &read_reply, sizeof(read_reply));
    }
    case MK_MSG_AUDIO_MIXER_READ: {
        mixer_ctrl_t control;

        if (request->payload_size != sizeof(control)) {
            return -1;
        }
        memcpy(&control, request->payload, sizeof(control));
        if (sys_audio_mixer_read(&control) != 0) {
            return audio_service_reply_result(reply, request, source_pid, -1);
        }
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &control, sizeof(control));
    }
    case MK_MSG_AUDIO_MIXER_WRITE:
        if (request->payload_size != sizeof(mixer_ctrl_t)) {
            return -1;
        }
        return audio_service_reply_result(reply,
                                          request,
                                          source_pid,
                                          sys_audio_mixer_write((const mixer_ctrl_t *)request->payload));
    case MK_MSG_AUDIO_CONTROL_INFO: {
        uint32_t index;
        struct mk_audio_control_info info;

        if (request->payload_size != sizeof(index)) {
            return -1;
        }
        memcpy(&index, request->payload, sizeof(index));
        memset(&info, 0, sizeof(info));
        if (sys_audio_control_info(index, &info) != 0) {
            return audio_service_reply_result(reply, request, source_pid, -1);
        }
        service_prepare_reply(reply, request, source_pid);
        return service_set_payload(reply, &info, sizeof(info));
    }
    default:
        return -1;
    }
}

static int video_service_reply_result(struct mk_message *reply,
                                      const struct mk_message *request,
                                      uint32_t source_pid,
                                      int value) {
    struct mk_video_result payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int video_service_reply_present(struct mk_message *reply,
                                       const struct mk_message *request,
                                       uint32_t source_pid,
                                       int value,
                                       uint32_t sequence) {
    struct mk_video_present_reply payload;

    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    payload.sequence = sequence;
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int video_service_reply_mode(struct mk_message *reply,
                                    const struct mk_message *request,
                                    uint32_t source_pid,
                                    int value,
                                    const struct video_mode *mode) {
    struct mk_video_mode_reply payload;

    memset(&payload, 0, sizeof(payload));
    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    if (mode != 0) {
        payload.mode = *mode;
    }
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int video_service_reply_caps(struct mk_message *reply,
                                    const struct mk_message *request,
                                    uint32_t source_pid,
                                    int value,
                                    const struct video_capabilities *caps) {
    struct mk_video_caps_reply payload;

    memset(&payload, 0, sizeof(payload));
    service_prepare_reply(reply, request, source_pid);
    payload.value = value;
    if (caps != 0) {
        payload.caps = *caps;
    }
    return service_set_payload(reply, &payload, sizeof(payload));
}

static int video_service_present_mode(uint32_t mode) {
    switch (mode) {
    case VIDEO_PRESENT_DIRTY:
        sys_present_dirty();
        return 0;
    case VIDEO_PRESENT_FULL:
        sys_present_full();
        return 0;
    case VIDEO_PRESENT_AUTO:
    default:
        sys_present();
        return 0;
    }
}

static int video_service_load_transfer(uint32_t transfer_id,
                                       uint32_t byte_count,
                                       void *dst,
                                       uint32_t capacity) {
    if (dst == 0 || byte_count == 0u || byte_count > capacity) {
        return -1;
    }
    if (sys_transfer_size(transfer_id) < byte_count) {
        return -1;
    }
    return sys_transfer_read(transfer_id, dst, byte_count);
}

static int video_service_handle_request(const struct mk_message *request,
                                        struct mk_message *reply,
                                        uint32_t source_pid) {
    if (request == 0 || reply == 0) {
        return -1;
    }

    switch (request->type) {
    case MK_MSG_VIDEO_CLEAR: {
        const struct mk_video_color_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_color_request *)request->payload;
        sys_clear((uint8_t)(payload->color & 0xFFu));
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_RECT: {
        const struct mk_video_rect_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_rect_request *)request->payload;
        sys_rect(payload->x,
                 payload->y,
                 payload->width,
                 payload->height,
                 (uint8_t)(payload->color & 0xFFu));
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_TEXT: {
        const struct mk_video_text_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_text_request *)request->payload;
        if (payload->text_length == 0u ||
            payload->text_length >= sizeof(g_video_service_text_buffer) ||
            sys_transfer_size(payload->transfer_id) < payload->text_length + 1u) {
            return -1;
        }
        memset(g_video_service_text_buffer, 0, sizeof(g_video_service_text_buffer));
        if (sys_transfer_read(payload->transfer_id,
                              g_video_service_text_buffer,
                              payload->text_length + 1u) != 0) {
            return -1;
        }
        if (g_video_service_text_buffer[payload->text_length] != '\0') {
            return -1;
        }
        sys_text(payload->x,
                 payload->y,
                 (uint8_t)(payload->color & 0xFFu),
                 g_video_service_text_buffer);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_TEXT_INLINE: {
        const struct mk_video_text_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_text_inline_request *)request->payload;
        if (payload->text_length == 0u ||
            payload->text_length >= MK_VIDEO_INLINE_TEXT_MAX ||
            payload->text[payload->text_length] != '\0') {
            return -1;
        }
        sys_text(payload->x,
                 payload->y,
                 (uint8_t)(payload->color & 0xFFu),
                 payload->text);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_FLIP:
        if (request->payload_size == 0u) {
            return video_service_reply_result(reply,
                                              request,
                                              source_pid,
                                              video_service_present_mode(VIDEO_PRESENT_AUTO));
        }
        if (request->payload_size == sizeof(struct mk_video_present_request)) {
            const struct mk_video_present_request *payload =
                (const struct mk_video_present_request *)request->payload;
            return video_service_reply_result(reply,
                                              request,
                                              source_pid,
                                              video_service_present_mode(payload->mode));
        }
        return -1;
    case MK_MSG_VIDEO_PRESENT_SUBMIT: {
        const struct mk_video_present_request *payload;
        uint32_t sequence = 0u;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_present_request *)request->payload;
        rc = sys_video_present_submit(payload->mode, &sequence);
        return video_service_reply_present(reply, request, source_pid, rc, sequence);
    }
    case MK_MSG_VIDEO_SET_PRESENT_POLICY: {
        const struct mk_video_u32_request *payload;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_u32_request *)request->payload;
        rc = sys_gfx_set_present_policy(payload->value);
        return video_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_VIDEO_SET_PRESENT_COPY_OVERRIDE: {
        const struct mk_video_u32_request *payload;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_u32_request *)request->payload;
        rc = sys_gfx_set_present_copy_override(payload->value);
        return video_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_VIDEO_LEAVE:
        if (request->payload_size != 0u) {
            return -1;
        }
        sys_leave_graphics();
        return video_service_reply_result(reply, request, source_pid, 0);
    case MK_MSG_VIDEO_BLIT8: {
        const struct mk_video_blit8_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->scale <= 0 ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height) ||
            video_service_load_transfer(payload->transfer_id,
                                        payload->byte_count,
                                        g_video_service_blit_buffer,
                                        sizeof(g_video_service_blit_buffer)) != 0) {
            return -1;
        }
        sys_gfx_blit8(g_video_service_blit_buffer,
                      payload->src_width,
                      payload->src_height,
                      payload->dst_x,
                      payload->dst_y,
                      payload->scale);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_BLIT8_PRESENT: {
        const struct mk_video_blit8_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->scale <= 0 ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height) ||
            video_service_load_transfer(payload->transfer_id,
                                        payload->byte_count,
                                        g_video_service_blit_buffer,
                                        sizeof(g_video_service_blit_buffer)) != 0) {
            return -1;
        }
        sys_gfx_blit8_present(g_video_service_blit_buffer,
                              payload->src_width,
                              payload->src_height,
                              payload->dst_x,
                              payload->dst_y,
                              payload->scale);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_BLIT8_INLINE: {
        const struct mk_video_blit8_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->scale <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        sys_gfx_blit8(payload->pixels,
                      payload->src_width,
                      payload->src_height,
                      payload->dst_x,
                      payload->dst_y,
                      payload->scale);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_BLIT8_PRESENT_INLINE: {
        const struct mk_video_blit8_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->scale <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        sys_gfx_blit8_present(payload->pixels,
                              payload->src_width,
                              payload->src_height,
                              payload->dst_x,
                              payload->dst_y,
                              payload->scale);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH: {
        const struct mk_video_blit8_stretch_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->dst_width <= 0 ||
            payload->dst_height <= 0 ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height) ||
            video_service_load_transfer(payload->transfer_id,
                                        payload->byte_count,
                                        g_video_service_blit_buffer,
                                        sizeof(g_video_service_blit_buffer)) != 0) {
            return -1;
        }
        sys_gfx_blit8_stretch(g_video_service_blit_buffer,
                              payload->src_width,
                              payload->src_height,
                              payload->dst_x,
                              payload->dst_y,
                              payload->dst_width,
                              payload->dst_height);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT: {
        const struct mk_video_blit8_stretch_present_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_present_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->dst_width <= 0 ||
            payload->dst_height <= 0 ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height) ||
            video_service_load_transfer(payload->transfer_id,
                                        payload->byte_count,
                                        g_video_service_blit_buffer,
                                        sizeof(g_video_service_blit_buffer)) != 0) {
            return -1;
        }
        sys_gfx_blit8_stretch_present(g_video_service_blit_buffer,
                                      payload->src_width,
                                      payload->src_height,
                                      payload->dst_x,
                                      payload->dst_y,
                                      payload->dst_width,
                                      payload->dst_height);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_INLINE: {
        const struct mk_video_blit8_stretch_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->dst_width <= 0 ||
            payload->dst_height <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        sys_gfx_blit8_stretch(payload->pixels,
                              payload->src_width,
                              payload->src_height,
                              payload->dst_x,
                              payload->dst_y,
                              payload->dst_width,
                              payload->dst_height);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_BLIT8_STRETCH_PRESENT_INLINE: {
        const struct mk_video_blit8_stretch_present_inline_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_blit8_stretch_present_inline_request *)request->payload;
        if (payload->src_width <= 0 ||
            payload->src_height <= 0 ||
            payload->dst_width <= 0 ||
            payload->dst_height <= 0 ||
            payload->byte_count == 0u ||
            payload->byte_count > MK_VIDEO_INLINE_BLIT8_MAX ||
            payload->byte_count < (uint32_t)(payload->src_width * payload->src_height)) {
            return -1;
        }
        sys_gfx_blit8_stretch_present(payload->pixels,
                                      payload->src_width,
                                      payload->src_height,
                                      payload->dst_x,
                                      payload->dst_y,
                                      payload->dst_width,
                                      payload->dst_height);
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_MODE_SET: {
        const struct mk_video_mode_request *payload;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_mode_request *)request->payload;
        rc = sys_gfx_set_mode(payload->width, payload->height, 8);
        return video_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_VIDEO_SET_PALETTE: {
        const struct mk_video_palette_request *payload;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_palette_request *)request->payload;
        if (payload->byte_count != MK_VIDEO_PALETTE_BYTES ||
            video_service_load_transfer(payload->transfer_id,
                                        payload->byte_count,
                                        g_video_service_palette_buffer,
                                        sizeof(g_video_service_palette_buffer)) != 0) {
            return -1;
        }
        rc = sys_gfx_set_palette(g_video_service_palette_buffer);
        return video_service_reply_result(reply, request, source_pid, rc);
    }
    case MK_MSG_VIDEO_GET_PALETTE: {
        const struct mk_video_palette_request *payload;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_video_palette_request *)request->payload;
        if (payload->byte_count != MK_VIDEO_PALETTE_BYTES ||
            sys_transfer_size(payload->transfer_id) < payload->byte_count) {
            return -1;
        }
        rc = sys_gfx_get_palette(g_video_service_palette_buffer);
        if (rc != 0) {
            return video_service_reply_result(reply, request, source_pid, rc);
        }
        if (sys_transfer_write(payload->transfer_id,
                               g_video_service_palette_buffer,
                               payload->byte_count) != 0) {
            return -1;
        }
        return video_service_reply_result(reply, request, source_pid, 0);
    }
    case MK_MSG_VIDEO_GET_INFO: {
        struct video_mode mode;
        int rc;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&mode, 0, sizeof(mode));
        rc = sys_gfx_info(&mode);
        return video_service_reply_mode(reply, request, source_pid, rc, &mode);
    }
    case MK_MSG_VIDEO_GET_CAPS: {
        struct video_capabilities caps;
        int rc;

        if (request->payload_size != 0u) {
            return -1;
        }
        memset(&caps, 0, sizeof(caps));
        rc = sys_gfx_caps(&caps);
        return video_service_reply_caps(reply, request, source_pid, rc, &caps);
    }
    default:
        return -1;
    }
}

static int network_service_socket_valid_domain(uint32_t domain) {
    return domain == AF_UNIX || domain == AF_LOCAL || domain == AF_INET;
}

static int network_service_socket_valid_type(uint32_t type) {
    return type == SOCK_STREAM || type == SOCK_DGRAM;
}

static int network_service_find_free_socket(void) {
    uint32_t i;

    for (i = 0; i < NETWORK_SERVICE_MAX_SOCKETS; ++i) {
        if (g_network_service_sockets[i].fd == -1) {
            return (int)i;
        }
    }
    return -1;
}

static int network_service_get_socket(int fd) {
    uint32_t i;

    for (i = 0; i < NETWORK_SERVICE_MAX_SOCKETS; ++i) {
        if (g_network_service_sockets[i].fd == fd) {
            return (int)i;
        }
    }
    return -1;
}

static void network_service_init_sockets(void) {
    uint32_t i;

    for (i = 0; i < NETWORK_SERVICE_MAX_SOCKETS; ++i) {
        g_network_service_sockets[i].fd = -1;
        g_network_service_sockets[i].domain = 0;
        g_network_service_sockets[i].type = 0;
        g_network_service_sockets[i].protocol = 0;
        g_network_service_sockets[i].flags = 0;
        memset(g_network_service_sockets[i].address, 0, MK_NETWORK_SOCKET_ADDRESS_MAX);
        g_network_service_sockets[i].address_length = 0;
        g_network_service_sockets[i].rx_queue_size = 0;
        g_network_service_sockets[i].tx_queue_size = 0;
        g_network_service_sockets[i].rx_bytes = 0;
        g_network_service_sockets[i].tx_bytes = 0;
        g_network_service_sockets[i].rx_packets = 0;
        g_network_service_sockets[i].tx_packets = 0;
        g_network_service_sockets[i].errors = 0;
    }
    g_network_service_socket_count = 0;
}

__attribute__((section(".entry"))) void userland_console_service_entry(void) {
    struct userland_launch_info info;
    struct mk_message request;
    struct mk_message reply;
    uint32_t source_pid;

    if (sys_launch_info(&info) != 0) {
        for (;;) {
            sys_yield();
        }
    }

    source_pid = (uint32_t)info.pid;
    service_log_online(&info);
    service_log_mode(info.name);
    console_vt_init();

    for (;;) {
        if (sys_service_receive(&request) != (int)sizeof(request)) {
            continue;
        }

        if (console_service_handle_request(&request, &reply, source_pid) != 0) {
            service_prepare_error_reply(&reply, &request, source_pid);
        }
        if (reply.source_pid == 0u) {
            reply.source_pid = source_pid;
        }
        if (reply.target_pid == 0u) {
            reply.target_pid = request.source_pid;
        }
        (void)sys_service_send(&reply);
    }
}

__attribute__((section(".entry"))) void userland_storage_service_entry(void) {
    struct userland_launch_info info;
    struct mk_message request;
    struct mk_message reply;
    uint32_t source_pid;

    if (sys_launch_info(&info) != 0) {
        for (;;) {
            sys_yield();
        }
    }

    source_pid = (uint32_t)info.pid;
    service_log_online(&info);
    service_log_mode(info.name);

    for (;;) {
        if (sys_service_receive(&request) != (int)sizeof(request)) {
            continue;
        }

        if (storage_service_handle_request(&request, &reply, source_pid) != 0) {
            service_prepare_error_reply(&reply, &request, source_pid);
        }
        if (reply.source_pid == 0u) {
            reply.source_pid = source_pid;
        }
        if (reply.target_pid == 0u) {
            reply.target_pid = request.source_pid;
        }
        (void)sys_service_send(&reply);
    }
}

__attribute__((section(".entry"))) void userland_filesystem_service_entry(void) {
    struct userland_launch_info info;
    struct mk_message request;
    struct mk_message reply;
    uint32_t source_pid;

    if (sys_launch_info(&info) != 0) {
        for (;;) {
            sys_yield();
        }
    }

    source_pid = (uint32_t)info.pid;
    service_log_online(&info);
    service_log_mode(info.name);

    for (;;) {
        if (sys_service_receive(&request) != (int)sizeof(request)) {
            continue;
        }

        if (filesystem_service_handle_request(&request, &reply, source_pid) != 0) {
            service_prepare_error_reply(&reply, &request, source_pid);
        }
        if (reply.source_pid == 0u) {
            reply.source_pid = source_pid;
        }
        if (reply.target_pid == 0u) {
            reply.target_pid = request.source_pid;
        }
        (void)sys_service_send(&reply);
    }
}

__attribute__((section(".entry"))) void userland_network_service_entry(void) {
    struct userland_launch_info info;
    struct mk_message request;
    struct mk_message reply;
    uint32_t source_pid;

    if (sys_launch_info(&info) != 0) {
        for (;;) {
            sys_yield();
        }
    }

    source_pid = (uint32_t)info.pid;
    service_log_online(&info);
    service_log_mode(info.name);
    network_service_init_sockets();

    for (;;) {
        if (sys_service_receive(&request) != (int)sizeof(request)) {
            continue;
        }

        if (network_service_handle_request(&request, &reply, source_pid) != 0) {
            service_prepare_error_reply(&reply, &request, source_pid);
        }
        if (reply.source_pid == 0u) {
            reply.source_pid = source_pid;
        }
        if (reply.target_pid == 0u) {
            reply.target_pid = request.source_pid;
        }
        (void)sys_service_send(&reply);
    }
}

__attribute__((section(".entry"))) void userland_input_service_entry(void) {
    struct userland_launch_info info;
    struct mk_message request;
    struct mk_message reply;
    uint32_t source_pid;

    if (sys_launch_info(&info) != 0) {
        for (;;) {
            sys_yield();
        }
    }

    source_pid = (uint32_t)info.pid;
    service_log_online(&info);

    for (;;) {
        if (sys_service_receive(&request) != (int)sizeof(request)) {
            continue;
        }

        if (input_service_handle_request(&request, &reply, source_pid) != 0) {
            service_prepare_error_reply(&reply, &request, source_pid);
        }
        if (reply.source_pid == 0u) {
            reply.source_pid = source_pid;
        }
        if (reply.target_pid == 0u) {
            reply.target_pid = request.source_pid;
        }
        (void)sys_service_send(&reply);
    }
}

__attribute__((section(".entry"))) void userland_audio_service_entry(void) {
    struct userland_launch_info info;
    struct mk_message request;
    struct mk_message reply;
    uint32_t source_pid;

    if (sys_launch_info(&info) != 0) {
        for (;;) {
            sys_yield();
        }
    }

    sys_text_write("audiosvc: launch_info\n");
    sys_write_debug("audiosvc: launch_info\n");
    source_pid = (uint32_t)info.pid;
    sys_text_write("audiosvc: fs_init deferred\n");
    sys_write_debug("audiosvc: fs_init deferred\n");
    service_log_online(&info);
    sys_text_write("audiosvc: online\n");
    sys_write_debug("audiosvc: online\n");

    for (;;) {
        if (sys_service_receive(&request) != (int)sizeof(request)) {
            continue;
        }

        if (audio_service_handle_request(&request, &reply, source_pid) != 0) {
            service_prepare_error_reply(&reply, &request, source_pid);
        }
        if (reply.source_pid == 0u) {
            reply.source_pid = source_pid;
        }
        if (reply.target_pid == 0u) {
            reply.target_pid = request.source_pid;
        }
        (void)sys_service_send(&reply);
    }
}

__attribute__((section(".entry"))) void userland_video_service_entry(void) {
    struct userland_launch_info info;
    struct mk_message request;
    struct mk_message reply;
    uint32_t source_pid;

    if (sys_launch_info(&info) != 0) {
        for (;;) {
            sys_yield();
        }
    }

    source_pid = (uint32_t)info.pid;
    service_log_online(&info);

    for (;;) {
        if (sys_service_receive(&request) != (int)sizeof(request)) {
            continue;
        }

        if (video_service_handle_request(&request, &reply, source_pid) != 0) {
            service_prepare_error_reply(&reply, &request, source_pid);
        }
        if (reply.source_pid == 0u) {
            reply.source_pid = source_pid;
        }
        if (reply.target_pid == 0u) {
            reply.target_pid = request.source_pid;
        }
        (void)sys_service_send(&reply);
    }
}
