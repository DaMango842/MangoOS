// kernel32/render.c - unified software rendering pipeline for MangoOS.
//
// GUI path (ren_mark / ren_flush): every drawing primitive in
// gui_draw.c marks the rectangle it touched; the frame loop flushes
// the union of all marks once, so the framebuffer only receives the
// pixels that actually changed.  A static desktop costs nothing.
//
// FC path (ren_fc_init / ren_fc_flush): the emulator renders its
// 256x240 picture at 1x into fc_surface (a quarter of the pixels of a
// 2x render).  The flush blits the picture at 2x nearest neighbor into
// the framebuffer at (144,60), skipping pixels that match the previous
// frame's snapshot, and does the same for the 800x20 status bar.
//
// Memory layout (16 MB RAM):
//   0x100000           back buffer (800x600x4)
//   0x2E0000-0x400000  process stacks
//   0x500000           fc_surface (256x240x4 = 0xF000 bytes)
//   +0xF000            prev_win snapshot (256x240x4)
//   +0x1E000           prev_bar snapshot (800x20x4)
//   0x530000           prev_gui: full-screen LFB snapshot for the GUI
//                      cmp-skip flush (800x600x4) - under QEMU TCG each
//                      LFB write is costly, so only changed pixels go out

#include "render.h"
#include "video.h"
#include "gui.h"      /* for the shared 800x600 back buffer pointer */
#include "fc/fc.h"

#define FC_WIN_X 144
#define FC_WIN_Y 60

unsigned int *fc_surface;       /* 256x240 1x render target */
static unsigned int *prev_win;  /* 1x window snapshot (256x240) */
static unsigned int *prev_bar;  /* 800x20 status bar snapshot */
static unsigned int *prev_gui;  /* full-screen snapshot, 0 = cmp disabled */

static int d_any;
static int d_x0, d_y0, d_x1, d_y1;

void ren_mark(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= REN_W || y >= REN_H || w <= 0 || h <= 0)
        return;
    if (x + w > REN_W)
        w = REN_W - x;
    if (y + h > REN_H)
        h = REN_H - y;
    if (!d_any) {
        d_x0 = x;
        d_y0 = y;
        d_x1 = x + w;
        d_y1 = y + h;
        d_any = 1;
    } else {
        if (x < d_x0)
            d_x0 = x;
        if (y < d_y0)
            d_y0 = y;
        if (x + w > d_x1)
            d_x1 = x + w;
        if (y + h > d_y1)
            d_y1 = y + h;
    }
}

void ren_flush(void) {
    if (!d_any)
        return;
    if (prev_gui)
        video_blit_rect_cmp_fs(d_x0, d_y0, d_x1 - d_x0, d_y1 - d_y0,
                               back, prev_gui);
    else
        video_blit_rect(d_x0, d_y0, d_x1 - d_x0, d_y1 - d_y0, back);
    d_any = 0;
}

void ren_gui_init(void) {
    unsigned int *p;

    prev_gui = (unsigned int *)REN_GUI_PREV;
    /* invalidate so the first flush writes every pixel regardless of
       what the (possibly stale) LFB still holds after a mode switch */
    for (p = prev_gui; p < prev_gui + REN_W * REN_H; p++)
        *p = 0xFFFFFFFFu;
}

void ren_fc_init(void) {
    unsigned int *p;

    fc_surface = (unsigned int *)0x500000u;
    prev_win = fc_surface + 256 * 240;
    prev_bar = prev_win + 256 * 240;
    for (p = prev_win; p < prev_win + 256 * 240; p++)
        *p = 0xFFFFFFFFu;
    for (p = prev_bar; p < prev_bar + 800 * 20; p++)
        *p = 0xFFFFFFFFu;
}

void ren_fc_flush(void) {
    ren_flush();                        /* pending GUI rects (unused here) */
    video_blit_rect_cmp(0, 0, 800, 20, back, prev_bar);
    video_blit_rect_scale_cmp(FC_WIN_X, FC_WIN_Y, 256, 240,
                              fc_surface, 256, prev_win, 2);
}