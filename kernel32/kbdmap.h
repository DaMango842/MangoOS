#ifndef MANGO_KBDMAP_H
#define MANGO_KBDMAP_H

/* Special key codes used by the mapping tables (ASCII range). */
#define KEY_TAB       0x09
#define KEY_ENTER     0x0D
#define KEY_BACKSPACE 0x08
#define KEY_ESC       0x1B

/* Convert one raw scan-code payload byte from set 2 to set 1.
   Prefix bytes (0xE0/0xF0) and codes with no set-2 meaning pass
   through unchanged. */
unsigned char kbd_s2_to_s1(unsigned char base);

/* Look up the ASCII character for a set-1 make code.
   Returns 0 when the code has no printable mapping. */
char kbd_lookup_char(unsigned char sc, int shift);

#endif
