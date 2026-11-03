; boot/stage2.asm - Kernel loader
; Loaded to 0x7E00 by stage1 (position read from the BootMap at LBA1).
; Reads the C kernel as a RAW sector blob (position/size come from the
; BootMap, passed by stage1 at 0x520-0x52B), then enters 32-bit protected
; mode (A20 + GDT) and jumps to it at 0x10000.
; The kernel filesystem is managed exclusively by the kernel itself; this
; loader does not parse any file table.
[org 0x7E00]
[bits 16]

KERNEL_SEG       equ 0x1000  ; Load at 0x1000:0000 = physical 0x10000
KERNEL_ADDR      equ 0x10000 ; Flat linear address of the kernel entry
BOOT_INFO_DRIVE  equ 0x500   ; boot drive byte
BOOT_INFO_FS     equ 0x502   ; dword: kernel FS region start LBA
K_PASS_LBA       equ 0x520   ; dword: kernel LBA (from stage1)
K_PASS_SEC       equ 0x524   ; dword: kernel sector count (from stage1)
K_PASS_FS        equ 0x528   ; dword: fs region start LBA (from stage1)

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov ax, 0x9000
    mov ss, ax
    mov sp, 0xFFFE

    ; ---- report status ----
    mov al, '2'
    out 0xE9, al
    mov si, msg_stage2
    call print_string

    ; ---- recover boot drive (stage1 stored it at 0x7DFE) ----
    mov al, [0x7DFE]
    mov [boot_drive], al

    ; ---- geometry probe for the CHS fallback path ----
    call fs_probe_geometry
    mov al, 'G'
    out 0xE9, al

    ; ---- load the kernel as a raw blob (LBA/sectors from stage1) ----
    mov ax, [K_PASS_LBA]
    mov [kernel_lba], ax
    mov ax, [K_PASS_SEC]
    mov [kernel_sectors], ax

    mov ax, KERNEL_SEG
    mov es, ax
    xor bx, bx
    mov ax, [kernel_lba]
    mov cx, [kernel_sectors]
    call disk_read
    jc .error
    mov al, 'L'
    out 0xE9, al

    ; ---- verify the kernel landed (entry begins with 0xFA = 'cli') ----
    mov ax, KERNEL_SEG
    mov es, ax
    mov al, [es:0]
    cmp al, 0xFA
    je .kern_ok
    mov al, '?'
    out 0xE9, al
    jmp .halt
.kern_ok:
    mov al, 'K'
    out 0xE9, al

    mov si, msg_kern_1
    call print_string

    ; ---- hand boot info to the kernel ----
    mov al, [boot_drive]
    mov [BOOT_INFO_DRIVE], al
    mov eax, [K_PASS_FS]
    mov dword [BOOT_INFO_FS], eax

    ; ---- VBE framebuffer (best effort, real mode) ----
    call vbe_init
    cmp dword [BOOT_INFO_VBE_STATUS], 0
    je .vbe_none
    mov al, 'V'
    out 0xE9, al
    jmp .vbe_done
.vbe_none:
    mov al, 'v'
    out 0xE9, al
.vbe_done:

    ; ---- Enable A20 (fast gate on port 0x92) ----
    mov al, 'P'
    out 0xE9, al
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al

    ; ---- Switch to 32-bit protected mode ----
    mov al, '^'
    out 0xE9, al
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

.error:
    mov si, err_msg
    call print_string
.halt:
    hlt
    jmp .halt

[bits 32]
pm_entry:
    ; Flat data segment (selector 0x10)
    mov al, 'W'
    out 0xE9, al
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Fresh kernel stack (top at 0x90000, grows down)
    mov esp, 0x90000

    ; Jump to the C kernel (linked at KERNEL_ADDR)
    mov al, '>'
    out 0xE9, al
    jmp KERNEL_ADDR

boot_drive db 0
kernel_lba dw 0
kernel_sectors dw 0
err_msg db 'Kernel load failed', 13, 10, 0
msg_stage2 db 'stage2 loaded', 13, 10, 0
msg_kern_1 db 'kernel read ok', 13, 10, 0

; ---- Minimal flat GDT ----
align 4
gdt:
    dq 0                            ; null
    ; code segment
    dw 0xFFFF                       ; limit 15:0
    dw 0x0000                       ; base 15:0
    db 0x00                         ; base 23:16
    db 0x9A                         ; access: P=1, DPL=0, code, exec, read
    db 0xCF                         ; flags: 4KB, 32-bit; limit 19:16 = F
    db 0x00                         ; base 31:24
    ; data segment
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92                         ; access: P=1, DPL=0, data, write
    db 0xCF
    db 0x00

gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1            ; limit (table size - 1)
    dd gdt                          ; base (org-relative = physical, ds = 0)

[bits 16]
%include "boot/printlib.asm"
%include "boot/filesys/disk.asm"
%include "boot/filesys/geometry.asm"
%include "boot/vbe.asm"