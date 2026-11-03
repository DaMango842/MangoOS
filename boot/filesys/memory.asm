; boot/filesys/memory.asm - String/memory helpers

; ========================
; memcmp - compare CX bytes at DS:SI vs ES:DI
; Input:  DS:SI, ES:DI, CX = byte count
; Output: ZF set if equal (ZF clear if differ)
; Clobbers: SI, DI, CX
; ========================
memcmp:
    push si
    push di
    push cx
    rep cmpsb
    pop cx
    pop di
    pop si
    ret

; ========================
; memcpy - copy CX bytes from DS:SI to ES:DI
; Input:  DS:SI = source, ES:DI = destination, CX = byte count
; Clobbers: SI, DI, CX
; ========================
memcpy:
    push si
    push di
    push cx
    rep movsb
    pop cx
    pop di
    pop si
    ret
