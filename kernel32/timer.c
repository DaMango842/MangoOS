// kernel32/timer.c - PIC remap, PIT ch0 100 Hz, IRQ0 handler registration.
//
// PIT ch0 is owned by the kernel tick.  Subsystems that need ch0 as a
// free-running microsecond counter (MangoWin stats, the NES emulator)
// call timer_pit_borrow() / timer_pit_return() and must give the channel
// back - see timer.h.
#include "idt.h"
#include "proc/proc.h"
#include "timer.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define PIT_CTRL   0x43
#define PIT_CH0    0x40

extern void irq0_entry(void);   // asm entry in irq0.asm
extern void irq1_entry(void);   // asm entry in irq1.asm

/* Non-zero while PIT ch0 is programmed as a free-running counter, i.e.
   the 100 Hz kernel tick is temporarily suspended. */
static int pit_borrowed;

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ---- PIC init: remap IRQ0-7 -> 0x20-0x27, IRQ8-15 -> 0x28-0x2F ---- */
static void pic_remap(void) {
    /* ICW1: start init, cascaded, need ICW4 */
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    /* ICW2: vector offsets */
    outb(PIC1_DATA, 0x20);      /* master -> 0x20..0x27 */
    outb(PIC2_DATA, 0x28);      /* slave  -> 0x28..0x2F */

    /* ICW3: cascade identity */
    outb(PIC1_DATA, 0x04);      /* slave at IRQ2 */
    outb(PIC2_DATA, 0x02);      /* cascade to master IRQ2 */

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* OCW1: mask all except IRQ0 (timer) and IRQ2 (cascade).
       NOTE: 0xFC (11111100b) unmasked IRQ0 *and* IRQ1 - the PS/2
       keyboard interrupt was left open with no handler installed,
       which killed the machine (#GP on the invalid IDT gate 0x21)
       the moment the 8042 raised a key IRQ.  0xFB is the correct
       mask for "only IRQ0 + cascade". */
    outb(PIC1_DATA, 0xFB);
    outb(PIC2_DATA, 0xFF);      /* mask all slave IRQs */
}

/* ---- PIT ch0: mode 3 (square wave), binary, divisor for ~100 Hz ---- */
static void pit_init_100hz(void) {
    /* divisor = 1193182 / 100 = 11931 = 0x2E9B */
    outb(PIT_CTRL, 0x36);       /* ch0, LSB+MSB, mode 3, binary */
    outb(PIT_CH0, 0x9B);        /* LSB */
    outb(PIT_CH0, 0x2E);        /* MSB */
    pit_borrowed = 0;
}

/* ---- PIT ch0 borrowed as a free-running 16-bit down counter (mode 2).
   Mode 2 wraps every 65536 / 1193182 = 54.9 ms, which is why
   pit_us_since_export() callers must always re-sample well inside that
   window.  While borrowed, the 100 Hz tick is dead: IRQ0 no longer
   fires, so proc_tick() never runs.  That is the whole reason
   timer_pit_return() exists. ---- */
static void pit_init_free_run(void) {
    outb(PIT_CTRL, 0x34);       /* ch0, mode 2, LSB+MSB, binary */
    outb(PIT_CH0, 0xFF);
    outb(PIT_CH0, 0xFF);
    pit_borrowed = 1;
}

void timer_pit_borrow(void) {
    if (pit_borrowed)
        return;
    pit_init_free_run();
}

void timer_pit_return(void) {
    if (!pit_borrowed)
        return;
    pit_init_100hz();
}

int timer_pit_is_borrowed(void) {
    return pit_borrowed;
}

unsigned int pit_now_export(void) {
    unsigned char lo, hi;

    outb(PIT_CTRL, 0x00);       /* latch channel 0 count */
    lo = inb(PIT_CH0);
    hi = inb(PIT_CH0);
    return (unsigned int)((hi << 8) | lo);
}

unsigned long pit_us_since_export(unsigned int since) {
    unsigned int now = pit_now_export();
    unsigned int ticks = (unsigned int)(since - now) & 0xFFFFu;

    /* us = ticks * 1e6 / 1193182.  The naive product overflows 32 bits
       above ~4.3 ms and wrapped to garbage, making every PIT-based
       measurement above that (frame times, fps) silently wrong.
       10476/12500 = 0.83808 vs 0.838095 (exact ratio): sub-us error.
       Split into quotient/remainder so no intermediate exceeds 32 bits. */
    return (ticks / 12500ul) * 10476ul +
           (ticks % 12500ul) * 10476ul / 12500ul;
}

/* send EOI to master PIC */
static inline void pic_eoi(void) {
    outb(PIC1_CMD, 0x20);
}

/* ---- C handler called from irq0_entry.asm ---- */
void irq0_handler(void) {
    pic_eoi();
    proc_tick();                  /* wake sleepers, charge the time slice */
}

/* public: called from kernel_main after syscall_init */
void timer_init(void) {
    pic_remap();
    pit_init_100hz();
    idt_install(0x20, irq0_entry, IDT_TYPE_INT_GATE_32, 0);
    /* IRQ1 (keyboard): masked at the PIC because the driver polls,
       but install a real gate anyway - an invalid (type 0) gate
       turns any stray IRQ into a fatal #GP. */
    idt_install(0x21, irq1_entry, IDT_TYPE_INT_GATE_32, 0);
    idt_load();                              /* reload IDT with IRQ0 entry */
}

/* port I/O helpers */
