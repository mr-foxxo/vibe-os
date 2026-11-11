#ifndef KERNEL_MICROKERNEL_TRANSFER_H
#define KERNEL_MICROKERNEL_TRANSFER_H

#include <stdint.h>

#define MK_TRANSFER_SLOTS 32u
#define MK_TRANSFER_PERM_READ 1u
#define MK_TRANSFER_PERM_WRITE 2u

void mk_transfer_init(void);
int mk_transfer_create(uint32_t owner_pid, uint32_t size, uint32_t *transfer_id_out);
int mk_transfer_share(uint32_t transfer_id, uint32_t pid, uint32_t permissions);
const void *mk_transfer_data_read(uint32_t transfer_id);
void *mk_transfer_data_write(uint32_t transfer_id);
/* Release the reference taken by a successful data_read/data_write. Every
 * successful call to those MUST be paired with exactly one release before
 * the caller returns, or the slot leaks (its buffer is never freed). */
void mk_transfer_release(uint32_t transfer_id);
uint32_t mk_transfer_size(uint32_t transfer_id);
int mk_transfer_copy_from(uint32_t transfer_id, const void *src, uint32_t size);
int mk_transfer_copy_to(uint32_t transfer_id, void *dst, uint32_t size);
int mk_transfer_destroy(uint32_t transfer_id);

#endif
