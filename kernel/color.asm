; kernel/color.asm - color command
; Responsibilities:
;   - cmd_do_color / cmd_color_reset: argument glue
;   - cmd_color: set default attribute by name / hex / reset
;   - parse_hex_attr / hex_digit: hex parsing helpers
;   - color name table and strings

cmd_do_color:
    mov si, input_buffer
    add si, 5
.skip_space:
    lodsb
    or al, al
    jz cmd_color_reset  ; "color" alone → reset
    cmp al, ' '
    je .skip_space
    dec si
    mov dx, si
    call cmd_color
    jmp main_loop

cmd_color_reset:
    mov al, ATTR_DEFAULT
    mov [default_attr], al
    call set_attr
    jmp main_loop

; ========================
; cmd_color - set default text color
; Input:  DX = argument ("reset", color name, or 1-2 hex digits)
; ========================
cmd_color:
    ; "reset" → default
    mov si, dx
    mov di, reset_str
    mov cx, 5
    call strncmp
    jc .try_names
    mov si, dx
    add si, 5
    lodsb
    cmp al, 0
    je .do_reset
    cmp al, ' '
    jne .try_names
.do_reset:
    mov al, ATTR_DEFAULT
    mov [default_attr], al
    call set_attr
    jmp .echo

    ; Match against the 16 color names
.try_names:
    mov si, color_names
    xor bx, bx          ; color index
.next_name:
    movzx cx, byte [si]
    cmp cl, 0
    je .try_hex
    inc si
    mov di, [si]
    add si, 2
    push si
    push bx
    push cx
    mov si, dx
    call strncmp
    pop cx
    pop bx
    pop si
    jc .name_no_match
    mov si, dx
    add si, cx
    lodsb
    cmp al, 0
    je .name_match
    cmp al, ' '
    je .name_match
.name_no_match:
    inc bx
    jmp .next_name
.name_match:
    mov al, bl
    mov [default_attr], al
    call set_attr
    jmp .echo

    ; Try 1-2 hex digits (e.g. "0A", "1F")
.try_hex:
    call parse_hex_attr
    jc .bad
    mov [default_attr], al
    call set_attr
    jmp .echo

.bad:
    mov si, color_usage_msg
    call print_error
    ret

.echo:
    mov si, color_set_msg
    call print_string
    ret

; ========================
; parse_hex_attr - parse 1-2 hex digits into an attribute
; Input:  DX = argument pointer
; Output: AL = attribute, CF=0 on success; CF=1 on error
; ========================
parse_hex_attr:
    mov si, dx
    lodsb
    call hex_digit
    jc .fail
    mov bl, al
    lodsb
    cmp al, 0
    je .one_digit
    cmp al, ' '
    je .one_digit
    call hex_digit
    jc .fail
    shl bl, 4
    add bl, al
    lodsb
    cmp al, 0
    je .two_digits
    cmp al, ' '
    je .two_digits
    jmp .fail
.one_digit:
    mov al, bl
    clc
    ret
.two_digits:
    mov al, bl
    clc
    ret
.fail:
    stc
    ret

; ========================
; hex_digit - convert AL to a nibble
; Output: AL = nibble, CF=0; CF=1 if not a hex digit
; ========================
hex_digit:
    cmp al, '0'
    jb .bad
    cmp al, '9'
    ja .upper
    sub al, '0'
    clc
    ret
.upper:
    or al, 32           ; to lowercase
    cmp al, 'a'
    jb .bad
    cmp al, 'f'
    ja .bad
    sub al, 'a' - 10
    clc
    ret
.bad:
    stc
    ret

color_names:
    db 5
    dw cname_black
    db 4
    dw cname_blue
    db 5
    dw cname_green
    db 4
    dw cname_cyan
    db 3
    dw cname_red
    db 7
    dw cname_magenta
    db 5
    dw cname_brown
    db 10
    dw cname_light_gray
    db 9
    dw cname_dark_gray
    db 10
    dw cname_light_blue
    db 11
    dw cname_light_green
    db 10
    dw cname_light_cyan
    db 9
    dw cname_light_red
    db 13
    dw cname_light_magenta
    db 6
    dw cname_yellow
    db 5
    dw cname_white
    db 0

reset_str           db 'reset', 0
cname_black         db 'black', 0
cname_blue          db 'blue', 0
cname_green         db 'green', 0
cname_cyan          db 'cyan', 0
cname_red           db 'red', 0
cname_magenta       db 'magenta', 0
cname_brown         db 'brown', 0
cname_light_gray    db 'light-gray', 0
cname_dark_gray     db 'dark-gray', 0
cname_light_blue    db 'light-blue', 0
cname_light_green   db 'light-green', 0
cname_light_cyan    db 'light-cyan', 0
cname_light_red     db 'light-red', 0
cname_light_magenta db 'light-magenta', 0
cname_yellow        db 'yellow', 0
cname_white         db 'white', 0

color_set_msg   db 'Color set.', 13, 10, 0
color_usage_msg db 'Usage: color <name | hex | reset>', 13, 10, 0
