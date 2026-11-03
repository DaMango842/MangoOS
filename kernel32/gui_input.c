// kernel32/gui_input.c - MangoWin mouse interaction state machine.
// Hit order: taskbar first (system layer), then live windows from the
// top of the z-order (skipping minimized ones), then desktop icons.
// Press on the close button acts immediately (window closed); press on a
// button control shows a pressed face and fires on release; press on the
// title bar and drag moves the window; pressing any window activates it.

#include "gui.h"

static int mouse_down;            /* left button currently held */
static int drag_win = -1;         /* window being dragged, -1 = none */
static int drag_offx, drag_offy;  /* drag anchor: window pos - mouse pos */
static int press_title;           /* press started on the title bar */
static int press_moved;           /* pointer moved far enough = dragging */
static int down_x, down_y;        /* position where the press happened */

static int iabs(int v) {
    return v < 0 ? -v : v;
}

/* locate what the pointer hit: returns window index, -2 for the close
   button, -1 for nothing; sets *ctl to the control index (-1 = title) */
static int gui_hit_test(int mx, int my, int *ctl) {
    int i, c;

    *ctl = -1;
    for (i = zorder_n - 1; i >= 0; i--) {
        const gui_win_t *w = &wins[zorder[i]];

        if (!w->alive || !w->visible)
            continue;
        if (mx < w->x || mx >= w->x + w->w ||
            my < w->y || my >= w->y + w->h)
            continue;
        if (my < w->y + GUI_TITLE_H) {
            if (win_inside_close(w, mx, my))
                return -2;              /* close button */
            return zorder[i];           /* title bar */
        }
        for (c = 0; c < GUI_MAX_CTL; c++) {
            const gui_ctl_t *b = &w->ctl[c];
            int ax = w->x + b->x, ay = w->y + GUI_TITLE_H + b->y;

            if (b->w == 0 || b->type == CTL_TYPE_LABEL)
                continue;
            if (mx >= ax && mx < ax + b->w &&
                my >= ay && my < ay + b->h) {
                *ctl = c;
                return zorder[i];
            }
        }
        return zorder[i];               /* content area, no control */
    }
    return -1;
}

static void gui_mouse_down(int mx, int my) {
    int ctl = -1;
    int hit;

    mouse_down = 1;
    press_moved = 0;
    drag_win = -1;
    press_win = -1;
    press_ctl = -1;
    press_title = 0;
    down_x = mx;
    down_y = my;

    /* start menu first: item click runs it, outside click closes it and
       lets the press continue to whatever is underneath */
    if (gui_start_menu_is_open()) {
        hit = gui_start_menu_hit(mx, my);
        if (hit >= 0) {
            gui_start_menu_click(hit);
            return;
        }
        gui_start_menu_close();
    }

    hit = gui_taskbar_hit(mx, my);
    if (hit == -2) {                    /* start button: toggle the menu */
        gui_start_menu_toggle();
        return;
    }
    if (hit >= 0) {                     /* taskbar button: act now */
        gui_taskbar_click(hit);
        return;
    }

    hit = gui_hit_test(mx, my, &ctl);
    if (hit == -2) {                    /* close button: act immediately */
        int i;

        for (i = zorder_n - 1; i >= 0; i--)
            if (wins[zorder[i]].alive && wins[zorder[i]].visible &&
                win_inside_close(&wins[zorder[i]], mx, my))
                break;
        if (i >= 0)
            gui_win_close(zorder[i]);
        return;
    }
    if (hit >= 0) {
        gui_win_activate(hit);
        press_win = hit;
        press_title = (my < wins[hit].y + GUI_TITLE_H);
        if (ctl >= 0) {
            press_ctl = ctl;            /* button: show pressed state */
            gui_dirty = 1;
        }
    } else {
        hit = gui_icon_hit(mx, my);
        if (hit >= 0)
            gui_icon_click(hit);        /* desktop icon: act now */
    }
}

static void gui_mouse_release(void) {
    mouse_down = 0;
    if (drag_win >= 0) {
        drag_win = -1;                  /* drop the window */
        return;
    }
    if (press_win >= 0 && press_ctl >= 0 &&
        wins[press_win].alive) {
        gui_ctl_trigger(&wins[press_win],
                        &wins[press_win].ctl[press_ctl]);
    }
    if (press_win >= 0)
        gui_dirty = 1;                  /* restore button face */
    press_win = -1;
    press_ctl = -1;
}

void gui_input_reset(void) {
    mouse_down = 0;
    drag_win = -1;
    press_win = -1;
    press_ctl = -1;
    press_title = 0;
    press_moved = 0;
}

void gui_input_process(int btn_down, int moved, int mx, int my) {
    if (btn_down && !mouse_down) {
        gui_mouse_down(mx, my);
    } else if (!btn_down && mouse_down) {
        gui_mouse_release();
    } else if (btn_down && mouse_down && moved) {
        press_moved = 1;
        if (drag_win >= 0) {
            /* follow the pointer, keeping the title bar visible and out
               of the taskbar area */
            int wx = mx + drag_offx;
            int wy = my + drag_offy;

            if (wx < GUI_CLOSE - wins[drag_win].w)
                wx = GUI_CLOSE - wins[drag_win].w;
            if (wx > GUI_W - GUI_CLOSE)
                wx = GUI_W - GUI_CLOSE;
            if (wy < GUI_TITLE_H - wins[drag_win].h)
                wy = GUI_TITLE_H - wins[drag_win].h;
            if (wy > GUI_H - GUI_TASKBAR_H - GUI_TITLE_H)
                wy = GUI_H - GUI_TASKBAR_H - GUI_TITLE_H;
            if (wx != wins[drag_win].x || wy != wins[drag_win].y) {
                wins[drag_win].x = wx;
                wins[drag_win].y = wy;
                gui_dirty = 1;
            }
        } else if (press_win >= 0 && press_title && press_ctl < 0 &&
                   wins[press_win].alive &&
                   iabs(mx - down_x) + iabs(my - down_y) >= 4) {
            /* started dragging the window by its title bar */
            drag_win = press_win;
            drag_offx = wins[press_win].x - mx;
            drag_offy = wins[press_win].y - my;
            gui_dirty = 1;
        }
    }
}
