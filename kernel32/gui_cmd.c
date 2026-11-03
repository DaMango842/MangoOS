// kernel32/gui_cmd.c - MangoWin serial command injection.
// Commands: exit, clear, win <x> <y> <w> <h> <title>,
//           btn <win> <x> <y> <w> <h> <label> [print|close]
// The CLI keeps its own reader; while the GUI owns the loop, buffered
// bytes are consumed here (guaranteed complete lines).

#include "gui.h"
#include "serial.h"
#include "mouse.h"

static void itoa10(char *buf, int v) {
    char t[12];
    int i = 0, j = 0, neg = 0;

    if (v < 0) {
        neg = 1;
        v = -v;
    }
    do {
        t[i++] = '0' + (v % 10);
        v /= 10;
    } while (v);
    if (neg)
        buf[j++] = '-';
    while (i > 0)
        buf[j++] = t[--i];
    buf[j] = '\0';
}

static void gui_cmd(const char *line) {
    char arg[GUI_CAPTION];
    char ctl[CTL_LABEL];
    int i;
    int x, y, w, h;
    int act;

    if (line[0] == 'g' && line[1] == 'u' && line[2] == 'i' &&
        line[3] == ' ')
        line += 4;
    if (line[0] == 'm' && line[1] == 'p' && line[2] == 'o' &&
        line[3] == 's') {
        /* mpos: report the kernel-side mouse position */
        int mx, my;
        char buf[24];

        if (mouse_get_pos(&mx, &my) == 0) {
            serial_write("mpos ");
            itoa10(buf, mx);
            serial_write(buf);
            serial_write(",");
            itoa10(buf, my);
            serial_write(buf);
            serial_write("\r\n");
        }
        return;
    }
    if (line[0] == 's' && line[1] == 't' && line[2] == 'a' &&
        line[3] == 'r' && line[4] == 't') {
        /* start: toggle the start menu (scriptable) */
        gui_start_menu_toggle();
        return;
    }
    if (line[0] == 'e' && line[1] == 'x' && line[2] == 'i' &&
        line[3] == 't' && (line[4] == '\0' || line[4] == ' ')) {
        gui_quit = 1;
        return;
    }
    if (line[0] == 'c' && line[1] == 'l' && line[2] == 'e' &&
        line[3] == 'a' && line[4] == 'r') {
        for (i = GUI_MAX_WIN - 1; i >= 0; i--)
            if (wins[i].alive)
                gui_win_close(i);
        return;
    }
    if (line[0] == 'b' && line[1] == 't' && line[2] == 'n') {
        /* btn <win> <x> <y> <w> <h> <label> [print|close] */
        const char *p = line + 3;
        int win;

        while (*p == ' ')
            p++;
        win = 0;
        while (*p >= '0' && *p <= '9')
            win = win * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        x = 0;
        while (*p >= '0' && *p <= '9')
            x = x * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        y = 0;
        while (*p >= '0' && *p <= '9')
            y = y * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        w = 0;
        while (*p >= '0' && *p <= '9')
            w = w * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        h = 0;
        while (*p >= '0' && *p <= '9')
            h = h * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        for (i = 0; *p && *p != ' ' && i < CTL_LABEL - 1; i++)
            ctl[i] = *p++;
        ctl[i] = '\0';
        while (*p == ' ')
            p++;
        act = CTL_ACT_PRINT;
        if (p[0] == 'c' && p[1] == 'l' && p[2] == 'o' && p[3] == 's' &&
            p[4] == 'e')
            act = CTL_ACT_CLOSE;
        gui_ctl_add(win, x, y, w, h, ctl, act);
        return;
    }
    if (line[0] == 'l' && line[1] == 'a' && line[2] == 'b' &&
        line[3] == 'e' && line[4] == 'l') {
        /* label <win> <x> <y> <text> */
        const char *p = line + 5;
        int win;

        while (*p == ' ')
            p++;
        win = 0;
        while (*p >= '0' && *p <= '9')
            win = win * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        x = 0;
        while (*p >= '0' && *p <= '9')
            x = x * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        y = 0;
        while (*p >= '0' && *p <= '9')
            y = y * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        for (i = 0; *p && *p != '\n' && i < CTL_LABEL - 1; i++)
            arg[i] = *p++;
        arg[i] = '\0';
        gui_ctl_label(win, x, y, arg);
        return;
    }
    if (line[0] == 'w' && line[1] == 'i' && line[2] == 'n') {
        const char *p = line + 3;

        while (*p == ' ')
            p++;
        x = 0;
        while (*p >= '0' && *p <= '9')
            x = x * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        y = 0;
        while (*p >= '0' && *p <= '9')
            y = y * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        w = 0;
        while (*p >= '0' && *p <= '9')
            w = w * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        h = 0;
        while (*p >= '0' && *p <= '9')
            h = h * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
        for (i = 0; *p && i < GUI_CAPTION - 1; i++)
            arg[i] = *p++;
        arg[i] = '\0';
        gui_win_create(x, y, w, h, arg);
        return;
    }
}

void gui_serial_poll(void) {
    static char line[64];
    static int n = 0;
    int c;

    while ((c = serial_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            line[n] = '\0';
            if (n > 0)
                gui_cmd(line);
            n = 0;
        } else if (c == '\b') {
            if (n > 0)
                n--;
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = (char)c;
        }
    }
}
