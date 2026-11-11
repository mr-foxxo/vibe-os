#ifndef _MACHINE_BUS_H_
#define _MACHINE_BUS_H_

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <kernel/hal/io.h>

typedef uint32_t bus_dma_flag_t;
typedef char *caddr_t;

typedef uint32_t bus_size_t;
typedef uint32_t bus_addr_t;
typedef uint32_t bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;

#define BUS_SPACE_BARRIER_READ  0x01
#define BUS_SPACE_BARRIER_WRITE 0x02

static inline void bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t off, bus_size_t len, int flags) {
    (void)t; (void)h; (void)off; (void)len; (void)flags;
    __asm__ volatile("" ::: "memory");
}

static inline uint8_t bus_space_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o) {
    (void)t;
    if (h <= 0xFFFF) return inb((uint16_t)(h + o));
    return *(volatile uint8_t *)(h + o);
}
static inline uint16_t bus_space_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o) {
    (void)t;
    if (h <= 0xFFFF) return inw((uint16_t)(h + o));
    return *(volatile uint16_t *)(h + o);
}
static inline uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o) {
    (void)t;
    if (h <= 0xFFFF) return inl((uint16_t)(h + o));
    return *(volatile uint32_t *)(h + o);
}
static inline void bus_space_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint8_t v) {
    (void)t;
    if (h <= 0xFFFF) { outb((uint16_t)(h + o), v); return; }
    *(volatile uint8_t *)(h + o) = v;
}
static inline void bus_space_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint16_t v) {
    (void)t;
    if (h <= 0xFFFF) { outw((uint16_t)(h + o), v); return; }
    *(volatile uint16_t *)(h + o) = v;
}
static inline void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint32_t v) {
    (void)t;
    if (h <= 0xFFFF) { outl((uint16_t)(h + o), v); return; }
    *(volatile uint32_t *)(h + o) = v;
}

static inline int bus_space_map(bus_space_tag_t t, bus_addr_t addr,
    bus_size_t size, int flags, bus_space_handle_t *hp) {
    (void)t; (void)flags;
    *hp = (bus_space_handle_t)addr;
    (void)size;
    return 0;
}
static inline void bus_space_unmap(bus_space_tag_t t, bus_space_handle_t h, bus_size_t s) {
    (void)t; (void)h; (void)s;
}

struct bus_dma_tag { int dummy; };
struct bus_dma_segment {
    bus_addr_t ds_addr;
    bus_size_t ds_len;
};
struct bus_dmamap { struct bus_dma_tag *tag; bus_addr_t addr; struct bus_dma_segment dm_segs[1]; };

typedef struct bus_dma_tag *bus_dma_tag_t;
typedef struct bus_dmamap *bus_dmamap_t;
typedef struct bus_dma_segment bus_dma_segment_t;

#define BUS_DMA_NOWAIT    0x01
#define BUS_DMA_COHERENT  0x02
#define BUS_DMA_64BIT     0x04
#define BUS_DMA_READ      0x08
#define BUS_DMA_WRITE     0x10

int bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t align,
    bus_dma_flag_t flags, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    bus_dma_flag_t rflags);
void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs);
int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    bus_size_t size, caddr_t *kaddr, bus_dma_flag_t flags);
void bus_dmamem_unmap(bus_dma_tag_t t, caddr_t kaddr, bus_size_t size);
int bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, bus_dma_flag_t flags,
    bus_dmamap_t *mapp);
void bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map);
int bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, caddr_t buf,
    bus_size_t buflen, struct proc *p, bus_dma_flag_t flags);
void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map);
void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t len, int ops);

#define BUS_DMASYNC_PREREAD  0x01
#define BUS_DMASYNC_POSTREAD 0x02
#define BUS_DMASYNC_PREWRITE 0x04
#define BUS_DMASYNC_POSTWRITE 0x08

#endif
