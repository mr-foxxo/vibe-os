#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/video/video.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/console.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/scheduler.h>
#include <kernel/userland_service.h>

static uint32_t mk_console_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_console_share_transfer(uint32_t transfer_id, uint32_t permissions) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_CONSOLE);

    if (service == 0 || service->pid <= 0) {
        return 0;
    }
    return mk_transfer_share(transfer_id, (uint32_t)service->pid, permissions);
}

static int mk_console_prepare_request(struct mk_message *message,
                                      uint32_t type,
                                      const void *payload,
                                      size_t payload_size) {
    const struct mk_service_record *service;
    process_t *current;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_CONSOLE);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    current = scheduler_current();
    message->source_pid = current != 0 ? (uint32_t)current->pid : 0u;
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_console_reply_result(struct mk_message *reply, int value) {
    struct mk_console_result result;

    result.value = value;
    return mk_message_set_payload(reply, &result, sizeof(result));
}

static int mk_console_decode_result(const struct mk_message *reply) {
    struct mk_console_result result;

    if (reply == 0 || reply->payload_size != sizeof(result)) {
        return -1;
    }

    memcpy(&result, reply->payload, sizeof(result));
    return result.value;
}

static int mk_console_validate_text_transfer(uint32_t transfer_id,
                                             uint32_t length,
                                             char **text_out) {
    char *text;

    if (text_out == 0 || length == 0u) {
        return -1;
    }

    text = (char *)mk_transfer_data_read(transfer_id);
    if (text == 0) {
        return -1;
    }
    /* data_read pinned the transfer; release on failure, caller owns on success. */
    if (mk_transfer_size(transfer_id) < length + 1u) {
        mk_transfer_release(transfer_id);
        return -1;
    }
    if (text[length] != '\0') {
        mk_transfer_release(transfer_id);
        return -1;
    }

    *text_out = text;
    return 0;
}

static int mk_console_local_handler(const struct mk_message *request,
                                    struct mk_message *reply,
                                    void *context) {
    (void)context;
    if (request == 0 || reply == 0) {
        return -1;
    }

    mk_message_init(reply, request->type);
    reply->source_pid = request->target_pid;
    reply->target_pid = request->source_pid;

    switch (request->type) {
    case MK_MSG_CONSOLE_WRITE_DEBUG: {
        const struct mk_console_text_request *payload;
        char *text;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_console_text_request *)request->payload;
        if (mk_console_validate_text_transfer(payload->transfer_id, payload->length, &text) != 0) {
            return -1;
        }
        kernel_debug_puts(text);
        mk_transfer_release(payload->transfer_id);
        return mk_console_reply_result(reply, 0);
    }
    case MK_MSG_CONSOLE_TEXT_CLEAR:
        if (request->payload_size != 0u) {
            return -1;
        }
        kernel_text_clear();
        return mk_console_reply_result(reply, 0);
    case MK_MSG_CONSOLE_TEXT_PUTC: {
        const struct mk_console_putc_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_console_putc_request *)request->payload;
        kernel_text_putc((char)(payload->character & 0xFFu));
        return mk_console_reply_result(reply, 0);
    }
    case MK_MSG_CONSOLE_TEXT_WRITE: {
        const struct mk_console_text_request *payload;
        char *text;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_console_text_request *)request->payload;
        if (mk_console_validate_text_transfer(payload->transfer_id, payload->length, &text) != 0) {
            return -1;
        }
        kernel_text_puts(text);
        mk_transfer_release(payload->transfer_id);
        return mk_console_reply_result(reply, 0);
    }
    case MK_MSG_CONSOLE_CURSOR_MOVE: {
        const struct mk_console_cursor_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_console_cursor_request *)request->payload;
        kernel_text_move_cursor(payload->delta);
        return mk_console_reply_result(reply, 0);
    }
    default:
        return -1;
    }
}

static int mk_console_service_launch_deferred(void) {
    return mk_service_launch_task(MK_SERVICE_CONSOLE,
                                  "console",
                                  mk_console_local_handler,
                                  0,
                                  userland_console_service_entry,
                                  32768u,
                                  MK_LAUNCH_FLAG_BOOTSTRAP |
                                  MK_LAUNCH_FLAG_BUILTIN |
                                  MK_LAUNCH_FLAG_CRITICAL);
}

void mk_console_service_init(void) {
    (void)mk_service_register_deferred_launcher(MK_SERVICE_CONSOLE,
                                                mk_console_service_launch_deferred);
}

static int mk_console_service_string_request(uint32_t type, const char *message) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_console_text_request payload;
    uint32_t transfer_id;
    uint32_t length;

    if (message == 0) {
        return -1;
    }
    length = (uint32_t)strlen(message);
    if (mk_transfer_create(mk_console_current_pid(), length + 1u, &transfer_id) != 0) {
        return -1;
    }
    if (mk_console_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, message, length + 1u) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.length = length;
    payload.transfer_id = transfer_id;
    if (mk_console_prepare_request(&request, type, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_service_request(MK_SERVICE_CONSOLE, &request, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return mk_console_decode_result(&reply);
}

int mk_console_service_write_debug(const char *message) {
    return mk_console_service_string_request(MK_MSG_CONSOLE_WRITE_DEBUG, message);
}

int mk_console_service_text_write(const char *message) {
    return mk_console_service_string_request(MK_MSG_CONSOLE_TEXT_WRITE, message);
}

int mk_console_service_text_clear(void) {
    struct mk_message request;
    struct mk_message reply;

    if (mk_console_prepare_request(&request, MK_MSG_CONSOLE_TEXT_CLEAR, 0, 0u) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_CONSOLE, &request, &reply) != 0) {
        return -1;
    }
    return mk_console_decode_result(&reply);
}

int mk_console_service_text_move_cursor(int delta) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_console_cursor_request payload;

    payload.delta = delta;
    if (mk_console_prepare_request(&request, MK_MSG_CONSOLE_CURSOR_MOVE, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_CONSOLE, &request, &reply) != 0) {
        return -1;
    }
    return mk_console_decode_result(&reply);
}

int mk_console_service_text_putc(char c) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_console_putc_request payload;

    payload.character = (uint32_t)(uint8_t)c;
    if (mk_console_prepare_request(&request, MK_MSG_CONSOLE_TEXT_PUTC, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_CONSOLE, &request, &reply) != 0) {
        return -1;
    }
    return mk_console_decode_result(&reply);
}
