; kernel32/irq1.asm - IRQ1 (PS/2 keyboard) entry.
; The keyboard driver is poll-based and IRQ1 is masked at the PIC, but
; the gate must still point at real code: a type-0 (invalid) IDT entry
; turns any stray IRQ1 into a fatal #GP during interrupt delivery.
; Handler: ack the PIC and drain whatever byte sits in the 8042 output
; buffer so the line cannot re-trigger.
[bits 32]
[extern irq1_handler]
[global irq1_entry]

section .text
irq1_entry:
    cli
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10          ; kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call irq1_handler
    pop gs
    pop fs
    pop es
    pop ds
    popa
    sti
    iret
