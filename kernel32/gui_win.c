// kernel32/gui_win.c - MangoWin window and control (button) model:
// creation, painting, click actions, and the z-order / activation /
// minimize machinery backing the desktop and the taskbar.

#include "gui.h"
#include "serial.h"

int zorder[GUI_MAX_WIN];     /* paint/hit order, last element = top */
int zorder_n;
int active_win = -1;

/* move window idx to the top of the z-order (no-op when absent) */
static void zorder_raise(int idx) {
    int i, j;

    for (i = 0; i < zorder_n; i++)
        if (zorder[i] == idx)
            break;
    if (i == zorder_n)
        return;
    for (j = i; j < zorder_n - 1; j++)
        zorder[j] = zorder[j + 1];
    zorder[zorder_n - 1] = idx;
}

/* activate the topmost live, visible window (or none) */
static void gui_win_activate_topmost(void) {
    int i;

    active_win = -1;
    for (i = zorder_n - 1; i >= 0; i--) {
        int idx = zorder[i];
        if (wins[idx].alive && wins[idx].visible) {
            active_win = idx;
            break;
        }
    }
}

void gui_win_activate(int idx) {
    if (idx < 0 || idx >= GUI_MAX_WIN || !wins[idx].alive ||
        !wins[idx].visible)
        return;
    zorder_raise(idx);
    if (active_win != idx) {
        active_win = idx;
        gui_dirty = 1;
    }
}

void gui_win_minimize(int idx) {
    if (idx < 0 || idx >= GUI_MAX_WIN || !wins[idx].alive ||
        !wins[idx].visible)
        return;
    wins[idx].visible = 0;
    if (active_win == idx)
        gui_win_activate_topmost();
    gui_dirty = 1;
}

void gui_win_restore(int idx) {
    if (idx < 0 || idx >= GUI_MAX_WIN || !wins[idx].alive ||
        wins[idx].visible)
        return;
    wins[idx].visible = 1;
    gui_win_activate(idx);
    gui_dirty = 1;
}

void gui_win_close(int idx) {
    int i;

    if (idx < 0 || idx >= GUI_MAX_WIN || !wins[idx].alive)
        return;
    wins[idx].alive = 0;
    wins[idx].visible = 1;
    for (i = 0; i < zorder_n; i++)
        if (zorder[i] == idx) {
            for (; i < zorder_n - 1; i++)
                zorder[i] = zorder[i + 1];
            zorder_n--;
            break;
        }
    gui_icon_unlink(idx);
    if (active_win == idx)
        gui_win_activate_topmost();
    gui_dirty = 1;
}

int win_inside_close(const gui_win_t *w, int x, int y) {
    return x >= w->x + w->w - GUI_CLOSE && x < w->x + w->w &&
           y >= w->y && y < w->y + GUI_TITLE_H;
}

int gui_win_create(int x, int y, int w, int h, const char *title) {
    int i;
    const char *t;

    if (w < 48 || h < 32 || x < 0 || y < 0 ||
        x + w > GUI_W || y + h > GUI_H - GUI_TASKBAR_H)
        return -1;
    for (i = 0; i < GUI_MAX_WIN; i++)
        if (!wins[i].alive)
            break;
    if (i == GUI_MAX_WIN)
        return -1;
    wins[i].x = x;
    wins[i].y = y;
    wins[i].w = w;
    wins[i].h = h;
    for (t = title; *t && (t - title) < GUI_CAPTION - 1; t++)
        wins[i].title[t - title] = *t;
    wins[i].title[t - title] = '\0';
    wins[i].alive = 1;
    wins[i].visible = 1;
    {
        int c;
        for (c = 0; c < GUI_MAX_CTL; c++) {
            wins[i].ctl[c].w = 0;
            wins[i].ctl[c].type = CTL_TYPE_BUTTON;
        }
    }
    zorder[zorder_n++] = i;      /* new window starts on top */
    gui_win_activate(i);
    gui_dirty = 1;
    return i;
}

/* Add a button to a window.  Returns control index, or -1 when full. */
int gui_ctl_add(int win, int x, int y, int w, int h, const char *label,
                int act) {
    gui_win_t *wd;
    gui_ctl_t *b;
    const char *p;
    int i;

    if (win < 0 || win >= GUI_MAX_WIN || !wins[win].alive)
        return -1;
    if (w < 24 || h < 12 || x < 0 || y < 0)
        return -1;
    wd = &wins[win];
    for (i = 0; i < GUI_MAX_CTL; i++)
        if (wd->ctl[i].w == 0)
            break;
    if (i == GUI_MAX_CTL)
        return -1;
    b = &wd->ctl[i];
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    for (p = label; *p && p - label < CTL_LABEL - 1; p++)
        b->label[p - label] = *p;
    b->label[p - label] = '\0';
    b->type = CTL_TYPE_BUTTON;
    b->act = (unsigned char)act;
    gui_dirty = 1;
    return i;
}

/* Add a label (static text) to a window.  Returns control index, or -1
   when full.  Text wraps on '\n'; each line is 16 px tall.  The control
   rectangle matches the text extents so the slot is not seen as free. */
int gui_ctl_label(int win, int x, int y, const char *text) {
    gui_win_t *wd;
    gui_ctl_t *b;
    const char *p;
    int i;
    int maxlen = 0, curlen = 0, lines = 1;

    if (win < 0 || win >= GUI_MAX_WIN || !wins[win].alive)
        return -1;
    if (x < 0 || y < 0)
        return -1;
    wd = &wins[win];
    for (i = 0; i < GUI_MAX_CTL; i++)
        if (wd->ctl[i].w == 0)
            break;
    if (i == GUI_MAX_CTL)
        return -1;
    for (p = text; *p && p - text < CTL_LABEL - 1; p++) {
        if (*p == '\n') {
            if (curlen > maxlen)
                maxlen = curlen;
            curlen = 0;
            lines++;
        } else {
            curlen++;
        }
    }
    if (curlen > maxlen)
        maxlen = curlen;
    b = &wd->ctl[i];
    b->x = x;
    b->y = y;
    b->w = maxlen * 8;
    b->h = lines * 16;
    for (p = text; *p && p - text < CTL_LABEL - 1; p++)
        b->label[p - text] = *p;
    b->label[p - text] = '\0';
    b->type = CTL_TYPE_LABEL;
    b->act = CTL_ACT_PRINT;
    gui_dirty = 1;
    return i;
}

static void gui_draw_btn(const gui_win_t *w, const gui_ctl_t *b,
                         int pressed) {
    int ax = w->x + b->x, ay = w->y + GUI_TITLE_H + b->y;
    int i;
    unsigned int bg = pressed ? C_BTN_PRS : C_BTN;
    unsigned int edge = pressed ? C_BTN : C_BTN_EDGE;

    brect(ax, ay, b->w, b->h, bg);
    bframe(ax, ay, b->w, b->h, edge);
    for (i = 0; i < b->h - 4; i++) {
        bpx(ax + 1, ay + 1 + i, edge);
        bpx(ax + 1 + i, ay + 1, edge);
    }
    btext(ax + 6, ay + (b->h - 16) / 2, b->label, C_BTN_TEXT, bg);
}

/* static text control: draw the label, one 16 px line per '\n' */
static void gui_draw_lbl(const gui_win_t *w, const gui_ctl_t *b) {
    int ax = w->x + b->x, ay = w->y + GUI_TITLE_H + b->y;
    const char *p = b->label;
    int line = 0;

    while (*p) {
        const char *e = p;
        int n = 0;

        while (*e && *e != '\n') {
            e++;
            n++;
        }
        if (n > 0) {
            char tmp[CTL_LABEL];
            int i;
            for (i = 0; i < n; i++)
                tmp[i] = p[i];
            tmp[n] = '\0';
            btext(ax, ay + line * 16, tmp, C_TEXT, C_WINBG);
        }
        if (*e == '\n') {
            p = e + 1;
            line++;
        } else {
            break;
        }
    }
}

void gui_draw_win(const gui_win_t *w, int win_idx) {
    int cx, cy;
    int c;
    unsigned int title_c = (win_idx == active_win) ? C_TITLE : C_TITLE_INA;

    brect(w->x, w->y, w->w, w->h, C_WINBG);
    bframe(w->x, w->y, w->w, w->h, C_BORDER);
    brect(w->x, w->y, w->w, GUI_TITLE_H, title_c);
    bframe(w->x, w->y, w->w, GUI_TITLE_H, title_c);
    btext(w->x + 4, w->y + 3, w->title, C_TEXT, title_c);
    brect(w->x + w->w - GUI_CLOSE, w->y, GUI_CLOSE, GUI_TITLE_H,
          C_CLOSEBG);
    for (cy = 2; cy < GUI_TITLE_H - 2; cy++) {
        bpx(w->x + w->w - GUI_CLOSE + 3, w->y + cy, C_CLOSEFG);
        bpx(w->x + w->w - 4, w->y + cy, C_CLOSEFG);
    }
    for (cx = 3; cx < GUI_CLOSE - 3; cx++) {
        bpx(w->x + w->w - GUI_CLOSE + cx, w->y + 2, C_CLOSEFG);
        bpx(w->x + w->w - GUI_CLOSE + cx, w->y + GUI_TITLE_H - 3,
            C_CLOSEFG);
    }
    for (c = 0; c < GUI_MAX_CTL; c++) {
        const gui_ctl_t *b = &w->ctl[c];
        int pressed;

        if (b->w == 0)
            continue;
        if (b->type == CTL_TYPE_LABEL) {
            gui_draw_lbl(w, b);
            continue;
        }
        pressed = (win_idx == press_win && c == press_ctl);
        gui_draw_btn(w, b, pressed);
    }
}

void gui_ctl_trigger(gui_win_t *w, const gui_ctl_t *b) {
    if (b->act == CTL_ACT_CLOSE) {
        int i;

        for (i = 0; i < GUI_MAX_WIN; i++)
            if (&wins[i] == w)
                break;
        if (i < GUI_MAX_WIN)
            gui_win_close(i);    /* GUI stays up: desktop + taskbar live */
    } else {
        serial_write("btn: ");
        serial_write(b->label);
        serial_write("\r\n");
    }
}
