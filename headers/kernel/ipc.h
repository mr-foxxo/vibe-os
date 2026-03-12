#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <stddef.h>
#include <kernel/process.h>

int ipc_send(process_t *dest, const void *data, size_t len);
int ipc_receive(process_t *self, void *buf, size_t bufsize);

#endif /* KERNEL_IPC_H */
