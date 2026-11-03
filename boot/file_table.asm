; ============================================================================
; boot/file_table.asm - MangoFS v1.0 磁盘结构模板 (superblock + 根目录)
;
; 注意: 位置不再在此处硬编码! build_img.ps1 是唯一的布局规划器:
;   1. 顺序分配各区块的 LBA(完全动态, 不依赖固定位置)
;   2. 写 BootMap(LBA1): stage2 位置/大小 + fs_super_lba
;   3. patch 本模板中的 superblock 字段与目录项(start_lba / size)
;
; 本模板 = 1536 字节(3 扇区): superblock(512) + 根目录(1024, 64 项 x 32B)
;
; Superblock (512 bytes):
;   +0  char[8]  magic     "MANGOFS"
;   +8  byte     version   1
;   +9  byte     reserved
;   +10 word     root_lba      (由 build 脚本 patch)
;   +12 word     root_sectors
;   +14 word     data_lba      (由 build 脚本 patch)
;   +16 char[8]  label     "MANGO   "
;   +24 dword    total_sectors 32768
;
; 根目录项 (32 bytes):
;   +0  char[8]  name      (8.3, 空格填充, 大写)
;   +8  char[3]  ext
;   +11 byte     attr      (0=文件, 0x10=目录)
;   +12 byte     reserved
;   +13 byte     reserved
;   +14 word     start_lba      (由 build 脚本 patch)
;   +16 dword    size           (由 build 脚本 patch)
;   +20 word     mtime
;   +22 word     mdate
;   +24 dword    reserved
;   +28 dword    reserved
;   name[0]=0 表示目录结束
; ============================================================================

[bits 16]

; ----------------------------- Superblock (LBA 由 build 决定) ---------------------------
db 'MANGOFS'        ; magic (Mango namespace)
db 0x01             ; version
db 0x00             ; reserved
db 0x00             ; reserved (对齐: root_lba 落在 +10)
dw 0x0000           ; root_lba     <- PATCHED (fs_base + 1)
dw 0x0002           ; root_sectors
dw 0x0000           ; data_lba     <- PATCHED (kernel 之后的文件区起点)
db 'MANGO   '       ; +16 volume label
dd 32768            ; +24 total sectors (16 MB)
times 512 - ($ - $$) db 0x00

; -------------------------- 根目录 (LBA 由 build 决定) ------------------------
; --- Entry 0: KERNEL.BIN (start_lba/size 由 build 脚本 patch) ---
db 'KERNEL   '       ; name (8)
db 'BIN'             ; ext (3)
db 0x00              ; attr: file
db 0x00, 0x00        ; reserved
dw 0x0000            ; start_lba     <- PATCHED
dd 0x00000000        ; size          <- PATCHED
dw 0x0000            ; mtime
dw 0x0000            ; mdate
dd 0x00000000        ; reserved
dd 0x00000000        ; reserved

; --- Entry 1: HELLO.BIN (start_lba/size 由 build 脚本 patch) ---
db 'HELLO   '        ; name (8)
db 'BIN'             ; ext (3)
db 0x00              ; attr: file
db 0x00, 0x00        ; reserved
dw 0x0000            ; start_lba     <- PATCHED
dd 0x00000000        ; size          <- PATCHED
dw 0x0000            ; mtime
dw 0x0000            ; mdate
dd 0x00000000        ; reserved
dd 0x00000000        ; reserved

; --- Entry 2: TEST.BIN (start_lba/size 由 build 脚本 patch) ---
db 'TEST    '        ; name (8)
db 'BIN'             ; ext (3)
db 0x00              ; attr: file
db 0x00, 0x00        ; reserved
dw 0x0000            ; start_lba     <- PATCHED
dd 0x00000000        ; size          <- PATCHED
dw 0x0000            ; mtime
dw 0x0000            ; mdate
dd 0x00000000        ; reserved
dd 0x00000000        ; reserved

; --- Entry 3: HELLO.TXT (start_lba/size 由 build 脚本 patch) ---
db 'HELLO   '        ; name (8)
db 'TXT'             ; ext (3)
db 0x00              ; attr: file
db 0x00, 0x00        ; reserved
dw 0x0000            ; start_lba     <- PATCHED
dd 0x00000000        ; size          <- PATCHED
dw 0x0000            ; mtime
dw 0x0000            ; mdate
dd 0x00000000        ; reserved
dd 0x00000000        ; reserved

; --- 目录结束标记 ---
db 0x00
times 1536 - ($ - $$) db 0x00