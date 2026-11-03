// kernel32/proc/proc.c - MangoOS process management: process table,
// lifecycle, cooperative scheduler and per-process stack allocation.
// See proc.h for the design notes; switch.asm does the register dance.

#include "proc.h"
#include "../memory.h"

extern void proc_switch(proc_ctx_t *from, const proc_ctx_t *to);

#define PROC_STACK_SIZE   4096u   /* one 4 KB page from the memory manager */

static proc_t procs[PROC_MAX];
static proc_ctx_t sched_ctx;         /* scheduler resume point */
static proc_t *current;              /* process on the CPU, or NULL */
static unsigned int next_pid = 1;
static int last_idx;                 /* round-robin cursor */

/* ---- per-process stack allocation: one 4 KB page each ---- */

static unsigned int proc_stack_alloc(void) {
    return mem_alloc_pages(1);
}

static void proc_stack_free(unsigned int s) {
    if (s)
        mem_free_pages(s, 1);
}

/* ---- table helpers ---- */

static void proc_reclaim(proc_t *p) {
    if (p->state != PROC_EMPTY && p != current) {
        proc_stack_free(p->stack_top);
        p->stack_top = 0;
        p->pid = 0;
        p->state = PROC_EMPTY;
    }
}

/* Entry trampoline: runs the process function, then exits with 0.
   Entered via ret from the initial switch, on the process stack. */
static void proc_wrapper(void) {
    proc_t *p = current;

    if (p && p->fn)
        p->fn(p->arg);
    proc_exit(0);
}

/* ---- public API ---- */

void proc_init(void) {
    int i;

    for (i = 0; i < PROC_MAX; i++) {
        procs[i].pid = 0;
        procs[i].state = PROC_EMPTY;
        procs[i].name[0] = '\0';
        procs[i].fn = 0;
        procs[i].arg = 0;
        procs[i].stack_top = 0;
        procs[i].time_slice = 0;
        procs[i].sleep_ticks = 0;
    }
    current = 0;
    last_idx = -1;
}

proc_t *proc_create(proc_fn fn, void *arg, const char *name) {
    proc_t *p;
    int i;
    unsigned int sp;
    unsigned int base;

    if (!fn)
        return 0;
    for (i = 0; i < PROC_MAX; i++)
        if (procs[i].state == PROC_EMPTY)
            break;
    if (i == PROC_MAX)
        return 0;
    base = proc_stack_alloc();
    if (!base)
        return 0;
    p = &procs[i];
    p->pid = next_pid++;
    p->state = PROC_RUNNABLE;
    p->fn = fn;
    p->arg = arg;
    p->stack_top = base;
    p->retval = 0;
    p->time_slice = 10;              /* default quantum (10 ticks @100Hz) */
    p->sleep_ticks = 0;
    p->wchan = 0;
    for (i = 0; name && name[i] && i < PROC_NAME_MAX - 1; i++)
        p->name[i] = name[i];
    p->name[i] = '\0';

    /* fake a switch frame on the fresh stack:
       5 dummy regs (edi,esi,ebx,ebp,eflags=IF) + wrapper eip = 6 dwords */
    sp = base + PROC_STACK_SIZE;
    sp -= 6 * 4;
    ((unsigned int *)sp)[0] = 0;        /* edi */
    ((unsigned int *)sp)[1] = 0;        /* esi */
    ((unsigned int *)sp)[2] = 0;        /* ebx */
    ((unsigned int *)sp)[3] = 0;        /* ebp */
    ((unsigned int *)sp)[4] = 0x202;    /* eflags with IF=1 (bit 9) */
    ((unsigned int *)sp)[5] = (unsigned int)proc_wrapper;
    p->ctx.esp = sp;
    p->ctx.eip = (unsigned int)proc_wrapper;
    return p;
}

int proc_destroy(proc_t *p) {
    if (!p || p->state == PROC_EMPTY)
        return -1;
    if (p == current)
        return -1;               /* cannot kill the running process */
    p->state = PROC_ZOMBIE;      /* scheduler reclaims it next round */
    return 0;
}

void proc_exit(int code) {
    proc_t *p = current;

    if (p) {
        p->state = PROC_ZOMBIE;
        p->retval = code;
    }
    /* Drop the reference before switching back.  proc_reclaim refuses to
       touch the running process (p != current), so leaving current set
       would defer the reclaim by one round and, if this is the last live
       process, leak the zombie forever.
       The stack is NOT freed here on purpose: proc_switch below is still
       executing on it (it pushes the registers there).  The scheduler
       frees the stack when it reclaims the slot. */
    current = 0;
    /* never returns: back to the scheduler */
    proc_switch(p ? &p->ctx : &sched_ctx, &sched_ctx);
    for (;;)
        ;
}

void proc_yield(void) {
    if (current)
        proc_switch(&current->ctx, &sched_ctx);
}

proc_t *proc_current(void) {
    return current;
}

unsigned int proc_pid(const proc_t *p) {
    return p ? p->pid : 0;
}

int proc_state(const proc_t *p) {
    return p ? p->state : PROC_EMPTY;
}

const char *proc_name(const proc_t *p) {
    return p ? p->name : "";
}

void proc_tick(void) {
    int i;

    /* wake sleepers: decrement sleep_ticks of all PROC_BLOCKED procs */
    for (i = 0; i < PROC_MAX; i++) {
        proc_t *p = &procs[i];
        if (p->state == PROC_BLOCKED && p->sleep_ticks > 0) {
            p->sleep_ticks--;
            if (p->sleep_ticks == 0) {
                p->state = PROC_RUNNABLE;
                p->wchan = 0;
            }
        }
    }

    /* preempt running process if its time slice expired */
    if (current && current->time_slice > 0) {
        current->time_slice--;
        if (current->time_slice == 0) {
            current->time_slice = 10;       /* reset quantum (10 ticks @100Hz = 100ms) */
            /* only preempt if there's another runnable process */
            int runnable = 0;
            for (i = 0; i < PROC_MAX; i++) {
                if (procs[i].state == PROC_RUNNABLE && &procs[i] != current) {
                    runnable = 1;
                    break;
                }
            }
            if (runnable) {
                /* sti before yield so pushf in proc_switch captures IF=1 */
                __asm__ volatile("sti");
                proc_yield();
                __asm__ volatile("cli");
            }
        }
    }
}

/* Block current process on wait channel wchan (pipe, sleep, etc.) */
void proc_block(void *wchan) {
    if (!current)
        return;
    current->state = PROC_BLOCKED;
    current->wchan = wchan;
    current->sleep_ticks = 0;
    proc_t *p = current;
    current = 0;
    proc_switch(&p->ctx, &sched_ctx);
}

/* Wake a blocked process */
void proc_wake(proc_t *p) {
    if (p && p->state == PROC_BLOCKED) {
        p->state = PROC_RUNNABLE;
        p->wchan = 0;
        p->sleep_ticks = 0;
    }
}

/* Sleep for given timer ticks */
void proc_sleep(int ticks) {
    if (!current || ticks <= 0)
        return;
    current->sleep_ticks = ticks;
    proc_block((void *)1);   /* generic sleep channel */
}

/* reset time_slice for new process in proc_create */

void proc_scheduler(void) {
    for (;;) {
        int i;

        /* reclaim zombies left by proc_exit / proc_destroy */
        for (i = 0; i < PROC_MAX; i++)
            if (procs[i].state == PROC_ZOMBIE)
                proc_reclaim(&procs[i]);

        /* advance one RUNNABLE process per round: the scan starts after
           the last picked slot, so every process gets its turn exactly
           once per round (strict round-robin) */
        for (i = 1; i <= PROC_MAX; i++) {
            int idx = (last_idx + i) % PROC_MAX;
            proc_t *p = &procs[idx];

            if (p->state != PROC_RUNNABLE)
                continue;
            last_idx = idx;
            current = p;
            proc_switch(&sched_ctx, &p->ctx);
            /* resumed: the process yielded, slept or exited */
            if (p->state == PROC_ZOMBIE)
                proc_reclaim(p);
            break;              /* next pick comes after this one */
        }
        if (i > PROC_MAX) {
            /* nothing runnable: idle-spin (cooperative mode; a later
               preemptive IRQ could park the CPU here) */
            __asm__ volatile("pause");
        }
    }
}
