; kernel/command.asm - command dispatch layer
; Responsibilities:
;   - cmd_table: name → handler lookup table
;   - cmd_do_*: argument-extraction wrappers (one per built-in)
;   - command name strings

cmd_do_help:
    call cmd_help
    jmp main_loop

cmd_do_cls:
    call cmd_cls
    jmp main_loop

cmd_do_reboot:
    call cmd_reboot

cmd_do_type:
    mov si, input_buffer
    add si, 4
.skip_space:
    lodsb
    or al, al
    jz cmd_missing_param
    cmp al, ' '
    je .skip_space
    dec si
    mov dx, si
    call cmd_type
    jmp main_loop

cmd_do_run:
    mov si, input_buffer
    add si, 4
.skip_space:
    lodsb
    or al, al
    jz cmd_missing_param
    cmp al, ' '
    je .skip_space
    dec si
    mov dx, si
    call run_program
    jc cmd_run_not_found
    jmp main_loop

cmd_missing_param:
    mov si, missing_param_msg
    call print_error
    jmp main_loop

cmd_run_not_found:
    mov si, file_not_found_msg
    call print_error
    jmp main_loop

cmd_do_echo:
    call cmd_echo
    jmp main_loop

cmd_do_ver:
    call cmd_ver
    jmp main_loop

cmd_do_date:
    call cmd_date
    jmp main_loop

cmd_do_time:
    call cmd_time
    jmp main_loop

cmd_do_dir:
    call cmd_dir
    jmp main_loop

cmd_do_disabled:
    call cmd_disabled
    jmp main_loop

cmd_table:
    db 4
    dw help_str, cmd_do_help
    db 3
    dw cls_str, cmd_do_cls
    db 6
    dw reboot_str, cmd_do_reboot
    db 4
    dw type_str, cmd_do_type
    db 4
    dw echo_str, cmd_do_echo
    db 3
    dw ver_str, cmd_do_ver
    db 4
    dw date_str, cmd_do_date
    db 4
    dw time_str, cmd_do_time
    db 3
    dw dir_str, cmd_do_dir
    db 3
    dw run_str, cmd_do_run
    db 5
    dw color_str, cmd_do_color
    db 0  ; end marker

help_str     db 'help', 0
cls_str      db 'cls', 0
reboot_str   db 'reboot', 0
type_str     db 'type', 0
echo_str     db 'echo', 0
ver_str      db 'ver', 0
date_str     db 'date', 0
time_str     db 'time', 0
dir_str      db 'dir', 0
run_str      db 'run', 0
color_str    db 'color', 0
