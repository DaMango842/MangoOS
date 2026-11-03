; boot/filesys/geometry.asm - Disk geometry probe + LBA<->CHS conversion
; Assumes: boot_drive (set by the caller)

; CHS conversion variables (initialized by fs_probe_geometry)
; Defaults are for the 16MB IDE image so the CHS fallback works even when
; the BIOS geometry probe fails.
sectors_per_track dw 63
heads             dw 16

; ========================
; fs_probe_geometry - probe disk geometry via BIOS
; Input:  DL = boot drive (via boot_drive)
; ========================
fs_probe_geometry:
    push ax
    push bx
    push cx
    push dx
    push es

    mov dl, [boot_drive]
    mov ah, 0x08
    mov di, 0          ; ES:DI = 0:0 (some BIOSes need ES:DI set)
    mov es, di
    int 0x13
    jc .done

    and cx, 0x003F     ; CL bits 5-0 = sectors per track
    mov [sectors_per_track], cx

    mov al, dh         ; DH = number of heads - 1
    inc ax             ; heads = DH + 1
    mov [heads], ax

.done:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ========================
; lba_to_chs - convert LBA to CHS
; Input:  AX = LBA
; Output: CH = cylinder (full 10 bits), CL = sector | (cyl>>2 & C0), DH = head
; Clobbers: AX, DX (BX preserved)
; ========================
lba_to_chs:
    push bx
    xor dx, dx
    div word [sectors_per_track]  ; AX = track, DX = sector - 1
    mov bx, ax                    ; save track number
    mov cl, dl
    inc cl                        ; CL = sector (1-based)

    xor dx, dx
    mov ax, bx
    div word [heads]              ; AX = cylinder, DX = head
    mov ch, al                    ; CH = cylinder low 8 bits
    shl ah, 6                     ; cylinder bits 9-8 -> CL bits 7-6
    or cl, ah
    mov dh, dl                    ; DH = head
    pop bx
    ret
