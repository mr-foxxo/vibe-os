#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/process.h>

int ipc_send(process_t *dest, const void *data, size_t len);
int ipc_receive(process_t *self, void *buf, size_t bufsize);
int ipc_receive_wait(process_t *self, void *buf, size_t bufsize);
int ipc_receive_wait_timeout(process_t *self,
                             void *buf,
                             size_t bufsize,
                             uint32_t timeout_ticks);
/* Face-1 / P4 wake-loss instrumentation (read-only, lock-internal, never
 * blocks): depth of a process's receive queue + its waitable counters. The
 * timeout trace uses this to localize the stall by MEASUREMENT: a queue with
 * count>0 on the host side while the host is RUNNING = host loop not
 * draining; pending_signals>0 with no waiter = signal-before-wait;
 * count==0 on the client reply queue = reply never produced. */
void ipc_queue_stats(process_t *p, int *depth_out, uint32_t *pending_out,
                     uint32_t *wake_out, uint32_t *timeout_out,
                     int *waiters_out);

#endif /* KERNEL_IPC_H */
