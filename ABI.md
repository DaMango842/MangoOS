# MangoOS ABI Specification

> **SUPERSEDED — this document describes the legacy 16-bit real-mode
> kernel in `kernel/`, which is not built and does not compile.**
>
> The shipped kernel is `kernel32/` (32-bit protected mode, `int 0x80`,
> single address space, no user mode). Its actual contract is documented
> in **[ABI32.md](ABI32.md)**. Nothing below is implemented by the
> current build; keep this file only as historical context.

MangoOS Application Binary Interface (ABI) v0.1 — the contract between
user programs and the MangoOS kernel on x86 real mode.

## 1. Memory Layout (real mode)

```
Address      Size    Owner
0x00000      1KB     IVT (int 0x40 = system call gate)
0x00400      256B    BIOS data area
0x00500      2.75KB  Kernel data / boot variables
0x01000      12KB    Kernel code + data
0x04000      8KB     Reserved (kernel buffers)
0x06000      64KB    User program load area (CS=DS=ES=0x6000)
0x07000      4KB     Kernel file-table buffer
0x08000      64KB    User program stack (grows down from 0x8000:0xFFFE)
0x09000      64KB    Kernel stack
0x0A000      128KB   VGA / hardware
0x0C000      256KB   BIOS ROM
```

- Program limit: entry + code + bss must fit below `0x6000:0xF800`
  (the last 2KB of the segment is reserved for the command line).
- The kernel never touches the user stack; the user program owns it.

## 2. Program File Format

Flat binary, 16-bit real mode, `[org 0]`, loaded at `0x6000:0x0000`.

```
Offset  Size  Field
0       2     magic "MA" (0x4D41)
2       2     entry offset (relative to segment start)
4       2     bss size (zero-filled after code)
6       2     flags (reserved, must be 0)
8       n     code
8+n     m     bss (zero-filled by the loader)
```

- A file whose magic does not match `"MA"` is treated as a legacy
  flat program (entry = 0, bss = 0) for backward compatibility.
- Only `.BIN` files are executable.

## 3. Execution Environment

On entry to the program:

| Register | Value |
|----------|-------|
| CS:IP    | `0x6000:entry` |
| DS, ES   | `0x6000` |
| SS:SP    | `0x8000:0xFFFE` (independent user stack) |
| DX       | pointer to command line string (offset in DS, NUL-terminated) |

The command line is the full typed argument text copied by the kernel
to `0x6000:0xF800` (up to 256 bytes).

## 4. System Calls — int 0x40

Single gate: `int 0x40`, `AH` = function number, registers pass arguments.
On entry the kernel switches DS/ES to the kernel segment; caller registers
are preserved except documented outputs. Unsupported function numbers are
ignored (all registers unchanged).

| AH  | Function      | Inputs                    | Outputs          |
|-----|---------------|---------------------------|------------------|
| 0x00 | exit          | AL = exit code (ignored)  | never returns    |
| 0x01 | putchar       | AL = character            | —                |
| 0x02 | puts          | DS:SI = NUL-terminated string | —            |
| 0x03 | newline       | —                         | —                |
| 0x04 | getkey        | —                         | AX = key (AL=char, AH=scancode) |
| 0x05 | peekkey       | —                         | AX = key, or 0 if no key pending |
| 0x10 | setcolor      | AL = attribute            | —                |
| 0x20 | gettime       | ES:BX = 7-byte buffer     | buffer: sec, min, hour, day, mon, year, century (binary) |
| 0x21 | fs            | AL = subfunction (see below) | CF = error flag |

### 0x21 File system subfunctions

| AL | Function | Inputs | Outputs |
|----|----------|--------|---------|
| 0x01 | dir    | ES:DI = name buffer (11 bytes/entry) | CX = entry count; buffer filled with 8.3 names |
| 0x02 | read   | DS:SI = filename (ASCII, NUL-term), ES:DI = destination buffer | CX = file size (low 16 bits); file bytes loaded to ES:DI |

Error flag: for `0x21`, carry is set on failure (file not found / read
error), cleared on success.

## 5. Reserved function ranges

| AH range | Owner |
|----------|-------|
| 0x00-0x2F | Core services (output, keyboard, color, time, fs) |
| 0x30-0x4F | Reserved: video driver |
| 0x50-0x6F | Reserved: network services |
| 0x70-0xFF | Reserved: future extensions |

## 6. Sample program

```asm
; programs/hello.asm
[org 0]
[bits 16]

db 'MA'            ; magic
dw start           ; entry
dw 0               ; bss
dw 0               ; flags

start:
    mov si, msg
    mov ah, 0x02   ; puts
    int 0x40
    mov ah, 0x00   ; exit
    int 0x40

msg db 'Hello from HELLO.BIN!', 13, 10, 0
```
