#ifndef MANGO_IDT_H
#define MANGO_IDT_H

#include <stdint.h>

/* IDT gate types (bits 7..4: present / DPL is supplied separately) */
#define IDT_TYPE_INT_GATE_32  0x8E   /* present, 32-bit interrupt gate */
#define IDT_TYPE_TRAP_GATE_32 0x8F   /* present, 32-bit trap gate */
#define IDT_TYPE_SYSCALL_GATE 0xEE   /* present, 32-bit interrupt gate */

void idt_init(void);
void idt_load(void);

/* Install a gate.  `type` is the access byte with its low RPL bits
   ignored; `dpl` (0..3) is merged into it, so a ring-3 syscall gate is
   idt_install(0x80, h, IDT_TYPE_SYSCALL_GATE, 3). */
void idt_install(int vec, void *handler, uint8_t type, uint8_t dpl);

#endif