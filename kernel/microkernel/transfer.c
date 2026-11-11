#include <kernel/kernel_string.h>
#include <kernel/lock.h>
#include <kernel/memory/heap.h>
#include <kernel/microkernel/transfer.h>
#include <kernel/scheduler.h>

struct mk_transfer_slot {
    uint32_t id;
    uint32_t owner_pid;
    uint32_t shared_pid;
    uint32_t shared_permissions;
    uint32_t size;
    void *data;
    /*
     * Reference count of raw pointers currently handed out via
     * mk_transfer_data_read/write. A service handler holds one of those
     * pointers across an unlocked (possibly slow, e.g. ATA PIO) write. If
     * the owning client times out its request and destroys the transfer
     * while that write is in flight on another CPU, freeing the buffer
     * immediately is a use-after-free that lets the handler scribble on
     * whatever the allocator hands out next (observed as process-list /
     * scheduler-global corruption under SMP). While in_use > 0, destroy
     * only marks the slot dying; the last release performs the free.
     */
    uint32_t in_use;
    uint32_t dying;
};

static struct mk_transfer_slot g_transfer_slots[MK_TRANSFER_SLOTS];
static uint32_t g_next_transfer_id = 1u;
static spinlock_t g_transfer_lock;

void mk_transfer_init(void) {
    memset(g_transfer_slots, 0, sizeof(g_transfer_slots));
    g_next_transfer_id = 1u;
    spinlock_init(&g_transfer_lock);
}

static uint32_t mk_transfer_current_pid(void) {
    return scheduler_current_pid();
}

static struct mk_transfer_slot *mk_transfer_find_slot_unlocked(uint32_t transfer_id) {
    uint32_t i;

    if (transfer_id == 0u) {
        return 0;
    }

    for (i = 0; i < MK_TRANSFER_SLOTS; ++i) {
        if (g_transfer_slots[i].id == transfer_id) {
            return &g_transfer_slots[i];
        }
    }

    return 0;
}

static int mk_transfer_has_access(const struct mk_transfer_slot *slot, uint32_t pid, uint32_t permissions) {
    if (slot == 0) {
        return 0;
    }
    /*
     * pid==0 means "no task context" (g_current == NULL on this CPU: idle, or
     * pre-init boot code). It used to grant access to EVERY transfer — any
     * code running from such a context could read or scribble another task's
     * buffer unchecked, exactly the "text copied into a buffer it does not
     * own" shape of the classe #5 face-(a) dumps (decimal path digits landed
     * in the AP stack-block tail neighborhood). Tightened: the no-task
     * context only reaches transfers also owned by pid 0 (boot-created, one
     * trust domain before real tasks exist). Task-context callers (all
     * syscall handlers and service workers) carry their real pid and are
     * unaffected — proven by the UP/SMP gates run with this change.
     */
    if (pid == 0u) {
        return slot->owner_pid == 0u;
    }
    if (slot->owner_pid == pid) {
        return 1;
    }
    if (slot->shared_pid == pid && (slot->shared_permissions & permissions) == permissions) {
        return 1;
    }
    return 0;
}

int mk_transfer_create(uint32_t owner_pid, uint32_t size, uint32_t *transfer_id_out) {
    uint32_t i;
    struct mk_transfer_slot *slot;
    uint32_t flags;

    if (transfer_id_out == 0 || size == 0u) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = 0;
    for (i = 0; i < MK_TRANSFER_SLOTS; ++i) {
        if (g_transfer_slots[i].id == 0u) {
            slot = &g_transfer_slots[i];
            break;
        }
    }
    if (slot == 0) {
        spinlock_unlock_irqrestore(&g_transfer_lock, flags);
        return -1;
    }

    slot->data = kernel_malloc(size);
    if (slot->data == 0) {
        spinlock_unlock_irqrestore(&g_transfer_lock, flags);
        return -1;
    }

    slot->id = g_next_transfer_id++;
    if (slot->id == 0u) {
        slot->id = g_next_transfer_id++;
    }
    slot->owner_pid = owner_pid;
    slot->shared_pid = 0u;
    slot->shared_permissions = 0u;
    slot->size = size;
    memset(slot->data, 0, size);
    *transfer_id_out = slot->id;
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
    return 0;
}

int mk_transfer_share(uint32_t transfer_id, uint32_t pid, uint32_t permissions) {
    struct mk_transfer_slot *slot;
    uint32_t flags;
    uint32_t current_pid;

    if (pid == 0u || permissions == 0u) {
        return -1;
    }

    current_pid = mk_transfer_current_pid();
    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = mk_transfer_find_slot_unlocked(transfer_id);
    if (!mk_transfer_has_access(slot, current_pid, MK_TRANSFER_PERM_READ | MK_TRANSFER_PERM_WRITE)) {
        spinlock_unlock_irqrestore(&g_transfer_lock, flags);
        return -1;
    }
    slot->shared_pid = pid;
    slot->shared_permissions = permissions;
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
    return 0;
}

const void *mk_transfer_data_read(uint32_t transfer_id) {
    const void *data = 0;
    struct mk_transfer_slot *slot;
    uint32_t flags;

    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = mk_transfer_find_slot_unlocked(transfer_id);
    if (mk_transfer_has_access(slot, mk_transfer_current_pid(), MK_TRANSFER_PERM_READ)) {
        data = slot->data;
        slot->in_use += 1u;
    }
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
    return data;
}

void *mk_transfer_data_write(uint32_t transfer_id) {
    void *data = 0;
    struct mk_transfer_slot *slot;
    uint32_t flags;

    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = mk_transfer_find_slot_unlocked(transfer_id);
    if (mk_transfer_has_access(slot, mk_transfer_current_pid(), MK_TRANSFER_PERM_WRITE)) {
        data = slot->data;
        slot->in_use += 1u;
    }
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
    return data;
}

/*
 * Release one reference taken by mk_transfer_data_read/write. If the slot
 * was destroyed while this reference was held, the final release performs
 * the deferred free. Callers MUST release exactly once per successful
 * data_read/data_write before returning.
 */
void mk_transfer_release(uint32_t transfer_id) {
    struct mk_transfer_slot *slot;
    uint32_t flags;

    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = mk_transfer_find_slot_unlocked(transfer_id);
    if (slot != 0 && slot->in_use > 0u) {
        slot->in_use -= 1u;
        if (slot->dying != 0u && slot->in_use == 0u) {
            if (slot->data != 0) {
                kernel_free(slot->data);
            }
            memset(slot, 0, sizeof(*slot));
        }
    }
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
}

uint32_t mk_transfer_size(uint32_t transfer_id) {
    struct mk_transfer_slot *slot;
    uint32_t size = 0u;
    uint32_t flags;

    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = mk_transfer_find_slot_unlocked(transfer_id);
    if (mk_transfer_has_access(slot, mk_transfer_current_pid(), MK_TRANSFER_PERM_READ) ||
        mk_transfer_has_access(slot, mk_transfer_current_pid(), MK_TRANSFER_PERM_WRITE)) {
        size = slot->size;
    }
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
    return size;
}

int mk_transfer_copy_from(uint32_t transfer_id, const void *src, uint32_t size) {
    struct mk_transfer_slot *slot;
    uint32_t flags;

    if (src == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = mk_transfer_find_slot_unlocked(transfer_id);
    if (!mk_transfer_has_access(slot, mk_transfer_current_pid(), MK_TRANSFER_PERM_WRITE) ||
        size > slot->size) {
        spinlock_unlock_irqrestore(&g_transfer_lock, flags);
        return -1;
    }

    memcpy(slot->data, src, size);
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
    return 0;
}

int mk_transfer_copy_to(uint32_t transfer_id, void *dst, uint32_t size) {
    struct mk_transfer_slot *slot;
    uint32_t flags;

    if (dst == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = mk_transfer_find_slot_unlocked(transfer_id);
    if (!mk_transfer_has_access(slot, mk_transfer_current_pid(), MK_TRANSFER_PERM_READ) ||
        size > slot->size) {
        spinlock_unlock_irqrestore(&g_transfer_lock, flags);
        return -1;
    }

    memcpy(dst, slot->data, size);
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
    return 0;
}

int mk_transfer_destroy(uint32_t transfer_id) {
    struct mk_transfer_slot *slot;
    uint32_t flags;
    uint32_t current_pid;

    current_pid = mk_transfer_current_pid();
    flags = spinlock_lock_irqsave(&g_transfer_lock);
    slot = mk_transfer_find_slot_unlocked(transfer_id);

    if (slot == 0 || (current_pid != 0u && current_pid != slot->owner_pid)) {
        spinlock_unlock_irqrestore(&g_transfer_lock, flags);
        return -1;
    }

    /*
     * Defer the free while a service handler still holds a raw pointer
     * (in_use > 0). Mark the slot dying; the last mk_transfer_release does
     * the actual kernel_free + clear. This closes the client-timeout vs
     * host-write use-after-free that corrupted the heap under SMP.
     */
    if (slot->in_use > 0u) {
        slot->dying = 1u;
        spinlock_unlock_irqrestore(&g_transfer_lock, flags);
        return 0;
    }

    if (slot->data != 0) {
        kernel_free(slot->data);
    }
    memset(slot, 0, sizeof(*slot));
    spinlock_unlock_irqrestore(&g_transfer_lock, flags);
    return 0;
}
