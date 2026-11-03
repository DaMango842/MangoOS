/* kernel32/linker.ld
   Link the 32-bit kernel at physical address 0x10000.
   Stage2 loads at most 24 sectors (12 KB) starting at that address. */

OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* R + X */
    rodata PT_LOAD FLAGS(4);   /* R     */
    data   PT_LOAD FLAGS(6);   /* R + W */
}

SECTIONS
{
    . = 0x10000;
    __kernel_start = .;

    /* ---- Executable code ---- */
    .text : {
        *(.text*)
    } :text

    /* ---- Read-only data (no execute permission) ---- */
    .rodata : {
        *(.rodata*)
    } :rodata

    /* ---- Read/write initialized data ---- */
    .data : {
        *(.data*)
    } :data

    /* ---- Zero-initialized data ---- */
    .bss ALIGN(4) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

    /* ---- End of kernel image ---- */
    . = ALIGN(4096);
    __kernel_end = .;

    /* ---- Discard unnecessary metadata ---- */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.stab*)
        *(.stabstr*)
    }
}

/* ====================================================================
   Sanity assertions – catch build errors at link time
   ==================================================================== */

/* Entry point must land exactly at 0x10000 (Stage2 hard-codes this) */
ASSERT(_start == 0x10000,
       "ERROR: _start is not at 0x10000; check .text ordering or ENTRY()")

/* Kernel size is no longer limited by the loader: stage2 loads it through
   the file system (KERNEL.BIN), so the size comes from the directory entry
   and is computed by build_img.ps1 at every build. */
ASSERT(SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) <= 0x80000,
       "ERROR: kernel image exceeds 512 KB; check the link layout")

/* BSS is not stored on disk but must still fit in memory below 640 KB */
ASSERT(__kernel_end <= 0xA0000,
       "ERROR: kernel image extends past conventional memory limit")