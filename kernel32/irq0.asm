; kernel32/irq0.asm - IRQ0 (timer) entry: save all regs, call C handler, restore, iret
[bits 32]
[extern irq0_handler]
[global irq0_entry]

section .text
irq0_entry:
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
    call irq0_handler
    pop gs
    pop fs
    pop es
    pop ds
    popa
    sti
    iret