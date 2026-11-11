#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <stdint.h>
#include <kernel/process.h>

#define SMP_IPI_WAKE_VECTOR 0x82u
#define SMP_IPI_SCHED_VECTOR 0x83u
#define SMP_IPI_TLB_SHOOTDOWN_VECTOR 0x84u

void smp_init(void);
uint32_t smp_started_cpu_count(void);
uint32_t smp_cpu_count(void);
void smp_ap_entry(void);
void smp_scheduler_enable(void);
int smp_scheduler_enabled(void);
uint32_t smp_first_online_ap_apic(void);
kernel_trap_frame_t *smp_wakeup_ipi_irq_handler(kernel_trap_frame_t *frame);
void smp_wake_sleeping_cpus(void);
uint32_t smp_cpu_stage(uint32_t cpu_index);
uint8_t smp_trampoline_debug_stage(void);
/* Face-1 witness: payload bounds of the boot stack this CPU index was
 * launched on (0/-1 if the AP never got one). Lets the scheduler tick
 * witness judge ESP of a CPU that has NO current task (idle hlt loop). */
int smp_ap_stack_bounds(uint32_t cpu_index, uintptr_t *lo_out, uintptr_t *hi_out);
/* Face-1 tick-arrival counters (per cache-line padded). smp_timer_irq_handler
 * bumps its cpu slot; dump reads them at death to tell "tick stopped" from
 * "schedule_frame ran but returned early". */
void smp_tick_count_bump(uint32_t cpu_index);
void smp_tick_counts_dump(void);
uint32_t smp_tick_count(uint32_t cpu_index);
void smp_persist_trace_arm(void);
void smp_persist_trace_mark(uint8_t code);
void smp_persist_trace_disarm(void);
void smp_print_topology(void);

typedef void (*smp_ipi_handler_t)(void);
int smp_register_ipi_handler(uint8_t vector, smp_ipi_handler_t handler);
int smp_unregister_ipi_handler(uint8_t vector);
void smp_ipi_dispatch(uint8_t vector);

#endif
