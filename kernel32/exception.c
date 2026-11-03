// kernel32/exception.c - CPU exception handler: prints fault info on screen
// and halts.  Called from exception_entry.asm with a pointer to the
// full register frame + error code + vector number on the stack.
//
// Frame layout (pushed by isr_common_stub, low to high):
//   [esp+0]  gs       [esp+4]  fs       [esp+8]  es
//   [esp+12] ds       [esp+16] edi      [esp+20] esi
//   [esp+24] ebp      [esp+28] esp      [esp+32] ebx
//   [esp+36] edx      [esp+40] ecx      [esp+44] eax
//   [esp+48] vector   [esp+52] errcode

#include "serial.h"
#include "idt.h"
#include <stdint.h>

extern const char *proc_name(const void *p);
extern void *proc_current(void);

struct exc_frame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_orig;
    uint32_t ebx, edx, ecx, eax;
    uint32_t vector, errcode;
    uint32_t eip, cs, eflags;
};

static const char *exc_names[] = {
    "#DE DivErr",    "#DB Debug",     "NMI",           "#BP Break",
    "#OF Overflow",  "#BR Bound",     "#UD BadOpc",    "#NM NoDev",
    "#DF DblFlt",    " 9  CPE",       "#TS BadTSS",    "#NP NoSeg",
    "#SS BadStk",    "#GP GenPF",     "#PF PageFlt",   "15 Reserved",
    "#MF FPU",       "#AC Align",     "#MC Mach",      "#XM SIMD",
    "#VE Virt",      "#CP CtrlProt",  "22 Unknown",    "23 Unknown",
    "24 Unknown",    "25 Unknown",    "26 Unknown",    "27 Unknown",
    "28 Unknown",    "29 Unknown",    "30 Unknown",    "31 Unknown",
};

#define VGA_ADDR ((volatile uint16_t *)0xB8000)
#define VGA_W 80
#define VGA_H 25

static int scr_x, scr_y;

static void kprint_char(char c, int fg, int bg) {
    volatile uint16_t *vga = VGA_ADDR;
    if (c == '\n') {
        scr_x = 0;
        scr_y++;
        if (scr_y >= VGA_H) scr_y = 0;
        return;
    }
    if (scr_x >= VGA_W) { scr_x = 0; scr_y++; }
    if (scr_y >= VGA_H) scr_y = 0;
    vga[scr_y * VGA_W + scr_x] = (uint16_t)((uint16_t)(bg << 12 | fg << 8) | (uint8_t)c);
    scr_x++;
}

static void kprint_str(const char *s, int fg, int bg) {
    while (*s) kprint_char(*s++, fg, bg);
}

static void kprint_hex(uint32_t v, int fg, int bg) {
    int i;
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (i = 9; i >= 2; i--) {
        unsigned int nib = v & 0xF;
        buf[i] = (nib < 10) ? '0' + nib : 'a' + nib - 10;
        v >>= 4;
    }
    buf[10] = '\0';
    kprint_str(buf, fg, bg);
}

static void kprint_reg(const char *name, uint32_t val, int fg, int bg) {
    kprint_str(name, fg, bg);
    kprint_hex(val, fg, bg);
    kprint_char(' ', fg, bg);
}

static void clear_screen(void) {
    volatile uint16_t *vga = VGA_ADDR;
    int i;
    for (i = 0; i < VGA_W * VGA_H; i++)
        vga[i] = 0x0700;      /* black on white, space */
    scr_x = 0;
    scr_y = 0;
}

/* mirror the fault dump to the serial port: the VGA screen alone is
   invisible in headless/probe runs */
static void ser_hex8(char *p, uint32_t v) {
    static const char hexd[] = "0123456789ABCDEF";
    int i;
    for (i = 7; i >= 0; i--) {
        p[i] = hexd[v & 0xF];
        v >>= 4;
    }
}

static void serial_dump(const struct exc_frame *f) {
    char b[128];
    int i = 0;
    const char *t = "EXC vec=";

    while (*t)
        b[i++] = *t++;
    ser_hex8(b + i, f->vector); i += 8;
    t = " err=";
    while (*t)
        b[i++] = *t++;
    ser_hex8(b + i, f->errcode); i += 8;
    t = " eip=";
    while (*t)
        b[i++] = *t++;
    ser_hex8(b + i, f->eip); i += 8;
    t = " cs=";
    while (*t)
        b[i++] = *t++;
    ser_hex8(b + i, f->cs); i += 8;
    t = " esp=";
    while (*t)
        b[i++] = *t++;
    ser_hex8(b + i, f->esp_orig); i += 8;
    b[i++] = '\r';
    b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

/* second line: who faulted, and is the IDT still intact?
   A corrupted IDT entry makes the CPU load a garbage selector while
   delivering an interrupt (#GP with the EXT bit set in the error code). */
struct idt_gate_dump {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type;
    uint16_t offset_hi;
};

static void hex8_into(char *p, unsigned int v) {
    static const char hexd[] = "0123456789ABCDEF";
    int k;

    for (k = 7; k >= 0; k--) {
        p[k] = hexd[v & 0xF];
        v >>= 4;
    }
}

static void dump_gate(char *b, int *i, const char *label,
                      const struct idt_gate_dump *g) {
    const char *t = label;
    unsigned int off = ((unsigned int)g->offset_hi << 16) | g->offset_lo;

    while (*t)
        b[(*i)++] = *t++;
    hex8_into(b + *i, (unsigned int)g->selector);
    *i += 8;
    t = " off=";
    while (*t)
        b[(*i)++] = *t++;
    hex8_into(b + *i, off);
    *i += 8;
}

static void serial_dump2(void) {
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr;
    char b[224];
    int i = 0;
    const char *t;
    const struct idt_gate_dump *gates;
    const void *cur = proc_current();

    __asm__ volatile("sidt %0" : "=m"(idtr));
    gates = (const struct idt_gate_dump *)idtr.base;

    t = "EXC2 proc=";
    while (*t)
        b[i++] = *t++;
    t = proc_name(cur);
    while (*t)
        b[i++] = *t++;
    t = " idtbase=";
    while (*t)
        b[i++] = *t++;
    hex8_into(b + i, idtr.base);
    i += 8;

    /* timer gate: prime suspect for an EXT-bit #GP during delivery */
    dump_gate(b, &i, " vec20 sel=", gates + 0x20);
    /* the gate that delivered this very fault */
    dump_gate(b, &i, " vec13 sel=", gates + 13);

    b[i++] = '\r';
    b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

void exception_handler(struct exc_frame *f) {
    uint32_t vec = f->vector;

    serial_dump(f);
    serial_dump2();

    /* clear screen and print header */
    clear_screen();
    kprint_str("== MangoOS CPU Exception ==\n\n", 0x0F, 0x04);  /* white on red */

    /* exception name */
    if (vec < 32)
        kprint_str(exc_names[vec], 0x0F, 0x04);
    else
        kprint_str("Unknown", 0x0F, 0x04);
    kprint_str("  Vector=", 0x07, 0x00);
    kprint_hex(vec, 0x0F, 0x00);
    kprint_char('\n', 0x07, 0x00);

    /* error code */
    kprint_str("Error Code: ", 0x07, 0x00);
    kprint_hex(f->errcode, 0x0A, 0x00);
    kprint_char('\n', 0x07, 0x00);

    /* page fault info (PF) */
    if (vec == 14) {
        kprint_str("  PF present=", 0x07, 0x00);
        kprint_char((f->errcode & 1) ? 'Y' : 'N', 0x0A, 0x00);
        kprint_str("  write=", 0x07, 0x00);
        kprint_char((f->errcode & 2) ? 'Y' : 'N', 0x0A, 0x00);
        kprint_str("  user=", 0x07, 0x00);
        kprint_char((f->errcode & 4) ? 'Y' : 'N', 0x0A, 0x00);
        kprint_str("  reserved=", 0x07, 0x00);
        kprint_char((f->errcode & 8) ? 'Y' : 'N', 0x0A, 0x00);
        kprint_char('\n', 0x07, 0x00);
    }

    kprint_char('\n', 0x07, 0x00);

    /* register dump */
    kprint_reg("EAX=", f->eax, 0x0B, 0x00);
    kprint_reg("EBX=", f->ebx, 0x0B, 0x00);
    kprint_reg("ECX=", f->ecx, 0x0B, 0x00);
    kprint_reg("EDX=", f->edx, 0x0B, 0x00);
    kprint_char('\n', 0x07, 0x00);
    kprint_reg("ESI=", f->esi, 0x0B, 0x00);
    kprint_reg("EDI=", f->edi, 0x0B, 0x00);
    kprint_reg("EBP=", f->ebp, 0x0B, 0x00);
    kprint_reg("ESP=", f->esp_orig, 0x0B, 0x00);
    kprint_char('\n', 0x07, 0x00);

    kprint_char('\n', 0x07, 0x00);

    kprint_reg("EIP=", f->eip, 0x0E, 0x00);
    kprint_reg("CS=",  f->cs,  0x0E, 0x00);
    kprint_char('\n', 0x07, 0x00);
    kprint_reg("EFLAGS=", f->eflags, 0x0E, 0x00);
    kprint_char('\n', 0x07, 0x00);

    kprint_char('\n', 0x07, 0x00);

    kprint_reg("DS=", f->ds, 0x0D, 0x00);
    kprint_reg("ES=", f->es, 0x0D, 0x00);
    kprint_reg("FS=", f->fs, 0x0D, 0x00);
    kprint_reg("GS=", f->gs, 0x0D, 0x00);
    kprint_char('\n', 0x07, 0x00);

    /* halt */
    kprint_str("\nSYSTEM HALTED.", 0x0F, 0x04);
    for (;;)
        __asm__ volatile("cli; hlt");
}

/* ---- init: install all 32 exception vectors into the IDT ---- */

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

void exception_init(void) {
    idt_init();

    idt_install( 0, isr0,  0x8E, 0); idt_install( 1, isr1,  0x8E, 0);
    idt_install( 2, isr2,  0x8E, 0); idt_install( 3, isr3,  0x8E, 0);
    idt_install( 4, isr4,  0x8E, 0); idt_install( 5, isr5,  0x8E, 0);
    idt_install( 6, isr6,  0x8E, 0); idt_install( 7, isr7,  0x8E, 0);
    idt_install( 8, isr8,  0x8E, 0); idt_install( 9, isr9,  0x8E, 0);
    idt_install(10, isr10, 0x8E, 0); idt_install(11, isr11, 0x8E, 0);
    idt_install(12, isr12, 0x8E, 0); idt_install(13, isr13, 0x8E, 0);
    idt_install(14, isr14, 0x8E, 0); idt_install(15, isr15, 0x8E, 0);
    idt_install(16, isr16, 0x8E, 0); idt_install(17, isr17, 0x8E, 0);
    idt_install(18, isr18, 0x8E, 0); idt_install(19, isr19, 0x8E, 0);
    idt_install(20, isr20, 0x8E, 0); idt_install(21, isr21, 0x8E, 0);
    idt_install(22, isr22, 0x8E, 0); idt_install(23, isr23, 0x8E, 0);
    idt_install(24, isr24, 0x8E, 0); idt_install(25, isr25, 0x8E, 0);
    idt_install(26, isr26, 0x8E, 0); idt_install(27, isr27, 0x8E, 0);
    idt_install(28, isr28, 0x8E, 0); idt_install(29, isr29, 0x8E, 0);
    idt_install(30, isr30, 0x8E, 0); idt_install(31, isr31, 0x8E, 0);

    idt_load();
}
