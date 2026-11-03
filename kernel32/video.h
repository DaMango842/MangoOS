// kernel32/video.h - VBE linear framebuffer primitives.
//
// The mode itself is set by boot/vbe.asm in real mode, which leaves a
// bootinfo block at physical 0x500 (address, geometry, pitch, bpp).
// video_init() reads that block and probes the framebuffer with a
// write/read-back before declaring it usable.
//
// Every blit_* helper clips against the framebuffer bounds and is a no-op
// when no LFB is present or bpp != 32.  The "_cmp" variants keep a
// caller's snapshot buffer and only touch pixels that actually changed,
// which is what makes a static desktop produce zero framebuffer traffic
// under QEMU TCG.
#ifndef MANGO_VIDEO_H
#define MANGO_VIDEO_H

/* Probe the bootinfo framebuffer.  0 on success. */
int video_init(void);

int video_active(void);
unsigned int video_width(void);
unsigned int video_height(void);
unsigned int video_bpp(void);

/* Primitives.  rgb is 0x00RRGGBB. */
void video_clear(unsigned int rgb);
void video_rect(int x, int y, int w, int h, unsigned int rgb);
void video_line(int x0, int y0, int x1, int y1, unsigned int rgb);
void video_frame(int x, int y, int w, int h, unsigned int rgb);
void video_circle(int cx, int cy, int r, unsigned int rgb);

/* Full-screen 32bpp blit of a w*h image. */
void video_blit32(const void *src);

/* Sub-rect blit; src is a w-strided 32bpp buffer. */
void video_blit_rect(int x, int y, int w, int h, const void *src);

/* Blit only the pixels that differ from the w*h snapshot in prev. */
void video_blit_rect_cmp(int x, int y, int w, int h, const void *src,
                         const void *prev);

/* As above, but prev is a full-screen-stride snapshot (no stride change
   per row) and the inner loop is unrolled 4 pixels wide. */
void video_blit_rect_cmp_fs(int x, int y, int w, int h, const void *src,
                            const void *prev);

/* Blit src (src_stride in pixels) at integer 1/scale scale, skipping
   pixels that already match the w*h snapshot in prev. */
void video_blit_rect_scale_cmp(int x, int y, int w, int h, const void *src,
                               unsigned int src_stride, const void *prev,
                               unsigned int scale);

/* One 8x16 glyph from the linked CP437 table at pixel coordinates. */
void video_char(int x, int y, unsigned int ch, unsigned int fg,
                unsigned int bg);

#endif /* MANGO_VIDEO_H */