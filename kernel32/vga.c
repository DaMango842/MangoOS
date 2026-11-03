// kernel32/vga.c - console: B8000 text mode, or the VBE framebuffer
// when vga_enable_fb() found a linear framebuffer at boot.
#include "vga.h"
#include "video.h"

#define VGA_BUFFER  ((volatile unsigned short *)0xB8000)
#define VGA_WIDTH   80
#define VGA_ROWS    25
#define VGA_INDEX   0x3D4
#define VGA_DATA    0x3D5

static int vga_row = 0;
static int vga_col = 0;
static unsigned char vga_attr = 0x07;
static int fb_mode = 0;

/* console snapshot for the MangoWin GUI to restore on exit */
static unsigned char sv_cell[VGA_ROWS][VGA_WIDTH];
static unsigned char sv_cattr[VGA_ROWS][VGA_WIDTH];
static int sv_row, sv_col;
static unsigned char sv_attr;
static int sv_cur_r = -1, sv_cur_c = -1;

/* VGA 16-color palette -> RGB888 */
static const unsigned int vga_pal16[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static unsigned int fb_fg(unsigned char attr) {
    return vga_pal16[attr & 0x0F];
}

static unsigned int fb_bg(unsigned char attr) {
    return vga_pal16[(attr >> 4) & 0x07];
}

/* shadow cells so the framebuffer console behaves like the B8000 one */
static unsigned char fb_cell[VGA_ROWS][VGA_WIDTH];
static unsigned char fb_cattr[VGA_ROWS][VGA_WIDTH];

static void fb_draw_cell(int row, int col) {
    __asm__ volatile("out %0, %1" : : "a"('D'), "d"((unsigned short)0xE9));
    video_char(col * 8, row * 16, fb_cell[row][col],
               fb_fg(fb_cattr[row][col]), fb_bg(fb_cattr[row][col]));
}

static void fb_paint(void) {
    int r, c;
    video_clear(fb_bg(vga_attr));
    for (r = 0; r < VGA_ROWS; r++)
        for (c = 0; c < VGA_WIDTH; c++)
            fb_draw_cell(r, c);
}

static void fb_scroll(void) {
    int r, c;
    for (r = 0; r < VGA_ROWS - 1; r++)
        for (c = 0; c < VGA_WIDTH; c++) {
            fb_cell[r][c] = fb_cell[r + 1][c];
            fb_cattr[r][c] = fb_cattr[r + 1][c];
        }
    for (c = 0; c < VGA_WIDTH; c++) {
        fb_cell[VGA_ROWS - 1][c] = ' ';
        fb_cattr[VGA_ROWS - 1][c] = vga_attr;
    }
    fb_paint();
}

static int fb_cur_r = -1;
static int fb_cur_c = -1;

static void fb_cursor(void) {
    if (fb_cur_r >= 0 && fb_cur_r < VGA_ROWS && fb_cur_c >= 0 && fb_cur_c < VGA_WIDTH)
        fb_draw_cell(fb_cur_r, fb_cur_c);   /* erase the old cursor */
    fb_cur_r = vga_row;
    fb_cur_c = vga_col;
    video_rect(vga_col * 8, vga_row * 16 + 14, 8, 2, 0xFFFFFF);
}

void vga_enable_fb(void) {
    __asm__ volatile("out %0, %1" : : "a"('a'), "d"((unsigned short)0xE9));
    __asm__ volatile("out %0, %1" : : "a"('b'), "d"((unsigned short)0xE9));
    if (video_init()) {
        __asm__ volatile("out %0, %1" : : "a"('c'), "d"((unsigned short)0xE9));
        fb_mode = 1;
        __asm__ volatile("out %0, %1" : : "a"('d'), "d"((unsigned short)0xE9));
    }
}

static void outb(unsigned short port, unsigned char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void vga_hw_cursor(void) {
    int off = vga_row * VGA_WIDTH + vga_col;
    outb(VGA_INDEX, 0x0E);
    outb(VGA_DATA, (unsigned char)(off >> 8));
    outb(VGA_INDEX, 0x0F);
    outb(VGA_DATA, (unsigned char)(off & 0xFF));
}

void vga_set_cursor(int row, int col) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= VGA_ROWS) row = VGA_ROWS - 1;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    vga_row = row;
    vga_col = col;
    if (fb_mode)
        fb_cursor();
    else
        vga_hw_cursor();
}

void vga_get_cursor(int *row, int *col) {
    *row = vga_row;
    *col = vga_col;
}

void vga_show_cursor(int on) {
    if (fb_mode)
        return;              /* no hardware cursor in fb mode */
    if (on) {
        outb(VGA_INDEX, 0x0A);
        outb(VGA_DATA, (unsigned char)((inb(VGA_DATA) & 0xC0) | 14));
        outb(VGA_INDEX, 0x0B);
        outb(VGA_DATA, (unsigned char)((inb(VGA_DATA) & 0xE0) | 15));
    } else {
        outb(VGA_INDEX, 0x0A);
        outb(VGA_DATA, 0x20);
    }
}

void vga_set_attr(unsigned char attr) {
    vga_attr = attr;
}

static void vga_scroll(void) {
    int i;
    for (i = 0; i < (VGA_ROWS - 1) * VGA_WIDTH; i++)
        VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
    for (; i < VGA_ROWS * VGA_WIDTH; i++)
        VGA_BUFFER[i] = (unsigned short)(vga_attr << 8 | ' ');
}

void vga_clear(void) {
    if (fb_mode) {
        __asm__ volatile("out %0, %1" : : "a"('e'), "d"((unsigned short)0xE9));
        int r0, c0;
        for (r0 = 0; r0 < VGA_ROWS; r0++)
            for (c0 = 0; c0 < VGA_WIDTH; c0++) {
                fb_cell[r0][c0] = ' ';
                fb_cattr[r0][c0] = vga_attr;
            }
        vga_row = 0;
        vga_col = 0;
        fb_cur_r = -1;
        fb_cur_c = -1;
        fb_paint();
        fb_cursor();
        __asm__ volatile("out %0, %1" : : "a"('f'), "d"((unsigned short)0xE9));
        return;
    }
    int i;
    for (i = 0; i < VGA_WIDTH * VGA_ROWS; i++)
        VGA_BUFFER[i] = (unsigned short)(vga_attr << 8 | ' ');
    vga_set_cursor(0, 0);
}

static void fb_putc(char c) {
    if (c == '\b') {
        if (vga_col > 0)
            vga_col--;
    } else if (c == '\n' || c == '\r') {
        vga_col = 0;
        if (c == '\n') {
            vga_row++;
            if (vga_row >= VGA_ROWS) {
                vga_row = VGA_ROWS - 1;
                fb_scroll();
            }
        }
    } else {
        fb_cell[vga_row][vga_col] = (unsigned char)c;
        fb_cattr[vga_row][vga_col] = vga_attr;
        fb_draw_cell(vga_row, vga_col);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
            if (vga_row >= VGA_ROWS) {
                vga_row = VGA_ROWS - 1;
                fb_scroll();
            }
        }
    }
    fb_cursor();
}

void vga_putc(char c) {
    if (fb_mode) {
        fb_putc(c);
        return;
    }
    if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            vga_hw_cursor();
        }
        return;
    }
    if (c == '\n' || c == '\r') {
        vga_col = 0;
        if (c == '\n') {
            vga_row++;
            if (vga_row >= VGA_ROWS)
                vga_row = VGA_ROWS - 1, vga_scroll();
        }
    } else {
        VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] =
            (unsigned short)(vga_attr << 8 | (unsigned char)c);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
            if (vga_row >= VGA_ROWS)
                vga_row = VGA_ROWS - 1, vga_scroll();
        }
    }
    vga_hw_cursor();
}

void vga_write(const char *text) {
    while (*text)
        vga_putc(*text++);
}

void vga_snapshot(void) {
    int r, c;

    if (!fb_mode)
        return;
    for (r = 0; r < VGA_ROWS; r++)
        for (c = 0; c < VGA_WIDTH; c++) {
            sv_cell[r][c] = fb_cell[r][c];
            sv_cattr[r][c] = fb_cattr[r][c];
        }
    sv_row = vga_row;
    sv_col = vga_col;
    sv_attr = vga_attr;
    sv_cur_r = fb_cur_r;
    sv_cur_c = fb_cur_c;
}

void vga_restore(void) {
    int r, c;

    if (!fb_mode)
        return;
    for (r = 0; r < VGA_ROWS; r++)
        for (c = 0; c < VGA_WIDTH; c++) {
            fb_cell[r][c] = sv_cell[r][c];
            fb_cattr[r][c] = sv_cattr[r][c];
        }
    vga_row = sv_row;
    vga_col = sv_col;
    vga_attr = sv_attr;
    fb_cur_r = sv_cur_r;
    fb_cur_c = sv_cur_c;
    fb_paint();
    fb_cursor();
}