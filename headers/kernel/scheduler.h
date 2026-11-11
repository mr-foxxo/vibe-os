#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <kernel/process.h>
#include <stdint.h>
#include <include/userland_api.h>

/* scheduler: cooperative round-robin over a linked list of processes */

/* initialize scheduler data structures - call once during boot */
void scheduler_init(void);

/* add a newly created process to the run queue */
void scheduler_add_task(process_t *proc);
void scheduler_rsp_witness(uint32_t cpu_index);

/* Face-1 alias witness (class #5): returns the pid of a LIVE task whose
 * declared stack range [stack, stack+size) overlaps [lo, hi), or -1.
 * Called from kernel_malloc; read-only, capped walk, no locks. */
int scheduler_stack_overlap(uintptr_t lo, uintptr_t hi, uintptr_t *os_out,
                            uintptr_t *oe_out, int *state_out);
/* Record-range twin (P4 adendo 34): overlaps against the process_t blocks
 * of still-linked tasks; called from kernel_malloc/kernel_free. */
int scheduler_record_overlap(uintptr_t lo, uintptr_t hi, uintptr_t *os_out,
                             uintptr_t *oe_out, int *state_out);
/* Face-1 per-cpu schedule_frame entry counter + death dump (class #5,
 * adendo 17): pair with smp_tick_counts_dump to locate the stall stage. */
uint32_t scheduler_frame_entry_count(uint32_t cpu_index);
void scheduler_frame_counts_dump(void);
/* RECORD-WATCH (P4 classe #5 adendo 33): refresh the shadow for a task whose
 * context/stack changed legitimately outside the save wrapper (execve). */
void scheduler_rec_watch_refresh(process_t *task);
/* Hardware write-watchpoints on the victim record (P4 adendo 36):
 * maintain arms DR0(pid field)/DR1(stack_size+context window) on the calling
 * CPU; *_save_begin/_end bracket the ONE legitimate context writer outside
 * the wrapper (execve); db_event classifies/reports #DB hits. */
void scheduler_dr_maintain(uint32_t cpu_index);
void scheduler_db_enable(int on);
/* adendo 36: G0-exec-breakpoint support probe (fail-closed on emulators
 * without hw breakpoints); probe traps route here before ring push. */
int scheduler_db_supported(void);
int scheduler_db_probe_capture(uint32_t eip);
void scheduler_db_save_begin(const process_t *task);
void scheduler_db_save_end(const process_t *task);
void scheduler_db_event(uint32_t dr6, uint32_t eip, uint32_t cs,
                        uint32_t cpu_index);
/* Last C gate before an IRQ epilogue irets: validate the frame CONTENT the
 * CPU is about to resume (eip/cs sanity per ring) — catches the corrupted-
 * content-in-legit-stack entry the STALE-AT-ENTRY witness proved it is not. */
void scheduler_irq_frame_check(const kernel_trap_frame_t *frame,
                               uint32_t cpu_index);
/* Last-saved-frame ledger per cpu (adendo 28): dump at panic/#UD-halt to
 * compare the poisoned resume value against the last legit saves. */
void scheduler_save_ledger_dump(void);

/* enter the scheduler cooperatively from the current CPU context */
void schedule(void);

/* voluntarily relinquish the CPU (syscall/yield) */
void yield(void);

/* mark when timer-driven preemption may safely switch tasks */
void scheduler_set_preemption_ready(int ready);
void scheduler_set_task_events_ready(int ready);

/* schedule from an interrupt/yield trap and return the frame to resume */
kernel_trap_frame_t *scheduler_schedule_frame(kernel_trap_frame_t *frame, int preemptive);

/* return currently executing process (may be NULL) */
process_t *scheduler_current(void);
process_t *scheduler_current_for_cpu(uint32_t cpu_index);
uint32_t scheduler_current_pid(void);
uint32_t scheduler_current_pid_for_cpu(uint32_t cpu_index);
process_t *scheduler_find_task_by_pid(int pid);
void scheduler_terminate_task(process_t *task);
int scheduler_block_current(const void *wait_channel);
int scheduler_block_current_ex(const void *wait_channel,
                               uint32_t wait_deadline,
                               uint32_t wait_event_kind,
                               uint32_t wait_event_class,
                               uint32_t wait_owner_service);
int scheduler_wake_task(process_t *task);
int scheduler_task_is_linked(const process_t *proc);
int scheduler_complete_wait(process_t *task, uint32_t wait_result);
int scheduler_task_event_subscribe(process_t *subscriber,
                                   uint32_t event_mask,
                                   uint32_t task_class_mask);
int scheduler_task_event_receive(process_t *subscriber,
                                 struct mk_task_event *event,
                                 uint32_t timeout_ticks);
void scheduler_publish_lifecycle_event(uint32_t event_type, const process_t *task);
uint32_t scheduler_snapshot(struct task_snapshot_entry *entries,
                            uint32_t max_entries,
                            struct task_snapshot_summary *summary);

#endif /* KERNEL_SCHEDULER_H */
