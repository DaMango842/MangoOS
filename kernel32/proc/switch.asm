; kernel32/proc/switch.asm - register context switch with EFLAGS.
; void proc_switch(proc_ctx_t *from, const proc_ctx_t *to);
;
; Saves EFLAGS (pushf), callee-saved registers, and return address on the
; current stack, records them in *from (esp points at the saved
; [edi,esi,ebx,ebp,eflags,eip] frame), then switches to *to's stack,
; restores EFLAGS (popf), and resumes it.
;
; Frame layout on every process stack (low to high addresses):
;     [esp+0]  edi    [esp+4]  esi    [esp+8]  ebx
;     [esp+12] ebp    [esp+16] eflags  [esp+20] eip (popped by ret)
; New processes are entered with dummy regs, eflags=0x202 (IF=1), eip=proc_wrapper.
[bits 32]

[global proc_switch]

section .text
proc_switch:
    pushf                       ; save EFLAGS (IF)
    cli
    push ebp
    push ebx
    push esi
    push edi

    ; eax = from (arg1); stack now: [esp+20]=ret, [esp+24]=from
    mov eax, [esp + 24]
    mov [eax + 0], esp          ; from->esp = saved frame pointer
    mov ecx, [esp + 20]
    mov [eax + 4], ecx          ; from->eip = resume address (debug)

    ; eax = to (arg2); switch stack and pop its frame
    mov eax, [esp + 28]
    mov esp, [eax + 0]          ; to->esp
    pop edi
    pop esi
    pop ebx
    pop ebp
    popf                        ; restore EFLAGS (IF)
    ret                         ; jump to to's saved return address
