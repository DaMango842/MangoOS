; boot/stage1.asm - 从 BootMap(LBA1) 动态读取 Stage2
; 磁盘布局由 build_img.ps1 每次构建自动规划:
;   LBA 0 : 本引导扇区 (硬件规定)
;   LBA 1 : BootMap(512B): 'MBOOT' | ver | stage2_lba(u32) | stage2_sectors(u32)
;          | fs_base(u32) | kernel_lba(u32) | kernel_sectors(u32)
;   LBA 2+: stage2, 之后 KERNEL.BIN(裸存放), 之后内核文件系统区域
; BootMap 布局: 'MBOOT'(5B) | ver=2 | loader_ver=1 | stage2_lba(u32) | ...
[org 0x7C00]
[bits 16]

BOOTMAP_LBA   equ 1            ; BootMap 固定在 LBA1 (唯一约定)
BOOTMAP_SEG   equ 0x0300       ; 读入线性 0x3000 -> 段 0x0300
MAP_S2_LBA    equ 7            ; offset stage2_lba (u32)
MAP_S2_SEC    equ 11           ; offset stage2_sectors (u32)
MAP_FS_BASE   equ 15           ; offset fs_base: 内核文件系统区域起始 LBA (u32)
MAP_K_LBA     equ 19           ; offset kernel_lba (u32)
MAP_K_SEC     equ 23           ; offset kernel_sectors (u32)
K_PASS_LBA    equ 0x520        ; 传给 stage2: kernel LBA  (dword)
K_PASS_SEC    equ 0x524        ; 传给 stage2: kernel 扇区数 (dword)
K_PASS_FS     equ 0x528        ; 传给 stage2: fs_base (dword)
DAP_PACK      equ 0x7B00       ; Int13 磁盘地址包 (引导扇区下方, 避开栈区)

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl
    mov [0x7DFE], dl            ; stage2 从 0x7DFE 读回启动驱动器

    ; ---- 读取 BootMap ----
    mov ax, BOOTMAP_SEG
    mov es, ax
    xor bx, bx
    mov ax, BOOTMAP_LBA
    mov cx, 1
    call read_sectors
    jc disk_error

    ; ---- 校验 BootMap 标识 "MBOOT" ----
    mov ax, BOOTMAP_SEG
    mov es, ax
    cmp dword [es:0], 0x4F4F424D   ; little-endian "MBOO"
    jne disk_error
    cmp byte [es:4], 'T'
    jne disk_error

    ; ---- 把内核位置 + FS 区域传给 stage2 (LBA 均 < 64K) ----
    mov ax, [es:MAP_K_LBA]
    mov [K_PASS_LBA], ax
    mov ax, [es:MAP_K_SEC]
    mov [K_PASS_SEC], ax
    mov ax, [es:MAP_FS_BASE]
    mov [K_PASS_FS], ax

    ; ---- 按表读取 stage2 到 0x7E00 ----
    mov dx, [es:MAP_S2_LBA]
    mov cx, [es:MAP_S2_SEC]
    mov ax, 0x07E0
    mov es, ax
    xor bx, bx
    mov ax, dx
    call read_sectors
    jc disk_error

    jmp 0x0000:0x7E00

disk_error:
    mov al, 'E'
    call ser_char
    cli
.hang:
    hlt
    jmp .hang

; ========================
; read_sectors - 统一磁盘读取: AH=42h 为主, 失败改用 CHS (AH=02h) 逐扇区
; Input:  AX = LBA, CX = 扇区数, ES:BX = 目标
; Output: CF = 0 成功 / 1 失败
; Clobbers: AX, BX, CX, DX, SI, ES (调用后需重置)
; ========================
read_sectors:
    mov [.lba], ax
    mov [.cnt], cx
    mov [.seg], es
    mov [.off], bx
    mov [.start_lba], ax
    mov [.start_cnt], cx
    mov [.start_seg], es
    mov [.start_off], bx

    ; ---- AH=42h 主路径 ----
.try42:
    mov cx, [.cnt]
    and cx, cx
    jnz .do42
    jmp .ok

.do42:
    mov dx, cx                 ; chunk = min(cnt, 128)
    cmp dx, 128
    jbe .chunk_ok
    mov dx, 128
.chunk_ok:
    mov [.chunk], dx
    mov al, 'a'
    call ser_char

    push es
    xor ax, ax
    mov es, ax
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
    pop es

    mov si, DAP_PACK
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .chs

    mov dx, [.chunk]
    add [.lba], dx
    sub [.cnt], dx
    mov ax, [.chunk]
    call ser_nib
    mov al, 'h'
    call ser_char
    mov al, [.cnt]
    call ser_nib
    mov al, '.'
    call ser_char

    ; 推进目标 seg:off
    cmp dx, 128
    je .adv_full
    mov ax, dx
    shl ax, 9
    add [.off], ax
    jnc .try42
.adv_full:
    mov ax, [.seg]
    add ax, 0x1000
    mov [.seg], ax
    jmp .try42

    ; ---- CHS 回退路径（几何缺省 63/16） ----
.chs:
    mov al, 'd'
    call ser_char
    mov ax, [.start_lba]
    mov [.lba], ax
    mov ax, [.start_cnt]
    mov [.cnt], ax
    mov ax, [.start_seg]
    mov [.seg], ax
    mov ax, [.start_off]
    mov [.off], ax
    mov word [.spt], 63
    mov word [.heads], 16

.chs_loop:
    mov cx, [.cnt]
    and cx, cx
    jnz .do_chs
    jmp .ok

.do_chs:
    ; LBA -> CHS
    mov ax, [.lba]
    xor dx, dx
    div word [.spt]        ; AX = track, DX = sec-1
    mov cx, dx
    inc cx                 ; CL = 扇区 (1-based)
    mov bx, ax
    xor dx, dx
    mov ax, bx
    div word [.heads]      ; AX = cyl, DX = head
    mov ch, al
    shl ah, 6
    or cl, ah
    mov dh, dl

    mov al, 1
    mov ah, 0x02
    mov dl, [boot_drive]
    mov es, [.seg]
    mov bx, [.off]
    int 0x13
    jc .err
    mov al, 'e'
    call ser_char

    inc word [.lba]
    dec word [.cnt]

    add word [.off], 512
    jnc .chs_loop
    mov ax, [.seg]
    add ax, 0x1000
    mov [.seg], ax
    jmp .chs_loop

.ok:
    clc
    mov al, 'f'
    call ser_char
    jmp .exit
.err:
    stc
    mov al, 'g'
    call ser_char
.exit:
    ret

    ; ---- 内部状态 ----
.lba       dw 0
.cnt       dw 0
.seg       dw 0
.off       dw 0
.chunk     dw 0
.start_lba dw 0
.start_cnt dw 0
.start_seg dw 0
.start_off dw 0
.spt       dw 63
.heads     dw 16

; ========================
; ser_char - QEMU debugcon (端口 0xE9) 单字符调试输出
; ========================
ser_char:
    push ax
    out 0xE9, al
    pop ax
    ret

ser_nib:
    push ax
    and al, 0x0F
    cmp al, 10
    jb .dig
    add al, 'A' - 10
    jmp .out
.dig:
    add al, '0'
.out:
    out 0xE9, al
    pop ax
    ret

boot_drive db 0

times 510 - ($ - $$) db 0
dw 0xAA55