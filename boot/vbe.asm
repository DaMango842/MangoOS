; boot/vbe.asm - VBE linear-framebuffer init (called from stage2, real mode)
;
; Best-effort: if a usable LFB mode is found the display switches to it and
; the mode info is written into the bootinfo block; otherwise bootinfo says
; "no VBE" and the kernel keeps the text console.
; The BIOS 8x16 font is copied from the VGA ROM to physical 0x70000 so the
; kernel can render text into the framebuffer.
;
; Bootinfo block (ds = 0):
;   0x50A  dword  VBE status   (0 = text fallback, 1 = framebuffer active)
;   0x50E  dword  framebuffer physical address
;   0x512  word   width
;   0x514  word   height
;   0x516  word   pitch (bytes per scanline)
;   0x518  byte   bpp
; Font copy target: physical 0x70000 (256 glyphs * 16 bytes).

BOOT_INFO_VBE_STATUS  equ 0x50A
BOOT_INFO_VBE_FB      equ 0x50E
BOOT_INFO_VBE_W       equ 0x512
BOOT_INFO_VBE_H       equ 0x514
BOOT_INFO_VBE_PITCH   equ 0x516
BOOT_INFO_VBE_BPP     equ 0x518

VBE_INFO_BUF  equ 0x5000   ; controller info (512 B)
VBE_MODE_BUF  equ 0x5400   ; mode info (256 B)
VBE_FONT_SEG  equ 0x7000   ; font copy target segment (0x70000)

vbe_init:
    push es
    push ds

    mov dword [BOOT_INFO_VBE_STATUS], 0

    ; ---- query VBE controller ----
    mov al, 'a'
    out 0xE9, al
    xor ax, ax
    mov es, ax          ; SeaBIOS writes controller info to ES:DI
    mov ax, 0x4F00
    mov di, VBE_INFO_BUF
    int 0x10
    cmp ax, 0x004F
    jne .fail

    ; signature "VESA" (little-endian dword)
    mov al, 'b'
    out 0xE9, al
    mov si, VBE_INFO_BUF
    cmp dword [si], 0x41534556
    jne .fail

    ; ---- mode list: far pointer at offset 0x0E (seg,off) ----
    mov al, 'c'
    out 0xE9, al
    mov ax, [VBE_INFO_BUF + 0x10]
    mov es, ax
    mov si, [VBE_INFO_BUF + 0x0E]
    mov word [vbe_best], 0xFFFF
    mov byte [vbe_got], 0

.scan:
    mov ax, [es:si]
    cmp ax, 0xFFFF
    je .scan_done
    mov [vbe_mode], ax
    mov [vbe_si], si
    mov [vbe_seg], es

    xor ax, ax
    mov es, ax          ; SeaBIOS writes mode info to ES:DI
    mov ax, 0x4F01
    mov cx, [vbe_mode]
    mov di, VBE_MODE_BUF
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    mov ax, [vbe_seg]
    mov es, ax
    mov si, [vbe_si]
    mov al, 'd'
    out 0xE9, al

    test byte [VBE_MODE_BUF], 0x80   ; linear framebuffer supported?
    jz .next_mode

    ; score the mode: 2 = 32bpp@>=800x600, 1 = 16bpp@>=800x600,
    ; 0 = 15bpp@>=800x600, reject everything else
    mov al, 0xFF                     ; invalid score
    mov dl, [VBE_MODE_BUF + 0x19]    ; bits per pixel
    cmp dl, 32
    jne .chk16
    cmp word [VBE_MODE_BUF + 0x12], 800
    jb .score
    cmp word [VBE_MODE_BUF + 0x14], 600
    jb .score
    mov al, 2
    jmp .score
.chk16:
    cmp dl, 16
    jne .chk15
    cmp word [VBE_MODE_BUF + 0x12], 800
    jb .score
    cmp word [VBE_MODE_BUF + 0x14], 600
    jb .score
    mov al, 1
    jmp .score
.chk15:
    cmp dl, 15
    jne .score
    cmp word [VBE_MODE_BUF + 0x12], 800
    jb .score
    cmp word [VBE_MODE_BUF + 0x14], 600
    jb .score
    mov al, 0
.score:
    cmp al, 0xFF
    je .next_mode
    cmp byte [vbe_got], 0
    jne .score_cmp
    mov [vbe_score], al
    mov ax, [vbe_mode]
    mov [vbe_best], ax
    mov byte [vbe_got], 1
    jmp .next_mode
.score_cmp:
    cmp al, [vbe_score]
    jbe .next_mode
    mov [vbe_score], al
    mov ax, [vbe_mode]
    mov [vbe_best], ax
.next_mode:
    add si, 2
    jmp .scan

.scan_done:
    mov ax, [vbe_best]
    cmp ax, 0xFFFF
    je .fail

    ; ---- set the mode (linear framebuffer flag 0x4000) ----
    mov bx, ax
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .fail

    ; ---- re-query the chosen mode's info (buffer was clobbered
    ;      by the last mode of the scan) ----
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, [vbe_best]
    mov di, VBE_MODE_BUF
    int 0x10

    ; ---- copy the 8x16 BIOS font to 0x70000 ----
    mov ax, 0x1130
    mov bh, 0x06
    int 0x10
    mov ax, es
    mov ds, ax
    mov si, bp
    mov ax, VBE_FONT_SEG
    mov es, ax
    xor di, di
    mov cx, 256 * 16
    rep movsb
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; ---- record the mode info ----
    mov dword [BOOT_INFO_VBE_STATUS], 1
    mov al, '='
    out 0xE9, al
    ; dump 0x10..0x1F of mode info as hex
    mov si, VBE_MODE_BUF + 0x10
    mov cx, 16
.dump:
    mov al, [si]
    mov ah, al
    shr al, 4
    and al, 0x0F
    cmp al, 0x09
    jbe .d1
    add al, 7
.d1:
    add al, 0x30
    out 0xE9, al
    mov al, ah
    and al, 0x0F
    cmp al, 0x09
    jbe .d2
    add al, 7
.d2:
    add al, 0x30
    out 0xE9, al
    inc si
    loop .dump
    mov al, '='
    out 0xE9, al
    mov eax, [VBE_MODE_BUF + 0x28]   ; linear framebuffer address
    mov dword [BOOT_INFO_VBE_FB], eax
    mov ax, [VBE_MODE_BUF + 0x12]    ; x resolution
    mov word [BOOT_INFO_VBE_W], ax
    mov ax, [VBE_MODE_BUF + 0x14]    ; y resolution
    mov word [BOOT_INFO_VBE_H], ax
    mov ax, [VBE_MODE_BUF + 0x32]    ; linear bytes per scanline (VBE3)
    test ax, ax
    jnz .pitch_ok
    mov ax, [VBE_MODE_BUF + 0x10]    ; fallback: VBE1.2 bytes per scanline
.pitch_ok:
    mov word [BOOT_INFO_VBE_PITCH], ax
    mov al, [VBE_MODE_BUF + 0x19]    ; bits per pixel
    mov byte [BOOT_INFO_VBE_BPP], al
    ; dump fb address
    mov al, '@'
    out 0xE9, al
    mov ebx, [BOOT_INFO_VBE_FB]
    mov cx, 8
.hf1:
    rol ebx, 4
    mov al, bl
    and al, 0x0F
    add al, 0x30
    cmp al, 0x39
    jbe .fk1
    add al, 7
.fk1:
    out 0xE9, al
    loop .hf1
    jmp .out

.fail:
    mov dword [BOOT_INFO_VBE_STATUS], 0

.out:
    pop ds
    pop es
    ret

vbe_mode  dw 0
vbe_best  dw 0xFFFF
vbe_got   db 0
vbe_score db 0
vbe_si    dw 0
vbe_seg   dw 0