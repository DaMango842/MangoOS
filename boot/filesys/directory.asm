; boot/filesys/directory.asm - Directory lookup
; Assumes: disk_read, memcmp, fs_root_lba, fs_root_sectors, ROOT_DIR_SEG

; ========================
; fs_find - find file by 11-byte 8.3 name
; Input:  DX = pointer to 11-byte name (DS-relative, space-padded, uppercase)
; Output: AX = start LBA, CX = size in sectors, CF = 0 if found
; Clobbers: AX, BX, CX, DX, SI, DI, ES
; ========================
fs_find:
    mov [.name], dx            ; save name pointer (SI clobbered by disk I/O)

    ; ---- read root directory into ROOT_DIR_SEG:0 ----
    mov dx, [fs_root_lba]
    mov cx, [fs_root_sectors]
    mov ax, ROOT_DIR_SEG
    mov es, ax
    xor bx, bx
    mov ax, dx
    call disk_read
    jc .not_found
    mov al, 'R'
    out 0xE9, al

    ; ---- search entries (end marker = name[0]==0) ----
    mov cx, 32
    xor di, di
.search:
    mov ax, ROOT_DIR_SEG       ; ES may be clobbered by BIOS IRQ0 handlers
    mov es, ax
    cmp byte [es:di], 0
    je .not_found
    mov al, 's'
    out 0xE9, al

    push cx
    push di

    mov si, [.name]
    mov di, 0
    mov cx, 11
    call memcmp
    jne .next

    ; ---- found: entry at es:di ----
    pop di
    pop cx
    mov ax, ROOT_DIR_SEG       ; re-establish ES (BIOS IRQ may have clobbered it)
    mov es, ax
    mov ax, [es:di + 14]       ; start_lba (word)
    mov bx, [es:di + 16]       ; size low 16
    mov dx, [es:di + 18]       ; size high 16
    add bx, 511                ; round up to sectors
    adc dx, 0
    mov cx, 9
.shr_loop:
    shr dx, 1
    rcr bx, 1
    loop .shr_loop
    mov cx, bx                 ; sector count
    clc
    jmp .done

.next:
    pop di
    pop cx
    add di, 32
    loop .search

.not_found:
    stc

.done:
    ret
.name dw 0
