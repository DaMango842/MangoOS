// kernel32/gui_draw.c - MangoWin backbuffer primitives, cursor cache and
// full-scene rendering.  All drawing happens into the static backbuffer
// and marks the touched rectangle in the renderer; gui_flip pushes only
// the accumulated dirty region to the linear framebuffer.

#include "gui.h"
#include "render.h"
#include "font16.h"

/* cursor pixel cache so a move only repaints the 9x9 cursor rect */
static unsigned int cursor_save[9 * 9];
static int cursor_x = -1, cursor_y = -1, cursor_drawn = 0;

void bpx(int x, int y, unsigned int c) {
    if (x < 0 || y < 0 || x >= GUI_W || y >= GUI_H)
        return;
    back[(unsigned int)y * GUI_W + x] = c;
    ren_mark(x, y, 1, 1);
}

/* Fill a solid rect.  Clipping happens ONCE up front, then rows are
   written straight into the backbuffer - the full-screen desktop clear
   alone is 454K pixels per repaint, and a per-pixel bounds check made
   that the single biggest cost of every gui_render_all under QEMU
   TCG. */
void brect(int x, int y, int w, int h, unsigned int c) {
    int dy;

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
    if (x + w > GUI_W)
        w = GUI_W - x;
    if (y + h > GUI_H)
        h = GUI_H - y;
    if (w <= 0 || h <= 0)
        return;
    for (dy = 0; dy < h; dy++) {
        unsigned int *row = &back[(unsigned int)(y + dy) * GUI_W + x];
        int dx;

        for (dx = 0; dx < w; dx++)
            row[dx] = c;
    }
    ren_mark(x, y, w, h);
}

void bframe(int x, int y, int w, int h, unsigned int c) {
    int i;
    for (i = 0; i < w; i++) {
        bpx(x + i, y, c);
        bpx(x + i, y + h - 1, c);
    }
    for (i = 0; i < h; i++) {
        bpx(x, y + i, c);
        bpx(x + w - 1, y + i, c);
    }
}

/* Same trick as brect: derive the visible column/row range of the 8x16
   cell once, then write the rows without per-pixel checks. */
void bchar(int x, int y, unsigned char ch, unsigned int fg,
           unsigned int bg) {
    const unsigned char *g = &font8x16[ch * 16];
    int gx0 = 0, gx1 = 8, gy0 = 0, gy1 = 16, gy, gx;

    if (x < 0)
        gx0 = -x;
    if (y < 0)
        gy0 = -y;
    if (x + gx1 > GUI_W)
        gx1 = GUI_W - x;
    if (y + gy1 > GUI_H)
        gy1 = GUI_H - y;
    if (gx0 >= gx1 || gy0 >= gy1)
        return;
    for (gy = gy0; gy < gy1; gy++) {
        unsigned int *row = &back[(unsigned int)(y + gy) * GUI_W + x];
        unsigned int bits = g[gy];

        for (gx = gx0; gx < gx1; gx++)
            row[gx] = (bits & (unsigned int)(0x80 >> gx)) ? fg : bg;
    }
    ren_mark(x + gx0, y + gy0, gx1 - gx0, gy1 - gy0);
}

void btext(int x, int y, const char *s, unsigned int fg,
           unsigned int bg) {
    while (*s) {
        bchar(x, y, (unsigned char)*s++, fg, bg);
        x += 8;
    }
}

void gui_flip(void) {
    ren_flush();
}

/* full scene without the cursor: desktop, icons, live windows in
   z-order, then the taskbar system layer on top */
void gui_render_all(void) {
    int i;

    brect(0, 0, GUI_W, GUI_H - GUI_TASKBAR_H, C_BG);
    gui_icons_draw();
    for (i = 0; i < zorder_n; i++) {
        int idx = zorder[i];
        if (wins[idx].alive && wins[idx].visible)
            gui_draw_win(&wins[idx], idx);
    }
    gui_taskbar_draw();
    gui_start_menu_draw();   /* popup panel on top of everything */
}

void gui_cursor_reset(void) {
    cursor_drawn = 0;
    cursor_x = -1;
    cursor_y = -1;
}

void gui_cursor_erase(void) {
    int dx, dy;

    if (!cursor_drawn)
        return;
    for (dy = 0; dy < 9; dy++)
        for (dx = 0; dx < 9; dx++)
            bpx(cursor_x + dx, cursor_y + dy, cursor_save[dy * 9 + dx]);
    cursor_drawn = 0;
}

/* Save the 9x9 cursor area and paint the cursor at (x, y), without
   erasing the previous location.  Used after a full repaint (the
   backbuffer was just redrawn, the old snapshot would be stale) and
   after an explicit erase in the move path. */
void gui_cursor_draw(int x, int y) {
    int dx, dy;

    for (dy = 0; dy < 9; dy++)
        for (dx = 0; dx < 9; dx++) {
            int bx = x + dx, by = y + dy;
            if (bx >= 0 && bx < GUI_W && by >= 0 && by < GUI_H)
                cursor_save[dy * 9 + dx] = back[by * GUI_W + bx];
        }
    cursor_x = x;
    cursor_y = y;
    brect(x, y, 9, 9, C_CURSOR);
    cursor_drawn = 1;
}

/* current cursor position (used by the main loop for partial blits) */
int cursor_old_x(void) {
    return cursor_x;
}

int cursor_old_y(void) {
    return cursor_y;
}
