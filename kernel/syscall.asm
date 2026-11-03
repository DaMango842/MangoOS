; kernel/syscall.asm - Mango ABI system call layer (int 0x40)
; AH = function number (see ABI.md):
;   0x00 exit(AL=code)         0x10 setcolor(AL)
;   0x01 putchar(AL)           0x20 gettime(ES:BX -> 7 bytes)
;   0x02 puts(DS:SI)           0x21 fs(AL sub: 1=dir, 2=read)
;   0x03 newline
;   0x04 getkey -> AX
;   0x05 peekkey -> AX or 0
;
; Handler frame (SS = caller stack, DS/ES = caller segment on entry):
;   sp+0 es, +2 ds, +4 bp, +6 di, +8 si, +10 dx, +12 cx, +14 bx,
;   +16 ax, +18 ip, +20 cs, +22 flags

syscall_handler:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    ; DS/ES here are the caller's (user) segment; the stack holds copies.
    mov bp, sp
    mov ax, [ss:bp + 2]         ; caller DS
    mov bx, [ss:bp + 0]         ; caller ES
    xor cx, cx
    mov ds, cx
    mov es, cx
    mov [saved_user_ds], ax
    mov [saved_user_es], bx

    mov ax, [ss:bp + 16]        ; caller AX
    mov [saved_user_ax], ax

    cmp ah, 0x00
    je sys_exit
    cmp ah, 0x01
    je .putchar
    cmp ah, 0x02
    je .puts
    cmp ah, 0x03
    je .newline
    cmp ah, 0x04
    je .getkey
    cmp ah, 0x05
    je .peekkey
    cmp ah, 0x10
    je .setcolor
    cmp ah, 0x20
    je .gettime
    cmp ah, 0x21
    je .fs
    jmp .done_restore

.putchar:
    mov al, [saved_user_ax]
    call print_char
    jmp .done_restore

.puts:
    mov si, [ss:bp + 8]         ; caller SI
    mov ax, [saved_user_ds]
    mov ds, ax
    call print_string
    xor ax, ax
    mov ds, ax
    jmp .done_restore

.newline:
    call newline
    jmp .done_restore

.getkey:
    mov ah, 0x00
    int 0x16
    mov [ss:bp + 16], ax        ; return key in caller AX
    jmp .done_restore

.peekkey:
    mov ah, 0x01
    int 0x16
    jz .peek_none
    mov [ss:bp + 16], ax
    jmp .done_restore
.peek_none:
    mov word [ss:bp + 16], 0
    jmp .done_restore

.setcolor:
    mov al, [saved_user_ax]
    call set_attr
    jmp .done_restore

.gettime:
    call get_rtc_datetime
    mov di, [ss:bp + 14]        ; caller BX = buffer offset
    mov ax, [saved_user_es]
    mov es, ax                  ; ES = caller segment
    mov si, rtc_sec
    mov cx, 7
    rep movsb
    jmp .done_restore

; ----------------------------
; fs subfunctions
; ----------------------------
.fs:
    mov al, [saved_user_ax]
    cmp al, 0x01
    je .fs_dir
    cmp al, 0x02
    je .fs_read
    jmp .done_restore

.fs_dir:
    ; read file table into FILE_TABLE_BUFFER, copy 8.3 names to caller ES:DI
    mov ax, FILE_TABLE_LBA
    mov cx, 2
    mov bx, FILE_TABLE_BUFFER >> 4
    mov es, bx
    xor bx, bx
    call disk_read
    jc .fs_err

    mov ax, [saved_user_ds]
    mov [syscall_user_tgt], ax
    mov ax, [ss:bp + 6]
    mov [syscall_user_off], ax

    xor ax, ax                  ; entry count
    xor bx, bx                  ; offset in file table
.dir_loop:
    cmp byte [es:bx], 0         ; end marker
    je .dir_done
    push es
    push ax
    mov es, [syscall_user_tgt]
    mov di, [syscall_user_off]
    mov si, FILE_TABLE_BUFFER >> 4
    mov ds, si
    mov si, bx
    mov cx, 11
    rep movsb
    pop ax
    pop es
    xor dx, dx
    mov ds, dx
    add word [syscall_user_off], 11
    inc ax
    add bx, 32
    cmp bx, 32 * MAX_FILE_ENTRIES
    jb .dir_loop
.dir_done:
    mov [ss:bp + 12], ax        ; entry count in caller CX
    and word [ss:bp + 22], 0xFFFE
    jmp .done_restore

.fs_read:
    ; DS:SI = filename (caller), ES:DI = destination (caller)
    mov si, [ss:bp + 8]
    mov ax, [saved_user_ds]
    mov es, ax                  ; ES = caller segment (name source)
    push ds
    xor ax, ax
    mov ds, ax
    mov di, prog_name_buf
    mov cx, 12
.copy_name:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .name_done
    inc si
    inc di
    loop .copy_name
    mov byte [di], 0
.name_done:
    pop ds
    mov dx, prog_name_buf
    call parse_8_3
    mov dx, padded_name
    call fs_find                ; AX = LBA, DX = size
    jc .fs_err
    push dx                     ; keep file size
    mov bx, [ss:bp + 6]         ; caller DI = destination offset
    mov cx, dx
    add cx, 511
    shr cx, 9                   ; sector count
    mov dx, ax                  ; LBA (fs_load wants it in AX)
    mov ax, [saved_user_ds]
    mov es, ax                  ; ES = caller segment (destination)
    mov ax, dx
    call fs_load
    jc .fs_err_pop
    pop dx                      ; file size
    mov [ss:bp + 12], dx        ; size in caller CX
    and word [ss:bp + 22], 0xFFFE
    jmp .done_restore
.fs_err_pop:
    pop dx
.fs_err:
    or word [ss:bp + 22], 0x0001
    jmp .done_restore

.done_restore:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret

; ----------------------------
; sys_exit - exit current program
; Restores the kernel stack saved by run_program and jumps to its
; post-processing code (same path as a normal RETF return).
; ----------------------------
sys_exit:
    cli
    mov ax, [syscall_kernel_ss]
    mov ss, ax
    mov sp, [syscall_kernel_sp]
    sti
    jmp far [syscall_exit_far]

; ----------------------------
; syscall state (kernel segment, ds = 0)
; ----------------------------
saved_user_ax      dw 0
saved_user_ds      dw 0
saved_user_es      dw 0
syscall_kernel_ss  dw 0
syscall_kernel_sp  dw 0
syscall_exit_far   dd 0
syscall_prog_entry dd 0
syscall_user_tgt   dw 0
syscall_user_off   dw 0
prog_name_buf      db 13 dup (0)
