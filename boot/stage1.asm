; boot/stage1.asm - 使用 CHS 模式加载 Stage2
[org 0x7C00]
[bits 16]

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl

    ; 打印提示
    mov si, msg_loading
    call print_string

    ; 使用 CHS 模式读取 LBA=1（即第2个扇区）
    ; LBA=1 -> CHS: C=0, H=0, S=2
    mov ah, 0x02        ; INT 13h, AH=02h: Read sectors
    mov al, 4           ; 读 4 个扇区
    mov ch, 0           ; 柱面 = 0
    mov cl, 2           ; 扇区 = 2 (LBA=1)
    mov dh, 0           ; 磁头 = 0
    mov dl, [boot_drive] ; 驱动器号
    mov bx, 0x7E00      ; 加载到 0x7E00
    int 0x13
    jc disk_error       ; 如果失败，跳转到错误处理

    ; 成功加载，跳转到 Stage2
    jmp 0x0000:0x7E00

disk_error:
    mov si, msg_error
    call print_string
    cli
.hang:
    hlt
    jmp .hang

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_string
.done:
    ret

boot_drive db 0

msg_loading db 'Loading Stage2...', 13, 10, 0
msg_error   db 'Disk Error!', 13, 10, 0

; 填充到 510 字节
times 510 - ($ - $$) db 0
dw 0xAA55