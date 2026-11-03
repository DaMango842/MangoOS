// kernel32/gui_stats.c - PIT timing + performance statistics for MangoWin.
//
// PIT ch0 ownership lives in timer.c: stats_init() borrows the channel as
// a free-running 16-bit down counter (mode 2) and stats_shutdown() hands
// it back, restoring the 100 Hz kernel tick.  Every consumer of this
// module MUST pair the two, otherwise proc_tick() stops firing and the
// scheduler stalls for the rest of the session.

#include "gui.h"
#include "timer.h"
#include "serial.h"

extern char __kernel_start[], __kernel_end[];

static unsigned long stat_frames;        /* screen updates (flips) */
static unsigned long stat_render_us;     /* full repaint total us */
static unsigned long stat_render_max_us;
static unsigned long stat_cursor_us;     /* cursor-only updates total us */
static unsigned long stat_elapsed_us;
static unsigned int stat_tick_last;

/* decimal digits of v, appended to p; returns digit count */
static int utoa10(unsigned long v, char *p) {
    char tmp[16];
    int n = 0, i;

    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (i = 0; i < n; i++)
        p[i] = tmp[n - 1 - i];
    return n;
}

static void stats_reset_window(void) {
    stat_frames = 0;
    stat_render_us = 0;
    stat_render_max_us = 0;
    stat_cursor_us = 0;
    stat_elapsed_us = 0;
}

void stats_init(void) {
    timer_pit_borrow();
    stat_tick_last = pit_now_export();
    stats_reset_window();
}

void stats_shutdown(void) {
    timer_pit_return();
    stats_reset_window();
}

void stat_render_add(unsigned long du) {
    stat_render_us += du;
    if (du > stat_render_max_us)
        stat_render_max_us = du;
    stat_frames++;
}

void stat_cursor_add(unsigned long du) {
    stat_cursor_us += du;
    stat_frames++;
}

static void stats_report(void) {
    char buf[128];
    unsigned long fps, frametime_us, busy_pct, idle_pct, mem_kernel, mem_gui;
    unsigned int i = 0;
    unsigned long work_us = stat_render_us + stat_cursor_us;

    fps = stat_frames * 1000000ul / (stat_elapsed_us ? stat_elapsed_us : 1);
    frametime_us = stat_frames
        ? (stat_elapsed_us / stat_frames) : 0;
    busy_pct = work_us * 100ul /
        (stat_elapsed_us ? stat_elapsed_us : 1);
    idle_pct = 100 - busy_pct;
    mem_kernel = (unsigned long)(__kernel_end - __kernel_start) / 1024ul;
    mem_gui = GUI_W * GUI_H * 4ul / 1024ul;

    buf[i++] = 'g'; buf[i++] = 'u'; buf[i++] = 'i';
    buf[i++] = ' '; buf[i++] = 'f'; buf[i++] = 'p'; buf[i++] = 's';
    buf[i++] = '='; i += (unsigned int)utoa10(fps, buf + i);
    buf[i++] = ' '; buf[i++] = 'f'; buf[i++] = 'r';
    buf[i++] = 'a'; buf[i++] = 'm'; buf[i++] = 'e'; buf[i++] = '=';
    i += (unsigned int)utoa10(frametime_us, buf + i);
    buf[i++] = 'u'; buf[i++] = 's';
    buf[i++] = ' '; buf[i++] = 'r'; buf[i++] = 'e'; buf[i++] = 'n';
    buf[i++] = 'd'; buf[i++] = 'e'; buf[i++] = 'r'; buf[i++] = '=';
    i += (unsigned int)utoa10(stat_render_us, buf + i);
    buf[i++] = 'u'; buf[i++] = 's';
    buf[i++] = ' '; buf[i++] = 'm'; buf[i++] = 'a'; buf[i++] = 'x';
    buf[i++] = '='; i += (unsigned int)utoa10(stat_render_max_us, buf + i);
    buf[i++] = 'u'; buf[i++] = 's';
    buf[i++] = ' '; buf[i++] = 'c'; buf[i++] = 'u'; buf[i++] = 'r';
    buf[i++] = '='; i += (unsigned int)utoa10(stat_cursor_us, buf + i);
    buf[i++] = 'u'; buf[i++] = 's';
    buf[i++] = ' '; buf[i++] = 'b'; buf[i++] = 'u'; buf[i++] = 's';
    buf[i++] = 'y'; buf[i++] = '='; i += (unsigned int)utoa10(busy_pct, buf + i);
    buf[i++] = '%';
    buf[i++] = ' '; buf[i++] = 'i'; buf[i++] = 'd'; buf[i++] = 'l';
    buf[i++] = 'e'; buf[i++] = '='; i += (unsigned int)utoa10(idle_pct, buf + i);
    buf[i++] = '%';
    buf[i++] = ' '; buf[i++] = 'm'; buf[i++] = 'e'; buf[i++] = 'm';
    buf[i++] = '='; i += (unsigned int)utoa10(mem_kernel, buf + i);
    buf[i++] = 'K'; buf[i++] = '+'; i += (unsigned int)utoa10(mem_gui, buf + i);
    buf[i++] = 'K'; buf[i++] = '\r'; buf[i++] = '\n';
    buf[i] = '\0';
    serial_write(buf);
}

void stats_tick(void) {
    unsigned long du = pit_us_since_export(stat_tick_last);

    stat_tick_last = pit_now_export();
    stat_elapsed_us += du;
    if (stat_elapsed_us >= 2000000ul) {
        stats_report();
        stats_reset_window();
    }
}
