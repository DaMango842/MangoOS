// kernel32/gui_taskbar.c - MangoWin system layer: the taskbar (start
// button, one button per live window, live RTC clock), the start menu
// popup panel, and the desktop icons.
// The taskbar/menu are drawn last (above every window) and hit-tested
// first, so windows can never overlap them; window positions are
// constrained to the desktop area by gui_win.c / gui_input.c.

#include "gui.h"
#include "video.h"
#include "time.h"

#define TB_X0        2
#define TB_GAP       2
#define TB_BTN_W     88
#define TB_BTN_H     28
#define TB_BTN_Y     2
#define TB_START_W   64               /* start button */
#define TB_CLOCK_W   152              /* "2026-08-16 13:45:30" */
#define TB_BTNS_X    (TB_X0 + TB_START_W + TB_GAP)

#define MENU_W       220
#define MENU_ROW_H   40
#define MENU_PAD     4

#define ICON_X0      8
#define ICON_Y0      8
#define ICON_CELL_W  72              /* column pitch */
#define ICON_CELL_H  80              /* row pitch */
#define ICON_LABEL_Y 36              /* caption offset below the bitmap */

static int menu_open;

/* ---- taskbar ---- */

/* paint the top-left 28x28 crop of a 32x32 icon (transparent = 0) */
static void draw_icon_28(const unsigned int *img, int x, int y) {
    int dx, dy;

    for (dy = 0; dy < 28; dy++)
        for (dx = 0; dx < 28; dx++) {
            unsigned int c = img[(unsigned int)dy * 32 + dx];
            if (c != 0)
                bpx(x + dx, y + dy, c);
        }
}

/* right-aligned live clock: YYYY-MM-DD HH:MM:SS */
static void gui_taskbar_clock_draw(int ty) {
    char buf[20];
    int n = time_str(buf);

    if (n > 0)
        btext(GUI_W - 4 - n * 8, ty + (GUI_TASKBAR_H - 16) / 2, buf,
              C_TEXT, C_TASKBAR);
}

void gui_taskbar_draw(void) {
    int ty = GUI_H - GUI_TASKBAR_H;
    int i;
    static const char ver[] = "";      /* version moved out: clock lives here */

    (void)ver;
    brect(0, ty, GUI_W, GUI_TASKBAR_H, C_TASKBAR);
    for (i = 0; i < GUI_W; i++)
        bpx(i, ty, C_BORDER);

    /* start button (highlighted while the menu is open) */
    {
        unsigned int face = menu_open ? C_TB_ACT : C_TB_BTN;

        brect(TB_X0, ty + TB_BTN_Y, TB_START_W, TB_BTN_H, face);
        bframe(TB_X0, ty + TB_BTN_Y, TB_START_W, TB_BTN_H, C_BTN_EDGE);
        draw_icon_28(icon_start, TB_X0 + (TB_START_W - 28) / 2,
                     ty + TB_BTN_Y + 2);
    }

    /* window buttons follow the z-order, active window sits rightmost;
       stop short of the clock area */
    for (i = 0; i < zorder_n; i++) {
        const gui_win_t *w = &wins[zorder[i]];
        int bx = TB_BTNS_X + i * (TB_BTN_W + TB_GAP);
        unsigned int face;
        char txt[GUI_CAPTION];
        int n = 0;

        if (bx + TB_BTN_W > GUI_W - TB_CLOCK_W - 4)
            break;                      /* no room left: skip */
        if (!w->alive)
            continue;
        if (zorder[i] == active_win)
            face = C_TB_ACT;
        else if (!w->visible)
            face = C_TB_MIN;
        else
            face = C_TB_BTN;
        brect(bx, ty + TB_BTN_Y, TB_BTN_W, TB_BTN_H, face);
        bframe(bx, ty + TB_BTN_Y, TB_BTN_W, TB_BTN_H, C_BTN_EDGE);
        for (n = 0; w->title[n] && n < GUI_CAPTION - 1; n++)
            txt[n] = w->title[n];
        txt[n] = '\0';
        btext(bx + 6, ty + TB_BTN_Y + (TB_BTN_H - 16) / 2, txt, C_TEXT,
              face);
    }

    gui_taskbar_clock_draw(ty);
}

/* -2 = start button, window index = its button, -1 = empty taskbar */
int gui_taskbar_hit(int mx, int my) {
    int i;

    if (my < GUI_H - GUI_TASKBAR_H)
        return -1;
    if (mx >= TB_X0 && mx < TB_X0 + TB_START_W)
        return -2;
    for (i = 0; i < zorder_n; i++) {
        int bx = TB_BTNS_X + i * (TB_BTN_W + TB_GAP);

        if (bx >= GUI_W - TB_CLOCK_W - 4)
            break;
        if (mx >= bx && mx < bx + TB_BTN_W)
            return zorder[i];
    }
    return -1;
}

/* click toggles: active -> minimize, anything else -> restore + activate */
void gui_taskbar_click(int idx) {
    if (idx < 0 || idx >= GUI_MAX_WIN || !wins[idx].alive)
        return;
    if (idx == active_win && wins[idx].visible)
        gui_win_minimize(idx);
    else
        gui_win_restore(idx);
}

/* ---- start menu ---- */

typedef struct {
    const unsigned int *img;   /* 32x32, 0 = no icon */
    const char *name;
    void (*act)(void);
    int enabled;               /* 0 = placeholder, greyed out */
} menu_item_t;

static const menu_item_t menu_items[] = {
    { icon_computer, "My Computer",   gui_open_computer, 1 },
    { icon_start,    "mangoWin",      gui_open_mango,    1 },
    { icon_explorer, "File Explorer", 0,                 0 },
    { icon_recycle,  "Recycle Bin",   0,                 0 },
    { icon_settings, "Settings",      0,                 0 },
    { 0,             "Shutdown",      gui_exit,          1 },
};
#define MENU_ITEMS ((int)(sizeof(menu_items) / sizeof(menu_items[0])))

static int menu_height(void) {
    return MENU_PAD * 2 + MENU_ITEMS * MENU_ROW_H;
}

void gui_start_menu_toggle(void) {
    menu_open = !menu_open;
    gui_dirty = 1;
}

void gui_start_menu_close(void) {
    if (menu_open) {
        menu_open = 0;
        gui_dirty = 1;
    }
}

int gui_start_menu_is_open(void) {
    return menu_open;
}

void gui_start_menu_draw(void) {
    int k;
    int ty = GUI_H - GUI_TASKBAR_H;
    int mh = menu_height();
    int my = ty - mh;

    if (!menu_open)
        return;
    brect(0, my, MENU_W, mh, C_MENU_BG);
    bframe(0, my, MENU_W, mh, C_BORDER);
    for (k = 0; k < MENU_ITEMS; k++) {
        const menu_item_t *it = &menu_items[k];
        int ry = my + MENU_PAD + k * MENU_ROW_H;
        int dx, dy;
        unsigned int fg = it->enabled ? C_TEXT : C_TEXT_INA;

        if (it->img) {
            for (dy = 0; dy < 32; dy++)
                for (dx = 0; dx < 32; dx++) {
                    unsigned int c = it->img[(unsigned int)dy * 32 + dx];
                    if (c != 0)
                        bpx(8 + dx, ry + 4 + dy, c);
                }
        } else {
            /* no icon (Shutdown): a small power-style square */
            brect(14, ry + (MENU_ROW_H - 20) / 2, 20, 20, C_CLOSEBG);
            bframe(14, ry + (MENU_ROW_H - 20) / 2, 20, 20, C_CLOSEFG);
        }
        if (k == MENU_ITEMS - 1) {       /* separator above the last row */
            for (dx = 6; dx < MENU_W - 6; dx++)
                bpx(dx, ry - 2, C_BTN_EDGE);
        }
        btext(48, ry + (MENU_ROW_H - 16) / 2, it->name, fg, C_MENU_BG);
    }
}

int gui_start_menu_hit(int mx, int my) {
    int ty = GUI_H - GUI_TASKBAR_H;
    int mh = menu_height();
    int rel;

    if (!menu_open || mx < 0 || mx >= MENU_W)
        return -1;
    if (my < ty - mh || my >= ty)
        return -1;
    rel = (my - (ty - mh + MENU_PAD)) / MENU_ROW_H;
    return (rel >= 0 && rel < MENU_ITEMS) ? rel : -1;
}

void gui_start_menu_click(int idx) {
    if (idx < 0 || idx >= MENU_ITEMS)
        return;
    if (menu_items[idx].enabled && menu_items[idx].act)
        menu_items[idx].act();
    menu_open = 0;
    gui_dirty = 1;
}

/* repaint the clock region only (once per second), never a full scene
   redraw: a full repaint takes ~1 s of TCG time and starves every other
   process (the NES emulator drops to ~1 fps while it runs) */
void gui_taskbar_tick(void) {
    int y, mo, d, hh, mi, ss;
    static int last = -1;

    if (time_get(&y, &mo, &d, &hh, &mi, &ss) == 0) {
        int cur = hh * 3600 + mi * 60 + ss;
        if (cur != last) {
            int ty = GUI_H - GUI_TASKBAR_H;
            int x0 = GUI_W - 4 - 19 * 8;   /* fixed-width clock text */
            int x, yy;

            last = cur;
            for (yy = ty; yy < GUI_H; yy++)
                for (x = x0; x < GUI_W; x++)
                    bpx(x, yy, (yy == ty) ? C_BORDER : C_TASKBAR);
            gui_taskbar_clock_draw(ty);
            video_blit_rect(x0, ty, GUI_W - x0, GUI_H - ty, back);
        }
    }
}

/* ---- desktop icons ---- */

gui_icon_t gui_icons[GUI_MAX_ICON];

void gui_icons_draw(void) {
    int k, x, y;

    for (k = 0; k < GUI_MAX_ICON; k++) {
        const gui_icon_t *ic = &gui_icons[k];
        int ix, iy, tw = 0;

        if (!ic->img)
            continue;
        ix = ICON_X0 + (k % (GUI_W / ICON_CELL_W)) * ICON_CELL_W;
        iy = ICON_Y0 + (k / (GUI_W / ICON_CELL_W)) * ICON_CELL_H;
        for (y = 0; y < GUI_ICON_SIZE; y++)
            for (x = 0; x < GUI_ICON_SIZE; x++) {
                unsigned int c = ic->img[(unsigned int)y * GUI_ICON_SIZE + x];
                if (c != 0)
                    bpx(ix + x, iy + y, c);
            }
        while (ic->name[tw])
            tw++;
        btext(ix + (GUI_ICON_SIZE - tw * 8) / 2, iy + ICON_LABEL_Y,
              ic->name, C_TEXT, C_BG);
    }
}

int gui_icon_hit(int mx, int my) {
    int k, ix, iy;

    if (my >= GUI_H - GUI_TASKBAR_H)
        return -1;
    for (k = 0; k < GUI_MAX_ICON; k++) {
        if (!gui_icons[k].img)
            continue;
        ix = ICON_X0 + (k % (GUI_W / ICON_CELL_W)) * ICON_CELL_W;
        iy = ICON_Y0 + (k / (GUI_W / ICON_CELL_W)) * ICON_CELL_H;
        if (mx >= ix && mx < ix + ICON_CELL_W &&
            my >= iy && my < iy + ICON_CELL_H)
            return k;
    }
    return -1;
}

void gui_icon_click(int idx) {
    if (idx < 0 || idx >= GUI_MAX_ICON || !gui_icons[idx].img)
        return;
    if (gui_icons[idx].act)
        gui_icons[idx].act(idx);
}

/* a window died: forget which icon owned it */
void gui_icon_unlink(int win_idx) {
    int k;

    for (k = 0; k < GUI_MAX_ICON; k++)
        if (gui_icons[k].win == win_idx)
            gui_icons[k].win = -1;
}

void gui_icon_init(void) {
    int k;

    for (k = 0; k < GUI_MAX_ICON; k++) {
        gui_icons[k].img = 0;
        gui_icons[k].name[0] = '\0';
        gui_icons[k].act = 0;
        gui_icons[k].win = -1;
    }
}