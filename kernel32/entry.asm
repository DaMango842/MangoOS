; kernel32/entry.asm - 32-bit protected mode kernel entry
; Loaded at physical 0x10000 by stage2 (flat binary, linked at 0x10000)
[bits 32]

[extern kernel_main]
[extern __bss_start]
[extern __bss_end]
[global _start]

section .text
_start:
    cli

    ; ---- boot debounce marker (0xE9) ----
    mov al, 'k'
    out 0xE9, al

    ; ---- Zero-fill BSS ----
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; ---- Set up the kernel stack ----
    mov esp, 0x90000
    mov ebp, 0

    ; ---- Call the C kernel ----
    mov al, 'K'
    out 0xE9, al
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt
