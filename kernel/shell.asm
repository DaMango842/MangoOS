; kernel/shell.asm - shell input layer
; Responsibilities:
;   - read_line: read a line of keyboard input into input_buffer
;   - strncmp / cmd_match_exact: string compare helpers used by dispatch
;   - input_buffer / input_index data

; ========================
; read_line
; ========================
read_line:
    push ax
    push si
    mov byte [input_index], 0

.read_char:
    mov ah, 0x00
    int 0x16

    cmp al, 13
    je .done

    cmp al, 8
    je .backspace

    cmp al, ' '
    jb .read_char
    cmp al, 126
    ja .read_char

    mov si, input_index
    cmp byte [si], 31
    jge .read_char

    mov di, input_buffer
    movzx bx, byte [si]
    add di, bx
    mov [di], al
    inc byte [input_index]

    call print_char
    jmp .read_char

.backspace:
    mov si, input_index
    cmp byte [si], 0
    je .read_char
    dec byte [si]

    mov al, 8
    call print_char
    mov al, ' '
    call print_char
    mov al, 8
    call print_char
    jmp .read_char

.done:
    mov si, input_index
    mov di, input_buffer
    movzx bx, byte [si]
    add di, bx
    mov byte [di], 0

    call newline
    pop si
    pop ax
    ret

; strncmp - compare SI and DI for CX bytes
; CF=0 if equal, CF=1 if not
strncmp:
    push ax
    push si
    push di
.loop:
    cmp cx, 0
    je .equal
    lodsb
    mov ah, [di]
    inc di
    cmp al, ah
    jne .not_equal
    dec cx
    jmp .loop
.equal:
    clc
    jmp .done
.not_equal:
    stc
.done:
    pop di
    pop si
    pop ax
    ret

; cmd_match_exact
cmd_match_exact:
    push bx
    mov bx, cx
    call strncmp
    jc .no
    mov si, input_buffer
    add si, bx
    lodsb
    cmp al, 0
    je .yes
    cmp al, ' '
    je .yes
.no:
    stc
    jmp .done_cmd
.yes:
    clc
.done_cmd:
    pop bx
    ret

input_buffer:
    times 32 db 0
input_index     db 0
