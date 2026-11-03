; boot/filesys/disk.asm - INT13 disk read
; Primary:  INT 13h AH=42h (LBA packet / extended read, multi-sector).
; Fallback: AH=02h CHS one sector at a time (geometry from lba_to_chs).
; Assumes: boot_drive, lba_to_chs, sectors_per_track, heads

DAP_PACK    equ 0x7B00      ; 16-byte disk address packet (below boot area, away from BIOS scratch)

; ========================
; disk_read - read sectors
; Input:  AX = start LBA, CX = count
;         ES:BX = destination (seg,off); advanced automatically
; Output: CF = 0 on success, 1 on error
; Clobbers: AX, BX, CX, DX, SI (ES & BX preserved)
; ========================
disk_read:
    mov [.start_lba], ax
    mov [.start_sectors], cx
    mov [.start_seg], es
    mov [.start_off], bx
    mov [.lba], ax
    mov [.sectors], cx
    mov [.seg], es
    mov [.off], bx

; ---------------- AH=42h primary ----------------
.try42:
    mov cx, [.sectors]
    and cx, cx
    jnz .do42
    jmp .done_ok

.do42:
    mov dx, cx          ; chunk = min(sectors, 128)
    cmp dx, 128
    jbe .chunk_ok
    mov dx, 128
.chunk_ok:
    mov [.chunk], dx

    ; build DAP (DS already 0)
    mov word [DAP_PACK + 0], 0x10
    mov word [DAP_PACK + 2], dx
    mov ax, [.off]
    mov word [DAP_PACK + 4], ax
    mov ax, [.seg]
    mov word [DAP_PACK + 6], ax
    mov ax, [.lba]
    mov word [DAP_PACK + 8], ax
    mov word [DAP_PACK + 10], 0
    mov word [DAP_PACK + 12], 0
    mov word [DAP_PACK + 14], 0

    push es
    xor ax, ax
    mov es, ax
    mov si, DAP_PACK
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop es
    jc .fallback_chs

    mov dx, [.chunk]
    add [.lba], dx
    sub [.sectors], dx

    ; ---- advance seg:off by chunk*512 ----
    cmp dx, 128
    je .adv_full
    mov ax, dx
    shl ax, 9
    add [.off], ax
    jnc .try42
    jmp .bump_seg
.adv_full:
.bump_seg:
    mov ax, [.seg]
    add ax, 0x1000
    mov [.seg], ax
    jmp .try42

; ---------------- CHS fallback ----------------
.fallback_chs:
    mov ax, [.start_lba]
    mov [.lba], ax
    mov ax, [.start_sectors]
    mov [.sectors], ax
    mov ax, [.start_seg]
    mov [.seg], ax
    mov ax, [.start_off]
    mov [.off], ax

.chs_loop:
    mov cx, [.sectors]
    and cx, cx
    jnz .do_chs
    jmp .done_ok

.do_chs:
    mov ax, [.lba]
    inc word [.lba]
    call lba_to_chs          ; sets CH, CL, DH

    mov ah, 0x02
    mov al, 1
    mov dl, [boot_drive]
    mov es, [.seg]
    mov bx, [.off]
    int 0x13
    jc .exit

    dec word [.sectors]

    ; advance buffer by 512
    add word [.off], 512
    jnc .chs_loop
    mov ax, [.seg]
    add ax, 0x1000
    mov [.seg], ax
    jmp .chs_loop

.done_ok:
    clc
.exit:
    mov es, [.start_seg]
    mov bx, [.start_off]
    ret

; ---- per-call state ----
.start_lba   dw 0
.start_sectors dw 0
.start_seg   dw 0
.start_off   dw 0
.lba         dw 0
.sectors     dw 0
.seg         dw 0
.off         dw 0
.chunk       dw 0