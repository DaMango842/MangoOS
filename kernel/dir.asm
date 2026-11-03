; kernel/dir.asm - directory listing
; Responsibilities:
;   - cmd_dir: read the file table and list all entries
;   - print_filename: print a directory entry with per-extension color

; ========================
; cmd_dir
; ========================
cmd_dir:
    call newline

    mov ax, FILE_TABLE_BUFFER >> 4
    mov es, ax
    xor bx, bx

    mov ax, FILE_TABLE_LBA
    call lba_to_chs
    mov ah, 0x02
    mov al, 2               ; read 2 sectors
    mov dl, [boot_drive]
    int 0x13
    jc .read_error
    cmp al, 2
    jne .read_error

    ; Re-set ES after INT 13h (BIOS may clobber ES)
    mov ax, FILE_TABLE_BUFFER >> 4
    mov es, ax
    xor bx, bx

    mov cx, MAX_FILE_ENTRIES
    mov di, 0
    mov bx, 0

.loop:
    cmp byte [es:di], 0
    je .done
    call print_filename
    mov ax, [es:di + 18]
    call print_dec_word
    mov al, 'B'
    call print_char
    call newline
    inc bx
    add di, 32
    loop .loop

.done:
    mov al, 39
    call print_char
    mov ax, bx
    call print_dec_word
    mov al, 39
    call print_char
    mov si, files_found_msg
    call print_string
    ret

.read_error:
    mov si, read_error_msg
    call print_error
    ret

; ========================
; print_filename
; ========================
print_filename:
    push ax
    push cx
    push si

    ; Pick color by extension: .TXT yellow, .BIN green, else default
    cmp byte [es:di + 8], 'T'
    jne .check_bin
    cmp byte [es:di + 9], 'X'
    jne .check_bin
    cmp byte [es:di + 10], 'T'
    jne .check_bin
    mov al, ATTR_YELLOW
    jmp .set_attr
.check_bin:
    cmp byte [es:di + 8], 'B'
    jne .default_attr
    cmp byte [es:di + 9], 'I'
    jne .default_attr
    cmp byte [es:di + 10], 'N'
    jne .default_attr
    mov al, ATTR_GREEN
    jmp .set_attr
.default_attr:
    mov al, [default_attr]
.set_attr:
    call set_attr

    mov si, di
    mov cx, 8
    xor ah, ah

.print_base:
    cmp cx, 0
    je .end_base
    mov al, [es:si]
    cmp al, ' '
    je .end_base
    call print_char
    inc si
    dec cx
    inc ah
    jmp .print_base

.end_base:
    mov al, '.'
    call print_char
    inc ah

    mov cx, 3
    mov si, di
    add si, 8
.print_ext:
    cmp cx, 0
    je .end_ext
    mov al, [es:si]
    cmp al, ' '
    je .end_ext
    call print_char
    inc si
    dec cx
    inc ah
    jmp .print_ext

.end_ext:
    cmp ah, 16
    jae .done
.pad:
    mov al, ' '
    call print_char
    inc ah
    cmp ah, 16
    jb .pad
.done:
    mov al, [default_attr]
    call set_attr
    pop si
    pop cx
    pop ax
    ret
