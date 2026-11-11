#include <kernel/fs.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/filesystem.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/userland_service.h>
#include <kernel/scheduler.h>
#include <sys/stat.h>

static uint32_t mk_fs_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_fs_current_process_is_service_worker(void) {
    process_t *current = scheduler_current();

    return current != 0 && current->service_type == MK_SERVICE_FILESYSTEM;
}

static int mk_fs_share_transfer(uint32_t transfer_id, uint32_t permissions) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_FILESYSTEM);

    if (service == 0 || service->pid <= 0) {
        return 0;
    }
    return mk_transfer_share(transfer_id, (uint32_t)service->pid, permissions);
}

static int mk_fs_prepare_request(struct mk_message *message,
                                 uint32_t type,
                                 const void *payload,
                                 size_t payload_size) {
    const struct mk_service_record *service;
    process_t *current;

    if (message == 0) {
        return -1;
    }

    service = mk_service_find_by_type(MK_SERVICE_FILESYSTEM);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    current = scheduler_current();
    message->source_pid = current != 0 ? (uint32_t)current->pid : 0u;
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

static int mk_fs_reply_result(struct mk_message *reply, int value) {
    struct mk_fs_result result;

    result.value = value;
    return mk_message_set_payload(reply, &result, sizeof(result));
}

static int mk_fs_decode_result(const struct mk_message *reply) {
    struct mk_fs_result result;

    if (reply == 0 || reply->payload_size != sizeof(result)) {
        return -1;
    }

    memcpy(&result, reply->payload, sizeof(result));
    return result.value;
}

static int mk_fs_validate_path_transfer(uint32_t transfer_id,
                                        uint32_t path_length,
                                        char **path_out) {
    char *path;

    if (path_out == 0 || path_length == 0u) {
        return -1;
    }
    path = (char *)mk_transfer_data_read(transfer_id);
    if (path == 0) {
        return -1;
    }
    /*
     * data_read pinned the transfer; every failure from here must release,
     * and the success caller owns the reference and must release after use.
     */
    if (mk_transfer_size(transfer_id) < path_length + 1u) {
        mk_transfer_release(transfer_id);
        return -1;
    }
    if (path[path_length] != '\0') {
        mk_transfer_release(transfer_id);
        return -1;
    }
    *path_out = path;
    return 0;
}

static int mk_fs_local_handler(const struct mk_message *request,
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
    case MK_MSG_FS_OPEN: {
        const struct mk_fs_open_request *payload;
        char *path;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_open_request *)request->payload;
        if (mk_fs_validate_path_transfer(payload->path_transfer_id, payload->path_length, &path) != 0) {
            return -1;
        }
        {
            int rc = open(path, (int)payload->flags);
            mk_transfer_release(payload->path_transfer_id);
            return mk_fs_reply_result(reply, rc);
        }
    }
    case MK_MSG_FS_READ: {
        const struct mk_fs_io_request *payload;
        void *buffer;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_io_request *)request->payload;
        buffer = mk_transfer_data_write(payload->transfer_id);
        if (buffer == 0) {
            return -1;
        }
        if (mk_transfer_size(payload->transfer_id) < payload->size) {
            mk_transfer_release(payload->transfer_id);
            return -1;
        }
        rc = read(payload->fd, buffer, payload->size);
        mk_transfer_release(payload->transfer_id);
        return mk_fs_reply_result(reply, rc);
    }
    case MK_MSG_FS_WRITE: {
        const struct mk_fs_io_request *payload;
        void *buffer;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_io_request *)request->payload;
        buffer = (void *)mk_transfer_data_read(payload->transfer_id);
        if (buffer == 0) {
            return -1;
        }
        if (mk_transfer_size(payload->transfer_id) < payload->size) {
            mk_transfer_release(payload->transfer_id);
            return -1;
        }
        rc = write(payload->fd, buffer, payload->size);
        mk_transfer_release(payload->transfer_id);
        return mk_fs_reply_result(reply, rc);
    }
    case MK_MSG_FS_CLOSE: {
        const struct mk_fs_close_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_close_request *)request->payload;
        return mk_fs_reply_result(reply, close(payload->fd));
    }
    case MK_MSG_FS_LSEEK: {
        const struct mk_fs_seek_request *payload;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_seek_request *)request->payload;
        return mk_fs_reply_result(reply,
                                  (int)lseek(payload->fd,
                                             (off_t)payload->offset,
                                             payload->whence));
    }
    case MK_MSG_FS_STAT: {
        const struct mk_fs_stat_request *payload;
        struct stat st;
        char *path;
        int rc;
        int copy_err = 0;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_stat_request *)request->payload;
        if (mk_fs_validate_path_transfer(payload->path_transfer_id, payload->path_length, &path) != 0) {
            return -1;
        }
        if (mk_transfer_size(payload->stat_transfer_id) < sizeof(st)) {
            mk_transfer_release(payload->path_transfer_id);
            return -1;
        }
        rc = stat(path, &st);
        mk_transfer_release(payload->path_transfer_id);
        if (rc == 0 && mk_transfer_copy_from(payload->stat_transfer_id, &st, sizeof(st)) != 0) {
            copy_err = 1;
        }
        if (copy_err) {
            return -1;
        }
        return mk_fs_reply_result(reply, rc);
    }
    case MK_MSG_FS_FSTAT: {
        const struct mk_fs_fstat_request *payload;
        struct stat st;
        int rc;

        if (request->payload_size != sizeof(*payload)) {
            return -1;
        }
        payload = (const struct mk_fs_fstat_request *)request->payload;
        if (mk_transfer_size(payload->stat_transfer_id) < sizeof(st)) {
            return -1;
        }
        rc = fstat(payload->fd, &st);
        if (rc == 0 && mk_transfer_copy_from(payload->stat_transfer_id, &st, sizeof(st)) != 0) {
            return -1;
        }
        return mk_fs_reply_result(reply, rc);
    }
    default:
        return -1;
    }
}

static int mk_filesystem_service_launch_deferred(void) {
    return mk_service_launch_task(MK_SERVICE_FILESYSTEM,
                                  "filesystem",
                                  mk_fs_local_handler,
                                  0,
                                  userland_filesystem_service_entry,
                                  32768u,
                                  MK_LAUNCH_FLAG_BOOTSTRAP |
                                  MK_LAUNCH_FLAG_BUILTIN |
                                  MK_LAUNCH_FLAG_CRITICAL);
}

void mk_filesystem_service_init(void) {
    (void)mk_service_register_local_handler(MK_SERVICE_FILESYSTEM,
                                            "filesystem",
                                            mk_fs_local_handler,
                                            0);
    (void)mk_service_register_deferred_launcher(MK_SERVICE_FILESYSTEM,
                                                mk_filesystem_service_launch_deferred);
}

int mk_filesystem_service_open(const char *path, int flags) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_fs_open_request payload;
    uint32_t transfer_id;
    uint32_t path_length;

    if (path == 0) {
        return -1;
    }
    if (mk_fs_current_process_is_service_worker()) {
        return open(path, flags);
    }
    path_length = (uint32_t)strlen(path);
    if (mk_transfer_create(mk_fs_current_pid(), path_length + 1u, &transfer_id) != 0) {
        return -1;
    }
    if (mk_fs_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, path, path_length + 1u) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.flags = (uint32_t)flags;
    payload.path_length = path_length;
    payload.path_transfer_id = transfer_id;
    if (mk_fs_prepare_request(&request, MK_MSG_FS_OPEN, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_service_request(MK_SERVICE_FILESYSTEM, &request, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return mk_fs_decode_result(&reply);
}

static int mk_fs_stat_request_common(uint32_t type,
                                     const char *path,
                                     int fd,
                                     struct stat *buf) {
    struct mk_message request;
    struct mk_message reply;
    uint32_t stat_transfer_id;
    int rc;

    if (buf == 0) {
        return -1;
    }
    if (mk_fs_current_process_is_service_worker()) {
        if (type == MK_MSG_FS_STAT) {
            if (path == 0) {
                return -1;
            }
            return stat(path, buf);
        }
        return fstat(fd, buf);
    }
    if (mk_transfer_create(mk_fs_current_pid(), sizeof(*buf), &stat_transfer_id) != 0) {
        return -1;
    }
    if (mk_fs_share_transfer(stat_transfer_id, MK_TRANSFER_PERM_WRITE) != 0) {
        (void)mk_transfer_destroy(stat_transfer_id);
        return -1;
    }

    if (type == MK_MSG_FS_STAT) {
        struct mk_fs_stat_request payload;
        uint32_t path_transfer_id;
        uint32_t path_length;

        if (path == 0) {
            (void)mk_transfer_destroy(stat_transfer_id);
            return -1;
        }
        path_length = (uint32_t)strlen(path);
        if (mk_transfer_create(mk_fs_current_pid(), path_length + 1u, &path_transfer_id) != 0) {
            (void)mk_transfer_destroy(stat_transfer_id);
            return -1;
        }
        if (mk_fs_share_transfer(path_transfer_id, MK_TRANSFER_PERM_READ) != 0) {
            (void)mk_transfer_destroy(path_transfer_id);
            (void)mk_transfer_destroy(stat_transfer_id);
            return -1;
        }
        if (mk_transfer_copy_from(path_transfer_id, path, path_length + 1u) != 0) {
            (void)mk_transfer_destroy(path_transfer_id);
            (void)mk_transfer_destroy(stat_transfer_id);
            return -1;
        }

        payload.path_length = path_length;
        payload.path_transfer_id = path_transfer_id;
        payload.stat_transfer_id = stat_transfer_id;
        if (mk_fs_prepare_request(&request, type, &payload, sizeof(payload)) != 0) {
            (void)mk_transfer_destroy(path_transfer_id);
            (void)mk_transfer_destroy(stat_transfer_id);
            return -1;
        }
        rc = mk_service_request(MK_SERVICE_FILESYSTEM, &request, &reply);
        (void)mk_transfer_destroy(path_transfer_id);
    } else {
        struct mk_fs_fstat_request payload;

        payload.fd = fd;
        payload.stat_transfer_id = stat_transfer_id;
        if (mk_fs_prepare_request(&request, type, &payload, sizeof(payload)) != 0) {
            (void)mk_transfer_destroy(stat_transfer_id);
            return -1;
        }
        rc = mk_service_request(MK_SERVICE_FILESYSTEM, &request, &reply);
    }

    if (rc != 0) {
        (void)mk_transfer_destroy(stat_transfer_id);
        return -1;
    }
    rc = mk_fs_decode_result(&reply);
    if (rc == 0 && mk_transfer_copy_to(stat_transfer_id, buf, sizeof(*buf)) != 0) {
        (void)mk_transfer_destroy(stat_transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(stat_transfer_id);
    return rc;
}

int mk_filesystem_service_read(int fd, void *buf, uint32_t count) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_fs_io_request payload;
    uint32_t transfer_id;
    int rc;

    if (buf == 0) {
        return -1;
    }
    if (mk_fs_current_process_is_service_worker()) {
        return read(fd, buf, count);
    }
    if (count == 0u) {
        return -1;
    }
    if (mk_transfer_create(mk_fs_current_pid(), count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_fs_share_transfer(transfer_id, MK_TRANSFER_PERM_WRITE) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.fd = fd;
    payload.size = count;
    payload.transfer_id = transfer_id;
    if (mk_fs_prepare_request(&request, MK_MSG_FS_READ, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_service_request(MK_SERVICE_FILESYSTEM, &request, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    rc = mk_fs_decode_result(&reply);
    if (rc > 0 && mk_transfer_copy_to(transfer_id, buf, (uint32_t)rc) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return rc;
}

int mk_filesystem_service_write(int fd, const void *buf, uint32_t count) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_fs_io_request payload;
    uint32_t transfer_id;

    if (buf == 0) {
        return -1;
    }
    if (mk_fs_current_process_is_service_worker()) {
        return write(fd, buf, count);
    }
    if (count == 0u) {
        return -1;
    }
    if (mk_transfer_create(mk_fs_current_pid(), count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_fs_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, buf, count) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.fd = fd;
    payload.size = count;
    payload.transfer_id = transfer_id;
    if (mk_fs_prepare_request(&request, MK_MSG_FS_WRITE, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_service_request(MK_SERVICE_FILESYSTEM, &request, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return mk_fs_decode_result(&reply);
}

int mk_filesystem_service_close(int fd) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_fs_close_request payload;

    if (mk_fs_current_process_is_service_worker()) {
        return close(fd);
    }

    payload.fd = fd;
    if (mk_fs_prepare_request(&request, MK_MSG_FS_CLOSE, &payload, sizeof(payload)) != 0) {
        return -1;
    }
    if (mk_service_request(MK_SERVICE_FILESYSTEM, &request, &reply) != 0) {
        return -1;
    }
    return mk_fs_decode_result(&reply);
}

off_t mk_filesystem_service_lseek(int fd, off_t offset, int whence) {
    struct mk_message request;
    struct mk_message reply;
    struct mk_fs_seek_request payload;

    if (mk_fs_current_process_is_service_worker()) {
        return lseek(fd, offset, whence);
    }

    payload.fd = fd;
    payload.offset = (int32_t)offset;
    payload.whence = whence;
    if (mk_fs_prepare_request(&request, MK_MSG_FS_LSEEK, &payload, sizeof(payload)) != 0) {
        return (off_t)-1;
    }
    if (mk_service_request(MK_SERVICE_FILESYSTEM, &request, &reply) != 0) {
        return (off_t)-1;
    }
    return (off_t)mk_fs_decode_result(&reply);
}

int mk_filesystem_service_stat(const char *path, struct stat *buf) {
    return mk_fs_stat_request_common(MK_MSG_FS_STAT, path, -1, buf);
}

int mk_filesystem_service_fstat(int fd, struct stat *buf) {
    return mk_fs_stat_request_common(MK_MSG_FS_FSTAT, 0, fd, buf);
}
