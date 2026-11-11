#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stdint.h>
#include <stddef.h>

void kernel_mm_init(uintptr_t heap_start, size_t heap_size);
void *kernel_malloc(size_t size);
void *kernel_malloc_aligned(size_t size, size_t align);
void *kernel_realloc(void *ptr, size_t size);
void kernel_free(void *ptr);
uintptr_t kernel_heap_start(void);
uintptr_t kernel_heap_end(void);
size_t kernel_heap_used(void);
size_t kernel_heap_free(void);
int kernel_heap_is_initialized(void);
/* Membership probe: 1 iff ptr is the payload of a LIVE allocated block
 * (inside the heap window, 8-aligned, header magic ALLOC with sane size).
 * Used by the scheduler walk to reject wild pointers before following them. */
int kernel_heap_is_block(const void *ptr);
/* Allocation-site PC recorded in a live block's header (0 if unknown/not a
 * live block). Lets callers register watch PCs without exposing the header. */
uintptr_t kernel_heap_block_alloc_pc(const void *ptr);
/* Live canary probe: 1 iff ptr is a live block whose trailing canary is
 * intact. A LINKED task whose canary is trashed is direct evidence of a
 * live forward-overflow adjacent to it (free-time canary checks miss this
 * because the block is never freed). */
int kernel_heap_block_canary_ok(const void *ptr);
/* Full block validation with named failure modes: rc 0=valid, -1=uninit/null,
 * -2=outside heap/misaligned, -3=magic!=ALLOC (trashed header), -4=bad size,
 * -5=canary broken. hdr[] receives the header words when readable. */
int kernel_heap_block_validate(const void *ptr, uintptr_t hdr[4]);
/* Register an allocation-site PC to watch: any kernel_free of a block whose
 * allocpc matches emits a WATCHED-FREE trace naming the freeing caller.
 * Used to detect a task-owned block being released while still linked. */
void kernel_heap_watch_pc(uintptr_t pc);
/* Physical-predecessor attribution probe (class #2): walk the physically
 * contiguous heap to find the block immediately BELOW a victim block, the
 * only forward-overflow candidate. Returns 1 when pred_* are filled (the
 * victim is a well-formed block with a reachable predecessor); 0 when the
 * victim is first/not-a-block/the walk desynced. Caller dumps the
 * predecessor's allocpc+size to name the writer's allocation site. */
int kernel_heap_block_predecessor(const void *ptr,
                                  uintptr_t *pred_start_out,
                                  size_t *pred_size_out,
                                  uintptr_t *pred_allocpc_out,
                                  int *pred_canary_ok_out);

/*
 * Full physical sweep of the block chain (walk by header sizes from the
 * heap start, never by any list). Used at the moment a scheduler list node
 * is found corrupt: the predecessor of a victim may share the victim's
 * fate (its own header already trashed), which makes
 * kernel_heap_block_predecessor() give up silently — exactly what the
 * real-time smp2 `prehdr{...}` dumps show. The sweep instead prints the
 * last well-formed blocks' allocpc + canary state before the desync point
 * and every trashed canary it meets, so ONE run names the overflowing
 * buffer's allocation site. Budgeted internally (a couple of sweeps).
 */
void kernel_heap_forensic_sweep(const void *victim);

/* public-facing API (aliases) */
static inline void memory_init(uintptr_t start, size_t size) {
    kernel_mm_init(start, size);
}

static inline void *kmalloc(size_t sz) {
    return kernel_malloc(sz);
}

static inline void *kmalloc_aligned(size_t sz, size_t align) {
    return kernel_malloc_aligned(sz, align);
}

static inline void *krealloc(void *ptr, size_t sz) {
    return kernel_realloc(ptr, sz);
}

static inline void kfree(void *ptr) {
    kernel_free(ptr);
}

#endif /* KERNEL_MEMORY_HEAP_H */
