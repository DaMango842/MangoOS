// kernel32/video.c - VBE linear framebuffer driver (set up by stage2)
// Bootinfo block (written by boot/vbe.asm at physical 0x500):
//   0x50A  dword  status (1 = framebuffer active)
//   0x50E  dword  framebuffer physical address
//   0x512  word   width
//   0x514  word   height
//   0x516  word   pitch (bytes per scanline)
//   0x518  byte   bits per pixel
// Font: the 8x16 glyph table is linked in (font16.c); the pointer at
// 0x70000 is ignored (BIOS font copy is unreliable in some firmware).

#define BOOT_VBE       ((volatile unsigned int *)0x50A)
#define BOOT_FB        ((volatile unsigned int *)0x50E)
#define BOOT_W         ((volatile unsigned short *)0x512)
#define BOOT_H         ((volatile unsigned short *)0x514)
#define BOOT_PITCH     ((volatile unsigned short *)0x516)
#define BOOT_BPP       ((volatile unsigned char *)0x518)

#include "video.h"
#include "font16.h"

static volatile int fb_ready = 0;
static volatile unsigned int fb_w, fb_h, fb_pitch, fb_bpp;
static volatile unsigned char *fb;

int video_active(void) {
    return fb_ready;
}

unsigned int video_width(void) {
    return fb_w;
}

unsigned int video_height(void) {
    return fb_h;
}

unsigned int video_bpp(void) {
    return fb_bpp;
}

static void dbg(char c) {
    __asm__ volatile("out %0, %1" : : "a"(c), "d"((unsigned short)0xE9));
}

static void dbg_hex(unsigned int v) {
    int i;
    __asm__ volatile("out %0, %1" : : "a"('q'), "d"((unsigned short)0xE9));
    for (i = 7; i >= 0; i--) {
        unsigned char c = (unsigned char)('0' + ((v >> (i * 4)) & 0xF));
        if (c > '9')
            c = (unsigned char)(c + 7);
        __asm__ volatile("out %0, %1" : : "a"(c), "d"((unsigned short)0xE9));
    }
}

/* read-back probe: write a pattern into the LFB, then read it back */
static void fb_probe(void) {
    volatile unsigned int *p = (volatile unsigned int *)fb;
    unsigned int x;

    x = *p;                                   /* read first */
    dbg('R');
    *p = 0x12345678;                          /* write */
    dbg('W');
    x = *p;                                   /* read back */
    dbg(x == 0x12345678 ? 'K' : 'M');
}

int video_init(void) {
    dbg('1');
    if (*BOOT_VBE != 1)
        return 0;
    dbg('2');
    if (*BOOT_BPP != 32 && *BOOT_BPP != 24 && *BOOT_BPP != 16)
        return 0;
    dbg('3');
    fb = (volatile unsigned char *)(unsigned long)*BOOT_FB;
    if (fb == 0)
        return 0;
    dbg('4');
    fb_w = *BOOT_W;
    fb_h = *BOOT_H;
    fb_pitch = *BOOT_PITCH;
    fb_bpp = *BOOT_BPP;
    if (fb_w == 0 || fb_h == 0 || fb_pitch == 0)
        return 0;
    dbg('5');
    fb_probe();
    fb_ready = 1;
    dbg('6');
    return 1;
}

static void video_pixel(int x, int y, unsigned int rgb) {
    volatile unsigned char *p;
    unsigned int bpp;

    if (x < 0 || y < 0 || (unsigned int)x >= fb_w || (unsigned int)y >= fb_h)
        return;
    bpp = fb_bpp;
    p = fb + (unsigned int)y * fb_pitch + (unsigned int)x * (bpp / 8);
    if (bpp == 32) {
        p[0] = (unsigned char)(rgb);
        p[1] = (unsigned char)(rgb >> 8);
        p[2] = (unsigned char)(rgb >> 16);
        p[3] = 0;
    } else if (bpp == 24) {
        p[0] = (unsigned char)(rgb);
        p[1] = (unsigned char)(rgb >> 8);
        p[2] = (unsigned char)(rgb >> 16);
    } else if (bpp == 16) {
        p[0] = (unsigned char)(((rgb & 0xF8) << 3) | ((rgb & 0xFC00) >> 11));
        p[1] = (unsigned char)(((rgb & 0xF80000) >> 5) |
                               ((rgb & 0xFC0000) >> 16));
    }
}

void video_clear(unsigned int rgb) {
    volatile unsigned int *p32;
    unsigned int n, bpp;
    unsigned int px;

    bpp = fb_bpp;
    px = (bpp == 16) ? 0 : rgb;
    if (bpp == 16) {
        px = (unsigned int)(((rgb & 0xF8) << 8) | ((rgb & 0xFC00) >> 5) |
                            ((rgb & 0xF80000) >> 19));
        px |= px << 16;
    }
    p32 = (volatile unsigned int *)fb;
    n = fb_w * fb_h * (bpp / 8) / 4;
    while (n--)
        *p32++ = px;
}

void video_rect(int x, int y, int w, int h, unsigned int rgb) {
    int dx, dy;

    for (dy = 0; dy < h; dy++)
        for (dx = 0; dx < w; dx++)
            video_pixel(x + dx, y + dy, rgb);
}

void video_line(int x0, int y0, int x1, int y1, unsigned int rgb) {
    int dx, dy, sx, sy, err, e2;

    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y1 - y0 : y0 - y1;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx - dy;
    for (;;) {
        video_pixel(x0, y0, rgb);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void video_frame(int x, int y, int w, int h, unsigned int rgb) {
    if (w < 1 || h < 1)
        return;
    video_line(x, y, x + w - 1, y, rgb);
    video_line(x + w - 1, y, x + w - 1, y + h - 1, rgb);
    video_line(x, y + h - 1, x + w - 1, y + h - 1, rgb);
    video_line(x, y, x, y + h - 1, rgb);
}

/* Exact PI (30 significant digits). */
#define CIRCLE_PI 3.1415926535897932384626433832795

static int fpu_ok = 0;

static void fpu_init(void) {
    unsigned int cr0;

    if (fpu_ok)
        return;
    __asm__ volatile("movl %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x04u;                 /* clear EM */
    cr0 |= 0x02u;                  /* set MP */
    __asm__ volatile("movl %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("fninit");
    fpu_ok = 1;
}

static double fcos(double a) {
    double r;
    __asm__ volatile("fcos" : "=t"(r) : "0"(a));
    return r;
}

static double fsin(double a) {
    double r;
    __asm__ volatile("fsin" : "=t"(r) : "0"(a));
    return r;
}

static int iround(double v) {
    int i = (int)v;
    double d = v - (double)i;
    if (d >= 0.5)
        i++;
    else if (d <= -0.5)
        i--;
    return i;
}

void video_circle(int cx, int cy, int r, unsigned int rgb) {
    long n, i;
    double step;

    if (r < 1)
        return;
    fpu_init();
    n = (long)(2.0 * CIRCLE_PI * (double)r) + 1;
    step = 2.0 * CIRCLE_PI / (double)n;
    for (i = 0; i < n; i++) {
        double a = step * (double)i;
        int x = cx + iround((double)r * fcos(a));
        int y = cy - iround((double)r * fsin(a));
        video_pixel(x, y, rgb);
    }
}

void video_blit32(const void *src) {
    const unsigned int *s = (const unsigned int *)src;
    unsigned int y;

    if (!fb_ready || fb_bpp != 32)
        return;
    for (y = 0; y < fb_h; y++) {
        volatile unsigned int *row =
            (volatile unsigned int *)(fb + (unsigned int)y * fb_pitch);
        unsigned int x;
        for (x = 0; x < fb_w; x++)
            row[x] = s[y * fb_w + x];
    }
}

/* Copy a rectangular region of a 32bpp linear buffer to the framebuffer.
   Used by the GUI for small updates (e.g. the cursor) without a full
   frame blit. */
void video_blit_rect(int x, int y, int w, int h, const void *src) {
    const unsigned int *s = (const unsigned int *)src;
    unsigned int dy;

    if (!fb_ready || fb_bpp != 32)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0)
        return;
    if (x >= (int)fb_w || y >= (int)fb_h)
        return;
    if (x + w > (int)fb_w)
        w = fb_w - (unsigned int)x;
    if (y + h > (int)fb_h)
        h = fb_h - (unsigned int)y;
    for (dy = 0; dy < (unsigned int)h; dy++) {
        volatile unsigned int *row =
            (volatile unsigned int *)(fb + (unsigned int)(y + dy) * fb_pitch);
        unsigned int dx;
        for (dx = 0; dx < (unsigned int)w; dx++)
            row[(unsigned int)x + dx] = s[(unsigned int)(y + dy) * fb_w +
                                          (unsigned int)x + dx];
    }
}

/* Like video_blit_rect, but skips pixels that already match prev (a
   snapshot with w-stride).  Only differing pixels are written and prev
   is updated, so a static picture costs no framebuffer traffic.  src is
   indexed with the full fb_w stride (the whole back buffer). */
void video_blit_rect_cmp(int x, int y, int w, int h, const void *src,
                         const void *prev) {
    const unsigned int *s = (const unsigned int *)src;
    unsigned int *p = (unsigned int *)prev;
    unsigned int dy;

    if (!fb_ready || fb_bpp != 32)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0)
        return;
    if (x >= (int)fb_w || y >= (int)fb_h)
        return;
    if (x + w > (int)fb_w)
        w = fb_w - (unsigned int)x;
    if (y + h > (int)fb_h)
        h = fb_h - (unsigned int)y;
    for (dy = 0; dy < (unsigned int)h; dy++) {
        volatile unsigned int *row =
            (volatile unsigned int *)(fb + (unsigned int)(y + dy) * fb_pitch);
        const unsigned int *sr = s + (unsigned int)(y + dy) * fb_w +
                                 (unsigned int)x;
        unsigned int *pr = p + (unsigned int)dy * (unsigned int)w;
        unsigned int dx;
        for (dx = 0; dx < (unsigned int)w; dx++) {
            if (sr[dx] != pr[dx]) {
                row[(unsigned int)x + dx] = sr[dx];
                pr[dx] = sr[dx];
            }
        }
    }
}

/* Like video_blit_rect_cmp, but prev is a FULL-SCREEN snapshot stored
   with the back-buffer stride (fb_w), not a per-call w-stride snapshot.
   The dirty rect may therefore change size between calls - used by the
   GUI flush, where the accumulated dirty region varies every frame.
   Every framebuffer write costs a device-model round trip under QEMU
   TCG, so skipping unchanged pixels is the main lever for fps. */
void video_blit_rect_cmp_fs(int x, int y, int w, int h,
                            const void *src, const void *prev) {
    const unsigned int *s = (const unsigned int *)src;
    unsigned int *p = (unsigned int *)prev;
    unsigned int dy;

    if (!fb_ready || fb_bpp != 32)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0)
        return;
    if (x >= (int)fb_w || y >= (int)fb_h)
        return;
    if (x + w > (int)fb_w)
        w = fb_w - (unsigned int)x;
    if (y + h > (int)fb_h)
        h = fb_h - (unsigned int)y;
    for (dy = 0; dy < (unsigned int)h; dy++) {
        volatile unsigned int *row =
            (volatile unsigned int *)(fb + (unsigned int)(y + dy) * fb_pitch);
        const unsigned int *sr = s + (unsigned int)(y + dy) * fb_w +
                                 (unsigned int)x;
        unsigned int *pr = p + (unsigned int)(y + dy) * fb_w +
                           (unsigned int)x;
        unsigned int dx = 0, vw = (unsigned int)w;

        /* compare 4 pixels per iteration: a full repaint unions into a
           fullscreen dirty rect, so this loop alone visits 480K pixels
           and its per-pixel overhead dominates the frame time */
        for (; dx + 4 <= vw; dx += 4) {
            unsigned int a = sr[dx], b = sr[dx + 1];
            unsigned int c = sr[dx + 2], d = sr[dx + 3];

            if (a != pr[dx]) {
                row[(unsigned int)x + dx] = a;
                pr[dx] = a;
            }
            if (b != pr[dx + 1]) {
                row[(unsigned int)x + dx + 1] = b;
                pr[dx + 1] = b;
            }
            if (c != pr[dx + 2]) {
                row[(unsigned int)x + dx + 2] = c;
                pr[dx + 2] = c;
            }
            if (d != pr[dx + 3]) {
                row[(unsigned int)x + dx + 3] = d;
                pr[dx + 3] = d;
            }
        }
        for (; dx < vw; dx++) {
            unsigned int a = sr[dx];

            if (a != pr[dx]) {
                row[(unsigned int)x + dx] = a;
                pr[dx] = a;
            }
        }
    }
}

/* Change-detection blit with nearest-neighbor upscaling: src is a
   small surface (src_stride pixels per row) rendered at scale x scale
   at (x, y).  Pixels that match prev (stored with w-stride) are
   skipped; changed pixels write a scale x scale block and update
   prev.  Used by the FC window so a 256x240 frame costs a quarter of
   the render work while the framebuffer traffic stays the same. */
void video_blit_rect_scale_cmp(int x, int y, int w, int h,
                               const void *src, unsigned int src_stride,
                               const void *prev, unsigned int scale) {
    const unsigned int *s = (const unsigned int *)src;
    unsigned int *p = (unsigned int *)prev;
    unsigned int dy;

    if (!fb_ready || fb_bpp != 32 || scale == 0)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0)
        return;
    if (x >= (int)fb_w || y >= (int)fb_h)
        return;
    if ((unsigned int)x + (unsigned int)w * scale > fb_w)
        w = (fb_w - (unsigned int)x) / scale;
    if ((unsigned int)y + (unsigned int)h * scale > fb_h)
        h = (fb_h - (unsigned int)y) / scale;
    if (scale == 2u) {
        /* FC path (2x2 nearest neighbour).  The generic loop below
           recomputes the framebuffer pointer and runs a two-deep pixel
           loop for EVERY changed source pixel; with a scrolling NES frame
           almost every pixel changes, so that bookkeeping -- not the
           stores -- dominated the frame budget under TCG.  Here both
           destination rows are hoisted per source row and the inner body
           is straight-line. */
        for (dy = 0; dy < (unsigned int)h; dy++) {
            const unsigned int *sr = s + (unsigned int)dy * src_stride;
            unsigned int *pr = p + (unsigned int)dy * (unsigned int)w;
            volatile unsigned char *b0 = fb +
                (unsigned int)(y + dy * 2u) * fb_pitch;
            volatile unsigned int *r0 =
                (volatile unsigned int *)b0 + (unsigned int)x;
            volatile unsigned int *r1 =
                (volatile unsigned int *)(b0 + fb_pitch) + (unsigned int)x;
            unsigned int dx;

            for (dx = 0; dx < (unsigned int)w; dx++) {
                unsigned int c = sr[dx];

                if (c != pr[dx]) {
                    unsigned int o = dx * 2u;

                    pr[dx] = c;
                    r0[o] = c;
                    r0[o + 1u] = c;
                    r1[o] = c;
                    r1[o + 1u] = c;
                }
            }
        }
        return;
    }
    for (dy = 0; dy < (unsigned int)h; dy++) {
        const unsigned int *sr = s + (unsigned int)dy * src_stride;
        unsigned int *pr = p + (unsigned int)dy * (unsigned int)w;
        unsigned int dx;
        for (dx = 0; dx < (unsigned int)w; dx++) {
            if (sr[dx] != pr[dx]) {
                unsigned int sy, sx;
                volatile unsigned int *row =
                    (volatile unsigned int *)(fb +
                        (unsigned int)(y + dy * scale) * fb_pitch) +
                    (unsigned int)x + dx * scale;

                pr[dx] = sr[dx];
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++)
                        row[sx] = sr[dx];
                    row += fb_pitch / 4u;
                }
            }
        }
    }
}

void video_char(int x, int y, unsigned int ch, unsigned int fg,
                unsigned int bg) {
    const unsigned char *glyph;
    int gx, gy;
    unsigned char bits;

    if (ch > 255)
        return;
    glyph = &font8x16[ch * FONT_GLYPH_H];
    for (gy = 0; gy < FONT_GLYPH_H; gy++) {
        bits = glyph[gy];
        for (gx = 0; gx < 8; gx++)
            video_pixel(x + gx, y + gy,
                        (bits & (0x80 >> gx)) ? fg : bg);
    }
}