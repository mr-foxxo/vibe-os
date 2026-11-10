#include <kernel/drivers/storage/ata.h>
#include <kernel/telemetry.h>
#include <kernel/drivers/storage/block_device.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/message.h>
#include <kernel/microkernel/service.h>
#include <kernel/microkernel/storage.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/scheduler.h>
#include <kernel/cpu/cpu.h>
#include <kernel/smp.h>
#include <kernel/userland_service.h>

static struct mk_message g_last_storage_request;
static struct mk_message g_last_storage_reply;
#define MK_STORAGE_PERSIST_IO_CHUNK_SECTORS 64u

/*
 * Sector-count ceiling (COMPLETION_ACTION_PLAN P4, class #3). Message
 * payloads carry an UNTRUSTED uint32 sector_count; byte_count is
 * sector_count * 512 in uint32 arithmetic, so sector_count >= 0x800000
 * wraps the product toward 0 and can sail through the
 * "mk_transfer_size(...) < byte_count" checks — the ATA loop then
 * streams gigabytes of disk data into a small heap buffer, overwriting
 * whatever follows (observed real-time smp2 signature: task frames
 * holding data-byte registers, e.g. cs=0x454430 / eip=0x213 — ASCII
 * bytes as selector/eip — and DISPATCH-BAD-FORENSICS proving the frame
 * was intact AT dispatch, i.e. the write lands DURING execution).
 * 65536 sectors = 32 MiB per request: far above any legitimate path
 * (persist I/O chunks at 64 sectors; assets load in small pieces) and
 * small enough that sector_count * 512 can never wrap a 32-bit product.
 */
#define MK_STORAGE_MAX_SECTORS_PER_REQUEST 65536u

static uint32_t g_storage_sector_ceiling_trace_budget = VIBE_TRACE_BUDGET(8u);

static void mk_storage_reject_ceiling(const char *what, uint32_t sector_count) {
    if (g_storage_sector_ceiling_trace_budget == 0u) {
        return;
    }
    g_storage_sector_ceiling_trace_budget--;
    kernel_debug_printf("storage: REJECT %s sector_count=%u > ceiling %u\n",
                        what, (unsigned int)sector_count,
                        (unsigned int)MK_STORAGE_MAX_SECTORS_PER_REQUEST);
}

static uint32_t mk_storage_current_pid(void) {
    return scheduler_current_pid();
}

static int mk_storage_current_process_is_service_worker(void) {
    process_t *current = scheduler_current();

    return current != 0 && current->service_type == MK_SERVICE_STORAGE;
}

static int mk_storage_current_prefers_kernel_hot_path(void) {
    process_t *current = scheduler_current();
    const struct mk_launch_context *context;

    if (current == 0) {
        return 0;
    }
    if (current->service_type == MK_SERVICE_STORAGE) {
        return 1;
    }

    context = mk_launch_context_current();
    if (context == 0) {
        return 1;
    }

    if ((context->flags & (MK_LAUNCH_FLAG_BOOTSTRAP |
                           MK_LAUNCH_FLAG_CRITICAL)) != 0u) {
        return 1;
    }

    /*
     * Keep the direct kernel storage bypass narrowly scoped.
     * Audio hosts benefit from avoiding storage IPC on startup WAV playback,
     * but broadening this to the desktop/app-runtime path regresses modular
     * boot because ELF/AppFS loads rely on the regular service boundary.
     */
    return context->task_class == MK_TASK_CLASS_AUDIO_IO;
}

static int mk_storage_reply_result(struct mk_message *reply, int value) {
    struct mk_storage_result result;

    if (reply == 0) {
        return -1;
    }

    result.value = value;
    return mk_message_set_payload(reply, &result, sizeof(result));
}

static int mk_storage_decode_result(const struct mk_message *reply) {
    struct mk_storage_result result;

    if (reply == 0 || reply->payload_size != sizeof(result)) {
        return -1;
    }

    memcpy(&result, reply->payload, sizeof(result));
    return result.value;
}

static int mk_storage_share_transfer(uint32_t transfer_id, uint32_t permissions) {
    const struct mk_service_record *service = mk_service_find_by_type(MK_SERVICE_STORAGE);

    if (service == 0 || service->pid <= 0) {
        return 0;
    }
    return mk_transfer_share(transfer_id, (uint32_t)service->pid, permissions);
}

static int mk_storage_local_handler(const struct mk_message *request,
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
    case MK_MSG_BLOCK_READ: {
        static uint32_t g_hostbr_budget = VIBE_TRACE_BUDGET(4u);
        if (g_hostbr_budget != 0u) {
            g_hostbr_budget--;
            kernel_debug_printf("storage: HOST-BREAD sz=%u\n",
                                (unsigned int)request->payload_size);
        }
        if (request->payload_size == sizeof(struct mk_storage_sectors_request)) {
            const struct mk_storage_sectors_request *payload;
            uint32_t byte_count;
            void *buffer;
            int rc;

            payload = (const struct mk_storage_sectors_request *)request->payload;
            if (payload->sector_count == 0u ||
                payload->sector_count > MK_STORAGE_MAX_SECTORS_PER_REQUEST) {
                mk_storage_reject_ceiling("BLOCK_READ", payload->sector_count);
                return -1;
            }
            byte_count = payload->sector_count * KERNEL_PERSIST_SECTOR_SIZE;
            buffer = mk_transfer_data_write(payload->transfer_id);
            if (buffer == 0) {
                return -1;
            }
            if (mk_transfer_size(payload->transfer_id) < byte_count) {
                mk_transfer_release(payload->transfer_id);
                return -1;
            }
            rc = kernel_storage_read_sectors(payload->lba,
                                             buffer,
                                             payload->sector_count);
            mk_transfer_release(payload->transfer_id);
            return mk_storage_reply_result(reply, rc);
        }

        if (request->payload_size == sizeof(struct mk_storage_persist_request)) {
            const struct mk_storage_persist_request *payload;
            void *buffer;
            int rc;

            payload = (const struct mk_storage_persist_request *)request->payload;
            buffer = mk_transfer_data_write(payload->transfer_id);
            if (buffer == 0) {
                return -1;
            }
            if (mk_transfer_size(payload->transfer_id) < payload->size) {
                mk_transfer_release(payload->transfer_id);
                return -1;
            }
            rc = kernel_storage_load(buffer, payload->size);
            mk_transfer_release(payload->transfer_id);
            return mk_storage_reply_result(reply, rc);
        }

        return -1;
    }
    case MK_MSG_BLOCK_WRITE: {
        if (request->payload_size == sizeof(struct mk_storage_sectors_request)) {
            const struct mk_storage_sectors_request *payload;
            uint32_t byte_count;
            void *buffer;
            int rc;

            payload = (const struct mk_storage_sectors_request *)request->payload;
            if (payload->sector_count == 0u ||
                payload->sector_count > MK_STORAGE_MAX_SECTORS_PER_REQUEST) {
                mk_storage_reject_ceiling("BLOCK_WRITE", payload->sector_count);
                return -1;
            }
            byte_count = payload->sector_count * KERNEL_PERSIST_SECTOR_SIZE;
            buffer = (void *)mk_transfer_data_read(payload->transfer_id);
            if (buffer == 0) {
                return -1;
            }
            if (mk_transfer_size(payload->transfer_id) < byte_count) {
                mk_transfer_release(payload->transfer_id);
                return -1;
            }
            rc = kernel_storage_write_sectors(payload->lba,
                                              buffer,
                                              payload->sector_count);
            mk_transfer_release(payload->transfer_id);
            return mk_storage_reply_result(reply, rc);
        }

        if (request->payload_size == sizeof(struct mk_storage_persist_request)) {
            const struct mk_storage_persist_request *persist;
            void *buffer;
            int rc;

            persist = (const struct mk_storage_persist_request *)request->payload;
            buffer = (void *)mk_transfer_data_read(persist->transfer_id);
            if (buffer == 0) {
                return -1;
            }
            if (mk_transfer_size(persist->transfer_id) < persist->size) {
                mk_transfer_release(persist->transfer_id);
                return -1;
            }
            rc = kernel_storage_save(buffer, persist->size);
            mk_transfer_release(persist->transfer_id);
            return mk_storage_reply_result(reply, rc);
        }

        return -1;
    }
    case MK_MSG_HELLO:
    case MK_MSG_BLOCK_INFO: {
        struct mk_storage_info info;

        info.total_sectors = kernel_storage_total_sectors();
        info.partition_start_lba = kernel_storage_partition_start_lba();
        return mk_message_set_payload(reply, &info, sizeof(info));
    }
    default:
        return -1;
    }
}

static int mk_storage_prepare_request(struct mk_message *message,
                                      uint32_t type,
                                      const void *payload,
                                      size_t payload_size) {
    const struct mk_service_record *service;

    service = mk_service_find_by_type(MK_SERVICE_STORAGE);
    if (service == 0) {
        return -1;
    }

    mk_message_init(message, type);
    message->source_pid = mk_storage_current_pid();
    message->target_pid = service->pid > 0 ? (uint32_t)service->pid : 0u;
    return mk_message_set_payload(message, payload, payload_size);
}

void mk_storage_service_init(void) {
    memset(&g_last_storage_request, 0, sizeof(g_last_storage_request));
    memset(&g_last_storage_reply, 0, sizeof(g_last_storage_reply));
    if (kernel_storage_ready()) {
        (void)mk_service_launch_task(MK_SERVICE_STORAGE,
                                     "storage",
                                     mk_storage_local_handler,
                                     0,
                                     userland_storage_service_entry,
                                     32768u,
                                     MK_LAUNCH_FLAG_BOOTSTRAP |
                                     MK_LAUNCH_FLAG_BUILTIN |
                                     MK_LAUNCH_FLAG_CRITICAL);
    }
}

int mk_storage_service_ready(void) {
    return mk_service_find_by_type(MK_SERVICE_STORAGE) != 0 && kernel_storage_ready();
}

int mk_storage_service_read_sectors(uint32_t lba, void *dst, uint32_t sector_count) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_sectors_request payload;
    uint32_t transfer_id;
    uint32_t byte_count;
    int rc;

    if (dst == 0 || sector_count == 0u) {
        return -1;
    }
    if (mk_storage_current_process_is_service_worker() ||
        mk_storage_current_prefers_kernel_hot_path()) {
        /* The boot loader (init/lang tasks have NO mk_launch_context, so
         * context==0 selects THIS branch) reads app images here, NOT through
         * the host round-trip. Adendo 43 measurement lives on this side:
         * per-chunk host cost in ticks, 6 prints. */
        static uint32_t g_hot_dur_budget = VIBE_TRACE_BUDGET(6u);
        uint32_t hcpu = kernel_cpu_index();
        uint32_t ht0 = smp_tick_count(hcpu);
        int hrc = kernel_storage_read_sectors(lba, dst, sector_count);
        if (g_hot_dur_budget != 0u && sector_count > 1u) {
            g_hot_dur_budget -= 1u;
            kernel_debug_printf("storage: hot n=%u dur=%u cpu=%u\n",
                                (unsigned int)sector_count,
                                (unsigned int)(smp_tick_count(hcpu) - ht0),
                                (unsigned int)hcpu);
        }
        return hrc;
    }

    if (sector_count > MK_STORAGE_MAX_SECTORS_PER_REQUEST) {
        mk_storage_reject_ceiling("client_read", sector_count);
        return -1;
    }
    byte_count = sector_count * KERNEL_PERSIST_SECTOR_SIZE;
    if (mk_transfer_create(mk_storage_current_pid(), byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_storage_share_transfer(transfer_id, MK_TRANSFER_PERM_WRITE) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }

    payload.lba = lba;
    payload.sector_count = sector_count;
    payload.transfer_id = transfer_id;
    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_READ, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_reply = reply;
    rc = mk_storage_decode_result(&reply);
    if (rc == 0 && mk_transfer_copy_to(transfer_id, dst, byte_count) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    (void)mk_transfer_destroy(transfer_id);
    return rc;
}

int mk_storage_service_write_sectors(uint32_t lba, const void *src, uint32_t sector_count) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_sectors_request payload;
    uint32_t transfer_id;
    uint32_t byte_count;
    int rc;

    if (src == 0 || sector_count == 0u) {
        return -1;
    }
    if (mk_storage_current_process_is_service_worker() ||
        mk_storage_current_prefers_kernel_hot_path()) {
        return kernel_storage_write_sectors(lba, src, sector_count);
    }

    if (sector_count > MK_STORAGE_MAX_SECTORS_PER_REQUEST) {
        mk_storage_reject_ceiling("client_write", sector_count);
        return -1;
    }
    byte_count = sector_count * KERNEL_PERSIST_SECTOR_SIZE;
    if (mk_transfer_create(mk_storage_current_pid(), byte_count, &transfer_id) != 0) {
        return -1;
    }
    if (mk_storage_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, byte_count) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    payload.lba = lba;
    payload.sector_count = sector_count;
    payload.transfer_id = transfer_id;
    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_WRITE, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_reply = reply;
    rc = mk_storage_decode_result(&reply);
    (void)mk_transfer_destroy(transfer_id);
    return rc;
}

int mk_storage_service_load(void *dst, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;
    uint32_t full_sectors;
    uint32_t tail_bytes;
    uint32_t sector_index = 0u;
    uint8_t tail_sector[KERNEL_PERSIST_SECTOR_SIZE];

    if (dst == 0 || size == 0u) {
        return -1;
    }
    if (mk_storage_current_process_is_service_worker() ||
        mk_storage_current_prefers_kernel_hot_path()) {
        return kernel_storage_load(dst, size);
    }

    /*
     * Persist-image loads are only used by the in-memory VFS snapshot restore.
     * On slower machines the single large transfer/request path has proven much
     * more fragile than the regular sector IPC used by AppFS. Reuse the proven
     * sector-read path in bounded chunks so desktop/shell startup exercises the
     * normal storage boundary without stalling on one monolithic request.
     */
    full_sectors = size / KERNEL_PERSIST_SECTOR_SIZE;
    tail_bytes = size % KERNEL_PERSIST_SECTOR_SIZE;

    while (sector_index < full_sectors) {
        uint32_t chunk_sectors = full_sectors - sector_index;
        if (chunk_sectors > MK_STORAGE_PERSIST_IO_CHUNK_SECTORS) {
            chunk_sectors = MK_STORAGE_PERSIST_IO_CHUNK_SECTORS;
        }
        if (mk_storage_service_read_sectors(KERNEL_PERSIST_START_LBA + sector_index,
                                            out + (sector_index * KERNEL_PERSIST_SECTOR_SIZE),
                                            chunk_sectors) != 0) {
            return -1;
        }
        sector_index += chunk_sectors;
    }

    if (tail_bytes != 0u) {
        if (mk_storage_service_read_sectors(KERNEL_PERSIST_START_LBA + full_sectors,
                                            tail_sector,
                                            1u) != 0) {
            return -1;
        }
        memcpy(out + (full_sectors * KERNEL_PERSIST_SECTOR_SIZE), tail_sector, tail_bytes);
    }

    return 0;
}

int mk_storage_service_save(const void *src, uint32_t size) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_persist_request payload;
    uint32_t transfer_id;
    int rc;

    if (src == 0 || size == 0u) {
        return -1;
    }
    if (mk_storage_current_process_is_service_worker() ||
        mk_storage_current_prefers_kernel_hot_path()) {
        return kernel_storage_save(src, size);
    }

    if (mk_transfer_create(mk_storage_current_pid(), size, &transfer_id) != 0) {
        return -1;
    }
    if (mk_storage_share_transfer(transfer_id, MK_TRANSFER_PERM_READ) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    if (mk_transfer_copy_from(transfer_id, src, size) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    payload.size = size;
    payload.transfer_id = transfer_id;
    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_WRITE, &payload, sizeof(payload)) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        (void)mk_transfer_destroy(transfer_id);
        return -1;
    }
    g_last_storage_reply = reply;
    rc = mk_storage_decode_result(&reply);
    (void)mk_transfer_destroy(transfer_id);
    return rc;
}

uint32_t mk_storage_service_total_sectors(void) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_info info;

    if (mk_storage_current_process_is_service_worker()) {
        return kernel_storage_total_sectors();
    }

    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_INFO, 0, 0u) != 0) {
        return 0u;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        return 0u;
    }
    g_last_storage_reply = reply;
    if (reply.payload_size != sizeof(info)) {
        return 0u;
    }
    memcpy(&info, reply.payload, sizeof(info));
    return info.total_sectors;
}

uint32_t mk_storage_service_partition_start_lba(void) {
    struct mk_message request_message;
    struct mk_message reply;
    struct mk_storage_info info;

    if (mk_storage_current_process_is_service_worker()) {
        return kernel_storage_partition_start_lba();
    }

    if (mk_storage_prepare_request(&request_message, MK_MSG_BLOCK_INFO, 0, 0u) != 0) {
        return 0u;
    }
    g_last_storage_request = request_message;
    if (mk_service_request(MK_SERVICE_STORAGE, &request_message, &reply) != 0) {
        return 0u;
    }
    g_last_storage_reply = reply;
    if (reply.payload_size != sizeof(info)) {
        return 0u;
    }
    memcpy(&info, reply.payload, sizeof(info));
    return info.partition_start_lba;
}

int mk_storage_service_last_request(struct mk_message *message) {
    if (message == 0) {
        return -1;
    }

    *message = g_last_storage_request;
    return g_last_storage_request.type == MK_MSG_NONE ? -1 : 0;
}
