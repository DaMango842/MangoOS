; boot/printlib.asm - Shared print/libraries for Stage2 and kernel

print_string:
    push ax
.loop:
    lodsb
    or al, al
    jz .done
    call print_char
    jmp .loop
.done:
    pop ax
    ret

print_char:
    push ax
    push bx
    push cx
    push dx
    push es
    push di

    push ax             ; save char
    mov ah, 0x03
    mov bh, 0
    int 0x10            ; DH=row, DL=col
    pop ax              ; AL=char

    cmp al, 13
    je .do_cr
    cmp al, 10
    je .do_lf
    cmp al, 8
    je .do_bs
    cmp al, ' '
    jb .done_pos        ; other control chars: ignore

.plot:
    cmp dl, 80
    jb .plot_ok
    mov dl, 0
    inc dh
    cmp dh, 25
    jb .plot_ok
    call .scroll        ; hit bottom: scroll up, DH=24
    mov dl, 0

.plot_ok:
    push ax             ; save char
    push dx
    mov ax, 80
    mul dh              ; AX = row*80
    mov di, ax
    pop dx
    movzx bx, dl
    add di, bx
    shl di, 1           ; DI = (row*80 + col) * 2
    mov ax, 0xB800
    mov es, ax
    pop ax              ; AL = char
    mov [es:di], al
    mov al, [cur_attr]
    mov [es:di + 1], al ; attr

    ; Advance cursor: col+1, wrap/scroll at line end
    inc dl
    cmp dl, 80
    jb .done_pos
    mov dl, 0
    inc dh
    cmp dh, 25
    jb .done_pos
    call .scroll

.done_pos:
    mov ah, 0x02
    mov bh, 0
    int 0x10

    pop di
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.do_cr:
    mov dl, 0
    jmp .done_pos

.do_lf:
    inc dh
    cmp dh, 25
    jb .done_pos
    call .scroll
    jmp .done_pos

.do_bs:
    cmp dl, 0
    je .done_pos
    dec dl
    jmp .done_pos

.scroll:
    push dx
    mov ah, 0x06
    mov al, 1
    mov bh, [cur_attr]
    mov ch, 0
    mov cl, 0
    mov dh, 24
    mov dl, 79
    int 0x10
    pop dx
    mov dh, 24
    ret

print_hex_byte:
    push ax
    mov ah, al
    shr al, 4
    call print_nibble
    mov al, ah
    and al, 0x0F
    call print_nibble
    pop ax
    ret

print_nibble:
    cmp al, 10
    jl .digit
    add al, 'A' - 10
    jmp .print
.digit:
    add al, '0'
.print:
    call print_char
    ret

newline:
    push ax
    mov al, 13
    call print_char
    mov al, 10
    call print_char
    pop ax
    ret

clear_screen:
    push ax
    push bx
    push cx
    push dx
    mov ax, 0x0700
    mov bh, [cur_attr]
    mov cx, 0
    mov dx, 0x184F
    int 0x10
    mov ah, 0x02
    mov bh, 0
    mov dh, 0
    mov dl, 0
    int 0x10
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ========================
; set_attr - set current display attribute
; Input:  AL = attribute
; ========================
set_attr:
    mov [cur_attr], al
    ret

; ========================
; print_error - print SI string in bright red, then restore
; ========================
print_error:
    mov bl, [default_attr]
    mov al, 0x0C
    mov [cur_attr], al
    call print_string
    mov al, bl
    mov [cur_attr], al
    ret

; ========================
; print_ok - print SI string in bright green, then restore
; ========================
print_ok:
    push ax
    mov al, [default_attr]
    push ax
    mov al, 0x0A
    mov [cur_attr], al
    call print_string
    pop ax
    mov [cur_attr], al
    ret

set_cursor_top_left:
    push ax
    push bx
    push dx
    mov ah, 0x02
    mov bh, 0x00
    mov dh, 0
    mov dl, 0
    int 0x10
    pop dx
    pop bx
    pop ax
    ret

print_dec_word:
    push ax
    push bx
    push cx
    push dx

    mov bx, 10
    mov cx, 0

    cmp ax, 0
    jne .convert
    mov al, '0'
    call print_char
    jmp .done

.convert:
    xor dx, dx
    div bx
    push dx
    inc cx
    cmp ax, 0
    jne .convert

.print_digits:
    pop dx
    add dl, '0'
    mov al, dl
    call print_char
    loop .print_digits

.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

print_hex_word:
    push ax
    xchg al, ah
    call print_hex_byte
    xchg al, ah
    call print_hex_byte
    pop ax
    ret

; ========================
; 数据
; ========================
cur_attr     db 0x07    ; active display attribute
default_attr db 0x07    ; default text attribute (set by color command)
