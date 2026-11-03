// kernel32/vga.h - text console.
//
// Two backends behind one API:
//   * native 80x25 text at 0xB8000
//   * a software framebuffer console (shadow cell+attribute arrays) when
//     stage2 negotiated a VBE linear framebuffer, so the CLI stays usable
//     in graphical mode.
//
// vga_snapshot()/vga_restore() capture and restore the whole console
// (cells, attributes, cursor, current attribute) so MangoWin and the NES
// emulator can hand the CLI back untouched when they exit.
#ifndef MANGO_VGA_H
#define MANGO_VGA_H

/* Switch to the framebuffer console when VBE left us an LFB; otherwise
   stay on the 0xB8000 text buffer.  Call once, after video_init(). */
void vga_enable_fb(void);

/* Cursor position, 0..79 / 0..24. */
void vga_set_cursor(int row, int col);
void vga_get_cursor(int *row, int *col);

/* Hardware text cursor visibility. */
void vga_show_cursor(int on);

/* Current text attribute (VGA colour byte). */
void vga_set_attr(unsigned char attr);

void vga_clear(void);
void vga_putc(char c);
void vga_write(const char *text);

/* Save / restore the full console state.  Nesting is not supported. */
void vga_snapshot(void);
void vga_restore(void);

#endif /* MANGO_VGA_H */