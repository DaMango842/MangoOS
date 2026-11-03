; kernel/builtin.asm - simple built-in commands
; Responsibilities: cmd_help, cmd_cls, cmd_reboot, cmd_echo, cmd_ver,
;                   cmd_date, cmd_time, cmd_disabled and their message strings

; ========================
; 内置命令实现
; ========================
cmd_help:
    call newline
    mov si, help_text
    call print_string
    ret

cmd_cls:
    call clear_screen
    ret

cmd_reboot:
    call newline
    mov si, reboot_msg
    call print_string
    mov cx, 0xFFFF
.delay:
    loop .delay
    jmp 0xFFFF:0x0000

cmd_echo:
    call newline
    mov si, input_buffer
    add si, 4
.skip_space:
    lodsb
    or al, al
    jz .no_args
    cmp al, ' '
    je .skip_space
    dec si
.print_rest:
    lodsb
    or al, al
    jz .done
    call print_char
    jmp .print_rest
.no_args:
.done:
    call newline
    ret

cmd_ver:
    call newline
    mov si, version_str
    call print_string
    ret

cmd_date:
    call newline
    call get_rtc_datetime

    movzx ax, byte [rtc_cent]
    mov bl, 100
    mul bl
    movzx bx, byte [rtc_year]
    add ax, bx
    call print_dec_word

    mov al, '-'
    call print_char
    mov al, [rtc_mon]
    call print_dec_2
    mov al, '-'
    call print_char
    mov al, [rtc_day]
    call print_dec_2
    call newline
    ret

cmd_time:
    call newline
    call get_rtc_datetime

    mov al, [rtc_hour]
    call print_dec_2
    mov al, ':'
    call print_char
    mov al, [rtc_min]
    call print_dec_2
    mov al, ':'
    call print_char
    mov al, [rtc_sec]
    call print_dec_2
    call newline
    ret

; print_dec_2 - print AL (0-99) as two decimal digits, zero-padded
print_dec_2:
    push ax
    push dx
    xor ah, ah
    mov dl, 10
    div dl
    mov dl, ah
    add al, '0'
    call print_char
    mov al, dl
    add al, '0'
    call print_char
    pop dx
    pop ax
    ret

cmd_disabled:
    call newline
    mov si, command_disabled_msg
    call print_string
    ret

help_text       db 'Built-in commands:', 13, 10
                db '  help    - Show this help', 13, 10
                db '  cls     - Clear screen', 13, 10
                db '  reboot  - Restart system', 13, 10
                db '  type    - Display file content', 13, 10
                db '  run     - Run a program from disk', 13, 10
                db '  echo    - Echo arguments', 13, 10
                db '  color   - Set text color: color <name|hex|reset>', 13, 10
                db '  date    - Show current date (RTC/CMOS)', 13, 10
                db '  time    - Show current time (RTC/CMOS)', 13, 10
                db '  ver     - Show version', 13, 10
                db '  dir     - List files in directory', 13, 10, 0

reboot_msg      db 'Rebooting...', 0
command_disabled_msg db 'Command disabled.', 13, 10, 0

version_str db 'Mango DOS alpha build 0.1', 13, 10, 0
