#include <kernel/memory/heap.h>
#include <kernel/telemetry.h>
#include <kernel/lock.h>
#include <kernel/kernel_string.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/scheduler.h>

/*
 * Block layout: 16-byte header {size, magic, next, reserved} followed by an
 * 8-byte-aligned payload. `next` is only meaningful while a block is free.
 *
 * The magic turns heap-ownership bugs into named O(1) events:
 *  - malloc taking a block already marked ALLOC  => DOUBLE-ALLOC trace
 *    (leading hypothesis for the SMP task-list corruption);
 *  - free of a block not marked ALLOC            => FREE-UNTRACKED trace,
 *    and the free is skipped so one bad pointer cannot poison the list.
 *
 * The previous 4-byte header aliased its free-list `next` link over the
 * payload's first word, so "block->next = NULL" during free silently zeroed
 * the owner's first field (observed under SMP as the storage host's pid
 * becoming 0 while kind/state stayed intact).
 */
struct free_block {
    size_t size;
    size_t magic;
    struct free_block *next;
    size_t reserved;
};

#define HEAP_ALIGN 8u
#define HEAP_MIN_SPLIT 32u
#define ALIGNED_FLAG ((size_t)0x80000000u)
#define HEAP_MAGIC_FREE  ((size_t)0x46524545u) /* "FREE" */
#define HEAP_MAGIC_ALLOC ((size_t)0x414C4C4Fu) /* "ALLO" */
#define HEAP_HEADER_SIZE ((size_t)16u)
#define HEAP_CANARY_SIZE ((size_t)16u)
#define HEAP_CANARY_WORD ((size_t)0xDEADC0DEu)
#define HEAP_MIN_BLOCK   (HEAP_HEADER_SIZE + HEAP_ALIGN + HEAP_CANARY_SIZE)

/*
 * Free-list integrity diagnostics (COMPLETION_ACTION_PLAN P4). Budgeted so
 * a systemic bug cannot flood the serial line.
 */
#define HEAP_DIAG_BUDGET 6u
#define HEAP_FREELIST_WALK_CAP 4096u
static volatile uint32_t g_heap_diag_budget = HEAP_DIAG_BUDGET;

static void heap_diag(const char *what, uintptr_t block, size_t size, uintptr_t other) {
    if (g_heap_diag_budget == 0u) {
        return;
    }
    g_heap_diag_budget--;
    kernel_debug_printf("heap: %s block=0x%x size=0x%x other=0x%x\n",
                        what, (unsigned int)block, (unsigned int)size,
                        (unsigned int)other);
}

static uintptr_t g_heap_start = 0u;
static uintptr_t g_heap_end = 0u;
static struct free_block *g_free_list = NULL;
static size_t g_heap_used = 0u;
static spinlock_t g_heap_lock;
static volatile uint32_t g_heap_initialized = 0u;

/*
 * Edge canary: the last HEAP_CANARY_SIZE bytes of every allocated block are
 * a fixed pattern written at malloc and verified at free. A FORWARD overflow
 * from a block's payload (the refined SMP signature: neighbour headers filled
 * with return addresses / printed-address fragments) destroys the canary of
 * the block it overflows INTO only if that block is later freed — but it
 * ALWAYS destroys the tail of the overflowing block's own region first when
 * the write runs past the requested size. Verified before coalescing.
 */
static void heap_canary_write(struct free_block *block) {
    size_t *canary = (size_t *)((uint8_t *)block + block->size - HEAP_CANARY_SIZE);
    canary[0] = HEAP_CANARY_WORD;
    canary[1] = HEAP_CANARY_WORD;
    canary[2] = HEAP_CANARY_WORD;
    canary[3] = HEAP_CANARY_WORD;
}

static int heap_canary_ok(const struct free_block *block) {
    const size_t *canary = (const size_t *)((const uint8_t *)block + block->size - HEAP_CANARY_SIZE);
    return canary[0] == HEAP_CANARY_WORD && canary[1] == HEAP_CANARY_WORD &&
           canary[2] == HEAP_CANARY_WORD && canary[3] == HEAP_CANARY_WORD;
}

/*
 * Watch-PC registry (tests the leading SMP hypothesis directly): the refined
 * signature (victim header filled with SELF-REFERENTIAL heap pointers into
 * its own interior) is exactly what a block looks like after it is FREED and
 * REALLOCATED to a different structure while the scheduler list still links
 * the old address — the guards stay silent because the free/realloc are
 * legitimate API calls. Registering the allocation-site PC of task blocks
 * makes kernel_free trace any free of such a block, NAMING the caller that
 * freed it (freepc) — the smoking gun if a linked task is ever released.
 */
#define HEAP_WATCH_MAX 8u
static uintptr_t g_heap_watch_pcs[HEAP_WATCH_MAX];
static uint32_t g_heap_watch_count;
static volatile uint32_t g_heap_watch_budget = VIBE_TRACE_BUDGET(12u);

void kernel_heap_watch_pc(uintptr_t pc) {
    uint32_t i;
    uint32_t flags;

    if (pc == 0u) {
        return;
    }
    flags = spinlock_lock_irqsave(&g_heap_lock);
    for (i = 0u; i < g_heap_watch_count; ++i) {
        if (g_heap_watch_pcs[i] == pc) {
            spinlock_unlock_irqrestore(&g_heap_lock, flags);
            return;
        }
    }
    if (g_heap_watch_count < HEAP_WATCH_MAX) {
        g_heap_watch_pcs[g_heap_watch_count++] = pc;
    }
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
}

static int heap_pc_watched(uintptr_t pc) {
    uint32_t i;

    for (i = 0u; i < g_heap_watch_count; ++i) {
        if (g_heap_watch_pcs[i] == pc) {
            return 1;
        }
    }
    return 0;
}

static size_t heap_align_up(size_t size) {
    if (size & (HEAP_ALIGN - 1u)) {
        size += HEAP_ALIGN - (size & (HEAP_ALIGN - 1u));
    }
    return size;
}

static size_t heap_total_free(void) {
    size_t total = 0u;
    struct free_block *curr = g_free_list;
    uint32_t steps = 0u;

    while (curr) {
        total += curr->size;
        curr = curr->next;
        if (++steps > HEAP_FREELIST_WALK_CAP) {
            break;
        }
    }
    return total;
}

void kernel_mm_init(uintptr_t heap_start, size_t heap_size) {
    spinlock_init(&g_heap_lock);
    g_heap_start = heap_start;
    g_heap_end = heap_start + heap_size;
    g_heap_used = 0u;
    g_free_list = (struct free_block *)(uintptr_t)heap_start;
    g_free_list->size = heap_size;
    g_free_list->magic = HEAP_MAGIC_FREE;
    g_free_list->next = NULL;
    g_free_list->reserved = 0u;
    g_heap_initialized = 1u;
}

void *kernel_malloc(size_t size) {
    struct free_block *prev, *curr, *best_prev, *best;
    size_t best_size, needed, align_sz;
    uint32_t flags;
    /*
     * Allocation-site shadow (COMPLETION_ACTION_PLAN P4): the caller PC is
     * parked in the header's reserved word. The scheduler BAD-NODE trace
     * dumps it, so a corrupt task-list node names the allocation that
     * owns the block without relying on fixed-address watchpoints (heap
     * addresses shift between runs even under -icount).
     */
    uintptr_t call_pc = (uintptr_t)__builtin_return_address(0);

    if (size == 0u)
        return NULL;

    if (g_heap_initialized == 0u)
        return NULL;

    align_sz = heap_align_up(size);
    needed = HEAP_HEADER_SIZE + align_sz + HEAP_CANARY_SIZE;
    if (needed < HEAP_MIN_BLOCK)
        needed = HEAP_MIN_BLOCK;

    flags = spinlock_lock_irqsave(&g_heap_lock);

    best = NULL;
    best_prev = NULL;
    best_size = (size_t)-1;
    prev = NULL;
    curr = g_free_list;

    while (curr) {
        if (curr->magic != HEAP_MAGIC_FREE) {
            heap_diag("BAD-FREE-BLOCK", (uintptr_t)curr, curr->size, curr->magic);
            break;
        }
        if (curr->size >= needed && curr->size < best_size) {
            best = curr;
            best_prev = prev;
            best_size = curr->size;
            if (best_size == needed)
                break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (!best) {
        spinlock_unlock_irqrestore(&g_heap_lock, flags);
        return NULL;
    }

    if (best->magic == HEAP_MAGIC_ALLOC) {
        heap_diag("DOUBLE-ALLOC", (uintptr_t)best, best->size, 0u);
    }

    if (best->size - needed > HEAP_MIN_SPLIT) {
        struct free_block *rem = (struct free_block *)((uint8_t *)best + needed);

        rem->size = best->size - needed;
        rem->magic = HEAP_MAGIC_FREE;
        rem->next = best->next;
        rem->reserved = 0u;
        best->size = needed;
        if (best_prev)
            best_prev->next = rem;
        else
            g_free_list = rem;
    } else {
        if (best_prev)
            best_prev->next = best->next;
        else
            g_free_list = best->next;
    }

    best->magic = HEAP_MAGIC_ALLOC;
    best->next = NULL;
    best->reserved = call_pc;
    heap_canary_write(best);
    g_heap_used += best->size;
    spinlock_unlock_irqrestore(&g_heap_lock, flags);

    /*
     * Face-1 alias witness (COMPLETION_ACTION_PLAN P4, classe #5 face (f)):
     * a block we just handed out must not overlap the declared stack range
     * of a live task. If it does, the allocator returned memory another
     * owner is still using — the crash-storm signature (cs=heap address,
     * GP eip, stuck spinlock and ud-bt sp ALL inside
     * [0x390d8080,0x390e8080), one 64KB task stack) says several owners
     * point at one block while every field guard passes. Budgeted; names
     * the victim pair at the exact double-ownership moment. Runs AFTER the
     * heap lock is dropped; the walk itself takes no locks.
     */
    {
        uintptr_t blo = (uintptr_t)best;
        uintptr_t bhi = blo + best->size;
        uintptr_t os = 0u, oe = 0u;
        int ost = -1;
        int victim = scheduler_stack_overlap(blo, bhi, &os, &oe, &ost);
        int rec_victim;

        if (victim >= 0) {
            static volatile uint32_t g_heap_alias_budget = VIBE_TRACE_BUDGET(6u);
            if (g_heap_alias_budget != 0u) {
                g_heap_alias_budget--;
                kernel_debug_printf("heap-alias: alloc=[%x,%x) overlaps live pid=%d state=%d stack=[%x,%x) allocpc=%x\n",
                                    (unsigned int)blo, (unsigned int)bhi,
                                    victim, ost,
                                    (unsigned int)os, (unsigned int)oe,
                                    (unsigned int)call_pc);
            }
        }
        /* RECORD twin (P4 adendo 34): a block handed out overlapping the
         * process_t record of a still-linked task is stale-typed-pointer
         * double-ownership at its birth instant. Self-matches cannot occur
         * (the new owner is linked only later), so ANY hit is a finding. */
        ost = -1;
        rec_victim = scheduler_record_overlap(blo, bhi, &os, &oe, &ost);
        if (rec_victim >= 0) {
            static volatile uint32_t g_heap_recalias_budget = VIBE_TRACE_BUDGET(6u);
            if (g_heap_recalias_budget != 0u) {
                g_heap_recalias_budget--;
                kernel_debug_printf("heap-recalias: alloc=[%x,%x) overlaps live pid=%d state=%d rec=[%x,%x) allocpc=%x\n",
                                    (unsigned int)blo, (unsigned int)bhi,
                                    rec_victim, ost,
                                    (unsigned int)os, (unsigned int)oe,
                                    (unsigned int)call_pc);
            }
        }
    }

    return (void *)((uint8_t *)best + HEAP_HEADER_SIZE);
}

void *kernel_malloc_aligned(size_t size, size_t align) {
    void *raw;
    uintptr_t raw_addr, aligned_addr;
    size_t shift;

    if (size == 0u)
        return NULL;
    if (align == 0u)
        align = 8u;
    if ((align & (align - 1u)) != 0u)
        return NULL;
    if (size & (HEAP_ALIGN - 1u))
        size += HEAP_ALIGN - (size & (HEAP_ALIGN - 1u));

    /*
     * Reserve two alignment windows: if raw happens to already be aligned
     * we must still shift by one `align` so the link word written at
     * aligned_addr - sizeof(size_t) can never alias the block's own size
     * header (the old code clobbered the header with ALIGNED_FLAG when
     * shift==0, and the later free then treated a 0x80000000-byte block as
     * free space).
     */
    raw = kernel_malloc(size + 2u * align + sizeof(size_t));
    if (!raw)
        return NULL;

    raw_addr = (uintptr_t)raw;
    aligned_addr = (raw_addr + align - 1u) & ~(align - 1u);
    if (aligned_addr == raw_addr) {
        aligned_addr += align;
    }
    shift = aligned_addr - raw_addr;

    *(size_t *)(aligned_addr - sizeof(size_t)) = shift | ALIGNED_FLAG;
    return (void *)aligned_addr;
}

void *kernel_realloc(void *ptr, size_t size) {
    struct free_block *block;
    size_t old_size, copy_size;
    void *new_ptr;

    if (!ptr)
        return kernel_malloc(size);
    if (size == 0u) {
        kernel_free(ptr);
        return NULL;
    }

    block = (struct free_block *)((uint8_t *)ptr - HEAP_HEADER_SIZE);
    /* Usable payload excludes both the header and the trailing canary. */
    old_size = block->size - HEAP_HEADER_SIZE - HEAP_CANARY_SIZE;

    if (size & (HEAP_ALIGN - 1u))
        size += HEAP_ALIGN - (size & (HEAP_ALIGN - 1u));

    new_ptr = kernel_malloc(size);
    if (!new_ptr)
        return NULL;

    copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);
    kernel_free(ptr);
    return new_ptr;
}

void kernel_free(void *ptr) {
    struct free_block *block, *prev, *curr;
    size_t stored, free_size;
    uint32_t flags;

    if (!ptr)
        return;
    if (g_heap_initialized == 0u)
        return;

    stored = *(size_t *)((uint8_t *)ptr - sizeof(size_t));

    if (stored & ALIGNED_FLAG) {
        size_t shift = stored & ~ALIGNED_FLAG;
        block = (struct free_block *)((uint8_t *)ptr - shift - HEAP_HEADER_SIZE);
    } else {
        block = (struct free_block *)((uint8_t *)ptr - HEAP_HEADER_SIZE);
    }

    free_size = block->size;
    flags = spinlock_lock_irqsave(&g_heap_lock);

    if (block->magic != HEAP_MAGIC_ALLOC) {
        heap_diag("FREE-UNTRACKED", (uintptr_t)block, free_size, block->magic);
        spinlock_unlock_irqrestore(&g_heap_lock, flags);
        return;
    }

    if ((uintptr_t)block < g_heap_start ||
        (uintptr_t)block + free_size > g_heap_end ||
        free_size < HEAP_MIN_BLOCK ||
        (free_size & (HEAP_ALIGN - 1u)) != 0u) {
        heap_diag("BAD-BLOCK", (uintptr_t)block, free_size, g_heap_start);
        spinlock_unlock_irqrestore(&g_heap_lock, flags);
        return;
    }

    /*
     * Canary + watch checks run BEFORE coalescing mutates block->size.
     * A trashed canary names the overflowing block's allocation site; a
     * watched allocpc names the caller that freed a task-owned block (the
     * leading SMP hypothesis: a linked task block released and reused).
     */
    if (!heap_canary_ok(block)) {
        if (g_heap_diag_budget != 0u) {
            g_heap_diag_budget--;
            kernel_debug_printf("heap: CANARY block=0x%x size=0x%x allocpc=0x%x\n",
                                (unsigned int)(uintptr_t)block,
                                (unsigned int)free_size,
                                (unsigned int)block->reserved);
        }
    }
    if (heap_pc_watched(block->reserved) && g_heap_watch_budget != 0u) {
        g_heap_watch_budget--;
        kernel_debug_printf("heap: WATCHED-FREE block=0x%x size=0x%x allocpc=0x%x freepc=0x%x\n",
                            (unsigned int)(uintptr_t)block,
                            (unsigned int)free_size,
                            (unsigned int)block->reserved,
                            (unsigned int)(uintptr_t)__builtin_return_address(0));
    }

    /*
     * Face-1 FREE-side alias witness (P4, classe #5 face (f), adendo 12
     * sonda seguinte): the malloc-side check refuted the "allocator hands a
     * live stack block to a NEW owner" variant, but the complementary entry
     * — a stack block RETURNED TO THE FREELIST while its owner is still
     * alive and using it — is only observable here, at the release moment
     * itself (the owner keeps writing into memory that is also on the free
     * list; a later alloc need not even land on it for frames to collide).
     * Legit releases are covered by the walk rules: TERMINATED tasks are
     * skipped (process_destroy of a dead task is fine), and the execve
     * deferred free is safe because current->stack already points to the
     * NEW stack, not the parked block being released.
     */
    {
        uintptr_t lo = (uintptr_t)ptr;
        uintptr_t hi = lo + (free_size - HEAP_HEADER_SIZE - HEAP_CANARY_SIZE);
        uintptr_t os = 0u, oe = 0u;
        int ost = -1;
        int victim = scheduler_stack_overlap(lo, hi, &os, &oe, &ost);

        if (victim >= 0) {
            static volatile uint32_t g_heap_free_live_budget = VIBE_TRACE_BUDGET(6u);
            if (g_heap_free_live_budget != 0u) {
                g_heap_free_live_budget--;
                kernel_debug_printf("heap-free-live-stack: free=[%x,%x) names live pid=%d state=%d stack=[%x,%x) allocpc=%x freepc=%x\n",
                                    (unsigned int)lo, (unsigned int)hi,
                                    victim, ost,
                                    (unsigned int)os, (unsigned int)oe,
                                    (unsigned int)block->reserved,
                                    (unsigned int)(uintptr_t)__builtin_return_address(0));
            }
        }
        /* RECORD twin (P4 adendo 34): releasing a block that overlaps a
         * still-linked task's process_t record = freeing a live record
         * through some path the STILL-LINKED guard does not police. */
        ost = -1;
        {
            int rec_victim = scheduler_record_overlap((uintptr_t)block,
                                                      (uintptr_t)block + free_size,
                                                      &os, &oe, &ost);

            if (rec_victim >= 0) {
                static volatile uint32_t g_heap_free_rec_budget = VIBE_TRACE_BUDGET(6u);
                if (g_heap_free_rec_budget != 0u) {
                    g_heap_free_rec_budget--;
                    kernel_debug_printf("heap-free-live-record: free=[%x,%x) live pid=%d state=%d rec=[%x,%x) freepc=%x\n",
                                        (unsigned int)(uintptr_t)block,
                                        (unsigned int)((uintptr_t)block + free_size),
                                        rec_victim, ost,
                                        (unsigned int)os, (unsigned int)oe,
                                        (unsigned int)(uintptr_t)__builtin_return_address(0));
                }
            }
        }
    }

    block->magic = HEAP_MAGIC_FREE;
    block->reserved = 0u;

    prev = NULL;
    curr = g_free_list;
    {
        uint32_t steps = 0u;
        while (curr && (uintptr_t)curr < (uintptr_t)block) {
            if (curr == block) {
                heap_diag("DOUBLE-FREE", (uintptr_t)block, free_size, 0u);
                spinlock_unlock_irqrestore(&g_heap_lock, flags);
                return;
            }
            if (++steps > HEAP_FREELIST_WALK_CAP) {
                heap_diag("LIST-CYCLE", (uintptr_t)block, free_size, (uintptr_t)curr);
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }

    if (prev && (uint8_t *)prev + prev->size > (uint8_t *)block) {
        heap_diag("OVERLAP-PREV", (uintptr_t)block, free_size, (uintptr_t)prev);
    }
    if (curr && (uint8_t *)block + free_size > (uint8_t *)curr) {
        heap_diag("OVERLAP-NEXT", (uintptr_t)block, free_size, (uintptr_t)curr);
    }

    block->next = curr;
    if (prev)
        prev->next = block;
    else
        g_free_list = block;

    if (curr && (uint8_t *)block + block->size == (uint8_t *)curr &&
        curr->magic == HEAP_MAGIC_FREE) {
        block->size += curr->size;
        block->next = curr->next;
    }

    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)block &&
        prev->magic == HEAP_MAGIC_FREE) {
        prev->size += block->size;
        prev->next = block->next;
    }

    g_heap_used -= free_size;
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
}

uintptr_t kernel_heap_start(void) {
    return g_heap_start;
}

uintptr_t kernel_heap_end(void) {
    return g_heap_end;
}

size_t kernel_heap_used(void) {
    size_t value;
    uint32_t flags;

    if (g_heap_initialized == 0u)
        return 0u;
    flags = spinlock_lock_irqsave(&g_heap_lock);
    value = g_heap_used;
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return value;
}

size_t kernel_heap_free(void) {
    size_t value;
    uint32_t flags;

    if (g_heap_initialized == 0u)
        return 0u;
    flags = spinlock_lock_irqsave(&g_heap_lock);
    value = heap_total_free();
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return value;
}

int kernel_heap_is_initialized(void) {
    return g_heap_initialized != 0u;
}

/*
 * Membership probe (COMPLETION_ACTION_PLAN P4). Returns 1 when ptr is the
 * payload of a LIVE allocated block: inside the heap window, 8-aligned, and
 * its 16-byte header carries HEAP_MAGIC_ALLOC with a sane size. Used by the
 * scheduler walk to reject wild pointers (kernel TEXT addresses, interior
 * pointers) BEFORE following them as task nodes. The lock is taken only to
 * read the header consistently against a concurrent free on another CPU.
 */
int kernel_heap_is_block(const void *ptr) {
    const struct free_block *block;
    uintptr_t addr = (uintptr_t)ptr;
    uint32_t flags;
    int ok = 0;

    if (g_heap_initialized == 0u || ptr == 0) {
        return 0;
    }
    if (addr < g_heap_start + HEAP_HEADER_SIZE || addr >= g_heap_end) {
        return 0;
    }
    if ((addr & (HEAP_ALIGN - 1u)) != 0u) {
        return 0;
    }

    block = (const struct free_block *)(addr - HEAP_HEADER_SIZE);
    flags = spinlock_lock_irqsave(&g_heap_lock);
    if (block->magic == HEAP_MAGIC_ALLOC &&
        block->size >= HEAP_MIN_BLOCK &&
        (block->size & (HEAP_ALIGN - 1u)) == 0u &&
        addr + block->size <= g_heap_end + HEAP_HEADER_SIZE) {
        ok = 1;
    }
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return ok;
}

/*
 * Physical-predecessor attribution probe (COMPLETION_ACTION_PLAN P4, class
 * #2: a live task block's edge canary is destroyed while the block is still
 * ALLOCATED — not a free/realloc race, not a user pointer (zero EFAULT
 * proves it)). Blocks in this first-fit heap are physically contiguous:
 * each 16-byte header's `size` spans header+payload+canary, so walking from
 * g_heap_start by `size` enumerates every block in address order. Given a
 * victim payload pointer, return the block IMMEDIATELY BELOW it in memory —
 * the only forward-overflow candidate. The caller dumps the predecessor's
 * allocpc + size to name the writer's allocation site.
 *
 * Returns 1 on success (pred_* filled), 0 when the victim is the first
 * block, not a block, or the walk desynchronized (corrupted size mid-walk).
 * Runs entirely under g_heap_lock so the walk is consistent against
 * concurrent alloc/free on the other CPU.
 */
int kernel_heap_block_predecessor(const void *ptr,
                                  uintptr_t *pred_start_out,
                                  size_t *pred_size_out,
                                  uintptr_t *pred_allocpc_out,
                                  int *pred_canary_ok_out) {
    const struct free_block *prev_blk;
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t victim_hdr;
    uintptr_t cursor;
    uint32_t flags;
    uint32_t steps = 0u;

    if (g_heap_initialized == 0u || ptr == 0) {
        return 0;
    }
    if (addr < g_heap_start + HEAP_HEADER_SIZE || addr >= g_heap_end) {
        return 0;
    }
    if ((addr & (HEAP_ALIGN - 1u)) != 0u) {
        return 0;
    }
    victim_hdr = addr - HEAP_HEADER_SIZE;

    flags = spinlock_lock_irqsave(&g_heap_lock);

    /*
     * The victim's OWN header is deliberately NOT validated: a forward
     * overflow that reaches its canary has usually already trashed its
     * size/magic too (observed: pid=3 victim header magic=0x16 allocpc=0x1),
     * so trusting it would reject exactly the case we are diagnosing. The
     * walk advances by the PREDECESSORS' sizes and stops when it lands on
     * victim_hdr; it never reads the victim's own header.
     */
    prev_blk = NULL;
    cursor = g_heap_start;
    while (cursor < victim_hdr) {
        const struct free_block *cur = (const struct free_block *)cursor;

        if (cur->magic != HEAP_MAGIC_ALLOC && cur->magic != HEAP_MAGIC_FREE) {
            /* Walk desynced: a header mid-chain is trashed. */
            spinlock_unlock_irqrestore(&g_heap_lock, flags);
            return 0;
        }
        if (cur->size < HEAP_MIN_BLOCK || (cur->size & (HEAP_ALIGN - 1u)) != 0u ||
            cursor + cur->size > g_heap_end) {
            spinlock_unlock_irqrestore(&g_heap_lock, flags);
            return 0;
        }
        prev_blk = cur;
        cursor += cur->size;
        if (++steps > 65536u) {
            spinlock_unlock_irqrestore(&g_heap_lock, flags);
            return 0;
        }
    }

    if (prev_blk == NULL || cursor != victim_hdr) {
        spinlock_unlock_irqrestore(&g_heap_lock, flags);
        return 0;
    }

    if (pred_start_out) {
        *pred_start_out = (uintptr_t)prev_blk;
    }
    if (pred_size_out) {
        *pred_size_out = prev_blk->size;
    }
    if (pred_allocpc_out) {
        *pred_allocpc_out = (prev_blk->magic == HEAP_MAGIC_ALLOC) ? prev_blk->reserved : 0u;
    }
    if (pred_canary_ok_out) {
        *pred_canary_ok_out = (prev_blk->magic == HEAP_MAGIC_ALLOC) ? heap_canary_ok(prev_blk) : -1;
    }
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return 1;
}

/*
 * Physical block-chain sweep for corruption forensics (declared in
 * headers/kernel/memory/heap.h). Walks by header sizes from the heap start,
 * never by any scheduler/waitable list, so it still works when g_head and
 * the neighborhood of the victim are already trashed. It reports:
 *   - every ALLOC block with a broken EDGE canary (the overflowing source,
 *     named by its allocpc = allocation site),
 *   - the last up-to-2 well-formed blocks before the desync/victim (the
 *     overflow source is the block immediately before the first trashed
 *     header),
 *   - the exact desync address + raw header words.
 * Budgeted: two sweeps total, so it can never flood the serial.
 */
#define HEAP_SWEEP_MAX_BLOCKS 4096u

static volatile uint32_t g_heap_sweep_budget = 2u;
static volatile uint32_t g_heap_sweep_detail_budget = 2u;

void kernel_heap_forensic_sweep(const void *victim) {
    const struct free_block *prev1 = NULL;
    const struct free_block *prev2 = NULL;
    uintptr_t victim_hdr;
    uintptr_t cursor;
    uint32_t flags;
    uint32_t steps = 0u;

    if (g_heap_initialized == 0u || victim == 0) {
        return;
    }
    if (g_heap_sweep_budget == 0u) {
        return;
    }
    g_heap_sweep_budget--;

    cursor = g_heap_start;
    victim_hdr = ((uintptr_t)victim) - HEAP_HEADER_SIZE;

    flags = spinlock_lock_irqsave(&g_heap_lock);
    while (cursor < g_heap_end && steps < HEAP_SWEEP_MAX_BLOCKS) {
        const struct free_block *blk = (const struct free_block *)cursor;

        if (cursor == victim_hdr) {
            kernel_debug_printf("heap-sweep: victim-hdr block=0x%x size=0x%x magic=%x allocpc=%x\n",
                                (unsigned int)cursor,
                                (unsigned int)blk->size,
                                (unsigned int)blk->magic,
                                (unsigned int)blk->reserved);
            kernel_debug_printf("heap-sweep: victim-chain step=%u prev1{start=0x%x size=0x%x allocpc=0x%x cnry=%d} prev2{start=0x%x size=0x%x allocpc=0x%x cnry=%d}\n",
                                (unsigned int)steps,
                                prev1 ? (unsigned int)(uintptr_t)prev1 : 0u,
                                prev1 ? (unsigned int)prev1->size : 0u,
                                prev1 && prev1->magic == HEAP_MAGIC_ALLOC ? (unsigned int)prev1->reserved : 0u,
                                prev1 && prev1->magic == HEAP_MAGIC_ALLOC ? (heap_canary_ok(prev1) ? 1 : 0) : -1,
                                prev2 ? (unsigned int)(uintptr_t)prev2 : 0u,
                                prev2 ? (unsigned int)prev2->size : 0u,
                                prev2 && prev2->magic == HEAP_MAGIC_ALLOC ? (unsigned int)prev2->reserved : 0u,
                                prev2 && prev2->magic == HEAP_MAGIC_ALLOC ? (heap_canary_ok(prev2) ? 1 : 0) : -1);
            break;
        }

        if (blk->magic != HEAP_MAGIC_ALLOC && blk->magic != HEAP_MAGIC_FREE) {
            const uint32_t *w = (const uint32_t *)blk;
            kernel_debug_printf("heap-sweep: DESYNC cursor=0x%x words{%x %x %x %x} step=%u prev1{start=0x%x size=0x%x allocpc=0x%x cnry=%d}\n",
                                (unsigned int)cursor,
                                w[0], w[1], w[2], w[3],
                                (unsigned int)steps,
                                prev1 ? (unsigned int)(uintptr_t)prev1 : 0u,
                                prev1 ? (unsigned int)prev1->size : 0u,
                                prev1 && prev1->magic == HEAP_MAGIC_ALLOC ? (unsigned int)prev1->reserved : 0u,
                                prev1 && prev1->magic == HEAP_MAGIC_ALLOC ? (heap_canary_ok(prev1) ? 1 : 0) : -1);
            break;
        }
        if (blk->size < HEAP_MIN_BLOCK || (blk->size & (HEAP_ALIGN - 1u)) != 0u ||
            cursor + blk->size > g_heap_end) {
            kernel_debug_printf("heap-sweep: BADSIZE cursor=0x%x size=0x%x magic=0x%x step=%u\n",
                                (unsigned int)cursor, (unsigned int)blk->size,
                                (unsigned int)blk->magic, (unsigned int)steps);
            break;
        }
        if (blk->magic == HEAP_MAGIC_ALLOC && !heap_canary_ok(blk)) {
            kernel_debug_printf("heap-sweep: OVERFLOW block=0x%x size=0x%x allocpc=0x%x step=%u\n",
                                (unsigned int)cursor, (unsigned int)blk->size,
                                (unsigned int)blk->reserved, (unsigned int)steps);
            /*
             * Nature-of-writer dump (face-1, COMPLETION_ACTION_PLAN P4):
             * canary words + the NEIGHBOUR header + its first payload words.
             * The damaged pattern above a stack block's top is either
             * stack-shaped (kernel return addrs in [text_start,text_end)),
             * file/metadata-shaped, or pointer-chains — this dump decides.
             * Budgeted to one dump per sweep to stay readable.
             */
            {
                extern char __kernel_text_start[];
                extern char __kernel_text_end[];
                const uint32_t *cw = (const uint32_t *)((const uint8_t *)blk + blk->size - 16u);
                const uint32_t *nb = (const uint32_t *)(blk + 1);
                const uint8_t *nx = (const uint8_t *)blk + blk->size;
                uint32_t i;

                if (g_heap_sweep_detail_budget != 0u) {
                    g_heap_sweep_detail_budget--;
                    kernel_debug_printf("heap-sweep: OVER canary{%x %x %x %x} nexthdr{%x %x %x %x}\n",
                                        cw[0], cw[1], cw[2], cw[3],
                                        nb[0], nb[1], nb[2], nb[3]);
                    kernel_debug_printf("heap-sweep: OVER next+16{");
                    for (i = 0u; i < 8u; ++i) {
                        kernel_debug_printf("%x ", ((const uint32_t *)(nx + 16u))[i]);
                    }
                    kernel_debug_printf("} text=[%x,%x)\n",
                                        (unsigned int)(uintptr_t)__kernel_text_start,
                                        (unsigned int)(uintptr_t)__kernel_text_end);
                }
            }
        }

        prev2 = prev1;
        prev1 = blk;
        cursor += blk->size;
        steps++;
    }
    if (steps >= HEAP_SWEEP_MAX_BLOCKS) {
        kernel_debug_printf("heap-sweep: capped at %u blocks (victim 0x%x unreached)\n",
                            (unsigned int)HEAP_SWEEP_MAX_BLOCKS,
                            (unsigned int)(uintptr_t)victim);
    }
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
}

uintptr_t kernel_heap_block_alloc_pc(const void *ptr) {
    const struct free_block *block;
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t pc = 0u;
    uint32_t flags;

    if (g_heap_initialized == 0u || ptr == 0) {
        return 0u;
    }
    if (addr < g_heap_start + HEAP_HEADER_SIZE || addr >= g_heap_end) {
        return 0u;
    }
    if ((addr & (HEAP_ALIGN - 1u)) != 0u) {
        return 0u;
    }

    block = (const struct free_block *)(addr - HEAP_HEADER_SIZE);
    flags = spinlock_lock_irqsave(&g_heap_lock);
    if (block->magic == HEAP_MAGIC_ALLOC) {
        pc = block->reserved;
    }
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return pc;
}

int kernel_heap_block_canary_ok(const void *ptr) {
    const struct free_block *block;
    uintptr_t addr = (uintptr_t)ptr;
    uint32_t flags;
    int ok = 0;

    if (g_heap_initialized == 0u || ptr == 0) {
        return 0;
    }
    if (addr < g_heap_start + HEAP_HEADER_SIZE || addr >= g_heap_end) {
        return 0;
    }
    if ((addr & (HEAP_ALIGN - 1u)) != 0u) {
        return 0;
    }

    block = (const struct free_block *)(addr - HEAP_HEADER_SIZE);
    flags = spinlock_lock_irqsave(&g_heap_lock);
    if (block->magic == HEAP_MAGIC_ALLOC &&
        block->size >= HEAP_MIN_BLOCK &&
        (block->size & (HEAP_ALIGN - 1u)) == 0u &&
        (uintptr_t)block + block->size <= g_heap_end) {
        ok = heap_canary_ok(block);
    }
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return ok;
}

/*
 * Face-1 disambiguation witness (COMPLETION_ACTION_PLAN P4, classe #5,
 * adendo 15): kernel_heap_block_canary_ok() returns 0 for THREE different
 * situations — a trashed tail canary (upward overflow), a non-ALLOC/clobbered
 * block HEADER (a downward-growing stack that exhausted its payload smashes
 * its OWN header first), and a pointer that never was a block payload at all
 * (misaligned). The first STACK-CANARY-BAD capture (pid=0 record with
 * stack=0x390b6fa4 size=166074) is really the THIRD case: the pointer is
 * misaligned, i.e. garbage fields, not an overflow. This API separates the
 * cases so the next flag names the failure mode: rc<0 = why, rc=0 = block
 * fully valid. The header {size,magic,next,reserved} words are returned via
 * hdr[] when the header is inside the heap, whatever else is wrong.
 */
int kernel_heap_block_validate(const void *ptr, uintptr_t hdr[4]) {
    const struct free_block *block;
    uintptr_t addr = (uintptr_t)ptr;
    uint32_t flags;
    int rc = 0;

    if (g_heap_initialized == 0u || ptr == 0) {
        return -1;
    }
    if (addr < g_heap_start + HEAP_HEADER_SIZE ||
        addr >= g_heap_end ||
        (addr & (HEAP_ALIGN - 1u)) != 0u) {
        return -2;
    }
    block = (const struct free_block *)(addr - HEAP_HEADER_SIZE);
    flags = spinlock_lock_irqsave(&g_heap_lock);
    if (hdr != NULL) {
        hdr[0] = (uintptr_t)block->size;
        hdr[1] = block->magic;
        hdr[2] = (uintptr_t)block->next;
        hdr[3] = block->reserved;
    }
    if (block->magic != HEAP_MAGIC_ALLOC) {
        rc = -3; /* header trashed or block free: downward overflow or stale ptr */
    } else if (block->size < HEAP_MIN_BLOCK ||
               (block->size & (HEAP_ALIGN - 1u)) != 0u ||
               (uintptr_t)block + block->size > g_heap_end) {
        rc = -4; /* implausible block size: header trashed */
    } else if (!heap_canary_ok(block)) {
        rc = -5; /* payload overrun past the tail */
    }
    spinlock_unlock_irqrestore(&g_heap_lock, flags);
    return rc;
}
