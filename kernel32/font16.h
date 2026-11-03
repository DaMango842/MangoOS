// kernel32/font16.h - built-in 8x16 bitmap font.
//
// 256 CP437 glyphs, 16 bytes each (one byte per scanline, MSB = leftmost
// pixel).  Linked into the kernel from font16.c; the data is the public
// domain SeaBIOS font table, so there is no need to copy the BIOS font
// out of RAM at boot (boot/vbe.asm still does, but the pointer at
// 0x70000 is ignored).
#ifndef MANGO_FONT16_H
#define MANGO_FONT16_H

#define FONT_GLYPH_H     16              /* glyph height in pixels */
#define FONT_GLYPH_W      8              /* glyph width in pixels  */
#define FONT_GLYPH_SIZE (FONT_GLYPH_H * FONT_GLYPH_W)

/* Indexed by character code; char codes above 255 are ignored by
   video_char() and bchar(). */
extern const unsigned char font8x16[256 * FONT_GLYPH_H];

#endif /* MANGO_FONT16_H */