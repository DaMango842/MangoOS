// kernel32/keyboard.h - PS/2 keyboard.
//
// The driver is POLLING: there is no keyboard interrupt in normal
// operation (the PIC masks IRQ1, and an unhandled IRQ with an invalid
// gate was a fatal #GP).  irq1.asm exists only as a defensive drain.
//
// kbd_init() forces the device into scan code set 2 regardless of what
// the BIOS left behind, because SeaBIOS/QEMU can ship with both PS/2
// ports disabled and translation enabled.
//
// On top of ASCII there is a scancode-level state bitmap: kbd_scan_down()
// tells whether a raw set-2 code is currently held, which the NES
// emulator needs (it wants arrows, not letters).
#ifndef MANGO_KEYBOARD_H
#define MANGO_KEYBOARD_H

/* Reset the controller, select scan code set 2, re-enable scanning. */
void kbd_init(void);

/* Non-blocking: returns one ASCII character, or 0 when nothing is
   pending.  Printable ASCII plus \r, \n, \t, \b and the arrow keys as
   their 0x00/0xE0-prefixed extended codes. */
int kbd_get_key(void);

/* Raw set-2 scancode state (used by the emulator's joypad mapping). */
int kbd_scan_down(unsigned char sc);   /* 1 = held */
void kbd_copy_state(unsigned char *dst);  /* dst: 32 bytes */
void kbd_clear_state(void);

#endif /* MANGO_KEYBOARD_H */