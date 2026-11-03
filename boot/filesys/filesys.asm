; boot/filesys/filesys.asm - MangoOS boot file system (public API facade)
; Directory entry format (MangoFS v1.0, current on-disk layout):
;   +0   char[8]  name
;   +8   char[3]  ext
;   +11  byte     attr
;   +12  byte     reserved
;   +13  byte     reserved
;   +14  word     start_lba
;   +16  dword    size
;   +20  word     mtime
;   +22  word     mdate
;   +24  dword    reserved
;   +28  dword    reserved

[bits 16]

; ========================
; Constants
; ========================
SUPER_SEG   equ 0x0300      ; superblock buffer: linear 0x3000
ROOT_DIR_SEG equ 0x0380     ; root directory buffer: linear 0x3800

; ========================
; Module includes
; ========================
%include "boot/filesys/memory.asm"    ; memcpy / memcmp
%include "boot/filesys/geometry.asm"  ; fs_probe_geometry / lba_to_chs
%include "boot/filesys/disk.asm"      ; disk_read
%include "boot/filesys/directory.asm" ; fs_find
%include "boot/filesys/file.asm"      ; fs_load

; ========================
; FS state (filled by fs_mount)
; ========================
fs_super_lba    dw 0
fs_root_lba     dw 0
fs_root_sectors dw 0
fs_data_lba     dw 0

fs_magic db 'MANGOFS'

; ========================
; fs_mount - read + validate the MangoFS superblock
; Input:  AX = superblock LBA (from the BootMap)
; Output: CF = 0 if mounted (fs_root_lba / fs_root_sectors / fs_data_lba filled)
; Clobbers: AX, BX, CX, DX, SI, ES
; ========================
fs_mount:
    mov [fs_super_lba], ax

    ; read superblock into SUPER_SEG:0 (keep LBA alive in DX across ES setup)
    mov dx, ax
    mov cx, 1
    mov ax, SUPER_SEG
    mov es, ax
    xor bx, bx
    mov ax, dx
    call disk_read
    jc .error

    ; validate the "MANGOFS" magic
    mov si, fs_magic
    xor di, di
    mov cx, 7
    call memcmp
    jne .error

    ; superblock fields (byte offsets)
    mov ax, [es:10]              ; root_lba
    mov [fs_root_lba], ax
    mov ax, [es:12]              ; root_sectors
    mov [fs_root_sectors], ax
    mov ax, [es:14]              ; data_lba
    mov [fs_data_lba], ax

    clc
    ret

.error:
    stc
    ret