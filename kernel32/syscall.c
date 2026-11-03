// kernel32/syscall.c - int 0x80 system call dispatch using idt.c.
#include "syscall.h"
#include "idt.h"
#include "proc/proc.h"
#include "vga.h"
#include "keyboard.h"
#include "time.h"
#include "serial.h"
#include "random.h"

extern void syscall_entry(void);     /* asm stub in syscall_entry.asm */

/* NOTE: the IDT itself lives in idt.c.  This file used to keep a private
   256-entry copy plus its own IDTR that nothing ever loaded - ~2 KB of dead
   BSS that also diverged from the real table whenever a gate was changed. */

/* ---- services ---- */

static int sv_exit(struct syscall_frame *f) {
    proc_exit((int)f->ebx);
    return 0;                       /* never reached */
}

static int sv_yield(struct syscall_frame *f) {
    (void)f;
    proc_yield();
    return 0;
}

static int sv_putchar(struct syscall_frame *f) {
    vga_putc((char)f->ebx);
    return 0;
}

static int sv_puts(struct syscall_frame *f) {
    vga_write((const char *)f->ebx);
    return 0;
}

static int sv_getkey(struct syscall_frame *f) {
    (void)f;
    return kbd_get_key();
}

static int sv_time(struct syscall_frame *f) {
    return time_str((char *)f->ebx);
}

static int sv_random(struct syscall_frame *f) {
    (void)f;
    return (int)random_next();
}

static int sv_range(struct syscall_frame *f) {
    return random_range((int)f->ebx, (int)f->ecx);
}

static int sv_sputc(struct syscall_frame *f) {
    serial_putc((char)f->ebx);
    return 0;
}

static int sv_sputs(struct syscall_frame *f) {
    serial_write((const char *)f->ebx);
    return 0;
}

typedef int (*syscall_fn)(struct syscall_frame *);

static const syscall_fn syscall_table[SYS_COUNT] = {
    sv_exit, sv_yield, sv_putchar, sv_puts, sv_getkey,
    sv_time, sv_random, sv_range, sv_sputc, sv_sputs,
};

int syscall_dispatch(struct syscall_frame *f) {
    unsigned int nr = f->eax;

    if (nr >= SYS_COUNT)
        return -1;
    return syscall_table[nr](f);
}

void syscall_init(void) {
    /* IDT already initialized in kernel_main; just ensure syscall gate is present */
    idt_install(0x80, syscall_entry, IDT_TYPE_SYSCALL_GATE, 3);
    idt_load();
}