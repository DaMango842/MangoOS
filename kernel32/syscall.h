// kernel32/syscall.h - MangoOS system call ABI (int 0x80, ring-0 entry).
//   syscall.c    IDT setup + dispatch table
//   syscall.asm  interrupt stub (saves/restores the caller registers)
//
// ABI: eax = number, ebx/ecx/edx = up to three arguments, the return
// value comes back in eax.  The gate is DPL 3 so ring-3 processes can
// use it unchanged once user mode exists.
#ifndef MANGO_SYSCALL_H
#define MANGO_SYSCALL_H

enum {
    SYS_EXIT,          /* ebx = exit code; never returns */
    SYS_YIELD,         /* yield to the scheduler */
    SYS_PUTCHAR,       /* ebx = character (VGA text) */
    SYS_PUTS,          /* ebx = NUL-terminated string (VGA text) */
    SYS_GETKEY,        /* returns ASCII key, 0 when none */
    SYS_TIME,          /* ebx = char[20] buffer; returns length, 0 on fail */
    SYS_RANDOM,        /* returns a 32-bit pseudo-random value */
    SYS_RANGE,         /* ebx = lo, ecx = hi; returns value in [lo, hi] */
    SYS_SPUTC,         /* ebx = character (serial COM1) */
    SYS_SPUTS,         /* ebx = NUL-terminated string (serial COM1) */
    SYS_COUNT
};

/* Registers pushed by the int 0x80 stub, top of stack first.  The layout
   must match syscall.asm. */
struct syscall_frame {
    unsigned int es;
    unsigned int ds;
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;
    unsigned int esi;
    unsigned int edi;
    unsigned int ebp;
};

/* Call a system call from ring 0; returns the syscall result. */
static inline int syscall(int nr, unsigned int a, unsigned int b,
                          unsigned int c) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"((unsigned int)nr), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return ret;
}

/* syscall.c */
void syscall_init(void);
int syscall_dispatch(struct syscall_frame *f);

#endif /* MANGO_SYSCALL_H */