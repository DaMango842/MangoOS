// kernel32/idt.c - Interrupt Descriptor Table management.
#include "idt.h"

struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type;
    uint16_t offset_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

void idt_init(void) {
    int i;
    for (i = 0; i < 256; i++) {
        idt[i].offset_lo = 0;
        idt[i].selector = 0x08;
        idt[i].zero = 0;
        idt[i].type = 0;
        idt[i].offset_hi = 0;
    }
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)idt;
}

void idt_load(void) {
    __asm__ volatile("lidt %0" : : "m"(idtp) : "memory");
}

void idt_install(int vec, void *handler, uint8_t type, uint8_t dpl) {
    uint32_t base = (uint32_t)handler;

    /* The access/RPL nibble lives in the low 2 bits of the gate byte.
       They used to be ignored, so a caller asking for DPL 3 silently got
       a DPL 0 gate (privileged) - which is exactly what makes the int 0x80
       syscall entry unreachable once user mode arrives. */
    idt[vec].offset_lo = (uint16_t)(base & 0xFFFFu);
    idt[vec].selector = 0x08;
    idt[vec].zero = 0;
    idt[vec].type = (uint8_t)((type & 0xF0u) | (dpl & 0x03u));
    idt[vec].offset_hi = (uint16_t)(base >> 16);
}