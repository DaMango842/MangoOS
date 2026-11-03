// kernel32/keyboard.c - PS/2 keyboard driver (polling; device speaks
// scan code set 2, kbdmap.c converts to set 1 at one ingestion point)
#include "keyboard.h"
#include "kbdmap.h"
#include "serial.h"

#define KBD_STATUS  0x64
#define KBD_DATA    0x60

static void kbd_debug_scancode(const char *tag, unsigned char sc) {
    static const char hex[] = "0123456789ABCDEF";
    char b[48];
    int i = 0;

    b[i++] = 'K'; b[i++] = 'B'; b[i++] = 'D'; b[i++] = ':';
    b[i++] = ' ';
    while (*tag)
        b[i++] = *tag++;
    b[i++] = ' ';
    b[i++] = '0'; b[i++] = 'x';
    b[i++] = hex[(sc >> 4) & 15u];
    b[i++] = hex[sc & 15u];
    b[i++] = '\r'; b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

static unsigned char kbd_shift = 0;
static unsigned char kbd_caps = 0;
static unsigned char kbd_ext = 0;          /* 0xE0 prefix pending */
static unsigned char kbd_release = 0;      /* 0xF0 prefix pending */
static unsigned char kbd_down[32] = { 0 }; /* scancode level state */

static void outb(unsigned short port, unsigned char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void kbd_init(void) {
    int timeout;
    unsigned char ccb;

    /* Flush any pending data - but don't hang forever */
    for (timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(KBD_STATUS) & 0x01))
            break;
        (void)inb(KBD_DATA);
    }

    /* Take over the controller configuration.  SeaBIOS/QEMU firmware can
       leave the CCB with both PS/2 interfaces DISABLED (observed: 0x32,
       bits 4+5 set), in which case no scancode ever reaches the output
       buffer and polling reads nothing.  Read-modify-write via command
       0x20/0x60: enable both device interfaces, FORCE translation on
       (bit 6 disables translation when set - the tables below are set-1),
       leave IRQs off (this driver polls). */
    serial_write("KBD: ccb read\r\n");
    outb(KBD_STATUS, 0x20);                  /* read controller command byte */
    for (timeout = 10000; timeout > 0; timeout--) {
        if (inb(KBD_STATUS) & 0x01)
            break;
    }
    ccb = inb(KBD_DATA);
    /* Clear bit 6 (translation), the interface disable bits (4/5) and
       the IRQ enables (0/1).  Empirically on this QEMU the translate
       behaviour is inverted versus the datasheet: with bit6 SET the
       controller still pushed every byte through its set-2->set-1
       table (set-1 'M'=0x32 re-decoded as set-2 'B' -> 0x30), while
       bit6 CLEAR passes device bytes through raw.  Since we command
       the keyboard to transmit set 1 below, clear bit 6 and use the
       raw stream. */
    ccb = (unsigned char)(ccb & ~0x73u);
    serial_write("KBD: ccb write\r\n");
    outb(KBD_STATUS, 0x60);                  /* write controller command byte */
    for (timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(KBD_STATUS) & 0x02))
            break;
    }
    outb(KBD_DATA, ccb);

    /* Reset the keyboard device: it answers ACK (0xFA) then the self-test
       result (normally 0xAA) and restarts scanning in set 1. */
    serial_write("KBD: dev reset\r\n");
    for (timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(KBD_STATUS) & 0x02))
            break;
    }
    outb(KBD_DATA, 0xFF);
    for (timeout = 20000; timeout > 0; timeout--) {
        if (inb(KBD_STATUS) & 0x01)
            break;
    }
    (void)inb(KBD_DATA);                     /* ACK */
    for (timeout = 20000; timeout > 0; timeout--) {
        if (inb(KBD_STATUS) & 0x01)
            break;
    }
    (void)inb(KBD_DATA);                     /* BAT result (0xAA expected) */

    /* Flush whatever the reset left behind */
    serial_write("KBD: flush\r\n");
    for (timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(KBD_STATUS) & 0x01))
            break;
        (void)inb(KBD_DATA);
    }

    /* Command the keyboard to transmit scan code set 2 (0xF0, param
       0x02), each step answered with ACK (0xFA), then re-enable
       scanning (0xF4).  Set 2 is deliberate: on this firmware/QEMU
       path the controller translation cannot be trusted (it once
       translated make bytes while leaving break codes untouched, so
       press/release lived in two different code spaces).  Raw set 2
       is symmetric by definition: make = code, break = F0 + the SAME
       code; kbd_get_key converts every byte to set 1 in one place. */
    serial_write("KBD: set2\r\n");
    for (timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(KBD_STATUS) & 0x02))
            break;
    }
    outb(KBD_DATA, 0xF0);
    for (timeout = 20000; timeout > 0; timeout--) {
        if (inb(KBD_STATUS) & 0x01)
            break;
    }
    (void)inb(KBD_DATA);                     /* ACK */
    for (timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(KBD_STATUS) & 0x02))
            break;
    }
    outb(KBD_DATA, 0x02);                    /* set 2 */
    for (timeout = 20000; timeout > 0; timeout--) {
        if (inb(KBD_STATUS) & 0x01)
            break;
    }
    (void)inb(KBD_DATA);                     /* ACK */
    for (timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(KBD_STATUS) & 0x02))
            break;
    }
    outb(KBD_DATA, 0xF4);                    /* enable scanning */
    for (timeout = 20000; timeout > 0; timeout--) {
        if (inb(KBD_STATUS) & 0x01)
            break;
    }
    (void)inb(KBD_DATA);                     /* ACK */
    serial_write("KBD: init complete\r\n");
}

/* C handler for the defensive IRQ1 gate (see irq1.asm): the driver
   polls, so an interrupt means stale firmware state or a stray byte -
   ack the PIC and drain the 8042 output buffer. */
void irq1_handler(void) {
    int timeout;

    (void)inb(KBD_DATA);                     /* drain pending scancode */
    for (timeout = 100; timeout > 0; timeout--) {
        if (!(inb(KBD_STATUS) & 0x01))
            break;
        (void)inb(KBD_DATA);
    }
    outb(0x20, 0x20);                        /* EOI to master PIC */
}

static int kbd_pending(void) {
    /* PS/2 status: bit 0 = output buffer full, bit 5 = auxiliary device (mouse).
       Only process data when buffer is full AND it's NOT from the mouse. */
    unsigned char st = inb(KBD_STATUS);
    return (st & 0x01u) != 0u && (st & 0x20u) == 0u;
}

/* Set-2 -> set-1 conversion and the ASCII mapping tables live in
   kbdmap.c (see kbdmap.h) so the driver state machine stays table-free. */

/* Level state: is a scancode currently held down? (for game input) */
int kbd_scan_down(unsigned char sc) {
    return kbd_down[sc >> 3] & (1u << (sc & 7));
}

void kbd_copy_state(unsigned char *dst) {
    int i;

    for (i = 0; i < 32; i++)
        dst[i] = kbd_down[i];
}

void kbd_clear_state(void) {
    int i;

    for (i = 0; i < 32; i++)
        kbd_down[i] = 0;
    kbd_shift = 0;
    kbd_caps = 0;
    kbd_ext = 0;
    kbd_release = 0;
}

static void kbd_set_down(unsigned char sc, int down) {
    if (down)
        kbd_down[sc >> 3] |= (unsigned char)(1u << (sc & 7));
    else
        kbd_down[sc >> 3] &= (unsigned char)~(1u << (sc & 7));
}

/* Returns an ASCII key code, or 0 when no key is available. */
int kbd_get_key(void) {
    unsigned char sc, ch;

    if (!kbd_pending())
        return 0;

    sc = inb(KBD_DATA);
    sc = kbd_s2_to_s1(sc);           /* one symmetric conversion point */

    if (sc == 0xE0) {                    /* extended prefix: arrows etc. */
        kbd_ext = 1;
        kbd_debug_scancode("E0", sc);
        return 0;
    }
    if (sc == 0xF0) {                    /* release prefix: next byte is key up */
        kbd_release = 1;
        kbd_debug_scancode("F0", sc);
        return 0;
    }

    /* Scancode flow (all bytes already in set-1 space): E0-prefix /
       F0-release / base-byte.  The key state bitmap must be updated
       with the base byte, and extended keys are tracked by setting
       bit7 on that base value. */
    if (sc < 0x84 || kbd_ext || kbd_release || (sc & 0x80)) {
        unsigned char key = (unsigned char)(sc & 0x7Fu);
        int down = !(kbd_release || (sc & 0x80));

        if (kbd_ext)
            key |= 0x80u;
        kbd_set_down(key, down);
        kbd_debug_scancode(down ? "DOWN" : "UP", key);
    }

    if (sc == 0xAA || sc == 0xB6) {      /* left/right shift up */
        kbd_shift = 0;
        kbd_ext = 0;
        kbd_release = 0;
        return 0;
    }
    if (sc & 0x80) {
        unsigned char base = sc & 0x7Fu;
        if (base == 0x2A || base == 0x36) kbd_shift = 0;  /* break: clear shift */
        kbd_ext = 0;
        kbd_release = 0;
        return 0;
    }

    /* Release handling MUST precede the press checks below: a converted
       shift release arrives as plain 0x2A/0x36 (F0 0x12 -> 0x2A) and
       would otherwise fall into "shift down" and lock the keyboard into
       uppercase forever. */
    if (kbd_release) {                   /* set-2 break: F0 + code */
        if (sc == 0x2A || sc == 0x36) kbd_shift = 0;  /* shift released */
        kbd_ext = 0;
        kbd_release = 0;
        return 0;
    }
    if (kbd_ext) {                       /* extended keys yield no ASCII */
        kbd_ext = 0;
        return 0;
    }

    if (sc == 0x2A || sc == 0x36) {      /* left/right shift down */
        kbd_shift = 1;
        return 0;
    }
    if (sc == 0x3A) {                    /* caps lock */
        kbd_caps = !kbd_caps;
        return 0;
    }

    if (sc >= 0x3A)
        return 0;

    ch = kbd_lookup_char(sc, kbd_shift);
    if (!ch)
        return 0;

    if (ch >= 'a' && ch <= 'z' && kbd_caps)
        ch -= ('a' - 'A');
    else if (ch >= 'A' && ch <= 'Z' && kbd_caps)
        ch += ('a' - 'A');

    return (int)ch;
}