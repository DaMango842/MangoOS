// kernel32/gui.c - MangoWin graphical interface: main loop and mode
// enter/exit.  Owns the shared window state and the double-buffered
// render loop; the heavy lifting lives in the sibling modules
// (gui_draw/gui_win/gui_input/gui_cmd/gui_stats).
// Design: docs/graphics-design.md
//  - second run mode entered from the CLI with the "mangoWin" command
//  - double buffered: draws into a static backbuffer in extended memory
//    (1 MB, 800x600x4 = 1.92 MB), then blits the whole frame to the LFB
//  - exit: Esc key, the window close button, or "exit" via serial
#include "gui.h"
#include "render.h"
#include "proc/proc.h"
#include "video.h"
#include "vga.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"

unsigned int *back;
gui_win_t wins[GUI_MAX_WIN];
volatile int gui_quit;
int gui_dirty;
int press_win, press_ctl;   /* control currently pressed (for faces) */

/* factories for the sample windows; also used by the desktop icons */
static int mango_win = -1;
static int computer_win = -1;

static void mango_win_open(void) {
    if (mango_win >= 0 && wins[mango_win].alive) {
        gui_win_activate(mango_win);
        return;
    }
    mango_win = gui_win_create(140, 90, 340, 220, "MangoWin");
    if (mango_win >= 0) {
        gui_ctl_label(mango_win, 20, 12, "Hello Mango!");
        gui_ctl_add(mango_win, 20, 40, 80, 24, "Print", CTL_ACT_PRINT);
        gui_ctl_add(mango_win, 112, 40, 80, 24, "Quit", CTL_ACT_CLOSE);
    }
}

/* placeholder for the future file browser: opens a sample window */
static void computer_win_open(void) {
    if (computer_win >= 0 && wins[computer_win].alive) {
        gui_win_activate(computer_win);
        return;
    }
    computer_win = gui_win_create(420, 90, 300, 200, "My Computer");
    if (computer_win >= 0) {
        gui_ctl_label(computer_win, 20, 12, "C: mango (v0.2)");
        gui_ctl_add(computer_win, 20, 40, 80, 24, "Close", CTL_ACT_CLOSE);
    }
}

/* exported for the start menu / taskbar */
void gui_open_mango(void) {
    mango_win_open();
}

void gui_open_computer(void) {
    computer_win_open();
}

static void computer_icon_act(int self) {
    (void)self;
    gui_open_computer();
    if (computer_win >= 0)
        gui_icons[0].win = computer_win;
}

/* desktop icons: only My Computer lives on the desktop; the start
   button moved into the taskbar and the other icons are resource
   placeholders until their features exist - see assets/ */
static void gui_icon_setup(void) {
    const char *name = "Computer";
    int i;

    gui_icon_init();
    gui_icons[0].img = icon_computer;
    gui_icons[0].act = computer_icon_act;
    gui_icons[0].win = -1;
    for (i = 0; name[i] && i < 11; i++)
        gui_icons[0].name[i] = name[i];
    gui_icons[0].name[i] = '\0';
}

void gui_enter(void) {
    int i;
    int old_mouse_mode;

    if (!video_active() || video_width() != GUI_W ||
        video_height() != GUI_H || video_bpp() != 32)
        return;
    for (i = 0; i < GUI_MAX_WIN; i++)
        wins[i].alive = 0;
    zorder_n = 0;
    active_win = -1;
    back = GUI_BACK;
    gui_quit = 0;
    gui_dirty = 1;
    press_win = -1;
    press_ctl = -1;
    gui_cursor_reset();
    gui_icon_setup();

    /* open the sample windows so the screen is not empty */
    mango_win = -1;
    mango_win_open();
    {
        int w2 = gui_win_create(320, 260, 300, 180, "About");
        if (w2 >= 0) {
            gui_ctl_label(w2, 40, 12, "MangoWin v0.2");
            gui_ctl_add(w2, 40, 44, 80, 24, "Close", CTL_ACT_CLOSE);
        }
    }

    vga_snapshot();
    old_mouse_mode = mouse_set_mode(1);
    mouse_x = 400;
    mouse_y = 300;
    gui_input_reset();
    ren_gui_init();
    stats_init();

    while (!gui_quit) {
        int key = kbd_get_key();
        int nx, ny, moved;
        int btn;

        if (key == 0x1B) {               /* Esc: menu first, then quit */
            if (gui_start_menu_is_open())
                gui_start_menu_close();
            else
                break;
        }
        mouse_poll();
        gui_taskbar_tick();              /* repaint the clock each second */
        if (mouse_get_pos(&nx, &ny) != 0) {
            nx = 400;
            ny = 300;
        }
        moved = (nx != mouse_x || ny != mouse_y);
        mouse_x = nx;
        mouse_y = ny;
        btn = mouse_read_buttons() & 1;
        gui_input_process(btn, moved, mouse_x, mouse_y);
        gui_serial_poll();
        if (gui_dirty) {
            /* full repaint: rare (window change / drag / command) */
            unsigned int t0 = pit_now_export();
            unsigned long du;

            gui_render_all();
            gui_cursor_draw(mouse_x, mouse_y);
            gui_flip();
            du = pit_us_since_export(t0);
            stat_render_add(du);
            gui_dirty = 0;
        } else if (moved) {
            /* small update: repaint only the cursor rectangles (old and
               new position) so the LFB never keeps stale cursor pixels.
               Flush each 9x9 rect SEPARATELY - feeding both to the
               single dirty-rect accumulator would merge them into one
               bounding box that spans the whole move distance, turning
               a fast mouse sweep into a near-fullscreen blit. */
            unsigned int t0 = pit_now_export();

            gui_cursor_erase();            /* restores + marks old rect */
            ren_flush();                   /* push the old 9x9 alone */
            gui_cursor_draw(mouse_x, mouse_y);   /* marks the new 9x9 */
            ren_flush();                   /* push the new 9x9 alone */
            stat_cursor_add(pit_us_since_export(t0));
        }
        stats_tick();
        proc_yield();               /* let other processes run */
        __asm__ volatile("pause");
    }

    mouse_set_mode(old_mouse_mode);
    vga_restore();
    stats_shutdown();        /* give PIT ch0 back -> IRQ0 / proc_tick live */
}

void gui_exit(void) {
    gui_quit = 1;
}
