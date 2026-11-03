; kernel32/syscall.asm - int 0x80 entry: saves the caller registers, calls
; syscall_dispatch(struct syscall_frame *) and restores them.  The frame
; layout must match struct syscall_frame in syscall.h.
[bits 32]

[extern syscall_dispatch]
[global syscall_entry]

section .text
syscall_entry:
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax
    push ds
    push es
    mov ax, 0x10            ; kernel data segment (no-op at ring 0)
    mov ds, ax
    mov es, ax
    push esp
    call syscall_dispatch
    add esp, 4
    pop es
    pop ds
    add esp, 4              ; drop the saved eax; the return value is in eax
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp
    iret