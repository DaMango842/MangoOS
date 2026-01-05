; boot/stage2.asm - Full-featured Stage2 with command shell
; Loaded to 0x7E00 by Stage1
[org 0x7E00]
[bits 16]

start:
    jmp entry

; =========================
; 模块 / 函数库区
; =========================
; 包含文件系统初始化代码
%include "boot/filesys.asm"

; =========================
; 主入口
; =========================

entry:
    ; 初始化段寄存器
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; 设置安全栈（0x9000:0xFFFE ≈ 64KB 栈空间）
    mov ax, 0x9000
    mov ss, ax
    mov sp, 0xFFFE

    call set_cursor_top_left

    call clear_screen
    mov si, welcome_msg
    call print_string
    
    call fs_init
main_loop:
    mov si, prompt
    call print_string
    call read_line

    mov al, [input_index]
    test al, al
    jz main_loop

    ; help (4 chars)
    mov si, input_buffer
    mov di, cmd_help_str
    mov cx, 4
    call strncmp
    jnc .do_help

    ; cls (3 chars)
    mov si, input_buffer
    mov di, cmd_cls_str
    mov cx, 3
    call strncmp
    jnc .do_cls

    ; reboot (6 chars)
    mov si, input_buffer
    mov di, cmd_reboot_str
    mov cx, 6
    call strncmp
    jnc .do_reboot

    ; echo (4 chars)
    mov si, input_buffer
    mov di, cmd_echo_str
    mov cx, 4
    call strncmp
    jnc .do_echo

    ; unknown
    mov si, unknown_cmd
    call print_string
    jmp main_loop

.do_help:
    call cmd_help
    jmp main_loop

.do_cls:
    call cmd_cls
    jmp main_loop

.do_reboot:
    call cmd_reboot
    ; reboot 不返回，所以无需 jmp

.do_echo:
    call cmd_echo
    jmp main_loop

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
    ; call set_cursor_top_left
    ret

cmd_reboot:
    call newline
    mov si, reboot_msg
    call print_string
    ; 等待短暂时间让消息显示（可选）
    mov cx, 0xFFFF
.delay:
    loop .delay
    ; 然后重启
    jmp 0xFFFF:0x0000

cmd_echo:
    call newline
    mov si, input_buffer
    add si, 4           ; 跳过 "echo"
.skip_space:
    lodsb
    cmp al, ' '
    je .skip_space      ; 跳过多余空格
    or al, al
    jz .done
    ; 回退一个字符（因为 lodsb 已经读走了）
    dec si
.print_rest:
    lodsb
    or al, al
    jz .done
    call print_char
    jmp .print_rest
.done:
    call newline
    ret

; ========================
; I/O 函数
; ========================
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
    mov ah, 0x0E        ; Teletype output
    int 0x10
    pop ax
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
    mov ax, 0x0700      ; Scroll up
    mov bh, 0x07        ; White on black attribute
    mov cx, 0           
    mov dx, 0x184F      ; Bottom-right (24,79)
    int 0x10
    
    mov ah, 0x02
    mov bh, 0
    mov dh, 0
    mov dl, 0
    int 0x10
    ret

; ========================
; 输入函数：读取一行（带退格）
; 结果存入 input_buffer（以 0 结尾）
; ========================
read_line:
    push ax
    push si
    mov byte [input_index], 0

.read_char:
    mov ah, 0x00
    int 0x16            ; Wait for key

    cmp al, 13          ; Enter?
    je .done

    cmp al, 8           ; Backspace?
    je .backspace

    ; 只接受可打印 ASCII（32~126）
    cmp al, ' '
    jb .read_char
    cmp al, 126
    ja .read_char

    ; 检查缓冲区是否满（31 字符）
    mov si, input_index
    cmp byte [si], 31
    jge .read_char

    ; 存入缓冲区
    mov di, input_buffer
    movzx bx, byte [si]
    add di, bx
    mov [di], al
    inc byte [input_index]

    ; 回显
    call print_char
    jmp .read_char

.backspace:
    mov si, input_index
    cmp byte [si], 0
    je .read_char
    dec byte [si]

    ; 回显退格：BS + space + BS
    mov al, 8
    call print_char
    mov al, ' '
    call print_char
    mov al, 8
    call print_char
    jmp .read_char

.done:
    ; 添加字符串结束符
    mov si, input_index
    mov di, input_buffer
    movzx bx, byte [si]
    add di, bx
    mov byte [di], 0

    call newline
    pop si
    pop ax
    ret

; 比较 SI 和 DI 的前 CX 个字符
; 返回：CF=0 if equal, CF=1 if not equal
strncmp:
    push ax
    push si
    push di
.loop:
    cmp cx, 0
    je .equal
    lodsb               ; AL = [SI++]
    mov ah, [di]        ; AH = [DI]
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

; 设置光标到屏幕左上角 (row=0, col=0)
set_cursor_top_left:
    push ax
    push bx
    push dx
    mov ah, 0x02        ; BIOS: Set cursor position
    mov bh, 0x00        ; Page 0
    mov dh, 0           ; Row = 0
    mov dl, 0           ; Column = 0
    int 0x10
    pop dx
    pop bx
    pop ax
    ret

; ========================
; 数据区
; ========================


welcome_msg     db 'Simple OS v0.2 [Stage2 Shell]', 13, 10, 0
prompt          db 'A:\> ', 0
help_text       db 'Built-in commands:', 13, 10
                db '  help    - Show this help', 13, 10
                db '  cls     - Clear screen', 13, 10
                db '  reboot  - Restart system', 13, 10
                db '  echo    - Echo arguments', 13, 10, 0
reboot_msg      db 'Rebooting...', 0
unknown_cmd     db 'Unknown command. Type "help" for list.', 13, 10, 0

cmd_help_str    db 'help'
cmd_cls_str     db 'cls'
cmd_reboot_str  db 'reboot'
cmd_echo_str    db 'echo'

fs_init_error_msg db 'File system initialization failed.', 13, 10, 0



; 输入缓冲区
input_buffer:
    times 32 db 0       ; 31 chars + null terminator
input_index     db 0

; 填充到 2048 字节（确保磁盘读取完整）
times 2048 - ($ - $$) db 0