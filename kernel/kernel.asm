; kernel/kernel.asm - MangoOS kernel
; Loaded to 0x1000 by Stage2. Entry point, shell main loop and dispatch.
;
; Module layout (one responsibility per file):
;   boot/filesys/...    - file system layer (disk/geometry/directory/file)
;   boot/printlib.asm   - print / display library
;   kernel/shell.asm    - keyboard input, input buffer, string compare
;   kernel/fs.asm       - file services (parse_8_3, load, run, type)
;   kernel/dir.asm      - directory listing + colored filenames
;   kernel/builtin.asm  - simple commands (help/cls/reboot/echo/ver/date/time)
;   kernel/color.asm    - color command + attribute parsing
;   kernel/rtc.asm      - RTC / CMOS access (date & time)
;   kernel/syscall.asm  - Mango ABI system call layer (int 0x40)
;   kernel/command.asm  - command table + argument wrappers
;   kernel/messages.asm - shared message strings
[org 0x1000]
[bits 16]

start:
    jmp entry

; =========================
; 常量
; =========================
FILE_LOAD_SEG   equ 0x6000    ; 384KB mark, buffer for file content
USER_PROG_SEG   equ 0x6000    ; user program segment (DS=CS=ES)
USER_STACK_SEG  equ 0x8000    ; user stack segment (grows down, 64KB)
USER_CMDLINE_OFF equ 0xF800   ; command line buffer in the program segment
USER_CMDLINE_MAX equ 256      ; max command line bytes copied

ATTR_DEFAULT    equ 0x07    ; light gray on black
ATTR_RED        equ 0x0C    ; bright red (errors)
ATTR_GREEN      equ 0x0A    ; bright green (.BIN files)
ATTR_YELLOW     equ 0x0E    ; bright yellow (.TXT files)

; =========================
; 模块
; =========================
%include "boot/filesys/filesys.asm"
%include "boot/printlib.asm"
%include "kernel/shell.asm"
%include "kernel/fs.asm"
%include "kernel/dir.asm"
%include "kernel/builtin.asm"
%include "kernel/color.asm"
%include "kernel/rtc.asm"
%include "kernel/syscall.asm"
%include "kernel/command.asm"
%include "kernel/messages.asm"

; =========================
; 主入口
; =========================

entry:
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov ax, 0x9000
    mov ss, ax
    mov sp, 0x0000

    mov byte [boot_drive], dl

    ; Install int 0x40 system call gate
    cli
    xor ax, ax
    mov es, ax
    mov word [es:0x40 * 4], syscall_handler
    mov word [es:0x40 * 4 + 2], 0x0000
    sti

    call set_cursor_top_left

    call clear_screen
    mov si, welcome_msg
    call print_string
    
    call fs_init
main_loop:
    mov al, [default_attr]
    call set_attr
    mov si, prompt
    call print_string
    call read_line

    mov al, [input_index]
    test al, al
    jz main_loop

    mov si, cmd_table
.try_cmd:
    movzx cx, byte [si]
    inc si
    cmp cl, 0
    je .unknown
    mov di, [si]
    mov bx, [si + 2]
    add si, 4
    push si
    mov si, input_buffer
    call cmd_match_exact
    pop si
    jc .try_cmd
    call bx
    jmp main_loop

.unknown:
    ; Direct execution: try the typed word as a program on disk
    mov si, input_buffer
    mov dx, si
    call run_program
    jc .unknown_msg
    jmp main_loop

.unknown_msg:
    mov si, unknown_cmd
    call print_error
    jmp main_loop

; ========================
; 数据区
; ========================

boot_drive  db 0

welcome_msg     db 'Mango DOS alpha Build [Kernel]', 13, 10, 0
prompt          db 'A:\> ', 0
unknown_cmd     db 'Unknown command. Type "help" for list.', 13, 10, 0

times 12288 - ($ - $$) db 0
