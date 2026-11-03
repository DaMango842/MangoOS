; boot/filesys/file.asm - File loading
; Assumes: disk_read, boot_drive

; ========================
; fs_load - load sectors from LBA to ES:BX
; Input:  AX = start LBA, CX = sector count
;         ES:BX = destination buffer (set by caller)
; Output: CF = 0 on success, 1 on error
; ========================
fs_load:
    push ax
    push cx
    push es
    push bx

    jcxz .success

    call disk_read
    jc .error

.success:
    clc
    jmp .done

.error:
    stc

.done:
    pop bx
    pop es
    pop cx
    pop ax
    ret
