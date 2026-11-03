// kernel32/kernel.c - MangoOS C kernel
#include "proc/proc.h"
#include "syscall.h"
#include "sound/sound.h"
#include "idt.h"
#include "timer.h"
#include "exception.h"
#include "vga.h"
#include "video.h"
#include "serial.h"

void shell_run(void);
void pcspk_register(void);
void pcspk_beep_test(void);
void sb16_register(void);
void sb16_test_tone(uint16_t freq, uint16_t ms);

/* kept for the normal interactive image */
static void shell_entry(void *arg) {
    (void)arg;
    shell_run();                 /* never returns */
}

void kernel_main(void) {
    __asm__ volatile("out %0, %1" : : "a"('z'), "d"((unsigned short)0xE9));

    /* Install exception handlers (0-31) + syscall gate (0x80) */
    exception_init();
    syscall_init();                 /* installs + reloads the int 0x80 gate */
    idt_load();

    vga_enable_fb();                 /* VBE framebuffer if stage2 set it up */
    vga_clear();

    vga_set_cursor(0, 0);              /* start at top-left */
    vga_show_cursor(1);

    vga_set_attr(0x0A);                /* bright green */
    vga_write("Mango DOS ");
    vga_set_attr(0x0F);                /* bright white */
    vga_write("v0.2");
    vga_set_attr(0x07);                /* light grey */
    vga_write("\r\n");

    vga_set_attr(0x03);                /* cyan */
    vga_write("kernel loaded!\r\n");
    vga_set_attr(0x07);

    serial_init();
    serial_write("Mango DOS v0.2 - kernel loaded!\r\n");

    sound_init();

    /* Preferred: SB16 if present; PC speaker remains as fallback. */
    sb16_register();
    pcspk_register();
    pcspk_beep_test();
    sb16_test_tone(440, 50);

    /* timer_init will add IRQ0 and reload IDT */
    timer_init();

    proc_init();
    /* Start the Mango OS shell; it is the only runnable process in the
       normal image, so the scheduler effectively runs it to completion
       and it cooperates via proc_yield().  Additional processes are
       created from the shell (and by the GUI/emulator) as needed. */
    proc_create(shell_entry, 0, "shell");
    serial_write("Mango DOS v0.2 - shell loaded successfully!\r\n");

    proc_scheduler();                  /* takes over, never returns */
}
