// kernel32/render.h - unified software rendering pipeline (render.c).
// Both display paths end here:
//   GUI: drawing primitives mark dirty rectangles on the 800x600 back
//        buffer; ren_flush() blits only the accumulated dirty region
//        1:1 to the framebuffer (a static scene emits no blits).
//   FC:  the emulator renders its 256x240 picture at 1x into
//        fc_surface; ren_fc_flush() upscales it 2x to (144,60) with
//        per-pixel change detection plus the 800x20 status bar.
// Memory layout (16 MB RAM): 0x100000 back buffer, 0x2E0000-0x400000
// process stacks, 0x500000 fc_surface + snapshots, 0x530000 GUI
// full-screen change-detection snapshot (REN_GUI_PREV).
#ifndef MANGO_RENDER_H
#define MANGO_RENDER_H

#define REN_W 800
#define REN_H 600

#define REN_GUI_PREV  0x530000u   /* 800x600x4 LFB snapshot, reserved in memory.c */

/* 256x240 1x NES render target, defined in render.c.  The emulator paints
   into this, ren_fc_flush() upscales it 2x to the framebuffer. */
extern unsigned int *fc_surface;

/* GUI path: clip (x,y,w,h) into the back buffer's dirty rectangle */
void ren_mark(int x, int y, int w, int h);

/* GUI path: push the accumulated dirty rectangle to the framebuffer */
void ren_flush(void);

/* GUI path: (re)initialise the full-screen change-detection snapshot;
   call on every gui_enter so the first flush writes every pixel */
void ren_gui_init(void);

/* FC path: set up fc_surface + snapshots, invalidate them (called by
   fc_enter before the first frame so the first flush is a full blit) */
void ren_fc_init(void);

/* FC path: flush the status bar (change detection) and the FC window
   (2x nearest-neighbor scaling + change detection) */
void ren_fc_flush(void);

#endif /* MANGO_RENDER_H */