// kernel32/proc/proc.h - MangoOS process management: shared definitions.
// Cooperative scheduler with a preemption-ready layout (time_slice /
// proc_tick are the hooks a future PIT IRQ will drive).
//   proc.c       table, lifecycle, scheduler, stack allocator
//   switch.asm   register context switch
//
// Design notes:
//  - processes share the kernel address space (no MMU); a process is a
//    C function (proc_fn) with one argument
//  - each process gets its own 4 KB kernel stack in extended memory
//    (above the 1.875 MB GUI backbuffer)
//  - proc_scheduler() never returns; it runs on the caller's stack and
//    round-robins RUNNABLE processes, reclaiming zombies
#ifndef MANGO_PROC_H
#define MANGO_PROC_H

#define PROC_MAX          16
#define PROC_NAME_MAX     16

typedef void (*proc_fn)(void *arg);

enum {
    PROC_EMPTY,          /* slot free */
    PROC_RUNNABLE,       /* can be scheduled */
    PROC_BLOCKED,        /* waiting (reserved: sleep etc.) */
    PROC_ZOMBIE          /* finished, awaiting reclamation */
};

/* Saved register frame; esp points at [edi,esi,ebx,ebp,eip] on the
   process stack, eip is the resume target (popped by ret). */
typedef struct {
    unsigned int esp;
    unsigned int eip;
} proc_ctx_t;

typedef struct proc {
    unsigned int pid;
    int state;
    char name[PROC_NAME_MAX];
    proc_fn fn;                    /* entry function */
    void *arg;                     /* argument handed to fn */
    unsigned int stack_top;        /* allocated stack base, 0 = none */
    proc_ctx_t ctx;                /* saved context (not running) */
    int retval;                    /* exit code */
    int time_slice;                /* 0 = cooperative; >0 ticks before
                                      preemption (reserved) */
    int sleep_ticks;               /* blocked until this many ticks
                                      (reserved, driven by proc_tick) */
    void *wchan;                   /* wait channel: non-NULL = blocked on something */
} proc_t;

/* proc.c */
void proc_init(void);
proc_t *proc_create(proc_fn fn, void *arg, const char *name);
int proc_destroy(proc_t *p);
void proc_exit(int code) __attribute__((noreturn));
void proc_yield(void);
proc_t *proc_current(void);
unsigned int proc_pid(const proc_t *p);
int proc_state(const proc_t *p);
const char *proc_name(const proc_t *p);
void proc_scheduler(void) __attribute__((noreturn));

/* Preemption hook: called once per timer tick (IRQ0).
   Decrements running process's time_slice; when it hits zero,
   requests a preemptive reschedule via proc_yield().
   Also decrements sleep_ticks of blocked processes and wakes them. */
void proc_tick(void);

/* Block current process on wait channel wchan (pipe, sleep, etc.).
   Sets state = PROC_BLOCKED, wchan = wchan, clears current, yields. */
void proc_block(void *wchan);

/* Wake a blocked process. Sets state = PROC_RUNNABLE, wchan = NULL. */
void proc_wake(proc_t *p);

/* Sleep for given timer ticks. */
void proc_sleep(int ticks);

#endif /* MANGO_PROC_H */
