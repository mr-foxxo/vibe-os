#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>  /* memcpy/memset helpers */

#include <kernel/scheduler.h>
#include <kernel/process.h>
#include <lang/include/vibe_app.h>  /* VIBE_APP_STACK_SIZE: user-stack window for ring3 frame checks */
#include <kernel/kernel.h>    /* for panic, if needed */
#include <kernel/lock.h>
#include <kernel/cpu/cpu.h>
#include <kernel/smp.h>
#include <kernel/ipc.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/video/video.h>
#include <kernel/event.h>
#include <kernel/memory/heap.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/service.h>

/* simple singly‑linked list of processes */
#define SCHED_LIST_DIAG_BUDGET 6u
#define SCHED_LIST_WALK_CAP 512u
static int scheduler_node_sane(process_t *node);
static void scheduler_list_diag(const char *what, process_t *node, process_t *prev);
static process_t *g_head = NULL;
static process_t *g_current[32];
static process_t *g_cursor[32];
static uint32_t g_timeslice_remaining[32];
static volatile int g_scheduler_preemption_ready = 0;
static volatile uint32_t g_sched_frame_calls;
static volatile uint32_t g_sched_frame_preemptive_true;
/*
 * Face-1 per-cpu schedule_frame entry counter (P4 adendo 17), cache-line
 * padded. Paired with smp_tick_counts_dump it names the exact stage: if
 * ticks arrive but this stays frozen on cpu1, schedule_frame is never
 * reached (enabled gate or dispatch stall above it). Exposed for the death
 * dumps.
 */
typedef struct {
    volatile uint32_t n;
    uint32_t pad[15];
} sched_frame_ctr_t;
static sched_frame_ctr_t g_sched_frame_cpu_count[4];

uint32_t scheduler_frame_entry_count(uint32_t cpu_index) {
    return (cpu_index < 4u) ? (uint32_t)g_sched_frame_cpu_count[cpu_index].n : 0u;
}

/*
 * Face-1 idle-reentry guard (adendo 17): bit set while schedule_frame's no
 * tasks idle hlt loop owns the CPU, so a tick interrupt re-entering
 * schedule_frame is rejected at the top instead of nesting another hlt loop
 * on the same stack. Mutated only under g_scheduler_lock. reentry-suppressed
 * trace is budgeted so the guard itself is observable and never silent.
 */
static volatile uint32_t g_sched_idle_mask;
static volatile uint32_t g_sched_idle_reentry_budget = 8u;

void scheduler_frame_counts_dump(void) {
    kernel_debug_printf("sched-frame: cpu0=%u cpu1=%u cpu2=%u cpu3=%u\n",
                        (unsigned int)g_sched_frame_cpu_count[0].n,
                        (unsigned int)g_sched_frame_cpu_count[1].n,
                        (unsigned int)g_sched_frame_cpu_count[2].n,
                        (unsigned int)g_sched_frame_cpu_count[3].n);
}
static volatile int g_sched_frame_last_cpu = -2;
static volatile int g_sched_frame_last_cur_pid = -2;
/*
 * Per-pid selection counters (item 0.6, budget-free). The point-in-time census
 * alone could not distinguish "the starved shell is never selected" from "it is
 * selected but stuck", because the sample instant is a single phase of the
 * 4-tick timeslice cycle (2000 % 4 == 0 aliases ts0 to a constant). Counting how
 * many times the dispatcher actually handed the CPU to each pid is phase-free
 * and directly answers whether pid 5 monopolises selection while 10/11 stay at
 * zero. g_sched_ts_phase breaks the ts0 alias: census samples ts0 at two
 * different modular phases (0 and 1025) so a cycling timeslice is visible.
 */
static volatile uint32_t g_sched_sel_count[32];

/* bug #19 ring3 resume-eip witness (see sched-rip in the dispatch path). */
static volatile uint32_t g_sched_rip_budget = 10u;
/* Per-pid last-emit tick: a hot 4-tick poller (shell host, bug #19 run2 spent
 * the whole budget on p=11 before the app even launched) must not starve the
 * sampling of the task under investigation. */
static volatile uint32_t g_sched_rip_last[32];

/*
 * bug #19 (01/11, run 19_run12): wake-to-dispatch latency probe. run12
 * fingerprint `host-state=0 hostQ=1 hostW{pend=1 wake=0 waiters=0}` = the
 * input host was READY (popped+SIGNALED+budget-12 by the request's
 * kernel_waitable_signal) with its request still queued, yet never ran for
 * the full 128-tick reply budget. Whether that is "starved DESPITE the
 * -128 wake boost" or "dispatched but the reply never left the host" can
 * only be settled by measuring the gap, so: scheduler_complete_wait stamps
 * the tick of every SIGNALED wake that carries an IPC boost, the dispatch
 * path clears the stamp and records the max gap seen per pid; a gap over
 * 20 ticks prints wake-stale (budgeted) naming the task's boost state at
 * dispatch time. w6=/w11= in the census expose the running max for the two
 * tasks the handshake depends on (input host, shell).
 */
static volatile uint32_t g_ipc_wake_tick[32];
static volatile uint32_t g_wake_max_d[32];
static volatile uint32_t g_wake_stale_budget = 12u;

/*
 * item 0.13 (02/11): the probe above only stamps SIGNALED+IPC wakes, so
 * the OTHER class of wake — the timeout sweep publishing TIMED_OUT — was
 * invisible while it starved. Census evidence: p=10 (host supervising the
 * app) and p=11 (shell polling) sit READY with sel11 climbing ~1 per 367
 * ticks: a 4-tick supervision poll needs a wake whose dispatch gap is
 * small, but a TIMED_OUT wake carries no boost, no bonus, and for IPC
 * client waits even a +12 penalty, so the gap is decided by the aging
 * ramp (~396 ticks to beat the fresh tier-0 poller) instead of the 4-tick
 * budget. Same stamp/clear/max machinery, second array, for timed-out
 * wakes; wT10/wT11 in the census.
 */
static volatile uint32_t g_tmo_wake_tick[32];
static volatile uint32_t g_tmo_wake_max_d[32];
static volatile uint32_t g_tmo_stale_budget = 16u;

/*
 * Overdue-wake detector (COMPLETION_ACTION_PLAN P4 / KNOWN_BUGS "Sessao
 * interativa trava no meio", item 0.6 branch decision). Every bounded block
 * records (task,deadline) in this small ring; each ~25 ticks the timeout hook
 * checks entries still BLOCKED long after their deadline AND asks whether the
 * walk from g_head still REACHES the node. "overdue + unreachable" means the
 * timed wait can never be woken because the list walk stops before it
 * (truncation/phantom node — the LIST-CYCLE/BAD-NODE family); "overdue +
 * reachable" would instead point at the wake logic itself. Budgeted, so it
 * can never flood the serial.
 */
#define SCHED_BLOCK_RING 64u
#define SCHED_OVERDUE_GRACE_TICKS 200u
struct sched_block_record {
    process_t *task;
    uint32_t deadline;
    uint8_t flagged;
};
static struct sched_block_record g_sched_block_ring[SCHED_BLOCK_RING];
static uint32_t g_sched_block_ring_next;
static volatile uint32_t g_sched_overdue_budget = 12u;


/* Budget for the ghost-RUNNING repair trace (the repair itself is not
 * budgeted: an unrepaired ghost is a permanent deadlock). */
static volatile uint32_t g_sched_ghost_running_budget = 16u;

static void scheduler_note_block(process_t *task, uint32_t wait_deadline) {
    uint32_t idx;

    if (task == NULL || wait_deadline == 0u) {
        return;
    }
    idx = g_sched_block_ring_next % SCHED_BLOCK_RING;
    g_sched_block_ring[idx].task = task;
    g_sched_block_ring[idx].deadline = wait_deadline;
    g_sched_block_ring[idx].flagged = 0u;
    g_sched_block_ring_next += 1u;
}

static int scheduler_walk_reaches(process_t *needle) {
    process_t *task;
    uint32_t steps = 0u;

    for (task = g_head; task != NULL; task = task->next) {
        if (!scheduler_node_sane(task)) {
            break; /* same early exit the real sweep must take */
        }
        if (task == needle) {
            return 1;
        }
        if (++steps > SCHED_LIST_WALK_CAP) {
            break;
        }
    }
    return 0;
}

/*
 * item 0.12 (02/11): budgets cut from 40/80 to 12/24. Each census row is
 * ~600 chars and each score row ~60, emitted every 1000 ticks; on this
 * 2-core TCG host the serial write itself costs wall-clock and the guest
 * only reached t=40000 in 900 s of gate time (19_ns25), so the tail of a
 * long command never got to run inside the window. Less noise per second
 * buys the guest ticks, and 12 rows is still plenty to see a phase.
 */
static volatile uint32_t g_sched_census_budget = 12u;
static volatile uint32_t g_sched_score_budget = 24u;

static int scheduler_task_score(const process_t *task, uint32_t cpu_index);

static void sched_cat(char *dst, int cap, int *pos, const char *s) {
    while (*s != '\0' && *pos < cap - 1) {
        dst[(*pos)++] = *s++;
    }
    dst[*pos] = '\0';
}

static void sched_cat_u32(char *dst, int cap, int *pos, unsigned int v) {
    char tmp[12];
    int n = 0;

    if (v == 0u) {
        tmp[n++] = '0';
    } else {
        while (v > 0u && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    while (n > 0 && *pos < cap - 1) {
        dst[(*pos)++] = tmp[--n];
    }
    dst[*pos] = '\0';
}

static void sched_cat_i32(char *dst, int cap, int *pos, int v) {
    if (v < 0) {
        sched_cat(dst, cap, pos, "-");
        v = -v;
    }
    sched_cat_u32(dst, cap, pos, (unsigned int)v);
}

/*
 * 0.6 mode-iii (30/10, bug #19): one short line per task of the RUNNABLE
 * population at census time. The census row proves pid13 sits READY, but
 * what find_next compares is the SCORE, so print the score directly plus
 * the fields that decide which path produced it. Discriminates the two
 * live hypotheses without touching the app write relay:
 *   - sc clamped at 0 on the starved tier-6 app with wt=512 while the
 *     current task scores 0..3 => the score clamp only TIES the starved
 *     task with the busy tier-0 one (hypothesis a);
 *   - sc in the -40..-12 band => a bootstrap/wake-boost early return is
 *     active and the task MUST win the walk — if it still does not run,
 *     the walk itself is the suspect;
 *   - wt small although the task never ran => last_start_tick is being
 *     re-stamped by something the census does not see (hypothesis b's
 *     territory: the app write path re-entering dispatch).
 * lss=last_start_tick, wt=waited capped like the scoring term (512),
 * tr=priority_tier, sw=context_switches, wr=wait_result. Line kept <=80
 * chars (serial overrun rule); all fields are read under
 * g_scheduler_lock because the census runs inside the scan lock.
 */
static void sched_score_line(const process_t *task, uint32_t tick,
                             uint32_t cpu_index, int current) {
    char line[96];
    int pos = 0;
    int score;
    uint32_t waited = 0u;

    if (g_sched_score_budget == 0u || task == NULL) {
        return;
    }
    g_sched_score_budget -= 1u;
    score = scheduler_task_score(task, cpu_index);
    if (task->last_start_tick != 0u) {
        waited = tick - task->last_start_tick;
        if (waited > 512u) {
            waited = 512u;
        }
    }
    sched_cat(line, (int)sizeof(line), &pos, "sched-score t=");
    sched_cat_u32(line, (int)sizeof(line), &pos, tick);
    sched_cat(line, (int)sizeof(line), &pos, " p=");
    sched_cat_i32(line, (int)sizeof(line), &pos, task->pid);
    if (current) {
        sched_cat(line, (int)sizeof(line), &pos, " cur");
    }
    sched_cat(line, (int)sizeof(line), &pos, " sc=");
    sched_cat_i32(line, (int)sizeof(line), &pos, score);
    sched_cat(line, (int)sizeof(line), &pos, " lss=");
    sched_cat_u32(line, (int)sizeof(line), &pos, task->last_start_tick);
    sched_cat(line, (int)sizeof(line), &pos, " wt=");
    sched_cat_u32(line, (int)sizeof(line), &pos, waited);
    sched_cat(line, (int)sizeof(line), &pos, " tr=");
    sched_cat_u32(line, (int)sizeof(line), &pos, (unsigned int)task->priority_tier);
    sched_cat(line, (int)sizeof(line), &pos, " sw=");
    sched_cat_u32(line, (int)sizeof(line), &pos, task->context_switches);
    sched_cat(line, (int)sizeof(line), &pos, " wr=");
    sched_cat_i32(line, (int)sizeof(line), &pos, (int)task->wait_result);
    kernel_debug_printf("%s\n", line);
}

static void scheduler_census(uint32_t tick) {
    process_t *scan;
    process_t *runnable[4];
    uint32_t steps = 0u;
    uint32_t cpu;
    uint32_t scored = 0u;
    char line[512];
    int pos;

    cpu = kernel_cpu_index();
    if (cpu >= 32u) {
        cpu = 0u;
    }

    if (g_sched_census_budget == 0u) {
        return;
    }
    g_sched_census_budget -= 1u;
    pos = 0;
    line[0] = '\0';
    sched_cat(line, (int)sizeof(line), &pos, "sched-census t=");
    sched_cat_u32(line, (int)sizeof(line), &pos, tick);
    sched_cat(line, (int)sizeof(line), &pos, " calls=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_sched_frame_calls);
    sched_cat(line, (int)sizeof(line), &pos, " pre=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_sched_frame_preemptive_true);
    sched_cat(line, (int)sizeof(line), &pos, " lastcpu=");
    sched_cat_u32(line, (int)sizeof(line), &pos, (unsigned int)(g_sched_frame_last_cpu >= 0 ? (unsigned int)g_sched_frame_last_cpu : 9u));
    sched_cat(line, (int)sizeof(line), &pos, " lastcur=");
    sched_cat_u32(line, (int)sizeof(line), &pos, (unsigned int)(g_sched_frame_last_cur_pid >= 0 ? (unsigned int)g_sched_frame_last_cur_pid : 9u));
    sched_cat(line, (int)sizeof(line), &pos, " preempt=");
    sched_cat_u32(line, (int)sizeof(line), &pos, (unsigned int)g_scheduler_preemption_ready);
    sched_cat(line, (int)sizeof(line), &pos, " ts0=");
    sched_cat_u32(line, (int)sizeof(line), &pos, (unsigned int)g_timeslice_remaining[0]);
    sched_cat(line, (int)sizeof(line), &pos, " sel5=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_sched_sel_count[5u & 31u]);
    sched_cat(line, (int)sizeof(line), &pos, " sel10=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_sched_sel_count[10u & 31u]);
    sched_cat(line, (int)sizeof(line), &pos, " sel11=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_sched_sel_count[11u & 31u]);
    sched_cat(line, (int)sizeof(line), &pos, " sel12=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_sched_sel_count[12u & 31u]);
    /* bug #19: the netmgrd app lands at pid 13 in the rescue-shell boot;
     * the pre-fix rows stopped counting at sel12, right before the starved
     * task of interest. */
    sched_cat(line, (int)sizeof(line), &pos, " sel13=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_sched_sel_count[13u & 31u]);
    /* bug #19 wake-latency maxima for the handshake pair (input host,
     * shell): w6/w11 > 128 = a woken IPC participant waited past the
     * whole reply budget before its next dispatch (starved DESPITE the
     * -128 boost); small w6 with request timeouts = the stall is after
     * the host ran (reply path). */
    sched_cat(line, (int)sizeof(line), &pos, " w6=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_wake_max_d[6u & 31u]);
    sched_cat(line, (int)sizeof(line), &pos, " w11=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_wake_max_d[11u & 31u]);
    /* item 0.12 (02/11): same probe for the NETWORK host (p=8) and the app
     * that drives it (p=13). w8 > 128 with a queued request = the network
     * host is starved past the reply budget; small w8 with timeouts = the
     * stall is inside the host's own handler. */
    sched_cat(line, (int)sizeof(line), &pos, " w8=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_wake_max_d[8u & 31u]);
    sched_cat(line, (int)sizeof(line), &pos, " w13=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_wake_max_d[13u & 31u]);
    /* item 0.13: timeout-wake gap for the supervision pair (task-exit host
     * p=10, shell p=11). wT >> HOST_TASK_EXIT_POLL_TICKS(4) = the poll loop
     * starves on the wake->run gap, not on the wait itself. */
    sched_cat(line, (int)sizeof(line), &pos, " wT10=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_tmo_wake_max_d[10u & 31u]);
    sched_cat(line, (int)sizeof(line), &pos, " wT11=");
    sched_cat_u32(line, (int)sizeof(line), &pos, g_tmo_wake_max_d[11u & 31u]);
    sched_cat(line, (int)sizeof(line), &pos, ":");
    for (scan = g_head; scan != NULL; scan = scan->next) {
        if (!scheduler_node_sane(scan)) {
            sched_cat(line, (int)sizeof(line), &pos, " [BAD]");
            break;
        }
        sched_cat(line, (int)sizeof(line), &pos, " ");
        sched_cat_u32(line, (int)sizeof(line), &pos, (unsigned int)(scan->pid > 0 ? scan->pid : -scan->pid));
        sched_cat(line, (int)sizeof(line), &pos, ":");
        sched_cat_u32(line, (int)sizeof(line), &pos, (unsigned int)scan->state);
        sched_cat(line, (int)sizeof(line), &pos, ":");
        sched_cat_u32(line, (int)sizeof(line), &pos,
                      (unsigned int)(scan->current_cpu >= 0 ? (unsigned int)scan->current_cpu : 9u));
        sched_cat(line, (int)sizeof(line), &pos, ":");
        sched_cat_u32(line, (int)sizeof(line), &pos, scan->wait_deadline);
        /* wait_event_kind and wait_owner_service name WHAT a BLOCKED task is
         * waiting on (item 0.6: the child re-arms a timed wait that never
         * resolves; the census must show which channel/kind/owner). */
        sched_cat(line, (int)sizeof(line), &pos, ":");
        sched_cat_u32(line, (int)sizeof(line), &pos, scan->wait_event_kind);
        sched_cat(line, (int)sizeof(line), &pos, ":");
        sched_cat_u32(line, (int)sizeof(line), &pos, scan->wait_owner_service);
        /* bug #19: collect the runnable-but-unplaced tasks so their score
         * lines print right after this row (still under the scan lock). */
        if (scored < 4u && scan->state == PROCESS_READY && scan->current_cpu < 0) {
            runnable[scored] = scan;
            scored += 1u;
        }
        if (++steps >= 40u) {
            break;
        }
    }
    kernel_debug_printf("%s\n", line);
    {
        uint32_t i;
        process_t *cur = g_current[cpu];

        for (i = 0u; i < scored; ++i) {
            sched_score_line(runnable[i], tick, cpu, 0);
        }
        sched_score_line(cur, tick, cpu, 1);
    }
}

static void scheduler_overdue_scan(uint32_t tick) {
    uint32_t i;

    {
        /*
         * Sample the census at two modular phases (0 and 1025), both multiples
         * of the 25-tick scan cadence. Phase 0 aliases the 4-tick timeslice
         * (2000 % 4 == 0), so it always reads ts0 right after a fresh dispatch;
         * phase 1025 (1025 % 4 == 1) reads a different point of the cycle and
         * exposes whether ts0 actually decrements between dispatches.
         * item 0.13 (02/11): cadence 2000 -> 8000 (budget 12 -> window t<=48k).
         * The interesting events (app termination, supervision wake) happen
         * after the shell is already running — at 1000-tick spacing the 12
         * rows were spent by t=12000 and the timeout-wake probe never saw
         * them; each row costs ~600 chars of serial wall.
         */
        uint32_t census_phase = tick % 8000u;

        if (census_phase == 0u || census_phase == 1025u) {
            scheduler_census(tick);
        }
    }

    if (g_sched_overdue_budget == 0u) {
        return;
    }
    for (i = 0u; i < SCHED_BLOCK_RING; ++i) {
        struct sched_block_record *rec = &g_sched_block_ring[i];
        process_t *task = rec->task;

        if (task == NULL || rec->flagged != 0u || rec->deadline == 0u) {
            continue;
        }
        if (tick < rec->deadline ||
            (uint32_t)(tick - rec->deadline) < SCHED_OVERDUE_GRACE_TICKS) {
            continue;
        }
        if (task->state != PROCESS_BLOCKED || task->wait_channel == 0 ||
            task->wait_deadline == 0u) {
            continue;
        }
        rec->flagged = 1u;
        if (g_sched_overdue_budget != 0u) {
            g_sched_overdue_budget -= 1u;
            /* live vs ring: the printed `deadline` is the ring COPY. If
             * live= closely tracks tick+grace the stream is a stale-ring
             * artifact of fast re-arms; if live==the old deadline the wait
             * is genuinely stuck. (adendo 25 discriminator) */
            kernel_debug_printf("sched-watch: overdue pid=%d state=%d deadline=%u live=%u ch=%x tick=%u reachable=%d\n",
                                task->pid, (int)task->state,
                                (unsigned int)rec->deadline,
                                (unsigned int)task->wait_deadline,
                                (unsigned int)(uintptr_t)task->wait_channel,
                                (unsigned int)tick,
                                scheduler_walk_reaches(task));
        }
    }
}

static spinlock_t g_scheduler_lock;

/* Face-1 witnesses (P4 classe #5): budget for the AP esp-vs-stack report
 * (6 anomalies) + 2 "OK" baseline reports per cpu-role so a silent witness
 * is never confused with an armed one. */
static volatile uint32_t g_sched_apsp_budget = 6u;
static volatile uint32_t g_sched_apsp_baseline = 2u;

/* Face-1 intra-execution stack guard (adendo 7 candidate #1). */
static volatile uint32_t g_sched_tick_guard_budget = 8u;
static volatile uint32_t g_sched_tick_field_budget = 8u;
static volatile uint32_t g_sched_tick_tss_budget = 8u;
static volatile uint32_t g_sched_tick_bottom_budget = 8u;
static volatile uint32_t g_sched_tick_ctx_budget = 8u;
static volatile uint32_t g_sched_rsp_idle_budget = 4u;
static volatile uint32_t g_sched_rsp_idle_baseline = 2u;
static volatile uint32_t g_sched_rsp_idle_boot_edge_baseline = 2u;

static uintptr_t scheduler_current_rsp(void) {
    uintptr_t rsp;
    __asm__ volatile("mov %%esp, %0" : "=r"(rsp));
    return rsp;
}

/* Called from the AP's LAPIC tick handler (and BSP PIT) at a known point:
 * answers "where is this CPU's ESP vs the stack of the task it claims to be
 * running" — the OVER-detail dump (16/10 parte 5) showed real trap-frame
 * chains (kernel_tss_set_kernel_stack retaddrs) sitting ABOVE the AP
 * declared stack_top; this witness says whether ESP actually lives there. */
void scheduler_rsp_witness(uint32_t cpu_index) {
    process_t *t;
    uintptr_t rsp = scheduler_current_rsp();
    uintptr_t s, se;
    uint32_t flags;

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    t = (cpu_index < 32u) ? g_current[cpu_index] : NULL;
    if (t == 0 || t->stack == 0 || t->stack_size == 0u) {
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        /*
         * IDLE ESP witness (face-1, P4 adendo 16; corrected same day): the
         * first version compared rsp to the CPU's BOOTSTRAP stack and fired
         * `IDLE-OUTSIDE cpu=1 rsp=390bdf90` — the AP was idling on a HEAP
         * address (the stack window of another task). That is not by itself
         * a bug: hlt-loop ESP legitimately lives on whichever task stack the
         * CPU last ran. What is a bug is idling on a STACK THAT NO LONGER
         * BELONGS TO A LIVE TASK: after terminate/destroy frees a task stack,
         * a CPU still parked in hlt on that page keeps pushing IRQ frames
         * into recycled memory — the exact "resumed onto the other CPU's
         * stack / CS holding a stack address" family named in smp_ap_entry's
         * comment. So: while IDLE, rsp must lie inside a live task's stack
         * (alias-check helper), or inside the CPU's own bootstrap stack.
         * Anything else names the orphan ESP. Budget 4 per boot.
         */
        {
            uintptr_t lo = 0u, hi = 0u;
            uintptr_t os = 0u, oe = 0u;
            int ost = -1;
            int in_boot = (smp_ap_stack_bounds(cpu_index, &lo, &hi) == 0 &&
                           rsp >= lo && rsp < hi);

            if (in_boot) {
                /* Idling on this CPU's own bootstrap stack is the designed
                 * state (hlt loop after boot). One-shot baseline so silence
                 * is never mistaken for "not armed". */
                if (g_sched_rsp_idle_baseline != 0u) {
                    g_sched_rsp_idle_baseline--;
                    kernel_debug_printf("sched-rsp: IDLE-BOOT cpu=%u rsp=%x boot=[%x,%x)\n",
                                        (unsigned int)cpu_index,
                                        (unsigned int)rsp,
                                        (unsigned int)lo, (unsigned int)hi);
                }
            } else {
                int owner = scheduler_stack_overlap(rsp, rsp + 1u, &os, &oe, &ost);

                if (owner >= 0 && g_sched_rsp_idle_baseline != 0u) {
                    /* NOTE (adendo 33): the first capture —
                     * IDLE-TASK pid=1 rsp=0x390be600 — was MY artifact:
                     * the scheduler_stack_overlap half-open test
                     * [lo,hi) accepts the stack TOP itself, and the
                     * hlt-loop ESP at the bootstrap stack's top is the
                     * designed idle state, not a collision. Half-open
                     * windows where rsp==oe are the exact top edge;
                     * treat only strict interior as "on live". */
                    int top_edge = (rsp == oe);

                    if (top_edge && owner == 1) {
                        if (g_sched_rsp_idle_boot_edge_baseline != 0u) {
                            g_sched_rsp_idle_boot_edge_baseline--;
                            kernel_debug_printf("sched-rsp: IDLE-BOOT-EDGE cpu=%u rsp=%x init-top\n",
                                                (unsigned int)cpu_index,
                                                (unsigned int)rsp);
                        }
                    } else if (g_sched_rsp_idle_baseline != 0u) {
                        /* "on live" with rsp strictly INSIDE a stack while
                         * the CPU is idle = COLLISION: the hlt loop parked
                         * on a task stack that may resume underneath. */
                        g_sched_rsp_idle_baseline--;
                        kernel_debug_printf("sched-rsp: IDLE-TASK cpu=%u rsp=%x on live pid=%d state=%d stack=[%x,%x)\n",
                                            (unsigned int)cpu_index,
                                            (unsigned int)rsp, owner, ost,
                                            (unsigned int)os, (unsigned int)oe);
                    }
                } else if (owner < 0 && g_sched_rsp_idle_budget != 0u) {
                    g_sched_rsp_idle_budget--;
                    kernel_debug_printf("sched-rsp: IDLE-ORPHAN cpu=%u rsp=%x boot=[%x,%x) no live stack covers it\n",
                                        (unsigned int)cpu_index,
                                        (unsigned int)rsp,
                                        (unsigned int)lo, (unsigned int)hi);
                }
            }
        }
        return;
    }
    s = (uintptr_t)t->stack;
    se = s + t->stack_size;
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);

    if (rsp + 64u >= s && rsp <= se + 64u) {
        if (g_sched_apsp_baseline != 0u) {
            g_sched_apsp_baseline--;
            kernel_debug_printf("sched-rsp: OK cpu=%u pid=%d rsp=%x stack=[%x,%x)\n",
                                (unsigned int)cpu_index, t->pid,
                                (unsigned int)rsp, (unsigned int)s, (unsigned int)se);
        }
        return;
    }
    if (g_sched_apsp_budget != 0u) {
        g_sched_apsp_budget--;
        kernel_debug_printf("sched-rsp: OUTSIDE cpu=%u pid=%d state=%d rsp=%x stack=[%x,%x)\n",
                            (unsigned int)cpu_index, t->pid, (int)t->state,
                            (unsigned int)rsp, (unsigned int)s, (unsigned int)se);
    }
}
/*
 * Face-1 alias witness (COMPLETION_ACTION_PLAN P4, classe #5 face (f)): every
 * address of the smp2 crash storm (frame cs=heap, GP eip, stuck spinlock,
 * ud-bt sp) landed INSIDE one live 64KB task-stack block
 * [0x390d8080,0x390e8080) while every field guard passed — several owners
 * pointing at the same block at once. This check is called from
 * kernel_malloc: a freshly allocated heap block overlapping a live task's
 * declared stack range means the allocator handed back memory someone is
 * still using; it names the victim pid at the exact double-ownership moment.
 * Read-only walk without g_scheduler_lock (g_head is only mutated under it;
 * sampling is fine for a diagnostic, node walk is capped).
 */
int scheduler_stack_overlap(uintptr_t lo, uintptr_t hi, uintptr_t *os_out,
                            uintptr_t *oe_out, int *state_out) {
    const process_t *task;
    uint32_t guard = 0u;

    for (task = g_head; task != NULL && guard < 256u; task = task->next, ++guard) {
        uintptr_t s = (uintptr_t)task->stack;
        uintptr_t e;

        if (s == 0u || task->stack_size < 1024u ||
            task->stack_size > (4u * 1024u * 1024u)) {
            continue;
        }
        if (task->state == PROCESS_TERMINATED) {
            continue;
        }
        e = s + task->stack_size;
        if (s < hi && lo < e) {
            if (os_out != NULL) {
                *os_out = s;
            }
            if (oe_out != NULL) {
                *oe_out = e;
            }
            if (state_out != NULL) {
                *state_out = (int)task->state;
            }
            return task->pid;
        }
    }
    return -1;
}

/*
 * Record-overlap twin of scheduler_stack_overlap (P4 classe #5, adendo 34):
 * a process_t record is kernel_malloc'd and ONLY ever owned by the task
 * itself; a heap block handed out (or released) that overlaps the record
 * range of a still-LINKED task is double-ownership caught at the moment of
 * the (re)allocation — the stale-typed-pointer writer (mailbox/transfer/
 * subscription whose block became a record, or a record freed while linked)
 * can no longer act silently. Read-only capped walk, no locks (diagnostic).
 */
int scheduler_record_overlap(uintptr_t lo, uintptr_t hi, uintptr_t *os_out,
                             uintptr_t *oe_out, int *state_out) {
    const process_t *task;
    uint32_t guard = 0u;

    for (task = g_head; task != NULL && guard < 256u; task = task->next, ++guard) {
        uintptr_t s = (uintptr_t)task;
        uintptr_t e = s + sizeof(process_t);

        if (task->state == PROCESS_TERMINATED) {
            continue;
        }
        if (s < hi && lo < e) {
            if (os_out != NULL) {
                *os_out = s;
            }
            if (oe_out != NULL) {
                *oe_out = e;
            }
            if (state_out != NULL) {
                *state_out = (int)task->state;
            }
            return task->pid;
        }
    }
    return -1;
}

static spinlock_t g_task_event_lock;
static int g_scheduler_boot_trace_emitted = 0;
static int g_scheduler_head_trace_done = 0;
static int g_scheduler_switch_trace_budget = 24;
static int g_scheduler_block_trace_budget = 24;
static int g_scheduler_timeout_trace_budget = 16;
static volatile int g_scheduler_task_events_ready = 0;
static uint8_t g_audio_task_added_trace_emitted = 0u;
static uint8_t g_audio_first_dispatch_trace_emitted = 0u;

#define SCHEDULER_TASK_EVENT_SUBSCRIBERS 8u
#define SCHEDULER_TASK_EVENT_QUEUE_SIZE 32u
#define SCHEDULER_TASK_CLASS_EVENT_QUEUE_SIZE 32u
#define SCHEDULER_TASK_CLASS_STREAM_COUNT (MK_TASK_CLASS_VIDEO_CONTROL + 1u)

#define SCHEDULER_TIMESLICE_TICKS 4u

struct scheduler_task_event_subscription {
    uint32_t pid;
    uint32_t event_mask;
    uint32_t task_class_mask;
    kernel_mailbox_t mailbox;
    struct mk_task_event events[SCHEDULER_TASK_EVENT_QUEUE_SIZE];
};

struct scheduler_task_class_stream {
    kernel_mailbox_t mailbox;
    struct mk_task_event events[SCHEDULER_TASK_CLASS_EVENT_QUEUE_SIZE];
};

static struct scheduler_task_event_subscription
    g_task_event_subscriptions[SCHEDULER_TASK_EVENT_SUBSCRIBERS];
static struct scheduler_task_class_stream
    g_task_class_streams[SCHEDULER_TASK_CLASS_STREAM_COUNT];
static uint32_t g_task_event_sequence = 0u;

static void scheduler_timeout_tick_hook(uint32_t tick);
static void scheduler_task_event_init(void);
static void scheduler_publish_task_event(uint32_t event_type, const process_t *task);
static uint32_t scheduler_task_event_mask_for_type(uint32_t event_type);
static uint32_t scheduler_task_class_mask_for_task(const process_t *task);
static uint32_t scheduler_wait_class_for_task_class(uint32_t task_class);
static struct scheduler_task_class_stream *
scheduler_task_class_stream_for_class(uint32_t task_class);
static uint32_t scheduler_task_class_pending_events(uint32_t task_class);
static uint32_t scheduler_task_class_dropped_events(uint32_t task_class);
static struct scheduler_task_event_subscription *
scheduler_find_task_event_subscription_locked(process_t *subscriber);
static struct scheduler_task_event_subscription *
scheduler_alloc_task_event_subscription_locked(process_t *subscriber);

static int scheduler_keep_wait_boost(const process_t *task) {
    return task != NULL && task->wake_boost_budget != 0u;
}

static void scheduler_account_runtime(process_t *task, uint32_t now_ticks) {
    if (task == NULL || task->state != PROCESS_RUNNING || task->last_start_tick == 0u) {
        return;
    }

    if (now_ticks >= task->last_start_tick) {
        task->runtime_ticks += (now_ticks - task->last_start_tick);
    }
    /*
     * 0.6 (26/10): DO NOT clear last_start_tick here. It is the "tick this
     * task last started running" stamp that the starvation guard in
     * scheduler_task_score ages by — clearing it on every switch-out left
     * every READY task with last_start_tick==0, so the guard could never
     * fire for exactly the tasks it was written to protect. Measured: the
     * init host (tier DESKTOP_USER, timeout-0 supervision poll + yield)
     * re-won every selection round and the rescue shell host (tier APP,
     * +96 base points) sat READY with sel frozen at 4 for 40k ticks —
     * "shell: ready" never arrived. Re-dispatch always re-stamps the field
     * (scheduler_schedule_frame), so keeping it here cannot double-count
     * runtime: the state!=RUNNING guard blocks a second account until the
     * next dispatch resets the stamp.
     */
}

static void scheduler_trace_switch(uint32_t cpu_index,
                                   const process_t *old_task,
                                   const process_t *next_task) {
    if (g_scheduler_switch_trace_budget <= 0 || next_task == NULL) {
        return;
    }

    g_scheduler_switch_trace_budget -= 1;
    kernel_debug_printf("scheduler: cpu=%d old=%d/%d next=%d/%d state=%d\n",
                        (int)cpu_index,
                        old_task != NULL ? old_task->pid : -1,
                        old_task != NULL ? (int)old_task->service_type : -1,
                        next_task->pid,
                        (int)next_task->service_type,
                        (int)next_task->state);
}

/*
 * Last-saved-frame ledger (P4 classe #5, adendo 28): WHO/CTX are 0/14 — the
 * death paths never touch the four saves, so the displacement happens in the
 * WINDOW between the last legit save and the resume. This ledger records the
 * last 4 (pid, frame, tick) saves per cpu (short entries; serial witnesses
 * must stay <=80 chars). scheduler_resume_lastsave_cmp() is called at
 * WATERMARK-FRAME time and prints the ledger row beside the poisoned resume
 * value: "last legit save X@T; resume used Y!=X" names the disappearance
 * window by comparison instead of sampling the writers.
 */
typedef struct {
    volatile uint32_t pid;
    volatile uint32_t frame;
    volatile uint32_t tick;
    volatile uint32_t valid;
} sched_save_rec_t;
static sched_save_rec_t g_sched_save_ledger[4][4];
static volatile uint32_t g_sched_save_head[4];

static void scheduler_ledger_note(uint32_t cpu_index, process_t *task,
                                  const kernel_trap_frame_t *frame) {
    sched_save_rec_t *rec;
    uint32_t head;

    if (cpu_index >= 4u) {
        return;
    }
    head = g_sched_save_head[cpu_index] & 3u;
    g_sched_save_head[cpu_index] = head + 1u;
    rec = &g_sched_save_ledger[cpu_index][head & 3u];
    rec->pid = (uint32_t)task->pid;
    rec->frame = (uint32_t)(uintptr_t)frame;
    rec->tick = kernel_timer_get_ticks();
    rec->valid = 1u;
}

/* Dumped from panic/#UD-halt beside the esp0 ring (declared in scheduler.h). */
void scheduler_save_ledger_dump(void) {
    uint32_t cpu;

    for (cpu = 0u; cpu < 2u; ++cpu) {
        uint32_t i;

        for (i = 0u; i < 4u; ++i) {
            const sched_save_rec_t *rec = &g_sched_save_ledger[cpu][i];

            if (rec->valid == 0u) {
                continue;
            }
            kernel_debug_printf("save-ledger cpu=%u %u: pid=%u f=%x t=%u\n",
                                (unsigned int)cpu, (unsigned int)i,
                                (unsigned int)rec->pid, (unsigned int)rec->frame,
                                (unsigned int)rec->tick);
        }
    }
}

static kernel_trap_frame_t *scheduler_sanitize_resume_frame(uint32_t cpu_index,
                                                            const process_t *old_task,
                                                            const process_t *next_task,
                                                            kernel_trap_frame_t *frame) {
    uint32_t original_cs;
    uint32_t original_eflags;
    uint32_t expected_cs;
    int frame_is_user_mode;

    if (frame == NULL) {
        return NULL;
    }

    /*
     * DISPATCH-CTX-MISMATCH (P4 classe #5, adendo 33): the rw2 capture
     * showed the dispatch resuming pid=9 with frame=1 (context read under
     * lock at selection) while the RECORD dump moments later (this function,
     * after the lock dropped) showed the live context field = a VALID
     * 0x392a0c94 — something rewrites the field inside the unlocked
     * gap-to-sanitize window, and the rec-watch 100-tick cadence + save
     * re-baselining hide exactly that transient. This check names the
     * transient at the only instant it exists: frame here != next_task->
     * context now (and the frame came FROM that field under lock, except in
     * the legit fallback frame==trap-frame path where context was NULL —
     * skip when context==frame or frame is the caller's trap on cpu stack).
     */
    if (next_task != NULL && next_task->context != NULL &&
        next_task->context != frame) {
        uintptr_t f = (uintptr_t)frame;
        uintptr_t s = (uintptr_t)next_task->stack;

        if (next_task->stack != NULL && next_task->stack_size != 0u &&
            (f < s || f + sizeof(kernel_trap_frame_t) > s + next_task->stack_size)) {
            static volatile uint32_t g_sched_ctxmismatch_budget = 8u;
            if (g_sched_ctxmismatch_budget != 0u) {
                g_sched_ctxmismatch_budget--;
                kernel_debug_printf("sched-resume: CTX-MISMATCH pid=%d cpu=%u dispatched=%x live=%x sw=%u\n",
                                    next_task->pid, (unsigned int)cpu_index,
                                    (unsigned int)f,
                                    (unsigned int)(uintptr_t)next_task->context,
                                    (unsigned int)next_task->context_switches);
            }
        }
    }

    /*
     * Face-1 witness (COMPLETION_ACTION_PLAN P4, classe #5). The smp2 GP
     * `err=a5f0 eip=1fc3e4(.bss)` decodes as an iret whose selector words
     * still hold 0xA5 — the process-stack watermark fill (classe #1
     * instrumentation) — i.e. a task was about to resume from a region that
     * never received a real trap frame (or from a stale context pointer
     * outside its own stack). Detect at the LAST gate before the iret and
     * name the victim; do NOT fabricate a frame (no repair before we know
     * the frequency and the creating path).
     */
    if (next_task != NULL && next_task->stack != NULL && next_task->stack_size != 0u) {
        uintptr_t f = (uintptr_t)frame;
        uintptr_t s = (uintptr_t)next_task->stack;
        uintptr_t se = s + next_task->stack_size;
        const uint32_t *w = (const uint32_t *)f;
        int wm_hits = 0;
        int outside = (f < s) || (f + sizeof(kernel_user_trap_frame_t) > se);
        uint32_t i;

        if (!outside && f + 8u * sizeof(uint32_t) <= se) {
            for (i = 0u; i < 8u; ++i) {
                if (w[i] == 0xA5A5A5A5u) {
                    wm_hits++;
                }
            }
        }
        if (outside || wm_hits >= 3) {
            static volatile uint32_t g_sched_wmframe_budget = 8u;
            if (g_sched_wmframe_budget != 0u) {
                const uint32_t *rec = (const uint32_t *)(const void *)next_task;
                int ri;
                g_sched_wmframe_budget--;
                kernel_debug_printf("sched-resume: WATERMARK-FRAME pid=%d kind=%d cpu=%u frame=%x stack=[%x,%x) outside=%d wm=%d eip=%x cs=%x sw0=%x sw1=%x\n",
                                    next_task->pid, (int)next_task->kind,
                                    (unsigned int)cpu_index,
                                    (unsigned int)f, (unsigned int)s, (unsigned int)se,
                                    outside, wm_hits,
                                    (unsigned int)frame->eip, (unsigned int)frame->cs,
                                    (f + 8u * sizeof(uint32_t) <= se) ? w[0] : 0u,
                                    (f + 8u * sizeof(uint32_t) <= se) ? w[1] : 0u);
                /*
                 * Record map (adendo 18): the ctx-save witness proved the
                 * four legitimate `current->context = frame` saves are NOT
                 * the writer (0 hits across runs, while resumes show
                 * context=8 inside pid=5's record). So the record's memory
                 * is being overwritten from outside — dump the whole first
                 * half of the record plus the offsetof() of every field the
                 * walk trusts (next/context/stack/state/pid), so the poison
                 * SHAPE names the source object (stride of a wrong
                 * subscription/mailbox copy vs frame-chain vs struct-of-
                 * another-type), like the OVER dumps did for the spiral.
                 */
                kernel_debug_printf("sched-resume: OFFSETS next=%x ctx=%x stack=%x size=%x state=%x pid=%x cpu=%x\n",
                                    (unsigned int)offsetof(process_t, next),
                                    (unsigned int)offsetof(process_t, context),
                                    (unsigned int)offsetof(process_t, stack),
                                    (unsigned int)offsetof(process_t, stack_size),
                                    (unsigned int)offsetof(process_t, state),
                                    (unsigned int)offsetof(process_t, pid),
                                    (unsigned int)offsetof(process_t, current_cpu));
                kernel_debug_printf("sched-resume: RECORD{");
                for (ri = 0; ri < 24; ++ri) {
                    kernel_debug_printf("%x ", rec[ri]);
                }
                kernel_debug_printf("} task=%x\n",
                                    (unsigned int)(uintptr_t)next_task);
                /* Comparison against THIS cpu's last legit saves: the
                 * resume frame `f` should equal one of them (the last one
                 * for this pid) — if not, the delta names the window where
                 * the saved value was displaced. */
                {
                    uint32_t li;

                    for (li = 0u; li < 4u; ++li) {
                        const sched_save_rec_t *sv =
                            &g_sched_save_ledger[cpu_index & 3u][li];

                        if (sv->valid != 0u && sv->pid == (uint32_t)next_task->pid) {
                            kernel_debug_printf("sched-resume: LEDGER %u f=%x t=%u (resume f=%x)\n",
                                                (unsigned int)li,
                                                (unsigned int)sv->frame,
                                                (unsigned int)sv->tick,
                                                (unsigned int)f);
                        }
                    }
                }
            }
        }
    }

    original_cs = frame->cs;
    original_eflags = frame->eflags;
    frame_is_user_mode = process_frame_is_user_mode(frame);
    expected_cs = frame_is_user_mode ? USER_CS_SELECTOR : KERNEL_CS_SELECTOR;

    /*
     * CTX-DRIFT (P4 classe #5, adendo 29): the static hunt for a record
     * copier came up empty (every pid/context writer is legit and
     * bounds-checked; node_sane hardened with the canary test saw 0 hits
     * across 6 runs). The remaining split: either the resume frame was
     * ALREADY stale at selection time, or the record's context field is
     * re-trampled between selection (under g_scheduler_lock) and this
     * iret path (after the lock drops). Re-read the live context NOW and
     * compare with the frame actually being resumed. equal => stale-saved
     * (look upstream of the saves); different => live trampling inside
     * the unlocked gap, i.e. a second execution context owns this record
     * RIGHT NOW. Budgeted short lines.
     */
    if (next_task != NULL && next_task->context != NULL &&
        next_task->context != frame) {
        static volatile uint32_t g_sched_ctxdrift_budget = 8u;
        if (g_sched_ctxdrift_budget != 0u) {
            g_sched_ctxdrift_budget--;
            kernel_debug_printf("sched-resume: CTX-DRIFT pid=%d cpu=%u frame=%x live=%x sw=%u\n",
                                next_task->pid, (unsigned int)cpu_index,
                                (unsigned int)(uintptr_t)frame,
                                (unsigned int)(uintptr_t)next_task->context,
                                (unsigned int)next_task->context_switches);
        }
    }

    if (next_task != NULL && next_task->stack != NULL) {
        uintptr_t esp0 = (uintptr_t)next_task->stack + next_task->stack_size;

        kernel_tss_set_kernel_stack(esp0);

        /*
         * Face-1 witness (P4): esp0 is ALWAYS derived from a kernel_malloc'd
         * task stack, so it must land inside the heap. A set from outside it
         * means the task we just dispatched carries garbage in stack/
         * stack_size (the victim pattern: run A showed pid=1 with
         * stack=0x390aefe4 pointing inside its own block), and every
         * ring3->ring0 trap from here on pushes frames into that wrong
         * region — which is how "frame chains above another block" appear.
         * Budgeted; names the victim pid at the exact poisoning moment.
         */
        {
            uintptr_t hs = kernel_heap_start();
            uintptr_t he = kernel_heap_end();
            /*
             * Narrowed (adendo 21): the pg4 SUSPECT (pid=12 kind=0
             * runs_in_user_mode=0 ctx==frame esp0==boot idle top) was a
             * FALSE POSITIVE — a kernel task never takes ring3->ring0, so
             * its esp0 is legitimately whatever the idle/boot path set
             * (0x391caad8 = a live kernel stack top). The poisoning mode
             * this witness was built for is a USER task resuming onto a
             * foreign kernel stack; require runs_in_user_mode.
             */
            if (hs != 0u && he != 0u && next_task->runs_in_user_mode != 0u &&
                (esp0 <= hs || esp0 > he)) {
                static volatile uint32_t g_sched_esp0_budget = 8u;
                if (g_sched_esp0_budget != 0u) {
                    g_sched_esp0_budget--;
                    kernel_debug_printf("sched-esp0: SUSPECT pid=%d kind=%d cpu=%u stack=%x size=%u esp0=%x heap=[%x,%x) ctx=%x state=%d caller=%x\n",
                                        next_task->pid, (int)next_task->kind,
                                        (unsigned int)cpu_index,
                                        (unsigned int)(uintptr_t)next_task->stack,
                                        (unsigned int)next_task->stack_size,
                                        (unsigned int)esp0,
                                        (unsigned int)hs, (unsigned int)he,
                                        (unsigned int)(uintptr_t)next_task->context,
                                        (int)next_task->state,
                                        (unsigned int)(uintptr_t)__builtin_return_address(0));
                }
            }
        }
    } else {
        kernel_tss_set_kernel_stack(0u);
    }

    if (!frame_is_user_mode) {
        if (original_cs != KERNEL_CS_SELECTOR ||
            (original_eflags & (0x4000u | 0x3000u)) != 0u) {
            kernel_debug_printf("scheduler: sanitize cpu=%d old=%d next=%d frame=%x eip=%x cs=%x eflags=%x stack=%x size=%u\n",
                                (int)cpu_index,
                                old_task != NULL ? old_task->pid : -1,
                                next_task != NULL ? next_task->pid : -1,
                                (unsigned int)(uintptr_t)frame,
                                (unsigned int)frame->eip,
                                (unsigned int)original_cs,
                                (unsigned int)original_eflags,
                                next_task != NULL ? (unsigned int)(uintptr_t)next_task->stack : 0u,
                                next_task != NULL ? (unsigned int)next_task->stack_size : 0u);
            frame->cs = KERNEL_CS_SELECTOR;
            frame->eflags = (original_eflags | 0x00000202u) & ~(0x4000u | 0x3000u);
        }
    } else {
        kernel_user_trap_frame_t *user_frame = (kernel_user_trap_frame_t *)(void *)frame;
        if (original_cs != expected_cs) {
            kernel_debug_printf("scheduler: user-frame mismatch cpu=%d old=%d next=%d frame=%x eip=%x cs=%x expected=%x esp_dummy=%x user_esp=%x user_ss=%x\n",
                                (int)cpu_index,
                                old_task != NULL ? old_task->pid : -1,
                                next_task != NULL ? next_task->pid : -1,
                                (unsigned int)(uintptr_t)frame,
                                (unsigned int)frame->eip,
                                (unsigned int)original_cs,
                                (unsigned int)expected_cs,
                                (unsigned int)frame->esp_dummy,
                                (unsigned int)user_frame->user_esp,
                                (unsigned int)user_frame->user_ss);
        }
        frame->cs = USER_CS_SELECTOR;
        user_frame->user_ss = USER_DS_SELECTOR;
        if (user_frame->user_esp == 0u && next_task != NULL && next_task->user_stack_top >= (sizeof(uint32_t) * 2u)) {
            user_frame->user_esp = (uint32_t)(next_task->user_stack_top - (sizeof(uint32_t) * 2u));
        }
        frame->eflags = (original_eflags | 0x00000202u) & ~(0x4000u | 0x3000u);
    }
    return frame;
}

/*
 * Face-1 survivor witness (P4 classe #5, adendo 18): the clean post-spiral
 * capture caught WATERMARK-FRAME naming the RESUME side — pid=9's saved
 * context = 0x390e7a5c, a pointer INSIDE pid=5's stack block, while pid=9's
 * own stack was [391026e0,391426e0). The only dynamic writers of `context`
 * are the four `current->context = frame` saves in schedule_frame; a save
 * lands on a foreign stack only if the CPU's live ESP did not belong to the
 * task g_current claims — i.e. ownership desync, one tick BEFORE the resume.
 * This helper wraps every save with the range check and names the site on
 * the FIRST poisoning write (budgeted). frame==NULL saves (terminate paths)
 * pass through unchecked (they are cleared intentionally).
 */
static volatile uint32_t g_sched_ctxsave_budget = 12u;

/*
 * HARDWARE WRITE-WATCHPOINTS on the victim record (P4 classe #5, adendo 36).
 * The elimination ladder (SSP 0/4, STALE-AT-ENTRY, IRQEXIT-gate 0/5,
 * FOREIGN-CPU 0/6, heap-recalias/free-record 0/4) leaves a writer touching
 * the LEGITIMATE record with no heap fingerprint and no save/concurrency
 * trace. DR0/DR1 watch the target record's pid field and context field
 * (the two observed corruption slots); the CPU itself then reports the
 * WRITING eip at the store instruction — naming the author directly,
 * including a user-mode writer (cs 0x1b/0x23) if one exists.
 *
 * Legit context writers are bracketed by scheduler_db_save_begin/end (the
 * save wrapper and execve); the pid field has no legitimate writer after
 * arming (creation precedes linking, arming follows linking), so ANY
 * bit-0 event is a finding. Budget SCHED_DB_BUDGET reports, then the watch
 * disarms itself on every CPU (keeps the epilogue cheap and avoids floods).
 */
#define SCHED_DB_TARGET_PID 9
#define SCHED_DB_BUDGET     8u
static volatile uintptr_t g_db_dr0;
static volatile uintptr_t g_db_dr1;
static volatile uint32_t g_db_budget = SCHED_DB_BUDGET;
static volatile uint32_t g_db_off;
static volatile uint32_t g_db_disarm_mask;
#define DB_EVT_RING 8u
typedef struct {
    uint32_t dr6;
    uint32_t eip;
    uint32_t cs;
    uint32_t kind;
} db_evt_t;
typedef struct {
    db_evt_t slots[DB_EVT_RING];
    volatile uint32_t seq;       /* producer bumps: odd=mid-write, even=committed */
    volatile uint32_t reported;  /* consumer cursor */
} db_evt_cpu_t;
static db_evt_cpu_t g_db_evt_per_cpu[4];
static uint32_t g_db_in_save[4];

/* forward: shadow update helper lives with RECORD-WATCH below, used by
 * store_ctx and execve (adendo 38) */
static void store_ctx(process_t *task, kernel_trap_frame_t *frame);

static void db_drain_events(void);

/*
 * SINGLE-WATCHPOINT POLICY (measured QEMU-TCG isolation ladder, adendo 36
 * follow-ups): writing DR0/DR1 with DR7=0 survives; DR7 with BOTH L0|L1
 * enabled freezes the guest before any trap reaches the handler (GDB-stub
 * watchpoint interaction in TCG); L0-only enabled survives. So the tool
 * runs exactly ONE watchpoint: DR0 = rec+8, LEN 8B, write-only — covering
 * {stack_size, context}, the slots every capture shows being rewritten.
 * ORDERING (Intel SDM Vol3B): a MOV to DR with live enable bits raises #DB
 * on the MOV itself, hence DR7 is cleared first.
 */
static void db_set_regs(uintptr_t a0, uintptr_t a1) {
    uint32_t zero = 0u;
    /* L0 | RW0=write | LEN0=8 */
    uint32_t on = 0x00000001u | (1u << 16) | (2u << 18);
    uint32_t dr7v = 0u, dr6v = 0u, dr5v = 0u;
    static volatile uint32_t g_db_arm_proof;

    (void)a0;
    __asm__ volatile("movl %0, %%dr7\n\t"
                     "movl %1, %%dr0\n\t"
                     "movl %2, %%dr7"
                     :
                     : "r"(zero), "r"((uint32_t)a1), "r"(on)
                     : "memory");
    __asm__ volatile("mov %%dr7, %0\n\t"
                     "mov %%dr6, %1\n\t"
                     "mov %%dr5, %2"
                     : "=r"(dr7v), "=r"(dr6v), "=r"(dr5v)
                     :
                     : "memory");
    if (g_db_arm_proof == 0u) {
        g_db_arm_proof = 1u;
        kernel_debug_printf("db: armed w=%x dr7=%x dr6=%x dr5=%x\n",
                            (unsigned int)a1, (unsigned int)dr7v,
                            (unsigned int)dr6v, (unsigned int)dr5v);
    }
}

static void db_clear_dr7(void) {
    __asm__ volatile("movl %0, %%dr7\n\t" : : "r"((uint32_t)0u) : "memory");
}

extern volatile uint32_t g_db_trap_count;

/*
 * ARMING IS DISABLED BY DEFAULT (adendo 36): measured — on QEMU x86 even an
 * EMPTY #DB handler that only clears DR6/DR7 freezes the guest at the USB
 * boot scenario the instant maintain first writes the watch DRs (same
 * freeze line as armed runs; no trap ever reported: the tick hook itself
 * dies). The #DB infrastructure (gate/stub/handler/ring/drain) stays in the
 * tree inert; next steps before enabling: (a) arm once from a thread
 * context instead of the tick hook, (b) reproduce under KVM vs TCG to
 * isolate the emulator interaction, (c) try RF-protected single-step
 * resume. Do not "fix" by removing the instruments — the data they will
 * produce is the whole point.
 */

/* Hardware debug support probe (adendo 36): this QEMU/TCG build has NO
 * hw-breakpoint implementation (G0+L0 DR7 writes accepted but a #DB never
 * arrives; the L0|L1 memory-watch config additionally hung the guest).
 * Enable the watchpoint tool only if a GLOBAL-exec breakpoint at a scratch
 * label actually traps. Cheap, one-shot, fail-closed. */
static volatile uint32_t g_db_probe_fired;
static volatile uint32_t g_db_probe_active;

extern void db_probe_target(void);
asm(".text\n.type db_probe_target,@function\ndb_probe_target:\n\tnop\n\tret\n.size db_probe_target,.-db_probe_target\n");

int scheduler_db_supported(void) {
    uint32_t zero = 0u;
    uint32_t g0_exec = 0x00000001u | (0x00000003u << 16);
    int supported;
    uintptr_t addr = (uintptr_t)&db_probe_target;

    __asm__ volatile("movl %0, %%dr7\n\t"
                     "movl %1, %%dr0\n\t"
                     "movl %2, %%dr7"
                     :
                     : "r"(zero), "r"(addr), "r"(g0_exec)
                     : "memory");
    g_db_probe_fired = 0u;
    g_db_probe_active = 1u;
    __asm__ volatile("call *%0" : : "g"(addr) : "memory");
    g_db_probe_active = 0u;
    supported = (g_db_probe_fired != 0u);
    __asm__ volatile("movl %0, %%dr7\n\t"
                     "movl %0, %%dr0"
                     :
                     : "r"(zero)
                     : "memory");
    if (!supported) {
        kernel_debug_printf("db: unsupported (probe silent) - tool disabled\n");
    }
    return supported;
}

/* returns 1 when the trap belongs to the probe (consumed, no ring push) */
int scheduler_db_probe_capture(uint32_t eip) {
    if (g_db_probe_active != 0u && eip == (uint32_t)(uintptr_t)&db_probe_target + 1u) {
        g_db_probe_fired = 1u;
        return 1;
    }
    return 0;
}

static volatile uint32_t g_db_arm_enable;

void scheduler_db_enable(int on) {
    g_db_arm_enable = (on != 0) ? 1u : 0u;
}

void scheduler_dr_maintain(uint32_t cpu_index) {
    process_t *t;
    uint32_t steps = 0u;
    uintptr_t a0 = 0u, a1 = 0u;

    db_drain_events(); /* tick context: safe to print from here */

    if (g_db_arm_enable != 1u || g_db_off != 0u) {
        if ((g_db_disarm_mask & (1u << (cpu_index & 31u))) == 0u) {
            g_db_disarm_mask |= (1u << (cpu_index & 31u));
            db_clear_dr7();
        }
        return;
    }
    /*
     * NO heap-lock here: this runs from the timeout hook every tick while
     * holding g_scheduler_lock, and scheduler->heap per-tick would widen a
     * lock-order race with any heap->scheduler window into a hard deadlock
     * (measured: the first per-tick node_sane version froze UP usb-boot
     * with zero output). The walk is therefore plain: bounded steps,
     * pid equality only; a list corrupted enough to send this walk into a
     * wild pointer would fault the tick handler, which is a far noisier
     * (and acceptable) failure than silent global lock-up.
     */
    for (t = g_head; t != NULL && steps < 64u; t = t->next, ++steps) {
        if (t->pid == SCHED_DB_TARGET_PID) {
            a0 = (uintptr_t)t;                              /* pid field */
            a1 = (uintptr_t)t + 8u;                         /* size+context */
            break;
        }
    }
    if (a0 == 0u) {
        return;
    }
    /*
     * Idempotent per-tick re-arm: the #DB handler disarms its own CPU's
     * DR7 at entry (recursion self-protection — a nested #DB over a deep
     * handler stack double-faulted silently in measurement); rewriting the
     * three debug registers costs a few cycles and guarantees at most one
     * tick of blind window. g_db_dr0/g_db_dr1 track the LAST published
     * target so a record move still re-arms immediately.
     */
    g_db_dr0 = a0;
    g_db_dr1 = a1;
    db_set_regs(a0, a1);
}

void scheduler_db_save_begin(const process_t *task) {
    uint32_t cpu = kernel_cpu_index();

    if (task != NULL && task->pid == SCHED_DB_TARGET_PID && cpu < 4u) {
        g_db_in_save[cpu] = 1u;
        __asm__ volatile("" ::: "memory");
    }
}

void scheduler_db_save_end(const process_t *task) {
    uint32_t cpu = kernel_cpu_index();

    if (task != NULL && task->pid == SCHED_DB_TARGET_PID && cpu < 4u) {
        __asm__ volatile("" ::: "memory");
        g_db_in_save[cpu] = 0u;
    }
}

void scheduler_db_event(uint32_t dr6, uint32_t eip, uint32_t cs,
                       uint32_t cpu_index) {
    /* LOCK-FREE trap-context push (seqcounted per-CPU ring). A spinlock
     * here self-deadlocked when the #DB landed on the BSP drain already
     * holding the ring lock during its printf — measured: empty handler
     * survived the USB boot, lock-using handler froze it. Rule of the
     * trap context: no I/O, no locks. */
    uint32_t cidx = cpu_index & 3u;
    db_evt_cpu_t *ring = &g_db_evt_per_cpu[cidx];
    uint32_t seq;
    uint32_t slot;
    uint32_t kind;

    if (cpu_index >= 4u) {
        return;
    }
    if ((dr6 & 3u) == 0u) {
        return; /* BS/step/other: not ours */
    }
    if (g_db_in_save[cidx] != 0u) {
        return; /* bracketed store: wrapper ctx save / execve size+ctx */
    }
    /* single-watchpoint policy: any live match is the {size,context} window */
    kind = 2u;

    seq = ring->seq + 1u;
    ring->seq = seq;                       /* odd: writing */
    __asm__ volatile("" ::: "memory");
    slot = seq & (DB_EVT_RING - 1u);
    ring->slots[slot].dr6 = dr6;
    ring->slots[slot].eip = eip;
    ring->slots[slot].cs = cs;
    ring->slots[slot].kind = kind;
    __asm__ volatile("" ::: "memory");
    ring->seq = seq + 1u;                  /* even: committed */
}

static void db_drain_events(void) {
    uint32_t c;

    for (c = 0u; c < 4u; ++c) {
        db_evt_cpu_t *ring = &g_db_evt_per_cpu[c];

        for (;;) {
            uint32_t seq = ring->seq;
            uint32_t idx, eip, cs, kind;

            if ((seq & 1u) != 0u) {
                break;                    /* producer mid-write */
            }
            if (seq == ring->reported) {
                break;                    /* nothing new */
            }
            if (seq - ring->reported > DB_EVT_RING) {
                ring->reported = seq - 1u; /* overflow: skip to newest */
            }
            idx = (ring->reported + 1u) & (DB_EVT_RING - 1u);
            eip = ring->slots[idx].eip;
            cs = ring->slots[idx].cs;
            kind = ring->slots[idx].kind;
            if (ring->seq != seq) {
                continue;                 /* raced during snapshot: retry */
            }
            ring->reported = ring->reported + 1u;
            (void)kind;
            if (g_db_budget != 0u) {
                g_db_budget--;
                kernel_debug_printf("db: CTX-WRITE eip=%x cs=%x cpu=%u w=%x\n",
                                    (unsigned int)eip, (unsigned int)cs,
                                    (unsigned int)c, (unsigned int)g_db_dr0);
            } else {
                g_db_off = 1u;
                return;
            }
        }
    }
}

static volatile uint32_t g_sched_foreign_cpu_budget = 10u;

/*
 * RECORD-WATCH (P4 classe #5, adendo 33): the writer was narrowed (SSP 0/4,
 * STALE-AT-ENTRY clean at deaths, CTX-DRIFT 0/6) to DIRECT field writes on
 * live records through some alias — so shadow the fields that must NEVER
 * change after creation (pid, stack pointer) plus the one field that changes
 * ONLY through the centralized save wrapper (context), and diff them every
 * 100 ticks in the timeout hook (which already walks the list under
 * g_scheduler_lock). A mismatch names the field, old and new value, and the
 * tick — the corruption event itself, instead of a downstream crash face.
 * Side table (no process_t/ABI layout change); 64 slots vs ~30 tasks.
 */
typedef struct {
    process_t *task;
    uintptr_t pid;      /* stored as unsigned */
    uintptr_t stack;
    uintptr_t ctx;
} sched_rec_shadow_t;
static sched_rec_shadow_t g_rec_shadow[64];
static volatile uint32_t g_rec_watch_budget = 12u;

static sched_rec_shadow_t *rec_shadow_find(process_t *task) {
    uint32_t i;

    for (i = 0u; i < 64u; ++i) {
        if (g_rec_shadow[i].task == task) {
            return &g_rec_shadow[i];
        }
    }
    return NULL;
}

static sched_rec_shadow_t *rec_shadow_slot(process_t *task) {
    uint32_t i;
    sched_rec_shadow_t *free_slot = NULL;

    for (i = 0u; i < 64u; ++i) {
        if (g_rec_shadow[i].task == task) {
            return &g_rec_shadow[i];
        }
        if (free_slot == NULL && g_rec_shadow[i].task == NULL) {
            free_slot = &g_rec_shadow[i];
        }
    }
    return free_slot;
}

/* called with g_scheduler_lock held */
static void rec_shadow_take(process_t *task) {
    sched_rec_shadow_t *sh = rec_shadow_slot(task);

    if (sh == NULL) {
        return;
    }
    sh->task = task;
    sh->pid = (uintptr_t)(uint32_t)task->pid;
    sh->stack = (uintptr_t)task->stack;
    sh->ctx = (uintptr_t)task->context;
}

/* save sites refresh ctx through the existing wrapper; execve refreshes
 * explicitly (it legitimately moves stack+context). */
void scheduler_rec_watch_refresh(process_t *task) {
    uint32_t flags;
    sched_rec_shadow_t *sh;

    if (task == NULL) {
        return;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    sh = rec_shadow_find(task);
    if (sh != NULL) {
        sh->stack = (uintptr_t)task->stack;
        sh->ctx = (uintptr_t)task->context;
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
}

static void rec_watch_scan(void) {
    process_t *t;
    uint32_t steps = 0u;
    static volatile uint32_t g_rec_watch_armed = 0u;

    /* armability proof (campaign lesson: never read 0-hits as evidence
     * before proving the instrument runs): first scan prints tracked count. */
    if (g_rec_watch_armed == 0u) {
        uint32_t i, n = 0u;

        g_rec_watch_armed = 1u;
        for (i = 0u; i < 64u; ++i) {
            if (g_rec_shadow[i].task != NULL) {
                n++;
            }
        }
        kernel_debug_printf("rec-watch: armed tracked=%u\n", (unsigned int)n);
    }

    for (t = g_head; t != NULL; t = t->next) {
        const sched_rec_shadow_t *sh;

        if (!scheduler_node_sane(t) || ++steps > SCHED_LIST_WALK_CAP) {
            break;
        }
        sh = rec_shadow_find(t);
        if (sh == NULL) {
            continue; /* born before table full: skip silently */
        }
        if ((uintptr_t)(uint32_t)t->pid != sh->pid ||
            (uintptr_t)t->stack != sh->stack ||
            (uintptr_t)t->context != sh->ctx) {
            if (g_rec_watch_budget != 0u) {
                g_rec_watch_budget--;
                kernel_debug_printf("rec-watch: CORRUPT task=%x pid %u->%d stack %x->%x ctx %x->%x\n",
                                    (unsigned int)(uintptr_t)t,
                                    (unsigned int)sh->pid, t->pid,
                                    (unsigned int)sh->stack,
                                    (unsigned int)(uintptr_t)t->stack,
                                    (unsigned int)sh->ctx,
                                    (unsigned int)(uintptr_t)t->context);
            }
            rec_shadow_take(t); /* re-baseline; next delta is a NEW event */
        }
    }
}


/* the one legitimate ctx writer besides execve; bracketed so the hardware
 * watchpoint classifies the owner as benign and only unknown eips report.
 * The shadow ctx is updated HERE (the only dynamic ctx writer): every save
 * site holds g_scheduler_lock and rec_watch_scan runs under that same lock,
 * so store+shadow are one atomic step — a CORRUPT line can then only be an
 * UNKNOWN write (adendo 38; pre-integration rc1 fired 12x on legit saves). */
static void store_ctx(process_t *task, kernel_trap_frame_t *frame) {
    uint32_t cpu = kernel_cpu_index();
    int guarded = (task->pid == SCHED_DB_TARGET_PID && cpu < 4u);
    sched_rec_shadow_t *sh;

    if (guarded) {
        g_db_in_save[cpu] = 1u;
        __asm__ volatile("" ::: "memory");
    }
    task->context = frame;
    sh = rec_shadow_find(task);
    if (sh != NULL) {
        sh->ctx = (uintptr_t)frame;
    }
    if (guarded) {
        __asm__ volatile("" ::: "memory");
        g_db_in_save[cpu] = 0u;
    }
}

static void scheduler_save_current_context(process_t *task,
                                           kernel_trap_frame_t *frame,
                                           const char *site) {
    /*
     * FOREIGN-CPU invariant (P4 classe #5, adendo 34): every save site is
     * only reachable when the saving CPU owns the task (switch-out/preempt-
     * skip/block-save demand current->current_cpu == cpu_index; the reuse
     * branch assigns current_cpu right before saving). A save where
     * task->current_cpu disagrees with THIS cpu is, by definition, the
     * double-execution signature the CTX-MISMATCH gap hinted at (cm2: the
     * hang run's field was rewritten DURING the unlock->sanitize window).
     * One compare on an already-hot path.
     */
    if (task != NULL) {
        uint32_t cpu_now = kernel_cpu_index();

        if ((int)cpu_now != task->current_cpu && g_sched_foreign_cpu_budget != 0u) {
            g_sched_foreign_cpu_budget--;
            kernel_debug_printf("sched-save: FOREIGN-CPU site=%s pid=%d task-cpu=%d here=%u frame=%x ctx=%x\n",
                                site, task->pid, task->current_cpu,
                                (unsigned int)cpu_now,
                                (unsigned int)(uintptr_t)frame,
                                (unsigned int)(uintptr_t)task->context);
        }
    }

    if (task != NULL && frame != NULL && task->stack != NULL &&
        task->stack_size != 0u) {
        uintptr_t f = (uintptr_t)frame;
        uintptr_t s = (uintptr_t)task->stack;
        uintptr_t se = s + task->stack_size;

        if (f < s || f + sizeof(kernel_trap_frame_t) > se) {
            if (g_sched_ctxsave_budget != 0u) {
                uintptr_t os = 0u, oe = 0u;
                int ost = -1;
                int owner;
                process_t *self_now;
                uint32_t cpu_now = kernel_cpu_index();

                g_sched_ctxsave_budget--;
                owner = scheduler_stack_overlap(f, f + 1u, &os, &oe, &ost);
                self_now = (cpu_now < 32u) ? g_current[cpu_now] : NULL;
                kernel_debug_printf("sched-ctx-save: OUTSIDE-OWN-STACK site=%s pid=%d kind=%d cpu=%d frame=%x stack=[%x,%x) sw=%u\n",
                                    site, task->pid, (int)task->kind,
                                    task->current_cpu,
                                    (unsigned int)f, (unsigned int)s,
                                    (unsigned int)se,
                                    (unsigned int)task->context_switches);
                /*
                 * Double-ownership decider (P4 adendo 22, nc1 chain): the
                 * poison value 0x390be1b0 was init's LIVE trap ESP saved as
                 * pid=9's context. This second line names, at the save
                 * moment: whose stack actually contains the frame (owner),
                 * the owner's state, and what g_current holds for the
                 * SAVING cpu. If owner == the cpu's real runner and differs
                 * from `task`, the inline-wake/desync pair is proven; if
                 * owner == -1 the frame sits in a free/non-stack block.
                 */
                /* Two SHORT lines: who4 proved a single 170-char line was
                 * eaten by serial overrun under smp2 load (the WHO line
                 * vanished, the OUTSIDE line arrived with a corrupted tail
                 * "1055)" instead of the stack end). */
                kernel_debug_printf("sched-ctx-save: WHO owner=%d st=%d rng=[%x,%x)\n",
                                    owner, ost,
                                    (unsigned int)os, (unsigned int)oe);
                kernel_debug_printf("sched-ctx-save: WHO cpu=%u cur=%d eip=%x cs=%x\n",
                                    (unsigned int)cpu_now,
                                    self_now ? self_now->pid : -1,
                                    (unsigned int)frame->eip,
                                    (unsigned int)frame->cs);
            }
        }
    }
    if (frame != NULL) {
        scheduler_ledger_note(kernel_cpu_index(), task, frame);
    }
    store_ctx(task, frame);
}

/*
 * FRAME-BAD-AT-IRQEXIT (P4 classe #5, adendo 31): the STALE-AT-ENTRY witness
 * proved deaths do NOT enter via a stale esp0 ring3->ring0; the chain starts
 * with an already-wrong ESP from ret/iret reading CORRUPTED CONTENT inside a
 * legitimate stack (pointers are stable — CTX-DRIFT 0/6). This is the last C
 * gate before the IRQ epilogue's iretd: validate what the frame ABOUT to be
 * iret'd says. Kernel frame: eip must lie inside .text; user frame: cs==RPL3
 * selector and eip above the null region (a tiny eip is always garbage — the
 * eip=3/7/0x83 deaths). One-shot baseline per cpu proves the witness is
 * armed; violations are budgeted short lines.
 */
extern char __kernel_text_start[];
extern char __kernel_text_end[];
static volatile uint32_t g_sched_irqframe_budget = 8u;
static volatile uint8_t g_sched_irqframe_baseline[4];

void scheduler_irq_frame_check(const kernel_trap_frame_t *frame, uint32_t cpu_index) {
    uintptr_t ts = (uintptr_t)__kernel_text_start;
    uintptr_t te = (uintptr_t)__kernel_text_end;
    uintptr_t eip = (uintptr_t)frame->eip;
    int bad = 0;

    if (frame == NULL || cpu_index >= 32u) {
        return;
    }
    if ((frame->cs & 3u) == 0u) {
        if (frame->cs != KERNEL_CS_SELECTOR) {
            bad = 1; /* kernel-mode frame with a foreign selector */
        } else if (eip < ts || eip >= te) {
            bad = 2; /* kernel return outside .text */
        }
    } else {
        const kernel_user_trap_frame_t *uf =
            (const kernel_user_trap_frame_t *)(const void *)frame;
        if (frame->cs != USER_CS_SELECTOR) {
            bad = 3;
        } else if (eip < 0x1000u) {
            bad = 4; /* null-page/IVT-class garbage eip */
        } else if (uf->user_ss != (USER_DS_SELECTOR | 3u) && uf->user_ss != USER_DS_SELECTOR) {
            bad = 5;
        }
    }

    if (cpu_index < 4u && g_sched_irqframe_baseline[cpu_index] == 0u) {
        g_sched_irqframe_baseline[cpu_index] = 1u;
        kernel_debug_printf("irq-exit: check armed cpu=%u eip=%x cs=%x\n",
                            (unsigned int)cpu_index, (unsigned int)eip,
                            (unsigned int)frame->cs);
    }
    if (bad != 0 && g_sched_irqframe_budget != 0u) {
        g_sched_irqframe_budget--;
        kernel_debug_printf("irq-exit: FRAME-BAD cpu=%u rc=%d eip=%x cs=%x fl=%x\n",
                            (unsigned int)cpu_index, bad, (unsigned int)eip,
                            (unsigned int)frame->cs, (unsigned int)frame->eflags);
    }
}

static uint32_t scheduler_online_cpu_count(void) {
    if (!smp_scheduler_enabled()) {
        return 1u;
    }

    uint32_t count = smp_cpu_count();

    if (count == 0u) {
        count = 1u;
    }
    if (count > kernel_cpu_count()) {
        count = kernel_cpu_count();
    }
    if (count == 0u) {
        count = 1u;
    }
    return count;
}

static uint32_t scheduler_cpu_load(uint32_t cpu_index) {
    process_t *task;
    uint32_t load = 0u;
    uint32_t steps = 0u;

    for (task = g_head; task != NULL; task = task->next) {
        if (!scheduler_node_sane(task)) {
            scheduler_list_diag("BAD-NODE-IN-LOAD", task, NULL);
            break;
        }
        if (++steps > SCHED_LIST_WALK_CAP) {
            break;
        }
        if (task->state == PROCESS_TERMINATED) {
            continue;
        }
        if (task->current_cpu == (int)cpu_index || task->preferred_cpu == (int)cpu_index) {
            load += 1u;
        }
    }
    return load;
}

static int scheduler_pick_target_cpu(void) {
    uint32_t cpu_count = scheduler_online_cpu_count();
    uint32_t best_cpu = 0u;
    uint32_t best_load = 0u;
    uint32_t cpu;

    for (cpu = 0u; cpu < cpu_count; ++cpu) {
        uint32_t load = scheduler_cpu_load(cpu);

        if (cpu == 0u || load < best_load) {
            best_cpu = cpu;
            best_load = load;
        }
    }

    return (int)best_cpu;
}

static process_t *scheduler_first_runnable(void) {
    process_t *task;
    uint32_t steps = 0u;

    for (task = g_head; task != NULL; task = task->next) {
        if (!scheduler_node_sane(task)) {
            scheduler_list_diag("BAD-NODE-IN-FIRST", task, NULL);
            break;
        }
        if (++steps > SCHED_LIST_WALK_CAP) {
            break;
        }
        if (task->state == PROCESS_READY && task->current_cpu < 0) {
            return task;
        }
    }
    return NULL;
}

static int scheduler_task_score(const process_t *task, uint32_t cpu_index) {
    int wait_bonus = 0;
    int wait_penalty = 0;
    int affinity_score;
    int base_score;

    if (task == NULL) {
        return 0x7fffffff;
    }
    affinity_score = 3;
    if (task->preferred_cpu == (int)cpu_index) {
        affinity_score = 0;
    } else if (task->last_cpu == (int)cpu_index) {
        affinity_score = 1;
    } else if (task->preferred_cpu < 0) {
        affinity_score = 2;
    }

    /*
     * Protect the most interactive classes from paying a large migration cost
     * under wakeup/restart pressure. They should remain responsive even when
     * load-balancing would otherwise move them away from the previous CPU.
     */
    if (task->priority_tier <= PROCESS_PRIORITY_INPUT && affinity_score > 1) {
        affinity_score = 1;
    } else if (task->priority_tier == PROCESS_PRIORITY_VIDEO && affinity_score > 2) {
        affinity_score = 2;
    }

    /*
     * Fresh service workers need one bootstrap slice before desktop/user tasks
     * dominate the queue, otherwise first-use IPC (audio/network especially)
     * can target a worker that never had the chance to enter its receive loop.
     */
    if (task->kind == PROCESS_KIND_SERVICE &&
        task->context_switches == 0u &&
        task->runtime_ticks == 0u) {
        return -40 + (int)task->priority_tier;
    }
    if (task->kind == PROCESS_KIND_USER &&
        task->context_switches == 0u &&
        task->runtime_ticks == 0u) {
        /*
         * Fresh user tasks need an immediate bootstrap slice before desktop
         * frame/input traffic dominates the ready queue. Newly launched app
         * runtimes are particularly sensitive here because they still need to
         * bring up the loader and hand control to the modular app entrypoint.
         *
         * Audio/network helpers also need an early slice or the desktop can
         * launch them successfully yet keep rendering/input busy long enough
         * that bootstrap WAVs and async reconnect work appear "hung".
         */
        if (task->priority_tier == PROCESS_PRIORITY_DESKTOP_USER) {
            return -36;
        }
        if (task->priority_tier == PROCESS_PRIORITY_AUDIO ||
            task->priority_tier == PROCESS_PRIORITY_NETWORK) {
            return -30;
        }
        if (task->priority_tier == PROCESS_PRIORITY_APP) {
            return -34;
        }
        return -12;
    }
    if (task->kind == PROCESS_KIND_USER &&
        task->priority_tier == PROCESS_PRIORITY_APP &&
        task->context_switches < 4u &&
        task->runtime_ticks < 16u) {
        /*
         * Let freshly launched app runtimes finish their initial loader/fs work
         * over the first few slices so they do not starve behind continuous
         * desktop/input wakeups on graphical boots.
         */
        return -26;
    }

    /*
     * Recently signaled tasks that were blocked on interactive queues should
     * run ahead of bulk/background work once they become ready again.
     */
    if (task->wait_result == TASK_WAIT_RESULT_SIGNALED) {
        if (task->wait_event_class == TASK_WAIT_CLASS_IPC &&
            task->wake_boost_budget != 0u) {
            /*
             * Synchronous service request/reply depends on both sides of the
             * mailbox running immediately once signaled, otherwise a top-tier
             * caller can block indefinitely behind unrelated ready work.
             *
             * Re-bounded 31/10 (bug #19, run 19_run7): -32/-24 could not
             * deliver that promise for tier >= 2. A fresh tier-0 incumbent
             * (the init host's timeout-0 supervision poll) scores 0..3 and
             * re-wins every selection round; a just-woken tier-6 host
             * scored 96+aff-32 = 64+ and lost to it for the whole
             * aging ramp (~384-512 ticks), while the service layer's
             * reply budget for control messages is 32 ticks. Measured:
             * first KEY_READ of the boot timed out with the host READY,
             * popped from the wait chain, request still queued —
             * `host-state=0 hostQ=1 hostW{pend=1 wake=0 waiters=0}`
             * (wake_count only increments after the waiter's own yield
             * returns, so 0 = woken but never dispatched). Input was
             * marked degraded, the retry/resend backlog produced
             * `unexpected reply` floods and the typed shell line
             * fragmented. The bound is now the starvation cap: 16*7+3-128
             * = -13 < 0, so a woken IPC participant scores below any
             * non-woken task (min 0) at every tier. Effect stays bounded
             * by the one-shot consumption at dispatch (fix A, same
             * commit-set): one priority dispatch per wake, then the task
             * falls back to tier+aging. Mechanism/trigger unchanged; if
             * the maintainer prefers a different magnitude it is this one
             * constant (architecture decision per AGENTS.md).
             */
            return -128;
        }
        switch (task->wait_event_class) {
        case TASK_WAIT_CLASS_INPUT:
            wait_bonus = 12;
            break;
        case TASK_WAIT_CLASS_VIDEO:
            wait_bonus = 8;
            break;
        case TASK_WAIT_CLASS_SUPERVISION:
            wait_bonus = 6;
            break;
        case TASK_WAIT_CLASS_STORAGE:
        case TASK_WAIT_CLASS_FILESYSTEM:
            wait_bonus = 4;
            break;
        case TASK_WAIT_CLASS_AUDIO:
        case TASK_WAIT_CLASS_NETWORK:
            wait_bonus = 2;
            break;
        default:
            break;
        }
    } else if (task->wait_result == TASK_WAIT_RESULT_TIMED_OUT &&
               (task->wait_event_class == TASK_WAIT_CLASS_IPC ||
                task->wait_event_class == TASK_WAIT_CLASS_SUPERVISION ||
                task->wait_event_class == TASK_WAIT_CLASS_INPUT)) {
        /*
         * item 0.13 (02/11): a TIMED_OUT wake of a bounded IPC wait is the
         * OTHER half of the request/reply handshake — the client whose
         * poll window just expired must re-check the queue and usually
         * re-send. It used to take only the +12 retry penalty, so the
         * wake waited for the aging ramp against the fresh tier-0 poller
         * before running: measured `tmo-stale: pid=11 d=304 cls=2` (gate
         * run 19_ns28) — a 30-tick poll cycle costing 304+ ticks, the
         * whole interactive session crawling and never finishing inside
         * the gate window even after the #19 printf fix. Same one-shot
         * semantics as the SIGNALED boost above: budget set by the
         * timeout sweep, consumed at dispatch; once spent the task falls
         * back to the retry penalty, so a stuck client can never outrank
         * the service host that must answer it.
         *
         * The SUPERVISION class is added for the same measured reason
         * (19_ns29: all `tmo-stale: pid=11 d=380 cls=9`): the session
         * host's `host_wait_for_task_exit` polls the task-event mailbox on
         * a 4-tick budget and the wake waits ~380 ticks for its slice, so
         * the shell never prints `shell: command exit ...` within the
         * gate window. A bounded supervision poll that just timed out is
         * by definition waiting for exactly one small step — hand it the
         * one-shot slice. The timeout-0 (wait-forever) supervision waits
         * never reach this branch: they have no deadline, so the sweep
         * never stamps them.
         *
         * INPUT added from 19_ns31: `tmo-stale: pid=6 d=63 cls=3` — the
         * input host's own 30-tick mailbox poll starved 63+ ticks per
         * timeout once the boosted client retries made request rate rise;
         * every stale request then cost the client a 128-tick timeout and
         * a re-send (`hostQ` 1->3, `hostW{pend=126}`, input degraded
         * 128026/158968/175939). Same one-shot rule, same reasoning.
         */
        if (task->wake_boost_budget != 0u) {
            return -128;
        }
        if (task->wait_event_class == TASK_WAIT_CLASS_IPC) {
            wait_penalty = 12;
        }
    }

    base_score = (int)(task->priority_tier * 16u) + affinity_score + wait_penalty - wait_bonus;

    /*
     * Starvation guard (measured 15/10, KNOWN_BUGS "Sessao interativa trava no
     * meio" item 0.6): the score had NO age term, so a frequently-yielding
     * higher-tier task re-won every selection round and a woken lower-tier
     * task stayed READY forever. Reproduced deterministically: the rescue
     * shell host (tier APP) and the lang loader sat READY (census: state=0)
     * for 16k ticks while a tier-service pid kept the CPU — hard livelock at
     * interactive app launch. Age the score by time since the task last ran.
     *
     * Bound re-measured 26/10 (0.6 close): the original cap of 64 points was
     * justified as "> the 4x16 tier span", but the tier enum spans 0..7
     * (DESKTOP_USER..BACKGROUND) = 112 points plus up to 3 affinity points.
     * A tier-6 shell host (96) behind a tier-0 busy task could NEVER
     * overtake on 64 — the guard was structurally too weak even after the
     * dead-field bug (account_runtime used to zero last_start_tick on every
     * switch-out, see above) was fixed. Cap 128 (> 112+3) with divisor 4:
     * full boost after 512 ticks (~5.1s) of not running.
     */
    if (task->last_start_tick != 0u) {
        uint32_t waited = (uint32_t)(kernel_timer_get_ticks() - task->last_start_tick);

        if (waited > 512u) {
            waited = 512u;
        }
        base_score -= (int)(waited / 4u);
    }
    /*
     * 0.6 modo-iii (31/10, bug #19): NO floor clamp. The old `if
     * (base_score < 0) base_score = 0` capped every fully-aged waiter at
     * exactly 0 — the same score a FRESH tier-0 task (init host) carries
     * (0 + affinity 0..3 - aging 0..128). Measured run3 31/10: with p=5
     * permanently re-dispatched by its timeout-0 supervision poll, the
     * aged shell host sat clamped at sc=0 with lss frozen 19k ticks
     * because ties at 0 were resolved by walk order/cursor, not strictly
     * in the waiter's favour. The comment above this block already
     * promised "cap 128 (> 112+3): a starved task can overtake" — the
     * clamp broke that contract (127-128 = -1 can no longer exist with
     * it). The early-return bootstrap/wake-boost bands (-40..-12) already
     * emit negative scores legally and find_next was changed (26/10) to
     * full-ring-min without a break-at-zero specifically so negative
     * scores can win; clamping only the aged path was the one remaining
     * place that pinned the floor above the intended meaning. Removing
     * the clamp: a tier-6 task aged to the cap (96+1-128 = -31) now
     * STRICTLY beats a fresh tier-0 winner, exactly as designed.
     */
    return base_score;
}

static void scheduler_clear_wait_channel(process_t *task) {
    if (task == NULL) {
        return;
    }

    task->wait_channel = 0;
    task->wait_deadline = 0u;
    task->wait_next = 0;
    task->wake_boost_budget = 0u;
}

void scheduler_init(void) {
    uint32_t i;

    g_head = NULL;
    for (i = 0; i < 32u; ++i) {
        g_current[i] = NULL;
        g_cursor[i] = NULL;
        g_timeslice_remaining[i] = SCHEDULER_TIMESLICE_TICKS;
    }
    spinlock_init(&g_scheduler_lock);
    g_scheduler_preemption_ready = 0;
    g_scheduler_task_events_ready = 0;
    scheduler_task_event_init();
    (void)kernel_timer_register_tick_hook(scheduler_timeout_tick_hook);
}

static void scheduler_task_event_init(void) {
    uint32_t index;

    spinlock_init(&g_task_event_lock);
    g_task_event_sequence = 0u;
    for (index = 0u; index < SCHEDULER_TASK_EVENT_SUBSCRIBERS; ++index) {
        struct scheduler_task_event_subscription *subscription =
            &g_task_event_subscriptions[index];

        memset(subscription, 0, sizeof(*subscription));
        kernel_mailbox_init(&subscription->mailbox,
                            subscription->events,
                            sizeof(subscription->events[0]),
                            SCHEDULER_TASK_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_OLDEST,
                            TASK_WAIT_CLASS_SUPERVISION,
                            0u);
    }
    for (index = 0u; index < SCHEDULER_TASK_CLASS_STREAM_COUNT; ++index) {
        struct scheduler_task_class_stream *stream = &g_task_class_streams[index];

        memset(stream, 0, sizeof(*stream));
        kernel_mailbox_init(&stream->mailbox,
                            stream->events,
                            sizeof(stream->events[0]),
                            SCHEDULER_TASK_CLASS_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_OLDEST,
                            scheduler_wait_class_for_task_class(index),
                            0u);
    }
}

static uint32_t scheduler_task_event_mask_for_type(uint32_t event_type) {
    switch (event_type) {
    case MK_TASK_EVENT_LAUNCHED:
        return MK_TASK_EVENT_MASK_LAUNCHED;
    case MK_TASK_EVENT_TERMINATED:
        return MK_TASK_EVENT_MASK_TERMINATED;
    case MK_TASK_EVENT_BLOCKED:
        return MK_TASK_EVENT_MASK_BLOCKED;
    case MK_TASK_EVENT_WOKE:
        return MK_TASK_EVENT_MASK_WOKE;
    case MK_TASK_EVENT_RESTART_REQUESTED:
        return MK_TASK_EVENT_MASK_RESTART_REQUESTED;
    default:
        return 0u;
    }
}

static uint32_t scheduler_task_class_mask_for_task(const process_t *task) {
    if (task == NULL || task->task_class == MK_TASK_CLASS_NONE) {
        return 0u;
    }
    return MK_TASK_CLASS_MASK(task->task_class);
}

static uint32_t scheduler_wait_class_for_task_class(uint32_t task_class) {
    switch (task_class) {
    case MK_TASK_CLASS_INPUT:
        return TASK_WAIT_CLASS_INPUT;
    case MK_TASK_CLASS_VIDEO_PRESENT:
    case MK_TASK_CLASS_VIDEO_CONTROL:
        return TASK_WAIT_CLASS_VIDEO;
    case MK_TASK_CLASS_STORAGE_IO:
        return TASK_WAIT_CLASS_STORAGE;
    case MK_TASK_CLASS_FILESYSTEM_IO:
        return TASK_WAIT_CLASS_FILESYSTEM;
    case MK_TASK_CLASS_AUDIO_IO:
        return TASK_WAIT_CLASS_AUDIO;
    case MK_TASK_CLASS_NETWORK_IO:
        return TASK_WAIT_CLASS_NETWORK;
    case MK_TASK_CLASS_SUPERVISION:
    case MK_TASK_CLASS_DESKTOP:
    case MK_TASK_CLASS_SHELL:
    case MK_TASK_CLASS_APP_RUNTIME:
    case MK_TASK_CLASS_CONSOLE_IO:
    default:
        return TASK_WAIT_CLASS_SUPERVISION;
    }
}

static struct scheduler_task_class_stream *
scheduler_task_class_stream_for_class(uint32_t task_class) {
    if (task_class == MK_TASK_CLASS_NONE || task_class >= SCHEDULER_TASK_CLASS_STREAM_COUNT) {
        return NULL;
    }

    return &g_task_class_streams[task_class];
}

static uint32_t scheduler_task_class_pending_events(uint32_t task_class) {
    struct scheduler_task_class_stream *stream = scheduler_task_class_stream_for_class(task_class);

    if (stream == NULL) {
        return 0u;
    }
    return kernel_mailbox_count(&stream->mailbox);
}

static uint32_t scheduler_task_class_dropped_events(uint32_t task_class) {
    struct scheduler_task_class_stream *stream = scheduler_task_class_stream_for_class(task_class);

    if (stream == NULL) {
        return 0u;
    }
    return kernel_mailbox_dropped(&stream->mailbox);
}

static struct scheduler_task_event_subscription *
scheduler_find_task_event_subscription_locked(process_t *subscriber) {
    uint32_t index;

    if (subscriber == NULL || subscriber->pid <= 0) {
        return NULL;
    }

    for (index = 0u; index < SCHEDULER_TASK_EVENT_SUBSCRIBERS; ++index) {
        struct scheduler_task_event_subscription *subscription =
            &g_task_event_subscriptions[index];

        if (subscription->pid == (uint32_t)subscriber->pid) {
            return subscription;
        }
    }
    return NULL;
}

static struct scheduler_task_event_subscription *
scheduler_alloc_task_event_subscription_locked(process_t *subscriber) {
    uint32_t index;
    struct scheduler_task_event_subscription *empty = NULL;

    if (subscriber == NULL || subscriber->pid <= 0) {
        return NULL;
    }

    for (index = 0u; index < SCHEDULER_TASK_EVENT_SUBSCRIBERS; ++index) {
        struct scheduler_task_event_subscription *subscription =
            &g_task_event_subscriptions[index];

        if (subscription->pid == (uint32_t)subscriber->pid) {
            return subscription;
        }
        if (empty == NULL &&
            (subscription->pid == 0u ||
             scheduler_find_task_by_pid((int)subscription->pid) == NULL)) {
            empty = subscription;
        }
    }

    if (empty != NULL) {
        memset(empty, 0, sizeof(*empty));
        empty->pid = (uint32_t)subscriber->pid;
        empty->event_mask = MK_TASK_EVENT_MASK_LIFECYCLE;
        empty->task_class_mask = MK_TASK_CLASS_MASK_ALL;
        kernel_mailbox_init(&empty->mailbox,
                            empty->events,
                            sizeof(empty->events[0]),
                            SCHEDULER_TASK_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_OLDEST,
                            TASK_WAIT_CLASS_SUPERVISION,
                            0u);
    }
    return empty;
}

static void scheduler_publish_task_event(uint32_t event_type, const process_t *task) {
    uint32_t index;
    uint32_t flags;
    uint32_t event_mask;
    uint32_t task_class_mask;
    const struct mk_launch_context *context;
    process_t *mutable_task;
    struct scheduler_task_class_stream *class_stream;
    struct mk_task_event event_buf;
    kernel_mailbox_t *target_mailboxes[SCHEDULER_TASK_EVENT_SUBSCRIBERS];
    uint32_t target_mailbox_count = 0u;

    if (task == NULL || task->pid <= 0 || event_type == MK_TASK_EVENT_NONE) {
        return;
    }
    if (!g_scheduler_task_events_ready) {
        return;
    }

    event_mask = scheduler_task_event_mask_for_type(event_type);
    if (event_mask == 0u) {
        return;
    }
    task_class_mask = scheduler_task_class_mask_for_task(task);
    if (task_class_mask == 0u) {
        task_class_mask = MK_TASK_CLASS_MASK_ALL;
    }

    context = mk_launch_context_for_pid(task->pid);
    mutable_task = (process_t *)task;
    flags = spinlock_lock_irqsave(&g_task_event_lock);

    memset(&event_buf, 0, sizeof(event_buf));
    event_buf.abi_version = 1u;
    event_buf.event_type = event_type;
    event_buf.pid = (uint32_t)task->pid;
    event_buf.kind = (uint32_t)task->kind;
    event_buf.service_type = task->service_type;
    event_buf.task_class = task->task_class;
    event_buf.priority_tier = task->priority_tier;
    event_buf.flags = context != 0 ? context->flags : 0u;
    event_buf.sequence = ++g_task_event_sequence;
    event_buf.tick = kernel_timer_get_ticks();

    class_stream = scheduler_task_class_stream_for_class(task->task_class);

    mutable_task->last_task_event_sequence = event_buf.sequence;
    mutable_task->last_task_event_type = event_buf.event_type;
    mutable_task->last_task_event_tick = event_buf.tick;

    for (index = 0u; index < SCHEDULER_TASK_EVENT_SUBSCRIBERS; ++index) {
        struct scheduler_task_event_subscription *subscription =
            &g_task_event_subscriptions[index];

        if (subscription->pid == 0u) {
            continue;
        }
        if ((subscription->event_mask & event_mask) == 0u) {
            continue;
        }
        if ((subscription->task_class_mask & task_class_mask) == 0u) {
            continue;
        }
        if (target_mailbox_count < SCHEDULER_TASK_EVENT_SUBSCRIBERS) {
            target_mailboxes[target_mailbox_count++] = &subscription->mailbox;
        }
    }
    spinlock_unlock_irqrestore(&g_task_event_lock, flags);

    if (class_stream != NULL) {
        (void)kernel_mailbox_try_send(&class_stream->mailbox, &event_buf);
        event_buf.class_pending_depth = kernel_mailbox_count(&class_stream->mailbox);
        event_buf.class_dropped_events = kernel_mailbox_dropped(&class_stream->mailbox);
    }
    for (index = 0u; index < target_mailbox_count; ++index) {
        (void)kernel_mailbox_try_send(target_mailboxes[index], &event_buf);
    }
    /* item 0.13 probe (02/11, campaign): the TERMINATED of the netmgrd run
     * is published (serial shows `task-event terminated pid=13`) and the
     * session host keeps polling for thousands of ticks without it. Name
     * the delivery: how many subscriptions matched, and their queue state. */
    if (event_type == MK_TASK_EVENT_TERMINATED) {
        static int g_evt_pub_trace = 10;
        if (g_evt_pub_trace != 0) {
            g_evt_pub_trace--;
            kernel_debug_printf("evt-pub: pid=%d match=%u cnt0=%u\n",
                                event_buf.pid,
                                (unsigned int)target_mailbox_count,
                                target_mailbox_count != 0u ?
                                    kernel_mailbox_count(target_mailboxes[0]) : 0u);
        }
    }
}

void scheduler_publish_lifecycle_event(uint32_t event_type, const process_t *task) {
    scheduler_publish_task_event(event_type, task);
}

/*
 * Task-list integrity diagnostics (COMPLETION_ACTION_PLAN P4). The SMP
 * corruption shows phantom nodes (invalid kind, pids at ~sizeof(process_t)
 * stride). These guards convert a corrupt list from "scheduler lock held
 * forever by a cyclic walk" into a named, budgeted serial trace that pinpoints
 * the offending node and its predecessor.
 */
static volatile uint32_t g_sched_list_diag_budget = SCHED_LIST_DIAG_BUDGET;

static int scheduler_node_sane(process_t *node) {
    /*
     * Membership first: every task payload comes from kernel_malloc
     * (process_create / elf_load / fork / thread), so a node that is not
     * a live heap block is a WILD pointer (kernel TEXT, interior pointer,
     * recycled block) — the refined SMP signature. Field-shape checks
     * alone accepted these because a wild pointer can carry any bytes.
     */
    if (kernel_heap_block_validate(node, NULL) != 0) {
        /*
         * is_block alone trusts the LOCAL header words at node-16, which an
         * INTERIOR pointer can fake (lg3's woken node had pid=0x390e744c =
         * an ESP value inside pid5's stack, owner field = the stack base):
         * the trap-frame words at [node-16] happened to decode as a valid
         * header. kernel_heap_block_validate adds the tail-canary check —
         * an interior pointer's "block" end is arbitrary stack data, so it
         * fails. Legit records keep their canary intact (a trashed one is
         * exactly the corruption we want flagged).
         */
        return 0;
    }
    /*
     * pid bound (P4 adendo 21): hg3 printed `scheduler: timeout
     * pid=823182467 class=-35526648 event=1979646058` — a node PASSES the
     * is_block + `pid>0` checks when a live record's payload is trashed:
     * `pid>0` accepted a 8-digit garbage pid. pids come from g_next_pid++,
     * never exceed a few hundred in any boot; 65536 is a generous hard cap.
     * Tightening membership converts "garbage record flows silently past
     * every per-field guard" into a named BAD-NODE at the FIRST poisoned
     * walk — earlier than any resume/timeout dereference.
     */
    return node->pid > 0 && node->pid < 65536 &&
           (int)node->kind <= (int)PROCESS_KIND_KERNEL_TASK &&
           (int)node->state <= (int)PROCESS_TERMINATED;
}

static void scheduler_list_diag(const char *what, process_t *node, process_t *prev) {
    uint32_t cpu;
    const uint32_t *hdr;

    if (g_sched_list_diag_budget == 0u) {
        return;
    }
    g_sched_list_diag_budget--;
    cpu = kernel_cpu_index();
    /*
     * Stack watermark: process stacks are filled with 0xA5 before the
     * initial frame is placed (process_create_with_stack) and grow DOWN,
     * so a stack block allocated right after a process_t block that
     * exhausts its stack walks backward through the process_t canary,
     * payload and header — the exact SMP signature (canary trashed first,
     * then return-address-shaped payload values, no heap-API guard fired).
     * clean_bottom=0 means the deepest used byte reached the block floor.
     */
    if (prev != NULL && kernel_heap_is_block(prev)) {
        uintptr_t st = (uintptr_t)prev->stack;
        uint32_t size = prev->stack_size;

        if (st != 0u && size >= 256u && size <= (1u << 20) &&
            (size & 15u) == 0u &&
            st + size <= kernel_heap_end() &&
            kernel_heap_is_block((const void *)st)) {
            const uint8_t *base = (const uint8_t *)st;
            uint32_t clean = 0u;

            while (clean < size && base[clean] == 0xA5u) {
                clean++;
            }
            kernel_debug_printf("sched-list:   watermark prev{st=0x%x size=%u clean_bottom=%u svc=%d}\n",
                                (unsigned int)st, size, clean,
                                (int)prev->service_type);
        } else {
            kernel_debug_printf("sched-list:   watermark prev{st=0x%x size=%u UNAVAILABLE}\n",
                                (unsigned int)st, size);
        }
    }
    /*
     * Heap-header forensics: the 16 bytes before a payload carry
     * {size, magic, next, alloc_pc}. If magic != 'ALLO' the "node" is not
     * a live heap block at all (stale/wild pointer reached through a
     * corrupt link). alloc_pc names the kernel_malloc site that owns the
     * block, which resolves the culprit without fixed-address watchpoints.
     */
    hdr = (const uint32_t *)((const uint8_t *)node - 16u);
    /*
     * node may be a WILD pointer (that is the whole point of the trace):
     * only dereference it when it is a live heap block, else dump the raw
     * header window so the trace cannot crash the kernel it diagnoses.
     * When a prev owner exists, dump ITS allocpc too: the poison lives in
     * prev->next, so naming prev's allocation site identifies which task
     * carried the corrupted link.
     */
    if (kernel_heap_is_block(node)) {
        kernel_debug_printf("sched-list: %s cpu=%u node=0x%x hdr{size=0x%x magic=0x%x allocpc=0x%x} pid=%d kind=%d state=%d next=0x%x prev=0x%x prevpid=%d\n",
                            what, cpu,
                            (unsigned int)(uintptr_t)node,
                            hdr[0], hdr[1], hdr[3],
                            node->pid, (int)node->kind, (int)node->state,
                            (unsigned int)(uintptr_t)node->next,
                            (unsigned int)(uintptr_t)(prev != NULL ? prev : 0),
                            prev != NULL ? prev->pid : -9999);
    } else {
        const uint32_t *phdr = (prev != NULL &&
                                kernel_heap_is_block(prev))
                               ? (const uint32_t *)((const uint8_t *)prev - 16u)
                               : 0;
        const uint32_t *raw = (const uint32_t *)((const uint8_t *)node - 48u);
        int prev_canary = (prev != NULL && kernel_heap_is_block(prev))
                          ? kernel_heap_block_canary_ok(prev) : -1;
        kernel_debug_printf("sched-list: %s cpu=%u WILD node=0x%x hdr{size=0x%x magic=0x%x allocpc=0x%x} prev=0x%x prevpid=%d prevallocpc=0x%x prevcnry=%d\n",
                            what, cpu,
                            (unsigned int)(uintptr_t)node,
                            hdr[0], hdr[1], hdr[3],
                            (unsigned int)(uintptr_t)(prev != NULL ? prev : 0),
                            prev != NULL ? prev->pid : -9999,
                            phdr != NULL ? phdr[3] : 0u,
                            prev_canary);
        /*
         * Neighbor forensics: the victim header holds non-heap values, the
         * signature of a FORWARD overflow from the block allocated just
         * before it. prevcnry=0 confirms the poisoned-link OWNER's block was
         * overflowed through (live forward-overflow); prehdr dumps the 32
         * bytes preceding the victim header (the neighbor's payload tail).
         */
        kernel_debug_printf("sched-list:   prehdr{0x%x 0x%x 0x%x 0x%x}\n",
                            raw[0], raw[1], raw[2], raw[3]);

        /*
         * Class #2 attribution (COMPLETION_ACTION_PLAN). prevcnry=0 means
         * prev's OWN block was overflowed while still allocated. Dump the
         * PHYSICAL NEIGHBOR below the victim (allocpc + size): together
         * with the watermark line above it identifies the size class of
         * the buffers around the victim. PROVEN USE (2026-10-13): pred
         * {size=8224, allocpc=process_create_with_stack} + watermark
         * {size=8192} pinpointed the last two 8192-stack workers
         * (video-present/video-control); their stacks exhausted DOWNWARD
         * through the victim's own block (stack block sits ABOVE its
         * owner's process_t; growth is downward), so the writer was the
         * stack ABOVE the victim, not the neighbor below — the neighbor's
         * trashed canary was the tail of the same write path.
         */
        if (prev_canary == 0 && prev != NULL) {
            uintptr_t pred_start = 0u;
            size_t pred_size = 0u;
            uintptr_t pred_allocpc = 0u;
            int pred_cnry = -1;

            if (kernel_heap_block_predecessor(prev, &pred_start, &pred_size,
                                              &pred_allocpc, &pred_cnry)) {
                kernel_debug_printf("sched-list:   OVERFLOW-WRITER pred{start=0x%x size=%u allocpc=0x%x cnry=%d}\n",
                                    (unsigned int)pred_start,
                                    (unsigned int)pred_size,
                                    (unsigned int)pred_allocpc,
                                    pred_cnry);
            } else {
                kernel_debug_printf("sched-list:   OVERFLOW-WRITER pred{UNREACHABLE}\n");
            }
        }
    }

    /*
     * Physical chain forensics. MUST run at the top level of the diag:
     * BAD-HEAD and BAD-NODE-IN-TIMEOUT call this with prev==NULL, and the
     * first placement (inside the prev!=NULL block) made the sweep dead —
     * exactly the "0 hits" that was mis-read as evidence last round. The
     * sweep walks the heap by header sizes and names the overflowing
     * source block's allocpc regardless of list state. Takes g_heap_lock
     * only — scheduler->heap order is established.
     */
    kernel_heap_forensic_sweep(node);
}

/*
 * DISPATCH-BAD-FORENSICS (COMPLETION_ACTION_PLAN P4, class #3).
 * Real-time smp2 showed pid=1 resuming with EBP/ESP pointing INSIDE its
 * own process_t block: the first kernel_debug_printf pushed {ret,
 * saved_ebp} over kind/service_type (directed 8-byte write; block header
 * stayed intact). Open question the probe pins down: WHEN did the saved
 * frame go bad — (a) initial frame corrupted before first dispatch,
 * (b) context pointer redirected into the task's own payload, or
 * (c) frame saved during a block/yield with registers already bad.
 *
 * Invariant: the resume frame always lives on the task's own kernel
 * stack (process_setup_initial_context[_arg] places it at the stack
 * top; IRQ stubs push frames at current esp on the kernel stack), and
 * the frame's ebp/esp_dummy point inside that same window. Violations
 * dump the full frame + stack watermark: frame outside the window with
 * a sane stack pointer => (b); first dispatch (sw==1) => (a); frame in
 * window but registers wild => (c).
 */
#define SCHED_DISPATCH_FORENSICS_BUDGET 6u
static volatile uint32_t g_sched_dispatch_forensics_budget =
    SCHED_DISPATCH_FORENSICS_BUDGET;

#define SCHED_CANARY_SWEEP_PERIOD_TICKS 8u
#define SCHED_CANARY_SWEEP_BUDGET 2000u
static volatile uint32_t g_sched_canary_sweep_budget = SCHED_CANARY_SWEEP_BUDGET;

static volatile uint32_t g_sched_double_dispatch_budget = 16u;
static volatile uint32_t g_sched_tr_mismatch_budget = 16u;
static volatile uint32_t g_sched_stack_prov_budget = 32u;
static volatile uint32_t g_sched_sweep_detail_budget = 12u;

static void scheduler_dispatch_forensics(uint32_t cpu_index,
                                         const process_t *task,
                                         const kernel_trap_frame_t *frame) {
    uintptr_t stack;
    uintptr_t stack_end;
    uintptr_t faddr;
    uintptr_t ebp;
    uintptr_t esp_dummy;
    uintptr_t eip;
    int frame_in_window;
    int regs_in_window;
    int eip_sane;
    int cs_sane;
    uint32_t clean = 0u;

    if (g_sched_dispatch_forensics_budget == 0u ||
        task == NULL || frame == NULL) {
        return;
    }
    stack = (uintptr_t)task->stack;
    if (stack == 0u || task->stack_size < 64u || task->stack_size > (1u << 20)) {
        /*
         * Blind spot removed: EVERY legitimate task gets its stack from
         * process_create_with_stack (failure returns NULL, never a
         * stackless task). A dispatched task without a sane stack window
         * IS the anomaly — print it instead of skipping silently.
         */
        if (g_sched_dispatch_forensics_budget != 0u) {
            g_sched_dispatch_forensics_budget--;
            kernel_debug_printf("sched-dispatch: BAD-FORENSICS-NOSTACK cpu=%u pid=%d sw=%u ctx=0x%x stack=0x%x size=%u\n",
                                cpu_index, task->pid, task->context_switches,
                                (unsigned int)(uintptr_t)frame,
                                (unsigned int)stack,
                                (unsigned int)task->stack_size);
        }
        return;
    }
    stack_end = stack + (uintptr_t)task->stack_size;
    faddr = (uintptr_t)frame;

    frame_in_window = (faddr >= stack &&
                       faddr + sizeof(kernel_trap_frame_t) <= stack_end);
    ebp = (uintptr_t)frame->ebp;
    esp_dummy = (uintptr_t)frame->esp_dummy;
    if (frame->cs == USER_CS_SELECTOR) {
        /*
         * ring3 resume frames carry USER registers (this is the frame the
         * kernel iret's back to user mode with), so ebp legitimately points
         * into the task's user stack — [user_stack_top - VIBE_APP_STACK_SIZE,
         * user_stack_top) — NOT into the kernel stack window. Validating it
         * against the kernel stack made this probe fire BAD-FORENSICS on
         * EVERY dispatch of a user task with a non-zero user ebp, flooding
         * the serial log and drowning validator markers (measured: an app
         * host dispatch repeated it every tick). A user frame is accepted
         * when ebp is 0, in the user window, or in the kernel window
         * (constructed frames hold either), and esp_dummy is in either
         * window; eip/cs sanity below keeps the real teeth.
         */
        uintptr_t ustack_top = (uintptr_t)task->user_stack_top;
        uintptr_t ustack_low = (ustack_top > VIBE_APP_STACK_SIZE)
                                   ? (ustack_top - VIBE_APP_STACK_SIZE) : 0u;
        int ebp_ok = (ebp == 0u) ||
                     (ustack_top != 0u && ebp >= ustack_low && ebp < ustack_top) ||
                     (ebp >= stack && ebp < stack_end);
        int esp_ok = (ustack_top != 0u && esp_dummy >= ustack_low && esp_dummy < ustack_top) ||
                     (esp_dummy >= stack && esp_dummy < stack_end);
        regs_in_window = ebp_ok && esp_ok;
    } else {
        /*
         * 0.8 (closure-plan wave 0): a ring3 task (runs_in_user_mode=1) is
         * ALSO switched out MID int-0x80 syscall. That saved frame is a
         * KERNEL frame (cs=0x8, eip in the syscall path) but its ebp/esp
         * still hold the APP's user-stack values — pusha in syscall_stub
         * preserves the app registers, and the frame resumes the kernel
         * path and irets back to the app. Measured healthy (desktop
         * pid=12): ctx on its own kernel stack (in_win=1), eip=0x59bc3
         * (syscall_stub), cs=0x8, ebp=0x37ffdf24 (user window),
         * esp=0x39267698 (kernel window). The old strict kernel-window
         * check fired BAD-FORENSICS on every such dispatch (budget 6).
         * Accept the user window for user tasks here too; kernel tasks
         * (runs_in_user_mode=0) stay strict, and the eip/cs sanity below
         * plus the window checks keep the real teeth (a corrupted frame
         * with a wild eip/cs or out-of-window esp is still caught).
         */
        uintptr_t ustack_top = (uintptr_t)task->user_stack_top;
        uintptr_t ustack_low = (ustack_top > VIBE_APP_STACK_SIZE)
                                   ? (ustack_top - VIBE_APP_STACK_SIZE) : 0u;
        int user_win = (task->runs_in_user_mode != 0u && ustack_top != 0u);
        int ebp_ok = (ebp == 0u) || (ebp >= stack && ebp < stack_end) ||
                     (user_win && ebp >= ustack_low && ebp < ustack_top);
        int esp_ok = (esp_dummy >= stack && esp_dummy < stack_end) ||
                     (user_win && esp_dummy >= ustack_low && esp_dummy < ustack_top);
        regs_in_window = ebp_ok && esp_ok;
    }

    /*
     * eip/cs sanity (second blind spot removed, class #4): the c4a run
     * showed a frame whose ebp/esp still looked in-window but whose TAIL
     * words held {eip=5, cs=2, eflags=1} — the sanitize guard caught it
     * only because it repairs cs/eflags, and my window check missed the
     * eip word entirely. A legitimate resume eip is either kernel text
     * [< ~8MB, below the app arenas] or inside a user arena window; a
     * legitimate resume cs is KERNEL_CS or USER_CS. Anything else is a
     * trashed frame regardless of the window.
     */
    /*
     * Legitimate resume eip: kernel text [0x10000, 0x800000) or any user
     * window >= 0x02000000 (arenas + mmap region). The gap [0x800000,
     * 0x2000000) is heap/boot-reserved — never executable. Below 0x10000
     * is the real-mode page — never a protected-mode resume target.
     */
    eip = (uintptr_t)frame->eip;
    eip_sane = (eip >= 0x00010000u &&
                (eip < 0x00800000u || eip >= 0x02000000u));
    cs_sane = (frame->cs == KERNEL_CS_SELECTOR || frame->cs == USER_CS_SELECTOR);

    if (frame_in_window && regs_in_window && eip_sane && cs_sane) {
        return;
    }

    g_sched_dispatch_forensics_budget--;
    {
        const uint8_t *base = (const uint8_t *)stack;
        while (clean < task->stack_size && base[clean] == 0xA5u) {
            clean++;
        }
    }
    kernel_debug_printf("sched-dispatch: BAD-FORENSICS cpu=%u pid=%d sw=%u ctx=0x%x stack=0x%x size=%u frame{ebp=0x%x esp=0x%x eip=0x%x cs=0x%x efl=0x%x} in_win=%d regs_in_win=%d clean_bottom=%u\n",
                        cpu_index, task->pid, task->context_switches,
                        (unsigned int)faddr, (unsigned int)stack,
                        (unsigned int)task->stack_size,
                        (unsigned int)ebp, (unsigned int)esp_dummy,
                        (unsigned int)frame->eip, (unsigned int)frame->cs,
                        (unsigned int)frame->eflags,
                        frame_in_window, regs_in_window, clean);
}

void scheduler_add_task(process_t *proc) {
    uint32_t flags;

    if (proc == NULL) {
        return;
    }
    /*
     * Watch this task block's allocation site: any later kernel_free of a
     * block allocated at the same site emits WATCHED-FREE naming the
     * freeing caller. Directly tests the leading SMP hypothesis (a linked
     * task block released and reused while still on the list).
     */
    kernel_heap_watch_pc(kernel_heap_block_alloc_pc(proc));
    /*
     * Stack-field provenance (COMPLETION_ACTION_PLAN P4, class #5). The
     * sweep finds pid=1 with stack=UNAVAILABLE and a trashed canary, but
     * cannot tell WHEN the stack field stopped pointing at a valid block:
     * corrupted at creation vs corrupted while linked/running. Record the
     * state at LINK time — if a task is ever linked with an already-invalid
     * stack, that is a creation-path bug; if it is valid here but invalid
     * at dispatch, it was corrupted after linking.
     */
    if (g_sched_stack_prov_budget != 0u) {
        uintptr_t st = (uintptr_t)proc->stack;
        uint32_t ssize = proc->stack_size;
        int ok = (st != 0u && ssize >= 256u && ssize <= (1u << 20) &&
                  (ssize & 15u) == 0u &&
                  st + ssize <= kernel_heap_end() &&
                  kernel_heap_is_block((const void *)st));
        if (!ok) {
            g_sched_stack_prov_budget--;
            kernel_debug_printf("sched-add: STACK-INVALID-AT-LINK pid=%d st=0x%x size=%u kind=%d svc=%d\n",
                                proc->pid, (unsigned int)st, (unsigned int)ssize,
                                (int)proc->kind, (int)proc->service_type);
        }
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    proc->next = NULL;
    proc->state = PROCESS_READY;
    proc->current_cpu = -1;
    proc->last_cpu = -1;
    if (proc->preferred_cpu < 0) {
        proc->preferred_cpu = scheduler_pick_target_cpu();
    }
    if (g_head == NULL) {
        g_head = proc;
        g_cursor[proc->preferred_cpu >= 0 ? (uint32_t)proc->preferred_cpu : 0u] = proc;
        /* One-shot breadcrumb: g_head is assigned EXACTLY once, so every
         * BAD-HEAD trace in any later run is about THIS block. Naming it at
         * birth (pid + node + alloc pc) turns the forensic "who freed the
         * head?" question into a direct lookup. Budget: once per boot. */
        if (!g_scheduler_head_trace_done) {
            g_scheduler_head_trace_done = 1;
            kernel_debug_printf("sched-head: first task pid=%d node=0x%x allocpc=0x%x\n",
                                proc->pid,
                                (unsigned int)(uintptr_t)proc,
                                (unsigned int)kernel_heap_block_alloc_pc(proc));
        }
    } else {
        process_t *p = g_head;
        uint32_t steps = 0u;
        while (p->next) {
            if (!scheduler_node_sane(p->next)) {
                scheduler_list_diag("BAD-NODE-AT-ADD", p->next, p);
                break;
            }
            p = p->next;
            if (++steps > SCHED_LIST_WALK_CAP) {
                scheduler_list_diag("LIST-CYCLE-AT-ADD", p, NULL);
                break;
            }
        }
        if (p->next == NULL) {
            p->next = proc;
        } else {
            /*
             * The walk stopped on a corrupt/cyclic node: appending is
             * impossible without destroying the wild pointer (evidence).
             * Name the event so the log correlates with "launch returned
             * a pid whose task never entered the list".
             */
            scheduler_list_diag("ADD-ABORTED", p->next, p);
        }
    }
    rec_shadow_take(proc); /* RECORD-WATCH baseline (adendo 33) */
    /* Watchpoint tool auto-arms once the victim record is linked (task
     * context, no tick-side surprise): the #DB ring is lock-free and one
     * 8-byte window, per the TCG isolation ladder in db_set_regs. */
    if (proc->pid == SCHED_DB_TARGET_PID && g_db_arm_enable == 0u) {
        g_db_arm_enable = scheduler_db_supported() ? 1u : 0xFFFFu; /* CLOSED */
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    if (!g_audio_task_added_trace_emitted &&
        proc->service_type == MK_SERVICE_AUDIO) {
        g_audio_task_added_trace_emitted = 1u;
        kernel_text_puts("audiosvc: task added\n");
    }
    if (proc->service_type == MK_SERVICE_AUDIO) {
        kernel_text_puts("audiosvc: before publish\n");
    }
    scheduler_publish_task_event(MK_TASK_EVENT_LAUNCHED, proc);
    if (proc->service_type == MK_SERVICE_AUDIO) {
        kernel_text_puts("audiosvc: after publish\n");
    }
    smp_wake_sleeping_cpus();
    if (proc->service_type == MK_SERVICE_AUDIO) {
        kernel_text_puts("audiosvc: wake sent\n");
    }
}

/*
 * Is `proc` currently reachable from g_head? Used by process_destroy to
 * refuse freeing a LINKED task: the list is singly-linked and only
 * scheduler_add_task ever writes g_head (once), so a freed while-linked
 * block is exactly how g_head ends up dangling at a reallocated block —
 * the class #5 signature (BAD-HEAD node whose header/payload is being
 * rewritten by a new owner during sys_storage_load on real-time smp2).
 * Walk is node-sane-guarded and step-capped: this runs from a destroy
 * path that is itself suspect, so it must never dereference blindly.
 */
int scheduler_task_is_linked(const process_t *proc) {
    process_t *task;
    uint32_t steps = 0u;
    uint32_t flags;
    int found = 0;

    if (proc == NULL) {
        return 0;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    for (task = g_head; task != NULL; task = task->next) {
        if (task == proc) {
            found = 1;
            break;
        }
        if (!scheduler_node_sane(task)) {
            break;
        }
        if (++steps > SCHED_LIST_WALK_CAP) {
            break;
        }
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return found;
}

static process_t *find_next(uint32_t cpu_index, process_t *current) {
    process_t *start;
    process_t *task;
    process_t *best = NULL;
    int best_score = 0x7fffffff;

    if (g_head == NULL) {
        return NULL;
    }

    /*
     * Entry sanity: g_head/g_cursor are the only list pointers that do
     * NOT originate from a task's next field — validate them before the
     * walk so a wild cursor is repaired (cleared) instead of poisoning
     * every subsequent dispatch on this CPU.
     */
    if (!scheduler_node_sane(g_head)) {
        scheduler_list_diag("BAD-HEAD", g_head, NULL);
        return NULL;
    }
    start = g_cursor[cpu_index];
    if (start != NULL && !scheduler_node_sane(start)) {
        scheduler_list_diag("CURSOR-WILD", start, NULL);
        g_cursor[cpu_index] = NULL;
        start = NULL;
    }
    if (start == NULL) {
        start = current != NULL && current->next != NULL ? current->next : g_head;
    }
    if (start == NULL) {
        start = scheduler_first_runnable();
    }
    if (start == NULL) {
        return NULL;
    }

    task = start;
    {
        uint32_t steps = 0u;
        process_t *prev_task = NULL;
        do {
            if (!scheduler_node_sane(task)) {
                /*
                 * prev_task names the corrupted link's OWNER: if prev is
                 * NULL the start pointer itself (g_cursor / g_head) is
                 * stale; otherwise prev->next is the poisoned pointer.
                 */
                scheduler_list_diag("BAD-NODE-IN-FIND", task, prev_task);
                break;
            }
            if (task->state == PROCESS_READY && task->current_cpu < 0) {
                int score = scheduler_task_score(task, cpu_index);

                if (best == NULL || score < best_score) {
                    best = task;
                    best_score = score;
                    /*
                     * No early "floor" break (0.6, 26/10): 0 is not a floor.
                     * The aging term above is clamped to >= 0, but
                     * scheduler_task_score's bootstrap/wake-boost early
                     * returns hand back negative scores directly (-40+tier
                     * fresh service, -36/-34/-26 fresh user/app, -32/-24
                     * signaled IPC waiter) and those bypass the clamp. A
                     * break at the first 0-score task would stop the walk
                     * before reaching a negative-score task sitting later in
                     * the list — exactly the freshly-launched app task the
                     * boost was written to protect.
                     */
                }
            }

            prev_task = task;
            task = task->next != NULL ? task->next : g_head;
            if (++steps > SCHED_LIST_WALK_CAP) {
                scheduler_list_diag("LIST-CYCLE-IN-FIND", task, prev_task);
                break;
            }
        } while (task != NULL && task != start);
    }

    return best;
}

void scheduler_set_preemption_ready(int ready) {
    g_scheduler_preemption_ready = ready != 0 ? 1 : 0;
}

void scheduler_set_task_events_ready(int ready) {
    g_scheduler_task_events_ready = ready != 0 ? 1 : 0;
}

kernel_trap_frame_t *scheduler_schedule_frame(kernel_trap_frame_t *frame, int preemptive) {
    uint32_t cpu_index = kernel_cpu_index();
    uint32_t now_ticks = kernel_timer_get_ticks();
    process_t *current;
    process_t *next;
    process_t *old_task;
    kernel_trap_frame_t *resume_frame;
    uint32_t flags = spinlock_lock_irqsave(&g_scheduler_lock);

    g_sched_frame_calls++;
    if (preemptive) {
        g_sched_frame_preemptive_true++;
    }
    g_sched_frame_last_cpu = (int)cpu_index;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }
    if (cpu_index < 4u) {
        g_sched_frame_cpu_count[cpu_index].n++;
    }
    current = g_current[cpu_index];
    g_sched_frame_last_cur_pid = (current != NULL) ? current->pid : -1;
    /*
     * ROOT-CAUSE FIX (face-1, P4 classe #5, adendo 17). The idle path below
     * runs `for(;;){ sti; hlt; ... find_next ... }` WITHOUT returning. While
     * a CPU sits in that loop its LAPIC tick still fires; the tick handler
     * calls scheduler_schedule_frame again, find_next still says NULL, and
     * the inner call enters ANOTHER idle loop — nested ~160 bytes per tick,
     * forever, on the stack region the last task left behind. Measured 2026-10-17:
     * IDLE-ORPHAN sampled the AP's ESP descending 0x390b65f0 -> 0x390b6550 ->
     * 0x390b64b0 -> 0x390b6410 (exactly 160B/tick) INSIDE the init process_t
     * block — each nested trap frame eats through the init record (poisoning
     * next/state/context -> LIST-CYCLE/BAD-HEAD), then the ramfs block below,
     * then the AP stack's own tail canary — ONE mechanism explains faces
     * (a)-(f) including "AP installs froze while it kept executing" (the
     * nested loop dispatches nothing; only the outer loop ever breaks).
     *
     * Fix: while the idle loop owns this CPU, schedule_frame returns the
     * caller's own frame untouched (the tick just irets back into hlt).
     * The loop already re-runs find_next after every wake, so no wakeup can
     * be missed by suppressing re-entry; suppressed reentries are traced
     * (budgeted) so the witness is never silently inert.
     */
    if ((g_sched_idle_mask & (1u << cpu_index)) != 0u) {
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        if (g_sched_idle_reentry_budget != 0u) {
            g_sched_idle_reentry_budget--;
            kernel_debug_printf("sched-idle: reentry-suppressed cpu=%u rsp=%x (tick irets to hlt loop)\n",
                                (unsigned int)cpu_index,
                                (unsigned int)(uintptr_t)frame);
        }
        return frame;
    }
    if (!g_scheduler_preemption_ready) {
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        return frame;
    }
    if (preemptive &&
        current != NULL &&
        current->state == PROCESS_RUNNING &&
        current->current_cpu == (int)cpu_index) {
        scheduler_save_current_context(current, frame, "preempt-skip");
        if (g_timeslice_remaining[cpu_index] > 1u) {
            g_timeslice_remaining[cpu_index] -= 1u;
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
            return frame;
        }
    }
    if (current && current->state == PROCESS_RUNNING && current->current_cpu == (int)cpu_index) {
        scheduler_save_current_context(current, frame, "switch-out");
        scheduler_account_runtime(current, now_ticks);
        if (current->wake_boost_budget != 0u) {
            current->wake_boost_budget -= 1u;
        }
        if (current->wait_result != TASK_WAIT_RESULT_NONE &&
            !scheduler_keep_wait_boost(current)) {
            /*
             * Wakeup priority should buy the task its first resumed slice, not
             * permanently bias every later reschedule after the wait was
             * already consumed in userland/kernel code.
             */
            current->wait_result = TASK_WAIT_RESULT_NONE;
            current->wait_event_kind = TASK_WAIT_EVENT_NONE;
            current->wait_event_class = TASK_WAIT_CLASS_NONE;
            current->wait_owner_service = 0u;
            current->wake_boost_budget = 0u;
        }
        current->state = PROCESS_READY;
        current->current_cpu = -1;
        current->last_cpu = (int)cpu_index;
    } else if (current && current->state == PROCESS_BLOCKED &&
               current->current_cpu == (int)cpu_index) {
        scheduler_save_current_context(current, frame, "block-save");
        scheduler_account_runtime(current, now_ticks);
        current->current_cpu = -1;
        current->last_cpu = (int)cpu_index;
    }
    next = find_next(cpu_index, current);
    if (next == NULL) {
        if (current && current->state == PROCESS_READY) {
            current->state = PROCESS_RUNNING;
            current->current_cpu = (int)cpu_index;
            current->last_cpu = (int)cpu_index;
            current->last_start_tick = now_ticks;
            g_timeslice_remaining[cpu_index] = SCHEDULER_TIMESLICE_TICKS;
            g_current[cpu_index] = current;
            resume_frame = current->context != NULL ? current->context : frame;
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
            return scheduler_sanitize_resume_frame(cpu_index, current, current, resume_frame);
        }

        /*
         * Invariant witness (face-1 survivor, P4 adendo 18): the no-task
         * branch legitimately nulls g_current for BLOCKED/TERMINATED holders
         * (their context was saved by the switch-out branches above — first
         * sampling fired on state=2 BLOCKED with curcpu=-1 and a VALID ctx,
         * i.e. the designed flow, so those states are excluded here). A
         * RUNNING task reaching this path means NO switch-out branch owned
         * it — g_current is cleared while the task still believes it runs on
         * this cpu — and is precisely the shape the timeout hook later
         * reports as GHOST-RUNNING (measured hang: pid=9 overdue BLOCKED ->
         * GHOST-RUNNING cur=0). Name the event while the evidence is live.
         */
        if (current != NULL && current->state == PROCESS_RUNNING &&
            g_current[cpu_index] == current) {
            static volatile uint32_t g_sched_idle_live_budget = 6u;
            if (g_sched_idle_live_budget != 0u) {
                g_sched_idle_live_budget--;
                kernel_debug_printf("sched-idle: clear-with-live-task cpu=%u pid=%d state=%d curcpu=%d ctx=%x sw=%u\n",
                                    (unsigned int)cpu_index, current->pid,
                                    (int)current->state, current->current_cpu,
                                    (unsigned int)(uintptr_t)current->context,
                                    (unsigned int)current->context_switches);
            }
        }

        g_current[cpu_index] = 0;
        g_timeslice_remaining[cpu_index] = SCHEDULER_TIMESLICE_TICKS;
        g_sched_idle_mask |= (1u << cpu_index);
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);

        for (;;) {
            __asm__ volatile("sti; hlt; cli" : : : "memory");
            now_ticks = kernel_timer_get_ticks();
            flags = spinlock_lock_irqsave(&g_scheduler_lock);
            next = find_next(cpu_index, 0);
            if (next != NULL) {
                g_sched_idle_mask &= ~(1u << cpu_index);
                break;
            }
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        }
    }

    g_cursor[cpu_index] = next->next != NULL ? next->next : g_head;
    old_task = current;

    /*
     * Deferred execve stack release (P4 UAF fix): once the task is being
     * scheduled through, nothing on this cpu references its parked old stack
     * any more (saved context lives in the NEW stack, esp0 was repointed at
     * execve time). kernel_free here runs off the new stack, never the
     * parked one, and takes g_heap_lock under g_scheduler_lock — the
     * scheduler->heap order already established by scheduler_node_sane().
     */
    if (old_task != NULL && old_task->pending_stack_free != NULL) {
        kernel_free(old_task->pending_stack_free);
        old_task->pending_stack_free = NULL;
    }

    if (next != current) {
        /*
         * Anti-double-dispatch assertion (COMPLETION_ACTION_PLAN P4, class
         * #5). find_next only returns state==READY && current_cpu<0 tasks,
         * and this whole block runs under g_scheduler_lock, so a task can
         * never be owned by two CPUs at once — IF the invariant holds.
         * The class #5 crash (real-time smp2: first storage exchange, #UD
         * at eip=3/7/0x213 = small integers = data-as-pointer, dispatch
         * frames valid) looks exactly like two CPUs resuming the SAME
         * frame: whichever loses the race finds its saved regs overwritten
         * by the winner. Assert the invariant here so a violation is named
         * loudly instead of corrupting a frame silently.
         */
        if (next->current_cpu >= 0 || next->state == PROCESS_RUNNING) {
            if (g_sched_double_dispatch_budget != 0u) {
                g_sched_double_dispatch_budget--;
                kernel_debug_printf("sched-dispatch: DOUBLE-DISPATCH cpu=%u pid=%d owner_cpu=%d state=%d sw=%u\n",
                                    cpu_index, next->pid, next->current_cpu,
                                    (int)next->state, next->context_switches);
            }
        }
        /*
         * TSS/TR assertion (class #5): the CPU's live TR must be the TSS
         * the scheduler sets esp0 on for THIS cpu_index, else ring3->ring0
         * entries load esp0 from another CPU's TSS and push frames onto the
         * wrong stack. kernel_cpu_tss_selector(cpu_index) is what
         * gdt_init() ltr'd on this CPU; a mismatch means kernel_cpu_index()
         * and the hardware TR disagree.
         */
        {
            uint16_t want_tr = kernel_cpu_tss_selector(cpu_index);
            uint16_t have_tr = kernel_current_tr();
            if (want_tr != have_tr && g_sched_tr_mismatch_budget != 0u) {
                g_sched_tr_mismatch_budget--;
                kernel_debug_printf("sched-dispatch: TR-MISMATCH cpu=%u pid=%d have_tr=0x%x want_tr=0x%x\n",
                                    cpu_index, next->pid,
                                    (unsigned)have_tr, (unsigned)want_tr);
            }
        }
        g_sched_sel_count[next->pid & 31] += 1u;
        /* bug #19 wake-to-dispatch probe: stamp was set by
         * scheduler_complete_wait at the SIGNALED+IPC-wake that made this
         * task eligible for the -128 boost; the gap to THIS dispatch is the
         * latency the service handshake actually paid. All under
         * g_scheduler_lock (fields read, no re-deref after unlock). */
        if (next->pid > 0 && next->pid < 32) {
            uint32_t wake_tick = g_ipc_wake_tick[next->pid & 31u];
            uint32_t gap;
            if (wake_tick != 0u) {
                g_ipc_wake_tick[next->pid & 31u] = 0u;
                gap = now_ticks - wake_tick;
                if (gap > g_wake_max_d[next->pid & 31u]) {
                    g_wake_max_d[next->pid & 31u] = gap;
                }
                if (gap > 20u && g_wake_stale_budget != 0u) {
                    g_wake_stale_budget -= 1u;
                    kernel_debug_printf("wake-stale: pid=%d d=%u wr=%d cls=%d bud=%u t=%u\n",
                                        next->pid, (unsigned int)gap,
                                        (int)next->wait_result,
                                        (int)next->wait_event_class,
                                        (unsigned int)next->wake_boost_budget,
                                        (unsigned int)now_ticks);
                }
            }
            /* item 0.13: same measurement for timeout wakes (stamp set by
             * the deadline sweep). Print when the supervision/poll loop is
             * starved: gap > 50 ticks = 12x the 4-tick poll budget, which
             * is the failure signature 0.13 needs to confirm or kill. */
            wake_tick = g_tmo_wake_tick[next->pid & 31u];
            if (wake_tick != 0u) {
                g_tmo_wake_tick[next->pid & 31u] = 0u;
                gap = now_ticks - wake_tick;
                if (gap > g_tmo_wake_max_d[next->pid & 31u]) {
                    g_tmo_wake_max_d[next->pid & 31u] = gap;
                }
                if (gap > 50u && g_tmo_stale_budget != 0u) {
                    g_tmo_stale_budget -= 1u;
                    kernel_debug_printf("tmo-stale: pid=%d d=%u cls=%d t=%u\n",
                                        next->pid, (unsigned int)gap,
                                        (int)next->wait_event_class,
                                        (unsigned int)now_ticks);
                }
            }
        }
        /*
         * 0.6 mode-iii (30/10, bug #19): name WHERE a ring3 userland task is
         * when the scheduler hands it the CPU. The score probe proved pid13
         * is dispatched and never starvation-blocked, yet its app console
         * output froze mid-status() with no `app: main returned`; this prints
         * the saved resume eip so the spin loop is identified by address
         * (stable eip across dispatches = loop body; symbolize with
         * `nm build/lang/netmgrd.elf`). Only USER-CS frames are printed: a
         * kernel-side saved eip means the task was switched out inside the
         * yield/syscall path (the census already says that), and the bug is
         * about WHERE in userland it burns slices. Per-pid 1000-tick throttle
         * (a first attempt without it spent the whole budget on the shell
         * host's 4-tick poll loop before the app even launched). Budgeted,
         * snapshot under the scheduler lock, <=80 chars.
         */
        {
            uint32_t pidx = (uint32_t)(next->pid & 31);

            if (g_sched_rip_budget != 0u && next->context != NULL &&
                next->kind == PROCESS_KIND_USER && next->runs_in_user_mode != 0u &&
                (unsigned int)(now_ticks - g_sched_rip_last[pidx]) >= 1000u &&
                next->context->cs != 8u && next->context->cs != (unsigned int)0x10u) {
                g_sched_rip_budget -= 1u;
                g_sched_rip_last[pidx] = now_ticks;
                kernel_debug_printf("sched-rip p=%d t=%u eip=%x cs=%x esp=%x\n",
                                    next->pid, (unsigned int)now_ticks,
                                    (unsigned int)next->context->eip,
                                    (unsigned int)next->context->cs,
                                    (unsigned int)next->context->esp_dummy);
            }
        }
        next->state = PROCESS_RUNNING;
        next->current_cpu = (int)cpu_index;
        next->last_cpu = (int)cpu_index;
        next->last_start_tick = now_ticks;
        next->context_switches += 1u;
        g_timeslice_remaining[cpu_index] = SCHEDULER_TIMESLICE_TICKS;
        g_current[cpu_index] = next;
        /*
         * 0.6 modo-iii (31/10, bug #19, second stall mode): make the IPC
         * wake-boost ONE-SHOT. The score early-return (-32 service / -24
         * user, scheduler_task_score) bypasses base+aging entirely, so a
         * task in a tight block/wake IPC poll loop (the busybox host-wait
         * poller: 4-tick deadline re-wakes far faster than the 8/12-slice
         * budget decays) re-earned the full budget on every wake and sat
         * at -24 permanently. The aging floor is the clamp at 0, which a
         * clamped waiter can never get below - the floors are inverted, so
         * the perpetually-boosted poller outranked the maximally-aged
         * shell host AND the tier-0 init host (measured run3 31/10: sel10
         * and sel5 frozen from t~7025 while sel11 climbed 176k, and the
         * typed command line never completed). Consuming the whole boost
         * on the first dispatch after the wake implements the intent the
         * switch-out comment already states: the wake buys ONE resumed
         * slice. One boosted slice per wake is exactly what synchronous
         * request/reply IPC needs (woken side runs, signals back, both
         * get one boosted slice each) without starving the waiters.
         *
         * ROOT-CAUSE CORRECTION (bug #19 A/B, 01/11): consumption must
         * touch ONLY `wake_boost_budget`. Clearing `wait_result`/class/
         * owner HERE — on the dispatch the woken task uses to resume —
         * steals the completion from the reader after `yield()` in
         * kernel_waitable_wait_timeout(): the task saw NONE, re-entered
         * the for-loop, re-enqueued and re-blocked WITHOUT running
         * `ipc_receive`, so the message rotted in the queue (witnesses:
         * `ipc-pos head=0` after each pop-wake, depth growing 1→16,
         * `waiters=1 wake=0` forever, zero `ipc-got`/`ipc-ret` prints).
         * On HEAD the leaked `pending_signals` masked this: the loop-top
         * consume rescued every stolen wake. One-shot semantics stay:
         * the -128 early return requires `wake_boost_budget != 0`, and
         * this zeroing makes the NEXT walk score tier+aging.
         */
        if ((next->wait_result == TASK_WAIT_RESULT_SIGNALED ||
             next->wait_result == TASK_WAIT_RESULT_TIMED_OUT) &&
            (next->wait_event_class == TASK_WAIT_CLASS_IPC ||
             next->wait_event_class == TASK_WAIT_CLASS_SUPERVISION ||
             next->wait_event_class == TASK_WAIT_CLASS_INPUT) &&
            next->wake_boost_budget != 0u) {
            next->wake_boost_budget = 0u;
        }
        /*
         * Face-1 witness precision fix (P4): publish esp0 together with
         * g_current, under the same lock. Before this, the TSS esp0 was
         * only written later by scheduler_sanitize_resume_frame AFTER the
         * unlock, so a tick guard on another CPU legitimately saw
         * g_current[cpu]=next with the PREVIOUS task's esp0 — the real-time
         * smp2 run 2026-10-13 caught exactly that (pid=5 running,
         * esp0=top-of-pid=12, every value a valid heap stack top, so NOT
         * corruption). The write is idempotent with the sanitize pass, which
         * still runs later and keeps its SUSPECT checks.
         */
        if (next->stack != NULL && next->stack_size != 0u) {
            kernel_tss_set_kernel_stack((uintptr_t)next->stack + next->stack_size);
        }
        resume_frame = next->context;
        if (resume_frame == NULL) {
            kernel_debug_printf("scheduler: select missing context pid=%d next_context=NULL\n",
                                next->pid);
        }
        scheduler_dispatch_forensics((uint32_t)cpu_index, next, resume_frame);
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        if (!g_audio_first_dispatch_trace_emitted &&
            next->service_type == MK_SERVICE_AUDIO) {
            g_audio_first_dispatch_trace_emitted = 1u;
            kernel_text_puts("audiosvc: first dispatch\n");
        }
        if (old_task == NULL && !g_scheduler_boot_trace_emitted) {
            g_scheduler_boot_trace_emitted = 1;
            kernel_debug_printf("scheduler: first dispatch cpu=%d pid=%d kind=%d service=%d eip=%x esp=%x\n",
                                (int)cpu_index,
                                next->pid,
                                (int)next->kind,
                                (int)next->service_type,
                                (unsigned int)process_saved_eip(next),
                                (unsigned int)process_saved_esp(next));
        }
        scheduler_trace_switch(cpu_index, old_task, next);
        return scheduler_sanitize_resume_frame(cpu_index,
                                               old_task,
                                               next,
                                               resume_frame != NULL ? resume_frame : frame);
    } else {
        g_sched_sel_count[current->pid & 31] += 1u;
        /* Reuse dispatch: the woken task is already current and runs NOW —
         * measure the wake->run gap the same way the switch-in path does,
         * then drop the stamps. */
        if (current->pid > 0 && current->pid < 32) {
            uint32_t t;
            uint32_t gap;
            t = g_ipc_wake_tick[current->pid & 31u];
            if (t != 0u) {
                g_ipc_wake_tick[current->pid & 31u] = 0u;
                gap = now_ticks - t;
                if (gap > g_wake_max_d[current->pid & 31u]) {
                    g_wake_max_d[current->pid & 31u] = gap;
                }
            }
            t = g_tmo_wake_tick[current->pid & 31u];
            if (t != 0u) {
                g_tmo_wake_tick[current->pid & 31u] = 0u;
                gap = now_ticks - t;
                if (gap > g_tmo_wake_max_d[current->pid & 31u]) {
                    g_tmo_wake_max_d[current->pid & 31u] = gap;
                }
            }
        }
        current->state = PROCESS_RUNNING;
        current->current_cpu = (int)cpu_index;
        current->last_cpu = (int)cpu_index;
        current->last_start_tick = now_ticks;
        scheduler_save_current_context(current, frame, "reuse");
        g_timeslice_remaining[cpu_index] = SCHEDULER_TIMESLICE_TICKS;
        resume_frame = current->context;
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        return scheduler_sanitize_resume_frame(cpu_index,
                                               current,
                                               current,
                                               resume_frame != NULL ? resume_frame : frame);
    }
}

void schedule(void) {
    yield();
}

void yield(void) {
    __asm__ volatile("int $0x81" : : : "memory");
}

process_t *scheduler_current(void) {
    return scheduler_current_for_cpu(kernel_cpu_index());
}

process_t *scheduler_current_for_cpu(uint32_t cpu_index) {
    process_t *current;
    uint32_t flags;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    current = g_current[cpu_index];
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return current;
}

uint32_t scheduler_current_pid_for_cpu(uint32_t cpu_index) {
    process_t *current;
    uint32_t flags;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    current = g_current[cpu_index];
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return current != NULL ? (uint32_t)current->pid : 0u;
}

uint32_t scheduler_current_pid(void) {
    return scheduler_current_pid_for_cpu(kernel_cpu_index());
}

process_t *scheduler_find_task_by_pid(int pid) {
    process_t *task;
    uint32_t flags;

    if (pid <= 0) {
        return NULL;
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    for (task = g_head; task != NULL; task = task->next) {
        /* Guarded: the returned pointer is dereferenced by waiters
         * (e.g. sys_thread_join on thread_join_waitable) — a wild match
         * would hand out a non-task pointer to kernel code. */
        if (!scheduler_node_sane(task)) {
            scheduler_list_diag("BAD-NODE-IN-FINDPID", task, NULL);
            break;
        }
        if (task->pid == pid && task->state != PROCESS_TERMINATED) {
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
            return task;
        }
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return NULL;
}

void scheduler_terminate_task(process_t *task) {
    uint32_t flags;
    uint32_t cpu;
    uint32_t now_ticks;

    if (task == NULL) {
        return;
    }

    now_ticks = kernel_timer_get_ticks();
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    scheduler_account_runtime(task, now_ticks);
    task->state = PROCESS_TERMINATED;
    task->current_cpu = -1;
    task->preferred_cpu = -1;
    task->last_cpu = -1;
    task->wait_channel = 0;
    task->wait_deadline = 0u;
    task->wait_result = TASK_WAIT_RESULT_NONE;
    task->wait_event_kind = TASK_WAIT_EVENT_NONE;
    task->wait_event_class = TASK_WAIT_CLASS_NONE;
    task->wait_owner_service = 0u;
    task->wake_boost_budget = 0u;
    task->wait_next = 0;

    /* Deferred execve stack release on the terminate path too (the task may
     * never switch away before dying). Runs on the task's CURRENT stack. */
    if (task->pending_stack_free != NULL) {
        kernel_free(task->pending_stack_free);
        task->pending_stack_free = NULL;
    }

    for (cpu = 0u; cpu < 32u; ++cpu) {
        if (g_current[cpu] == task) {
            g_current[cpu] = NULL;
        }
        if (g_cursor[cpu] == task) {
            g_cursor[cpu] = task->next != NULL ? task->next : g_head;
            if (g_cursor[cpu] == task) {
                g_cursor[cpu] = NULL;
            }
        }
    }

    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    scheduler_publish_task_event(MK_TASK_EVENT_TERMINATED, task);
    mk_launch_release_pid(task->pid);
}

int scheduler_block_current_ex(const void *wait_channel,
                               uint32_t wait_deadline,
                               uint32_t wait_event_kind,
                               uint32_t wait_event_class,
                               uint32_t wait_owner_service) {
    process_t *current;
    uint32_t cpu_index = kernel_cpu_index();
    uint32_t flags;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    current = g_current[cpu_index];
    if (current == NULL || current->state != PROCESS_RUNNING) {
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        return -1;
    }

    if (current->wait_result == TASK_WAIT_RESULT_SIGNALED) {
        /*
         * A waitable signal landed between the waiter enqueuing itself on
         * the waitable and this block transition (scheduler_complete_wait
         * kept the task RUNNING via the inline-wake path). Blocking now
         * would swallow the wake: the request stays queued forever (seen
         * on SMP as the storage host pid=1 BLOCKED with its queue full).
         * Abort the block; the wait loop consumes the pending signal.
         */
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        return -1;
    }

    current->state = PROCESS_BLOCKED;
    current->wait_channel = wait_channel;
    current->wait_deadline = wait_deadline;
    current->wait_result = TASK_WAIT_RESULT_NONE;
    current->wait_event_kind = wait_event_kind;
    current->wait_event_class = wait_event_class;
    current->wait_owner_service = wait_owner_service;
    current->wake_boost_budget = 0u;
    scheduler_note_block(current, wait_deadline);
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    if (g_scheduler_block_trace_budget > 0) {
        g_scheduler_block_trace_budget -= 1;
        kernel_debug_printf("scheduler: block pid=%d kind=%d svc=%d class=%d event=%d owner=%d deadline=%d\n",
                            current->pid,
                            (int)current->kind,
                            (int)current->service_type,
                            (int)wait_event_class,
                            (int)wait_event_kind,
                            (int)wait_owner_service,
                            (int)wait_deadline);
    }
    scheduler_publish_task_event(MK_TASK_EVENT_BLOCKED, current);
    return 0;
}

int scheduler_block_current(const void *wait_channel) {
    process_t *current = scheduler_current();

    /* Legacy block paths do not run the waitable loop's clear; drop a
     * stale SIGNALED so the signal-race abort in _ex only fires for a
     * wake that arrives during THIS block. */
    if (current != NULL) {
        current->wait_result = TASK_WAIT_RESULT_NONE;
    }
    return scheduler_block_current_ex(wait_channel,
                                      0u,
                                      TASK_WAIT_EVENT_WAITABLE,
                                      TASK_WAIT_CLASS_GENERIC,
                                      0u);
}

int scheduler_wake_task(process_t *task) {
    return scheduler_complete_wait(task, TASK_WAIT_RESULT_SIGNALED);
}

int scheduler_complete_wait(process_t *task, uint32_t wait_result) {
    uint32_t flags;

    if (task == NULL) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    if (task->state == PROCESS_BLOCKED) {
        int inline_cpu = task->current_cpu;
        int inline_wake = 0;

        if (inline_cpu >= 0 && inline_cpu < 32 && g_current[(uint32_t)inline_cpu] == task) {
            /*
             * A wait can complete before the task actually yields. In that
             * case the task is still executing on its current CPU, so leaving
             * it READY would cause the next trap to skip saving the live frame
             * and later resume stale stack state.
             */
            inline_wake = 1;
        }
        task->state = inline_wake ? PROCESS_RUNNING : PROCESS_READY;
        if (!inline_wake) {
            task->current_cpu = -1;
        }
        scheduler_clear_wait_channel(task);
        task->wait_result = wait_result;
        task->wake_boost_budget =
            (wait_result == TASK_WAIT_RESULT_SIGNALED &&
             task->wait_event_class == TASK_WAIT_CLASS_IPC)
                ? (task->kind == PROCESS_KIND_SERVICE ? 12u : 8u)
                : 0u;
        /* Inline wakes need no dispatch (the task keeps running), so they
         * are not stamped: only a BLOCKED task that must WIN a walk to run
         * is the subject of the latency probe. */
        if (!inline_wake && wait_result == TASK_WAIT_RESULT_SIGNALED &&
            task->wake_boost_budget != 0u &&
            task->pid > 0 && task->pid < 32) {
            g_ipc_wake_tick[task->pid & 31u] = kernel_timer_get_ticks();
        }
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        scheduler_publish_task_event(MK_TASK_EVENT_WOKE, task);
        return 0;
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return -1;
}

int scheduler_task_event_subscribe(process_t *subscriber,
                                   uint32_t event_mask,
                                   uint32_t task_class_mask) {
    uint32_t flags;
    struct scheduler_task_event_subscription *subscription;
    int rc;

    scheduler_set_task_events_ready(1);
    flags = spinlock_lock_irqsave(&g_task_event_lock);
    subscription = scheduler_alloc_task_event_subscription_locked(subscriber);
    if (subscription != NULL) {
        subscription->event_mask = event_mask != 0u ? event_mask : MK_TASK_EVENT_MASK_LIFECYCLE;
        subscription->task_class_mask = task_class_mask != 0u ? task_class_mask : MK_TASK_CLASS_MASK_ALL;
        rc = 0;
    } else {
        rc = -1;
    }
    spinlock_unlock_irqrestore(&g_task_event_lock, flags);
    return rc;
}

int scheduler_task_event_receive(process_t *subscriber,
                                 struct mk_task_event *event,
                                 uint32_t timeout_ticks) {
    int wait_rc;

    if (subscriber == NULL || event == NULL) {
        return -1;
    }

    for (;;) {
        struct scheduler_task_event_subscription *subscription;
        uint32_t flags;

        flags = spinlock_lock_irqsave(&g_task_event_lock);
        subscription = scheduler_find_task_event_subscription_locked(subscriber);
        if (subscription == NULL) {
            spinlock_unlock_irqrestore(&g_task_event_lock, flags);
            static int g_evt_nosub_trace = 6;
            if (g_evt_nosub_trace != 0) {
                g_evt_nosub_trace--;
                kernel_debug_printf("evt-rec: nosub pid=%d\n", subscriber->pid);
            }
            return -1;
        }
        if (kernel_mailbox_try_receive(&subscription->mailbox, event) == 0) {
            spinlock_unlock_irqrestore(&g_task_event_lock, flags);
            return 0;
        }
        spinlock_unlock_irqrestore(&g_task_event_lock, flags);
        /* item 0.13 probe (02/11, campaign): name the mailbox state at the
         * moment the session host blocks. cnt>0 with rc=-1 would name a
         * receive bug; cnt=0 + high dropped names a publisher-side loss. */
        if (subscriber->pid == 11 || subscriber->pid == 12) {
            static int g_evt_rec_trace = 8;
            if (g_evt_rec_trace != 0) {
                g_evt_rec_trace--;
                kernel_debug_printf("evt-rec: pid=%d cnt=%u drop=%u pend=%u\n",
                                    subscriber->pid,
                                    kernel_mailbox_count(&subscription->mailbox),
                                    kernel_mailbox_dropped(&subscription->mailbox),
                                    kernel_mailbox_pending(&subscription->mailbox));
            }
        }
        if (timeout_ticks == 0u) {
            return -1;
        }
        if (timeout_ticks == MK_TASK_EVENT_WAIT_FOREVER) {
            wait_rc = kernel_mailbox_wait(&subscription->mailbox, 0u);
        } else {
            wait_rc = kernel_mailbox_wait(&subscription->mailbox, timeout_ticks);
        }
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}

static volatile uint32_t g_wedge_budget = 6u;
static volatile uint32_t g_wedge_armed;
static uint32_t g_wedge_ticks;

static void supervisor_wedge_scan(uint32_t tick) {
    process_t *t = g_head;
    process_t *host = NULL;
    uint32_t steps = 0u;

    /* Every early task may be a service host (extracted workers get pids
     * beyond 4 — measured: storage/console hosts live at 1..8). Scan ALL
     * BLOCKED-with-wait-channel tasks up to the walk cap; ask each one's
     * OWN queue. Armability proof first (campaign rule Q5/lesson 38:
     * never read 0-hits before proving the witness runs). */
    if (g_wedge_armed == 0u) {
        g_wedge_armed = 1u;
        kernel_debug_printf("supervisor-wedge: armed scan=%u\n",
                            (unsigned int)tick);
    }
    for (; t != NULL && steps < 16u; t = t->next, ++steps) {
        if (!scheduler_node_sane(t)) {
            break;
        }
        if (t->state == PROCESS_BLOCKED && t->wait_channel != 0) {
            host = t;
            break;
        }
    }
    if (host == NULL) {
        g_wedge_ticks = 0u;
        return;
    }
    {
        int depth = -1, waiters = -1;
        uint32_t pend = 0u, wake = 0u, tmo = 0u;

        ipc_queue_stats(host, &depth, &pend, &wake, &tmo, &waiters);
        if (depth > 0) {
            g_wedge_ticks++;
            if (g_wedge_ticks >= 4u && g_wedge_budget != 0u) {
                g_wedge_budget--;
                kernel_debug_printf("supervisor-wedge: pid=%d host-blocked q=%d pend=%u waiters=%d wake=%u tmo=%u tick=%u\n",
                                    host->pid, depth, (unsigned int)pend,
                                    waiters, (unsigned int)wake,
                                    (unsigned int)tmo, (unsigned int)tick);
                g_wedge_ticks = 0u;
            }
        } else {
            g_wedge_ticks = 0u;
        }
    }
}

static void scheduler_timeout_tick_hook(uint32_t tick) {
    process_t *task;
    process_t *woke_tasks[16];
    /* Snapshot the printed fields UNDER the lock: the old post-unlock
     * re-dereference of woke_tasks[] printed whatever the poison had written
     * in the meantime (lg3: `timeout pid=957248588 owner=957186176` = ESP
     * values of pid5's stack region while the walk itself had collected the
     * node while still sane). Snapshotting makes the trace truthful. */
    struct {
        int pid;
        int cls;
        int evt;
        int owner;
    } woke_snap[16];
    uint8_t woke_inline[16];
    uint32_t woke_count = 0u;
    uint32_t wake_sleepers = 0u;
    uint32_t steps = 0u;
    uint32_t flags;

    flags = spinlock_lock_irqsave(&g_scheduler_lock);

    if ((tick % 25u) == 0u) {
        scheduler_overdue_scan(tick);
    }

    /* RECORD-WATCH: diff the never-change fields of every linked record
     * every ~100 ticks (adendo 33). */
    if ((tick % 100u) == 0u) {
        rec_watch_scan();
    }

    /*
     * SUPERVISOR-WEDGE witness (P4, KNOWN_BUGS "sessao interativa trava no
     * meio"/0.6): a request sitting in the queue of a BLOCKED-with-no-
     * deadline host for >=4 consecutive ticks is a lost wake — the host
     * cannot be waiting AND have work enqueued at once for that long under
     * correct signalling. Prints the host waitable stats (depth/pending/
     * waiters) so the loss localizes by measurement:
     *   depth>0 waiters=1 pending=0 => signal lost in handoff (send saw an
     *     empty wait_head, block-enqueue then raced past the pending check);
     *   depth>0 pending>0          => signalled but block aborted/ignored;
     *   depth=0                    => not a wedge, just idle (no print).
     */
    if ((tick % 4u) == 0u) {
        supervisor_wedge_scan(tick);
    }

    /* Hardware write-watchpoints on the victim record (adendo 36): the BSP
     * arms its own DR set here; the AP arms from its LAPIC tick handler.
     * Cheap (two compares when unchanged). */
    scheduler_dr_maintain(kernel_cpu_index());

    /*
     * INTRA-EXECUTION stack guard (face-1, P4 adendo 7 candidate #1 — the
     * first witness that does NOT depend on dispatch timing). For every CPU,
     * the tail canary of the stack block of its CURRENT running task must
     * stay intact: legitimate pushes live strictly inside the payload, the
     * canary sits ABOVE the declared stack top, so a broken canary means
     * something wrote past the top of a stack that is RUNNING RIGHT NOW —
     * naming the victim pid while the owner is still g_current, minutes
     * before the neighbor-header desync would show up in the list walk.
     * g_current[] is only mutated under this same lock, so reading other
     * CPUs' entries here is race-free. kernel_heap_block_canary_ok takes
     * g_heap_lock: scheduler->heap order already established.
     */
    /*
     * INTRA-EXECUTION stack guard (face-1, P4 adendo 7 candidate #1,
     * extended 17/10e). For every CPU, the CURRENT task's stack block must:
     *  (1) have stack/stack_size fields that are coherent (pointer inside
     *      the heap, plausible size) — the decoded face pattern places
     *      corrupt esp/eip INSIDE scheduler .bss globals, exactly what a
     *      clobbered `stack` field would produce at the next iret/trap;
     *  (2) keep its tail canary intact (upward overflow detector);
     *  (3) match this CPU's live TSS esp0 == stack+size, because esp0 lives
     *      in .bss next to the corrupted addresses — a direct write to
     *      g_cpu_tss bypasses every setter-side witness.
     * All reads run under g_scheduler_lock (g_current is only mutated under
     * it). Budgeted; never mutates state (witness-only until the face is
     * named consistently enough to justify a repair).
     */
    {
        uint32_t cpu;
        uintptr_t hs = kernel_heap_start();
        uintptr_t he = kernel_heap_end();

        for (cpu = 0u; cpu < 32u; ++cpu) {
            process_t *t = g_current[cpu];
            uintptr_t top;

            if (t == NULL || t->state != PROCESS_RUNNING) {
                continue;
            }
            if (t->stack == NULL || hs == 0u || he == 0u) {
                continue;
            }
            top = (uintptr_t)t->stack + t->stack_size;

            if ((uintptr_t)t->stack <= hs || (uintptr_t)t->stack >= he ||
                top >= he || t->stack_size < 1024u || t->stack_size > (4u << 20)) {
                if (g_sched_tick_field_budget != 0u) {
                    g_sched_tick_field_budget--;
                    kernel_debug_printf("tick-guard: STACK-FIELDS-BAD cpu=%u pid=%d stack=%x size=%u heap=[%x,%x) tick=%u\n",
                                        (unsigned int)cpu, t->pid,
                                        (unsigned int)(uintptr_t)t->stack,
                                        (unsigned int)t->stack_size,
                                        (unsigned int)hs, (unsigned int)he,
                                        (unsigned int)tick);
                    /* Name the current owner of the RECORD's own block: if
                     * the task record memory was re-handed to another
                     * allocation, the victim-hdr allocpc says who. */
                    kernel_heap_forensic_sweep(t);
                }
                continue;
            }
            {
                uintptr_t hdr[4] = {0u, 0u, 0u, 0u};
                int vrc = kernel_heap_block_validate(t->stack, hdr);

                /*
                 * Watermark bottom check (process.c fills the whole stack with
                 * 0xA5A5A5A5 before placing the initial frame): if the FIRST 8
                 * words at the stack base are all consumed, this RUNNING task
                 * has already touched the very bottom of its stack — the
                 * precondition for the downward-exhaustion poisoning named in
                 * face (f) (stacks sit right after their process_t; growth past
                 * the base smashes the block below). Witness-only, budgeted.
                 */
                if (vrc == 0) {
                    const uint32_t *base = (const uint32_t *)t->stack;
                    uint32_t untouched = 0u;
                    uint32_t i;

                    for (i = 0u; i < 8u; ++i) {
                        if (base[i] == 0xA5A5A5A5u) {
                            untouched++;
                        }
                    }
                    if (untouched == 0u && g_sched_tick_bottom_budget != 0u) {
                        g_sched_tick_bottom_budget--;
                        kernel_debug_printf("tick-guard: BOTTOM-TOUCHED cpu=%u pid=%d stack=%x size=%u tick=%u\n",
                                            (unsigned int)cpu, t->pid,
                                            (unsigned int)(uintptr_t)t->stack,
                                            (unsigned int)t->stack_size,
                                            (unsigned int)tick);
                    }
                }

                if (vrc == 0) {
                    /*
                     * Intra-execution CONTEXT witness (face-1 survivor,
                     * adendo 19): run 17/10k-po2 died at #UD eip=3 WITHOUT
                     * any WATERMARK-FRAME — the frame is only checked at
                     * dispatch, so poisoning that lands BETWEEN dispatch and
                     * the iret epilogue is invisible there. `context` is the
                     * resume frame pointer: for a RUNNING task it must lie
                     * INSIDE its own validated stack payload (every writer
                     * — schedule_frame saves, fork, execve — builds frames
                     * there). A context outside [stack, stack+size) while
                     * RUNNING is the exact mid-flight poison, caught one
                     * tick before death, with the RECORD map for free.
                     */
                    uintptr_t c = (uintptr_t)t->context;

                    if (c != 0u && (c < (uintptr_t)t->stack ||
                                    c + sizeof(kernel_trap_frame_t) > top)) {
                        if (g_sched_tick_ctx_budget != 0u) {
                            g_sched_tick_ctx_budget--;
                            kernel_debug_printf("tick-guard: CTX-OUTSIDE cpu=%u pid=%d state=%d ctx=%x stack=[%x,%x) sw=%u tick=%u\n",
                                                (unsigned int)cpu, t->pid,
                                                (int)t->state, (unsigned int)c,
                                                (unsigned int)(uintptr_t)t->stack,
                                                (unsigned int)top,
                                                (unsigned int)t->context_switches,
                                                (unsigned int)tick);
                            kernel_heap_forensic_sweep(t);
                        }
                    }
                }

                if (vrc == 0 || vrc == -5) {
                    if (vrc == -5 && g_sched_tick_guard_budget != 0u) {
                        g_sched_tick_guard_budget--;
                        kernel_debug_printf("tick-guard: STACK-CANARY-BAD cpu=%u pid=%d stack=%x size=%u hdr{size=%x magic=%x next=%x pc=%x} tick=%u\n",
                                            (unsigned int)cpu, t->pid,
                                            (unsigned int)(uintptr_t)t->stack,
                                            (unsigned int)t->stack_size,
                                            (unsigned int)hdr[0], (unsigned int)hdr[1],
                                            (unsigned int)hdr[2], (unsigned int)hdr[3],
                                            (unsigned int)tick);
                        kernel_heap_forensic_sweep(t->stack);
                    }
                    uintptr_t esp0 = kernel_tss_get_kernel_stack(cpu);
                    if (esp0 != 0u && esp0 != top && g_sched_tick_tss_budget != 0u) {
                        g_sched_tick_tss_budget--;
                        kernel_debug_printf("tick-guard: ESP0-MISMATCH cpu=%u pid=%d esp0=%x expected=%x stack=%x size=%u tick=%u\n",
                                            (unsigned int)cpu, t->pid,
                                            (unsigned int)esp0, (unsigned int)top,
                                            (unsigned int)(uintptr_t)t->stack,
                                            (unsigned int)t->stack_size,
                                            (unsigned int)tick);
                    }
                    continue;
                }
                if (vrc == -2) {
                    /* Misaligned / outside-heap payload: the RECORD's stack
                     * field is garbage data, not an overflow — this is what
                     * the first "CANARY-BAD" capture (17/10g) actually was. */
                    if (g_sched_tick_guard_budget != 0u) {
                        g_sched_tick_guard_budget--;
                        kernel_debug_printf("tick-guard: STACK-PTR-NOTBLOCK cpu=%u pid=%d stack=%x size=%u tick=%u\n",
                                            (unsigned int)cpu, t->pid,
                                            (unsigned int)(uintptr_t)t->stack,
                                            (unsigned int)t->stack_size,
                                            (unsigned int)tick);
                        kernel_heap_forensic_sweep(t);
                    }
                    continue;
                }
                if ((vrc == -3 || vrc == -4) && g_sched_tick_guard_budget != 0u) {
                    /* Own header trashed: a downward-exhausting stack smashes
                     * this block's header first, and the block immediately
                     * below is usually the RECORD (stacks are allocated right
                     * after the process_t) — sweep names both headers with
                     * allocpc so the poisoning victim is explicit. */
                    g_sched_tick_guard_budget--;
                    kernel_debug_printf("tick-guard: STACK-HDR-SMASHED cpu=%u pid=%d stack=%x size=%u hdr{size=%x magic=%x next=%x pc=%x} rc=%d tick=%u\n",
                                        (unsigned int)cpu, t->pid,
                                        (unsigned int)(uintptr_t)t->stack,
                                        (unsigned int)t->stack_size,
                                        (unsigned int)hdr[0], (unsigned int)hdr[1],
                                        (unsigned int)hdr[2], (unsigned int)hdr[3],
                                        vrc, (unsigned int)tick);
                    kernel_heap_forensic_sweep(t->stack);
                }
            }
        }

        /* ESP0-ORPHAN: every cpu's TSS esp0 must be 0, a live task's stack
         * top, or an AP boot stack top — anything else and a ring3->ring0
         * on that cpu writes a trap frame into unknown memory. Idle cpus
         * keep the last task's top legitimately; this runs per tick for ALL
         * cpus so the blind spot (esp0 corrupted while g_current==NULL) is
         * covered within one tick. (P4 adendo 41) */
        {
            static volatile uint32_t g_sched_esp0_orphan_budget = 6u;
            uint32_t c;

            for (c = 0u; c < 2u; ++c) {
                uintptr_t e0 = kernel_tss_get_kernel_stack(c);
                const process_t *tt;
                uint32_t guard = 0u;
                int found = 0;

                if (e0 == 0u) {
                    continue;
                }
                /* reachability proof: print ONCE, at the first observation
                 * of a NON-ZERO esp0 (0 proves nothing — adendo 41 lesson) */
                if (g_sched_esp0_orphan_budget == 6u) {
                    g_sched_esp0_orphan_budget = 5u;
                    kernel_debug_printf("tick-guard: esp0-sweep armed cpu=%u e0=%x tick=%u\n",
                                        (unsigned int)c, (unsigned int)e0,
                                        (unsigned int)tick);
                }
                for (tt = g_head; tt != NULL && guard < 256u;
                     tt = tt->next, ++guard) {
                    uintptr_t s0 = (uintptr_t)tt->stack;

                    if (s0 == 0u || tt->stack_size < 1024u ||
                        tt->stack_size > (4u * 1024u * 1024u)) {
                        continue;
                    }
                    /* TERMINATED tasks are NOT skipped here: an idle cpu's
                     * esp0 legitimately holds the last (possibly since
                     * terminated) task's top (lesson adendo 38). */
                    if (e0 == s0 + tt->stack_size) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    uintptr_t lo = 0u, hi = 0u;

                    if (smp_ap_stack_bounds(c, &lo, &hi) == 0 && e0 == hi) {
                        found = 1;
                    }
                }
                if (!found && g_sched_esp0_orphan_budget != 0u) {
                    g_sched_esp0_orphan_budget--;
                    kernel_debug_printf("tick-guard: ESP0-ORPHAN cpu=%u esp0=%x tick=%u\n",
                                        (unsigned int)c, (unsigned int)e0,
                                        (unsigned int)tick);
                }
            }
        }
    }

    /*
     * Budgeted canary sweep (COMPLETION_ACTION_PLAN P4, class #3). The
     * dispatch probe proved frames are INTACT at dispatch time, so the
     * trashing lands during execution — by the time a walk trips over a
     * BAD-NODE the writer is long gone. Sweep every LINKED task's edge
     * canary on a slow cadence: the FIRST violation names the victim pid
     * while its block is still alive, and the physical-predecessor probe
     * (class #2 tooling) attributes the writer. Lock order scheduler->
     * heap is already established by scheduler_node_sane().
     */
    if (g_sched_canary_sweep_budget != 0u &&
        (tick % SCHED_CANARY_SWEEP_PERIOD_TICKS) == 0u) {
        process_t *scan;
        uint32_t scan_steps = 0u;

        g_sched_canary_sweep_budget--;
        {
            uint32_t bad_count = 0u;
            uint32_t total = 0u;

            for (scan = g_head; scan != NULL; scan = scan->next) {
                uintptr_t pred_start = 0u;
                size_t pred_size = 0u;
                uintptr_t pred_allocpc = 0u;
                int pred_cnry = -1;

                if (!scheduler_node_sane(scan)) {
                    break;
                }
                if (++scan_steps > SCHED_LIST_WALK_CAP) {
                    break;
                }
                total++;
                if (kernel_heap_block_canary_ok(scan)) {
                    continue;
                }
                bad_count++;
                /*
                 * Report EVERY corrupted task block (not just the first):
                 * the COUNT distinguishes a targeted write (one victim) from
                 * a broad overwrite (many). Per-victim detail is budgeted so
                 * a wide overwrite does not flood the serial log.
                 */
                if (g_sched_sweep_detail_budget != 0u) {
                    uintptr_t st;
                    uint32_t ssize;
                    uint32_t clean = 0u;

                    g_sched_sweep_detail_budget--;
                    st = (uintptr_t)scan->stack;
                    ssize = scan->stack_size;
                    kernel_debug_printf("sched-scan: BAD-CANARY pid=%d node=0x%x allocpc=0x%x state=%d svc=%d\n",
                                        scan->pid,
                                        (unsigned int)(uintptr_t)scan,
                                        (unsigned int)kernel_heap_block_alloc_pc(scan),
                                        (int)scan->state, (int)scan->service_type);
                    if (st != 0u && ssize >= 256u && ssize <= (1u << 20) &&
                        (ssize & 15u) == 0u &&
                        st + ssize <= kernel_heap_end() &&
                        kernel_heap_is_block((const void *)st)) {
                        const uint8_t *base = (const uint8_t *)st;

                        while (clean < ssize && base[clean] == 0xA5u) {
                            clean++;
                        }
                        kernel_debug_printf("sched-scan:   stack{st=0x%x size=%u clean_bottom=%u}\n",
                                            (unsigned int)st, ssize, clean);
                    } else {
                        kernel_debug_printf("sched-scan:   stack{st=0x%x size=%u UNAVAILABLE}\n",
                                            (unsigned int)st, ssize);
                    }
                    if (kernel_heap_block_predecessor(scan, &pred_start, &pred_size,
                                                      &pred_allocpc, &pred_cnry)) {
                        kernel_debug_printf("sched-scan:   writer-candidate pred{start=0x%x size=%u allocpc=0x%x cnry=%d}\n",
                                            (unsigned int)pred_start,
                                            (unsigned int)pred_size,
                                            (unsigned int)pred_allocpc, pred_cnry);
                    }
                }
            }
            /*
             * Only report when something is actually wrong. The all-clean
             * case fires every SCHED_CANARY_SWEEP_PERIOD_TICKS and used to
             * print unconditionally, flooding the serial log with thousands
             * of identical "bad=0" lines (and drowning the markers the QEMU
             * validation harnesses grep for). A clean sweep is silent.
             */
            if (bad_count != 0u) {
                kernel_debug_printf("sched-scan: EXTENT bad=%u total=%u\n",
                                    bad_count, total);
                g_sched_canary_sweep_budget = 0u;
            }
        }
    }
    for (task = g_head; task != NULL; task = task->next) {
        int inline_cpu;
        int inline_wake;

        /*
         * Guarded walk: this hook runs every tick and DEREFERENCES
         * task->wait_channel to detach waiters. Following a wild pointer
         * here would corrupt unrelated structures (the timeout hook was
         * the first walker to observe the SMP phantom-node corruption in
         * earlier rounds). Cap steps as well, since a cycle would hold the
         * scheduler lock forever.
         */
        if (!scheduler_node_sane(task)) {
            scheduler_list_diag("BAD-NODE-IN-TIMEOUT", task, NULL);
            break;
        }
        if (++steps > SCHED_LIST_WALK_CAP) {
            scheduler_list_diag("LIST-CYCLE-IN-TIMEOUT", task, NULL);
            break;
        }

        /*
         * Ghost-RUNNING repair (COMPLETION_ACTION_PLAN P4, classe #5).
         * Invariant under g_scheduler_lock: a RUNNING task is exactly the
         * g_current of its own CPU (dispatch sets both together; block
         * flips state before anything else can observe). The inline-wake
         * heuristic in this hook can leave a task RUNNING+current_cpu=c
         * after CPU c moved on to another g_current — then find_next (which
         * only considers READY) skips it FOREVER. Observed on real-time
         * smp2: the storage host pinned at 1:1:1 while cpu1 dispatched
         * others, every `service: request timeout svc=2`, boot dead.
         * Detecting that contradiction and repairing to READY converts the
         * silent deadlock into a bounded, named recovery.
         */
        if (task->state == PROCESS_RUNNING) {
            int tcpu = task->current_cpu;
            int ghost = 0;
            if (tcpu >= 0 && tcpu < 32) {
                ghost = (g_current[(uint32_t)tcpu] != task);
            } else {
                ghost = 1; /* RUNNING with no cpu: unreachable by dispatch too */
            }
            if (ghost) {
                if (g_sched_ghost_running_budget != 0u) {
                    g_sched_ghost_running_budget--;
                    kernel_debug_printf("sched-repair: GHOST-RUNNING pid=%d cpu=%d cur=%x\n",
                                        task->pid, tcpu,
                                        (tcpu >= 0 && tcpu < 32)
                                            ? (unsigned int)(uintptr_t)g_current[(uint32_t)tcpu]
                                            : 0u);
                }
                task->state = PROCESS_READY;
                task->current_cpu = -1;
                wake_sleepers = 1u;
            }
            continue;
        }

        if (task->state != PROCESS_BLOCKED || task->wait_channel == 0 || task->wait_deadline == 0u) {
            continue;
        }
        if (tick < task->wait_deadline) {
            continue;
        }

        inline_cpu = task->current_cpu;
        inline_wake = 0;
        if (inline_cpu >= 0 && inline_cpu < 32 &&
            g_current[(uint32_t)inline_cpu] == task) {
            /*
             * A timed wait can expire before the blocked task actually yields.
             * Keep that task RUNNING so the live frame in-flight returns to the
             * caller instead of being treated as a stale READY context.
             */
            inline_wake = 1;
        }
        kernel_waitable_detach_task((kernel_waitable_t *)task->wait_channel, task);
        task->state = inline_wake ? PROCESS_RUNNING : PROCESS_READY;
        if (!inline_wake) {
            task->current_cpu = -1;
            wake_sleepers = 1u;
        }
        scheduler_clear_wait_channel(task);
        task->wait_result = TASK_WAIT_RESULT_TIMED_OUT;
        if (!inline_wake && task->pid > 0 && task->pid < 32) {
            g_tmo_wake_tick[task->pid & 31u] = tick;
            /* item 0.13: a timed-out bounded IPC wait is the client half of
             * a synchronous handshake, and a timed-out bounded SUPERVISION
             * wait is the session host's task-exit poll — both must get the
             * same one-shot wake slice scheduler_complete_wait gives a
             * SIGNALED IPC wake (measured d=304/380 ticks without it). */
            if (task->wait_event_class == TASK_WAIT_CLASS_IPC ||
                task->wait_event_class == TASK_WAIT_CLASS_SUPERVISION ||
                task->wait_event_class == TASK_WAIT_CLASS_INPUT) {
                task->wake_boost_budget =
                    (task->kind == PROCESS_KIND_SERVICE ? 12u : 8u);
            }
        }
        if (woke_count < (sizeof(woke_tasks) / sizeof(woke_tasks[0]))) {
            woke_tasks[woke_count++] = task;
            woke_inline[woke_count - 1u] = (uint8_t)inline_wake;
            woke_snap[woke_count - 1u].pid = task->pid;
            woke_snap[woke_count - 1u].cls = (int)task->wait_event_class;
            woke_snap[woke_count - 1u].evt = (int)task->wait_event_kind;
            woke_snap[woke_count - 1u].owner = (int)task->wait_owner_service;
        }
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);

    if (wake_sleepers != 0u) {
        smp_wake_sleeping_cpus();
    }

    for (uint32_t i = 0u; i < woke_count; ++i) {
        if (g_scheduler_timeout_trace_budget > 0) {
            g_scheduler_timeout_trace_budget -= 1;
            kernel_debug_printf("scheduler: timeout pid=%d class=%d event=%d owner=%d inline=%d\n",
                                woke_snap[i].pid, woke_snap[i].cls,
                                woke_snap[i].evt, woke_snap[i].owner,
                                (int)woke_inline[i]);
        }
        scheduler_publish_task_event(MK_TASK_EVENT_WOKE, woke_tasks[i]);
    }
}

uint32_t scheduler_snapshot(struct task_snapshot_entry *entries,
                            uint32_t max_entries,
                            struct task_snapshot_summary *summary) {
    process_t *task;
    uint32_t count = 0u;
    uint32_t flags;
    uint32_t now_ticks = kernel_timer_get_ticks();

    if (summary != NULL) {
        uint32_t snapshot_cpu = kernel_cpu_index();

        memset(summary, 0, sizeof(*summary));
        summary->abi_version = TASK_SNAPSHOT_ABI_VERSION;
        summary->uptime_ticks = now_ticks;
        summary->cpu_count = kernel_cpu_count();
        summary->current_pid = scheduler_current_pid_for_cpu(snapshot_cpu);
        for (uint32_t cpu = 0u; cpu < summary->cpu_count; ++cpu) {
            const struct kernel_cpu_state *state = kernel_cpu_state(cpu);

            if (state != NULL && state->started != 0u) {
                summary->started_cpu_count += 1u;
            }
        }
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    for (task = g_head; task != NULL; task = task->next) {
        uint32_t runtime_ticks = task->runtime_ticks;

        /* Guarded: this summary walk dereferences task->wait_channel. */
        if (!scheduler_node_sane(task)) {
            scheduler_list_diag("BAD-NODE-IN-SUMMARY", task, NULL);
            break;
        }

        if (task->state == PROCESS_TERMINATED) {
            continue;
        }

        if (task->state == PROCESS_RUNNING && task->last_start_tick != 0u && now_ticks >= task->last_start_tick) {
            runtime_ticks += (now_ticks - task->last_start_tick);
        }

        if (summary != NULL) {
            summary->total_tasks += 1u;
            if (task->state == PROCESS_RUNNING) {
                summary->running_tasks += 1u;
            } else if (task->state == PROCESS_READY) {
                summary->ready_tasks += 1u;
            } else if (task->state == PROCESS_BLOCKED) {
                summary->blocked_tasks += 1u;
            }
            if (task->wait_result == TASK_WAIT_RESULT_TIMED_OUT) {
                summary->timed_out_waits += 1u;
            } else if (task->wait_result == TASK_WAIT_RESULT_CANCELED) {
                summary->canceled_waits += 1u;
            }
            if (task->wait_channel != 0) {
                const kernel_waitable_t *waitable = (const kernel_waitable_t *)task->wait_channel;

                summary->pending_event_signals += waitable->pending_signals;
            }
        }

        if (entries != NULL && count < max_entries) {
            struct task_snapshot_entry *entry = &entries[count];

            memset(entry, 0, sizeof(*entry));
            entry->pid = (uint32_t)task->pid;
            entry->kind = (uint32_t)task->kind;
            entry->state = (uint32_t)task->state;
            entry->current_cpu = task->current_cpu;
            entry->preferred_cpu = task->preferred_cpu;
            entry->last_cpu = task->last_cpu;
            entry->stack_size = task->stack_size;
            entry->runtime_ticks = runtime_ticks;
            entry->context_switches = task->context_switches;
            entry->service_type = task->service_type;
            entry->priority_tier = task->priority_tier;
            entry->task_class = task->task_class;
            entry->wait_result = task->wait_result;
            entry->wait_event_kind = task->wait_event_kind;
            entry->wait_event_class = task->wait_event_class;
            entry->wait_owner_service = task->wait_owner_service;
            entry->wait_deadline = task->wait_deadline;
            entry->wait_pending_signals =
                task->wait_channel != 0 ? ((const kernel_waitable_t *)task->wait_channel)->pending_signals : 0u;
            entry->last_task_event_sequence = task->last_task_event_sequence;
            entry->last_task_event_type = task->last_task_event_type;
            entry->last_task_event_tick = task->last_task_event_tick;
            entry->task_class_pending_events = scheduler_task_class_pending_events(task->task_class);
            entry->task_class_dropped_events = scheduler_task_class_dropped_events(task->task_class);
            count += 1u;
        }
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);

    if (summary != NULL) {
        uint32_t event_flags;

        event_flags = spinlock_lock_irqsave(&g_task_event_lock);
        summary->latest_task_event_sequence = g_task_event_sequence;
        spinlock_unlock_irqrestore(&g_task_event_lock, event_flags);

        for (uint32_t task_class = 1u; task_class < SCHEDULER_TASK_CLASS_STREAM_COUNT; ++task_class) {
            summary->task_class_pending_events += scheduler_task_class_pending_events(task_class);
            summary->task_class_dropped_events += scheduler_task_class_dropped_events(task_class);
        }
    }

    return count;
}
