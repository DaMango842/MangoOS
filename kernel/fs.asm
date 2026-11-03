; kernel/fs.asm - kernel file services
; Responsibilities:
;   - parse_8_3: convert an ASCII filename to a padded 8.3 name
;   - fs_load_file: find + load a file into FILE_LOAD_SEG
;   - run_program: validate (.BIN only, non-empty) and execute a program
;   - cmd_type: display file content

; ========================
; parse_8_3 - parse an ASCII filename into padded_name
; Input:  DX = pointer to ASCII name (e.g. "hello.txt")
; Output: padded_name filled (11 bytes, space-padded, uppercase)
; Clobbers: AX, BX, CX, DX, SI, DI, ES
; ========================
parse_8_3:
    push ax
    xor ax, ax
    mov es, ax
    pop ax
    mov di, padded_name
    mov cx, 11
    mov al, ' '
    rep stosb

    mov si, dx
    mov di, padded_name
    mov cx, 8
.parse_base:
    lodsb
    or al, al
    jz .fill_base
    cmp al, '.'
    je .start_ext
    cmp al, 'a'
    jb .store_char
    cmp al, 'z'
    ja .store_char
    sub al, 32
.store_char:
    stosb
    loop .parse_base

.skip_to_dot:
    lodsb
    or al, al
    jz .fill_base
    cmp al, '.'
    jne .skip_to_dot

.start_ext:
    mov di, padded_name + 8
    mov cx, 3
.parse_ext:
    lodsb
    or al, al
    jz .fill_ext
    cmp al, '.'
    je .fill_ext
    cmp al, 'a'
    jb .store_ext_char
    cmp al, 'z'
    ja .store_ext_char
    sub al, 32
.store_ext_char:
    stosb
    loop .parse_ext

.fill_ext:
    mov cx, padded_name + 11
    sub cx, di
    jle .done_name
.fill_ext_loop:
    mov al, ' '
    stosb
    loop .fill_ext_loop
    jmp .done_name

.fill_base:
    mov cx, padded_name + 8
    sub cx, di
    jle .fill_ext
.fill_base_loop:
    mov al, ' '
    stosb
    loop .fill_base_loop
    ; No dot in name: NUL was already consumed, do NOT re-read input.
    ; Extension stays space-padded.
    jmp .fill_ext

.done_name:
    ret

; ========================
; fs_load_file - find a file and load it into FILE_LOAD_SEG
; Input:  padded_name filled (11 bytes)
; Output: CF=0 success: ES=0x6000, CX=file size, AX=LBA, DX=size
;         CF=1 not found / read error
; Clobbers: AX, BX, CX, DX, SI, DI, ES
; ========================
fs_load_file:
    push bx
    push si
    push di

    mov dx, padded_name
    call fs_find
    jc .fail

    push ax                     ; save start LBA
    push dx                     ; save file size

    add dx, 511
    mov cl, 9
    shr dx, cl                  ; DX = sector count (size+511)/512
    mov cx, dx                  ; CX = sector count

    mov ax, FILE_LOAD_SEG
    mov es, ax
    xor bx, bx
    pop dx                      ; DX = file size
    pop ax                      ; AX = start LBA
    push dx                     ; keep file size for caller

    call fs_load
    jc .read_error

    pop cx                      ; CX = file size (bytes)
    clc
    jmp .done

.read_error:
    pop dx                      ; discard pending file size
.fail:
    stc

.done:
    pop di
    pop si
    pop bx
    ret

; ========================
; run_program - parse name, load and execute a program
; Input:  DX = pointer to ASCII filename + arguments
; Output: CF=1 if not found / read error (nothing executed)
;         On success executes the program, then returns to main_loop
; Validation: only .BIN files are executable; empty files are rejected
; Program ABI (see ABI.md):
;   - 8-byte header: magic "MA", entry, bss, flags
;   - legacy files without magic run with entry = 0
;   - loaded at USER_PROG_SEG:0, CS=DS=ES=USER_PROG_SEG
;   - user stack USER_STACK_SEG (independent of kernel stack)
;   - DX = command line pointer (USER_CMDLINE_OFF in DS)
;   - exit via RETF or int 0x40 AH=0x00
; ========================
run_program:
    push dx                     ; keep argument pointer for the command line

    call parse_8_3

    ; If the name has no extension, append .BIN (DOS style)
    cmp byte [padded_name + 8], ' '
    jne .check_ext
    cmp byte [padded_name + 9], ' '
    jne .check_ext
    cmp byte [padded_name + 10], ' '
    jne .check_ext
    mov byte [padded_name + 8], 'B'
    mov byte [padded_name + 9], 'I'
    mov byte [padded_name + 10], 'N'

.check_ext:
    ; Extension whitelist: only .BIN is executable
    cmp byte [padded_name + 8], 'B'
    jne .not_program
    cmp byte [padded_name + 9], 'I'
    jne .not_program
    cmp byte [padded_name + 10], 'N'
    jne .not_program

    call fs_load_file
    jc .fail_pop

    ; Reject empty files
    and cx, cx
    jnz .prog_ok
    jmp .empty
.prog_ok:

    mov ax, FILE_LOAD_SEG
    mov es, ax

    ; --- Parse the 8-byte header ---
    mov ax, [es:0]
    cmp ax, 0x414D              ; "MA" (M=0x4D, A=0x41, little-endian)
    jne .legacy_format
    mov bx, [es:2]              ; entry
    mov dx, [es:4]              ; bss size
    mov ax, cx                  ; file size
    sub ax, 8                   ; code size
    jmp .size_check
.legacy_format:
    xor bx, bx                  ; entry = 0
    xor dx, dx                  ; bss = 0
    mov ax, cx                  ; code size = file size

.size_check:
    mov si, ax                  ; code size
    mov di, bx                  ; entry
    add di, si
    add di, dx
    cmp di, USER_CMDLINE_OFF
    ja .too_large

    ; --- Zero-fill the BSS area ---
    mov di, bx
    add di, si                  ; ES:DI = entry + code size
    mov cx, dx
    jcxz .bss_done
    mov al, 0
.bss_loop:
    mov [es:di], al
    inc di
    loop .bss_loop
.bss_done:

    ; --- Save exit / stack state for the syscall layer ---
    mov ax, ss
    mov [syscall_kernel_ss], ax
    mov ax, sp
    mov [syscall_kernel_sp], ax
    mov word [syscall_exit_far], .post
    mov word [syscall_exit_far + 2], 0x0000
    mov word [syscall_prog_entry], bx
    mov word [syscall_prog_entry + 2], USER_PROG_SEG

    ; --- Copy the command line into the program segment ---
    pop dx                      ; argument pointer (kernel segment)
    mov si, dx
    mov di, USER_CMDLINE_OFF
    mov cx, USER_CMDLINE_MAX
    xor ax, ax
    mov ds, ax                  ; DS = kernel (source)
    ; ES already = USER_PROG_SEG (destination)
.copy_cmd:
    lodsb
    mov [es:di], al
    or al, al
    jz .cmd_done
    inc di
    loop .copy_cmd
    mov byte [es:di], 0         ; force termination if truncated
.cmd_done:

.exec:
    ; DS = kernel (0), ES = USER_PROG_SEG here.
    ; Transfer to the program via RETF, with the return frame parked on
    ; the user stack so the program's RETF lands back at .exec_after.
    mov ax, [syscall_prog_entry]        ; target IP
    mov bx, [syscall_prog_entry + 2]    ; target CS
    mov dx, USER_CMDLINE_OFF
    mov cx, USER_PROG_SEG
    mov ds, cx
    mov es, cx

    ; Switch to the user stack
    mov cx, USER_STACK_SEG
    cli
    mov ss, cx
    mov sp, 0xFFFE
    sti

    push word 0x0000                    ; return CS
    push word .exec_after               ; return IP
    push bx                             ; target CS
    push ax                             ; target IP
    retf                                ; pop IP/CS -> user program

.exec_after:
    ; Normal RETF return: switch back to the kernel stack
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ax, [syscall_kernel_ss]
    cli
    mov ss, ax
    mov sp, [syscall_kernel_sp]
    sti

.post:
    call newline
    jmp main_loop

.not_program:
    pop dx
    mov si, not_program_msg
    call print_error
    jmp main_loop

.empty:
    pop dx
    mov si, empty_file_msg
    call print_error
    jmp main_loop

.too_large:
    pop dx
    mov si, prog_too_large_msg
    call print_error
    jmp main_loop

.fail_pop:
    pop dx
.fail:
    stc
    ret

; ========================
; cmd_type - display file content
; ========================
cmd_type:
    call parse_8_3
    call fs_load_file
    jc .not_found

    mov si, 0
.print_loop:
    mov al, [es:si]
    or al, al
    jz .print_done
    call print_char
    inc si
    cmp si, cx
    jb .print_loop

.print_done:
    call newline
    ret

.not_found:
    mov si, file_not_found_msg
    call print_error
    ret

padded_name:
    times 11 db ' '
