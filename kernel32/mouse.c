// kernel32/mouse.c - PS/2 mouse driver (polling, 3-byte protocol)
#include "mouse.h"

#define PS2_STATUS  0x64
#define PS2_DATA    0x60

#define MOUSE_STEP  8       /* pixels per character-cell step */
#define MOUSE_CHAR  0xDB    /* block glyph used for the pointer */
#define MOUSE_ATTR  0x0F    /* bright white on black, blinking */
#define MOUSE_BLINK 0x80    /* attribute blink bit */

static unsigned char mouse_cycle = 0;
static unsigned char mouse_buf[3];
static int mouse_buttons = 0;

int mouse_x = 40;            /* character columns, 0..79 */
int mouse_y = 12;            /* character rows,    0..24 */
int mouse_enabled = 0;

static int mouse_dx_acc = 0;
static int mouse_dy_acc = 0;

/* pixel-pointer mode (used by the MangoWin GUI) */
static int mouse_mode = 0;               /* 0 = text pointer, 1 = pixel */
static int mouse_px = 400;               /* pixel coordinates 0..799 */
static int mouse_py = 300;               /* 0..599 */

static unsigned short mouse_saved;
static unsigned char mouse_painted = 0;

/* Switch the pointer between text mode (B8000 cell) and pixel mode.
   Returns the previous mode. */
int mouse_set_mode(int mode) {
    int old = mouse_mode;

    if (mode == 1) {
        if (mouse_painted) {
            volatile unsigned short *buf =
                (volatile unsigned short *)0xB8000;
            buf[mouse_y * 80 + mouse_x] = mouse_saved;
            mouse_painted = 0;
        }
        mouse_px = mouse_x * 10;
        mouse_py = mouse_y * 24;
        mouse_mode = 1;
    } else {
        mouse_mode = 0;
        mouse_dx_acc = 0;
        mouse_dy_acc = 0;
    }
    return old;
}

int mouse_get_pos(int *x, int *y) {
    if (mouse_mode != 1)
        return -1;
    *x = mouse_px;
    *y = mouse_py;
    return 0;
}

static void outb(unsigned short port, unsigned char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int ps2_wait_input(void) {
    int timeout;
    for (timeout = 10000; timeout > 0; timeout--) {
        unsigned char status = inb(PS2_STATUS);
        if (status & 0x01)
            return 1;
    }
    return 0;
}

static int ps2_wait_output(void) {
    int timeout;
    for (timeout = 10000; timeout > 0; timeout--) {
        unsigned char status = inb(PS2_STATUS);
        if (!(status & 0x02))
            return 1;
    }
    return 0;
}

static void mouse_flush(void) {
    int timeout;
    for (timeout = 10000; timeout > 0; timeout--) {
        if (!(inb(PS2_STATUS) & 0x01))
            break;
        (void)inb(PS2_DATA);
    }
}

static void mouse_cursor(int x, int y);

/* Send one command to the mouse (0xD4 prefix) and verify the ACK. */
static int mouse_write(unsigned char cmd) {
    if (!ps2_wait_output())
        return 0;
    outb(PS2_STATUS, 0xD4);

    if (!ps2_wait_output())
        return 0;
    outb(PS2_DATA, cmd);

    if (!ps2_wait_input())
        return 0;
    return inb(PS2_DATA) == 0xFA;
}

/* Enable the auxiliary device and put the mouse in data-reporting mode. */
int mouse_init(void) {
    /* Try to enable auxiliary device - if it fails, mouse is not present */
    if (!ps2_wait_output())
        return 0;
    outb(PS2_STATUS, 0xA8);              /* enable auxiliary device */

    /* Send mouse reset command */
    if (!ps2_wait_output())
        return 0;
    outb(PS2_STATUS, 0xD4);
    if (!ps2_wait_output())
        return 0;
    outb(PS2_DATA, 0xF6);

    if (!ps2_wait_input())
        return 0;
    unsigned char ack = inb(PS2_DATA);
    if (ack != 0xFA)
        return 0;

    /* Enable data reporting */
    if (!ps2_wait_output())
        return 0;
    outb(PS2_STATUS, 0xD4);
    if (!ps2_wait_output())
        return 0;
    outb(PS2_DATA, 0xF4);

    if (!ps2_wait_input())
        return 0;
    ack = inb(PS2_DATA);
    if (ack != 0xFA)
        return 0;

    mouse_cycle = 0;
    mouse_painted = 0;
    mouse_dx_acc = 0;
    mouse_dy_acc = 0;
    mouse_x = 40;
    mouse_y = 12;
    mouse_enabled = 1;
    mouse_cursor(mouse_x, mouse_y);
    return 1;
}

/* Move the pointer on screen: restore the old cell, save the new one. */
static void mouse_cursor(int x, int y) {
    volatile unsigned short *buf = (volatile unsigned short *)0xB8000;

    buf += mouse_y * 80 + mouse_x;
    if (mouse_painted)
        *buf = mouse_saved;                     /* erase old pointer */

    mouse_x = x;
    mouse_y = y;
    buf = (volatile unsigned short *)0xB8000 + mouse_y * 80 + mouse_x;
    mouse_saved = *buf;                         /* remember background */
    *buf = (unsigned short)((MOUSE_ATTR | MOUSE_BLINK) << 8 | MOUSE_CHAR);
    mouse_painted = 1;
}

static void mouse_apply_delta(void) {
    int xd = (int)(int8_t)mouse_buf[1];
    int yd = (int)(int8_t)mouse_buf[2];

    if (mouse_mode == 1) {
        mouse_px += xd;
        mouse_py -= yd;                     /* y is up-positive */
        if (mouse_px < 0) mouse_px = 0;
        if (mouse_px > 799) mouse_px = 799;
        if (mouse_py < 0) mouse_py = 0;
        if (mouse_py > 599) mouse_py = 599;
        return;
    }

    int dx = 0, dy = 0;
    mouse_dx_acc += xd;
    mouse_dy_acc -= yd;                     /* y is up-positive */

    while (mouse_dx_acc >= MOUSE_STEP) {
        dx++;
        mouse_dx_acc -= MOUSE_STEP;
    }
    while (mouse_dx_acc <= -MOUSE_STEP) {
        dx--;
        mouse_dx_acc += MOUSE_STEP;
    }
    while (mouse_dy_acc >= MOUSE_STEP) {
        dy++;
        mouse_dy_acc -= MOUSE_STEP;
    }
    while (mouse_dy_acc <= -MOUSE_STEP) {
        dy--;
        mouse_dy_acc += MOUSE_STEP;
    }

    if (dx || dy) {
        int nx = mouse_x + dx, ny = mouse_y + dy;
        if (nx < 0) nx = 0;
        if (nx > 79) nx = 79;
        if (ny < 0) ny = 0;
        if (ny > 24) ny = 24;
        mouse_cursor(nx, ny);
    }
}

/* Drain any queued mouse packets. Call frequently from the main loop. */
void mouse_poll(void) {
    while ((inb(PS2_STATUS) & 0x21) == 0x21) {  /* buffer full + aux */
        unsigned char b = inb(PS2_DATA);

        if (mouse_cycle == 0) {
            /* first byte must be a status byte (bit 3 always set) */
            if (!(b & 0x08) || (b & 0xC0)) {   /* 0xC0 = x/y overflow */
                mouse_cycle = 0;               /* error: resync */
                continue;
            }
            mouse_buf[0] = b;
            mouse_cycle = 1;
        } else {
            mouse_buf[mouse_cycle++] = b;
            if (mouse_cycle == 3) {
                mouse_cycle = 0;
                mouse_buttons = mouse_buf[0] & 0x07;
                mouse_apply_delta();
            }
        }
    }
}

int mouse_read_buttons(void) {
    return mouse_buttons;
}