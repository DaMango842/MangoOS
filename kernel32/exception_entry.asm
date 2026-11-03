; kernel32/exception_entry.asm - CPU exception stubs for IDT.
; Each stub pushes a vector number (and dummy error code if the CPU
; didn't push one), then jumps to the common handler that saves all
; registers and calls exception_handler() in C.
;
; Exceptions 8, 10-14, 17 push an error code from the CPU.
; All others get a dummy 0.
[bits 32]

extern exception_handler

%macro ISR_NOERR 1
[global isr%1]
isr%1:
    push dword 0            ; dummy error code
    push dword %1           ; vector number
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
[global isr%1]
isr%1:
    push dword %1           ; vector number (error code already on stack)
    jmp isr_common_stub
%endmacro

; ---- exceptions WITHOUT error code ----
ISR_NOERR 0         ; #DE  divide error
ISR_NOERR 1         ; #DB  debug
ISR_NOERR 2         ; NMI
ISR_NOERR 3         ; #BP  breakpoint
ISR_NOERR 4         ; #OF  overflow
ISR_NOERR 5         ; #BR  bound range
ISR_NOERR 6         ; #UD  invalid opcode
ISR_NOERR 7         ; #NM  device not available
ISR_NOERR 9         ; coprocessor segment (legacy, no error)
ISR_NOERR 15        ; reserved
ISR_NOERR 16        ; #MF  x87 FPU error
ISR_NOERR 18        ; #MC  machine check
ISR_NOERR 19        ; #XM  SIMD exception
ISR_NOERR 20        ; #VE  virtualization
ISR_NOERR 21        ; #CP  control protection
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; ---- exceptions WITH error code ----
ISR_ERR   8         ; #DF  double fault
ISR_ERR   10        ; #TS  invalid TSS
ISR_ERR   11        ; #NP  segment not present
ISR_ERR   12        ; #SS  stack segment fault
ISR_ERR   13        ; #GP  general protection
ISR_ERR   14        ; #PF  page fault
ISR_ERR   17        ; #AC  alignment check

; ---- common stub ----
section .text
isr_common_stub:
    pusha                   ; eax ecx edx ebx esp ebp esi edi
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10            ; kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                ; pass pointer to exception frame
    call exception_handler
    add esp, 4              ; discard the argument
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8              ; pop vector number + error code
    iret
