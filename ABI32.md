# MangoOS 32-bit ABI (current)

This is the contract that the **shipped kernel** (`kernel32/`) actually
implements. It supersedes `ABI.md`, which describes the old 16-bit
real-mode kernel in `kernel/` and is retained only for historical
reference — see "Legacy" at the bottom.

Read `kernel32/syscall.h` alongside this document; it is the normative
declaration of the syscall numbers.

---

## 1. Execution model

MangoOS is a **single-address-space, ring-0-only** kernel. There is no
user mode, no paging, and no MMU.

| Property | Value |
|---|---|
| Kernel link address | `0x10000` (physical = linear; identity map, paging off) |
| Boot stack top | `0x90000` |
| Page size | 4 KB, pool managed by `memory.c` |
| System call gate | `int 0x80`, DPL 3 |
| IDT | 256 entries, `idt.c` |
| Timer | PIT ch0, 100 Hz, mode 3 → IRQ0 → `proc_tick()` |

### "Processes"

`proc/proc.h` implements a cooperative round-robin scheduler with 16
slots. A process is a **C function** `void (*)(void *arg)` plus a private
4 KB stack. There is no isolation whatsoever — a process can read and
write all kernel memory, including its own image. This is a scheduling
construct, not a protection boundary.

Preemption exists but only fires when a second process is `PROC_RUNNABLE`;
in the normal single-shell image the shell runs continuously and
cooperates via `proc_yield()`.

---

## 2. Memory layout

```
0x000000 - 0x0FFFFF   boot structures, VBE bootinfo block, VGA text/ROM
0x100000 - 0x2D4C00   GUI back buffer (fixed, 800x600x32 = 1.92 MB)
0x2E0000 - 0xFFFFFF   managed page pool (first-fit bitmap, 3360 pages)
                      reserved inside the pool:
                        0x500000-0x52E000  FC render surface + snapshots
                        0x530000-0x710000  GUI full-screen cmp snapshot
0x1000000             top of RAM (QEMU is started with -m 16)
```

`memory.c` provides two allocators: `mem_alloc_pages`/`mem_free_pages`
(4 KB pages) and `kmalloc`/`kfree` (a first-fit heap with no coalescing).
`fs/…`-backed program loading uses the page allocator.

---

## 3. System calls — `int 0x80`

```
eax = syscall number
ebx, ecx, edx = up to three arguments
eax = return value
```

Registers are preserved across the gate except `eax` (see
`struct syscall_frame` in `syscall.h`). `int 0x80` with an out-of-range
number returns `-1`.

| # | Name | Args | Returns |
|---|------|------|---------|
| 0 | `SYS_EXIT` | `ebx` = exit code | never returns |
| 1 | `SYS_YIELD` | — | 0 |
| 2 | `SYS_PUTCHAR` | `ebx` = char (VGA text) | 0 |
| 3 | `SYS_PUTS` | `ebx` = C string (VGA text) | 0 |
| 4 | `SYS_GETKEY` | — | ASCII key, 0 when none |
| 5 | `SYS_TIME` | `ebx` = `char[20]` buffer | length, 0 on failure |
| 6 | `SYS_RANDOM` | — | 32-bit value |
| 7 | `SYS_RANGE` | `ebx` = lo, `ecx` = hi | value in `[lo, hi]` |
| 8 | `SYS_SPUTC` | `ebx` = char (serial COM1) | 0 |
| 9 | `SYS_SPUTS` | `ebx` = C string (serial COM1) | 0 |

Kernel-side helper: `static inline int syscall(int nr, unsigned a,
unsigned b, unsigned c)` in `syscall.h`.

### Reserved ranges

Not yet assigned; they are placeholders carried over from `ABI.md`.

| Range | Intended subsystem |
|---|---|
| 10–15 | file system |
| 16–31 | memory / processes |
| 48–79 | video driver |
| 80–111 | network |
| 112–255 | future |

`network/` and `video/` are empty directories reserved for the last two.

---

## 4. Executable image format ("MA" programs)

The shell's `run <name>` command loads a flat binary from MangoFS:

```
offset  size  field
+0      2     magic 'MA' (0x4D 0x41)
+2      4     entry offset from the load base (little-endian)
+6      4     bss size to zero after the image (little-endian)
+10     ...   code and data
```

Rules the loader enforces (`cmd_run` in `shell.c`):

- file size >= 12 and the magic must match;
- `entry < file_size`, so a corrupt header cannot become a wild jump;
- `file_size + bss <= PROG_MAX_IMAGE` (256 KB);
- the program is copied into pages from the managed pool, **never** to a
  fixed address. The kernel image occupies `0x10000`…`__kernel_end`, so
  the old fixed load address `0x20000` was inside the kernel and `run`
  overwrote the running kernel;
- the pool pages are released when the program returns.

Because there is no address-space isolation, a program may freely call any
kernel symbol it can name. The `int 0x80` gate is DPL 3 precisely so that
this interface stays usable unchanged when user mode arrives.

---

## 5. Reserved hardware

Single-owner subsystems must not silently reprogram shared devices. The
canonical case is **PIT channel 0**, which the kernel tick owns and which
timing-sensitive code must borrow explicitly:

```c
timer_pit_borrow();     /* ch0 -> free-running 16-bit down counter  */
...  pit_now_export(), pit_us_since_export()  ...
timer_pit_return();     /* ch0 -> back to the 100 Hz tick */
```

While the channel is borrowed, IRQ0 does not fire, so `proc_tick()` does
not run and `proc_sleep()` / time slices stall. Every borrower must pair
the two calls; MangoWin and the NES emulator do so in
`stats_init()`/`stats_shutdown()`.

---

## 6. Observability

Serial COM1 (38400 8N1) is the universal channel for logs, keyboard
tracing, exception dumps, performance reports and scripted input. QEMU's
debug console on port `0xE9` carries single-character boot progress
markers parsed by the scripts in `probe/`.

Boot CLI (local): `python tools/serialcat.py` reads the TCP serial
redirect used by `make run-debug`.

---

## Legacy: `ABI.md`

`ABI.md` specifies the previous 16-bit real-mode kernel (`kernel/`):
`int 0x40` with an `AH`-indexed syscall table, user programs at
`0x6000:0`, a user stack at `0x8000:0xFFFE`. That code is **not in the
build** — the makefile never references `kernel/`, and `kernel/kernel_main.c`
does not even compile (it opens with a bare `extern "C"`).

Do not target `ABI.md`. It is kept for historical context only.