// kernel32/kbdmap.c - keyboard scan-code translation and mapping tables.
//
// Owns everything table-shaped about scancode decoding:
//   - kbd_s2_to_s1():   set-2 -> set-1 payload conversion
//   - kbd_lookup_char(): set-1 make code -> ASCII character
//
// Why set 2 in, set 1 out: the device is commanded to speak scan code
// set 2 (make = code, break = F0 + the SAME code - symmetric by
// definition) and this module converts every payload byte to set 1 so
// the driver state machine works in a single code space.  Relying on
// the 8042 controller translation instead proved unreliable on this
// firmware/QEMU path: it translated make bytes but passed break codes
// through untouched, leaving press and release in two encodings.

#include "kbdmap.h"

/* Set-2 -> set-1 map, indexed by the set-2 code.  A zero entry means
   "no set-2 meaning" - the byte passes through unchanged, which also
   keeps a genuine set-1 stream mostly working.  Prefix bytes 0xE0/0xF0
   are >= 0x78 and therefore always pass through. */
static const unsigned char s2s1[0x78] = {
    /* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x08 */ 0, 0, 0, 0, 0, 0x0F /*tab*/, 0x29 /*`*/, 0,
    /* 0x10 */ 0, 0x38 /*lalt*/, 0x2A /*lshift*/, 0, 0x1D /*lctrl*/, 0x10 /*q*/, 0x02 /*1*/, 0,
    /* 0x18 */ 0, 0, 0x2C /*z*/, 0x1F /*s*/, 0x1E /*a*/, 0x11 /*w*/, 0x03 /*2*/, 0,
    /* 0x20 */ 0, 0x2E /*c*/, 0x2D /*x*/, 0x20 /*d*/, 0x12 /*e*/, 0x05 /*4*/, 0x04 /*3*/, 0,
    /* 0x28 */ 0, 0x39 /*spc*/, 0x2F /*v*/, 0x21 /*f*/, 0x14 /*t*/, 0x13 /*r*/, 0x06 /*5*/, 0,
    /* 0x30 */ 0, 0x31 /*n*/, 0x30 /*b*/, 0x23 /*h*/, 0x22 /*g*/, 0x15 /*y*/, 0x07 /*6*/, 0,
    /* 0x38 */ 0, 0, 0x32 /*m*/, 0x24 /*j*/, 0x16 /*u*/, 0x08 /*7*/, 0x09 /*8*/, 0,
    /* 0x40 */ 0, 0x33 /*,*/, 0x25 /*k*/, 0x17 /*i*/, 0x18 /*o*/, 0x0B /*0*/, 0x0A /*9*/, 0,
    /* 0x48 */ 0, 0x34 /*.*/, 0x35 /*/*/, 0x26 /*l*/, 0x27 /*;*/, 0x19 /*p*/, 0x0C /*-*/, 0,
    /* 0x50 */ 0, 0, 0x28 /*'*/, 0, 0x1A /*[*/, 0x0D /*=*/, 0, 0,
    /* 0x58 */ 0x3A /*caps*/, 0x36 /*rshift*/, 0x1C /*enter*/, 0x1B /*]*/, 0, 0x2B /*\*/, 0, 0,
    /* 0x60 */ 0, 0, 0, 0, 0, 0, 0x0E /*bksp*/, 0,
    /* 0x68 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x70 */ 0, 0, 0, 0, 0, 0, 0x01 /*esc*/, 0
};

unsigned char kbd_s2_to_s1(unsigned char base) {
    if (base < 0x78u && s2s1[base] != 0u)
        return s2s1[base];
    return base;
}

/* ---- set-1 make code -> ASCII ---- */

static const char scan_lower[0x3A] = {
    /* 0x00 */ 0, KEY_ESC, '1', '2', '3', '4', '5', '6',
    /* 0x08 */ '7', '8', '9', '0', '-', '=', KEY_BACKSPACE, KEY_TAB,
    /* 0x10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    /* 0x18 */ 'o', 'p', '[', ']', KEY_ENTER, 0, 'a', 's',
    /* 0x20 */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    /* 0x28 */ '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    /* 0x30 */ 'b', 'n', 'm', ',', '.', '/', 0, 0,
    /* 0x38 */ 0, ' '
};

static const char scan_shift[0x3A] = {
    /* 0x00 */ 0, KEY_ESC, '!', '@', '#', '$', '%', '^',
    /* 0x08 */ '&', '*', '(', ')', '_', '+', KEY_BACKSPACE, KEY_TAB,
    /* 0x10 */ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    /* 0x18 */ 'O', 'P', '{', '}', KEY_ENTER, 0, 'A', 'S',
    /* 0x20 */ 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    /* 0x28 */ '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    /* 0x30 */ 'B', 'N', 'M', '<', '>', '?', 0, 0,
    /* 0x38 */ 0, ' '
};

char kbd_lookup_char(unsigned char sc, int shift) {
    if (sc >= 0x3Au)
        return 0;
    return shift ? scan_shift[sc] : scan_lower[sc];
}
