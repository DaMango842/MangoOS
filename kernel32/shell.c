// kernel32/shell.c - command-line shell for MangoOS
#include "proc/proc.h"
#include "mangofs.h"
#include "memory.h"
#include "syscall.h"
#include "vga.h"
#include "video.h"
#include "serial.h"
#include "keyboard.h"
#include "mouse.h"
#include "time.h"

#define SHELL_PROMPT "Mango> "

#define KEY_TAB       0x09
#define KEY_ENTER     0x0D
#define KEY_BACKSPACE 0x08
#define KEY_ESC       0x1B

#define LINE_MAX 128

/* Programs are loaded into managed pool memory (mem_alloc_pages), never at
   a hard-coded address - the old PROG_LOAD_ADDR 0x20000 lived inside the
   kernel image and `run` clobbered the kernel it was executing from.  The
   cap keeps a single program's code+bss well inside the 16 KiB
   FS_MAX_FILE_SIZE image limit plus a reasonable zero-init area. */
#define PROG_MAX_IMAGE  0x40000ul   /* 256 KB */
#define PROG_MAGIC     0x414D   /* 'MA' little-endian */

static unsigned char shell_attr = 0x07;

/* From the MangoWin GUI and the NES emulator, both shell commands. */
extern void gui_enter(void);
extern void fc_enter(void);

static void outb(unsigned short port, unsigned char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void shell_pause(void) {
    __asm__ volatile("pause");
}

static int shell_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

static void shell_print_dec(unsigned int v) {
    char buf[12];
    int i = 0;

    do {
        buf[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);

    while (i > 0)
        vga_putc(buf[--i]);
}

static void shell_print_dec2(int v) {
    vga_putc((char)('0' + v / 10));
    vga_putc((char)('0' + v % 10));
}

static int shell_atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

static const char *shell_next_arg(const char *s) {
    while (*s && *s != ' ')
        s++;
    while (*s == ' ')
        s++;
    return s;
}

static void cmd_version(void) {
    vga_write("Mango DOS v0.2\r\n");
}

static void cmd_help(void) {
    vga_write("help     - show this help\r\n");
    vga_write("version  - show kernel version\r\n");
    vga_write("cls      - clear the screen\r\n");
    vga_write("echo     - print the rest of the line\r\n");
    vga_write("dir      - list files on disk\r\n");
    vga_write("create   - create an empty file: create <name>\r\n");
    vga_write("write    - write text to a file: write <name> <text>\r\n");
    vga_write("del      - delete a file: del <name>\r\n");
    vga_write("type     - display file content\r\n");
    vga_write("run      - run a program from disk\r\n");
    vga_write("date     - show current date (RTC)\r\n");
    vga_write("time     - show current time (RTC)\r\n");
    vga_write("color    - set text color: color <name|hex|reset>\r\n");
    vga_write("mouse    - show mouse position and buttons\r\n");
    vga_write("video    - show framebuffer info\r\n");
    vga_write("mem      - show memory manager statistics\r\n");
    vga_write("rect     - fill a rectangle: rect <x> <y> <w> <h> [color]\r\n");
    vga_write("line     - draw a line: line <x0> <y0> <x1> <y1> [color]\r\n");
    vga_write("frame    - draw a rect outline: frame <x> <y> <w> <h> [color]\r\n");
    vga_write("circle   - draw a circle: circle <cx> <cy> <r> [color]\r\n");
    vga_write("grad     - draw a truecolor rainbow: grad <x> <y> <w> <h>\r\n");
    vga_write("mangoWin - open the MangoWin graphical interface\r\n");
    vga_write("fc       - run the built-in NES emulator (800x600x32 only)\r\n");
    vga_write("halt     - power down the system\r\n");
    vga_write("reboot   - reboot the system\r\n");
}

static void cmd_echo(const char *rest) {
    if (*rest)
        vga_write(rest);
    vga_write("\r\n");
}

static void cmd_halt(void) {
    vga_write("System halted.\r\n");
    for (;;)
        __asm__ volatile("hlt");
}

static void cmd_reboot(void) {
    vga_write("Rebooting...\r\n");
    outb(0x64, 0xFE);   /* 8042 reset pulse */
    for (;;)
        __asm__ volatile("hlt");
}

/* print an 8.3 entry as "NAME.EXT" (spaces stripped) */
static void shell_print_83(const mangofs_entry_t *e) {
    int i;

    for (i = 0; i < 8 && e->name[i] != ' '; i++)
        vga_putc(e->name[i]);
    vga_putc('.');
    for (i = 0; i < 3 && e->ext[i] != ' '; i++)
        vga_putc(e->ext[i]);
}

static void cmd_dir(void) {
    mangofs_entry_t e;
    int i, count = 0;

    if (!fs_ready()) {
        vga_write("File system not ready.\r\n");
        return;
    }
    if (fs_read_root() != 0) {
        vga_write("Read error.\r\n");
        return;
    }

    for (i = 0; i < 32; i++) {
        int r = fs_root_entry(i, &e);
        if (r != 0)
            continue;               /* skip free slots */

        /* color by extension: .TXT yellow, .BIN green */
        if (e.ext[0] == 'T' && e.ext[1] == 'X' && e.ext[2] == 'T')
            vga_set_attr(0x0E);
        else if (e.ext[0] == 'B' && e.ext[1] == 'I' && e.ext[2] == 'N')
            vga_set_attr(0x0A);
        else
            vga_set_attr(shell_attr);

        shell_print_83(&e);
        vga_set_attr(shell_attr);
        vga_write("  ");
        shell_print_dec(e.size);
        vga_write(" B\r\n");
        count++;
    }

    shell_print_dec(count);
    vga_write(" file(s)\r\n");
}

static void cmd_type(const char *arg) {
    mangofs_entry_t e;
    char name83[11];
    const unsigned char *data;
    int size, i;

    if (!fs_ready()) {
        vga_write("File system not ready.\r\n");
        return;
    }

    fs_parse_8_3(arg, name83);
    if (fs_find(name83, &e) != 0) {
        vga_write("File not found: ");
        vga_write(arg);
        vga_write("\r\n");
        return;
    }

    size = fs_read_file(&e);
    if (size < 0) {
        vga_write("Read error.\r\n");
        return;
    }
    if (size == 0) {
        vga_write("The file is empty.\r\n");
        return;
    }

    data = fs_file_data();
    for (i = 0; i < size; i++) {
        if (data[i] == 0)
            break;
        vga_putc((char)data[i]);
    }
    vga_write("\r\n");
}

static void cmd_create(const char *arg) {
    char name83[11];

    if (!*arg) {
        vga_write("Usage: create <name>\r\n");
        return;
    }
    if (!fs_ready()) {
        vga_write("File system not ready.\r\n");
        return;
    }
    fs_parse_8_3(arg, name83);
    if (fs_create(name83) != 0) {
        vga_write("Could not create: ");
        vga_write(arg);
        vga_write("\r\n");
        return;
    }
    vga_write("File created.\r\n");
}

static void cmd_write(const char *arg) {
    char name83[11];
    char *data;
    int len;

    if (!*arg) {
        vga_write("Usage: write <name> <text>\r\n");
        return;
    }
    if (!fs_ready()) {
        vga_write("File system not ready.\r\n");
        return;
    }

    data = (char *)arg;
    while (*data && *data != ' ')
        data++;
    if (*data == ' ') {
        *data = '\0';
        data++;
        while (*data == ' ')
            data++;
    }

    len = 0;
    while (data[len])
        len++;

    fs_parse_8_3(arg, name83);
    if (fs_write_file(name83, data, len) != 0) {
        vga_write("Could not write: ");
        vga_write(arg);
        vga_write("\r\n");
        return;
    }
    vga_write("File written.\r\n");
}

static void cmd_delete(const char *arg) {
    char name83[11];

    if (!*arg) {
        vga_write("Usage: del <name>\r\n");
        return;
    }
    if (!fs_ready()) {
        vga_write("File system not ready.\r\n");
        return;
    }
    fs_parse_8_3(arg, name83);
    if (fs_delete(name83) != 0) {
        vga_write("File not found: ");
        vga_write(arg);
        vga_write("\r\n");
        return;
    }
    vga_write("File deleted.\r\n");
}

static void cmd_run(const char *arg) {
    mangofs_entry_t e;
    char name83[11];
    const unsigned char *data;
    unsigned char *dst;
    unsigned int entry, bss, need_pages;
    int size, i;

    if (!fs_ready()) {
        vga_write("File system not ready.\r\n");
        return;
    }

    fs_parse_8_3(arg, name83);
    if (fs_find(name83, &e) != 0) {
        vga_write("Program not found: ");
        vga_write(arg);
        vga_write("\r\n");
        return;
    }

    size = fs_read_file(&e);
    if (size < 0) {
        vga_write("Read error.\r\n");
        return;
    }
    if (size < 12) {
        vga_write("Not a Mango program.\r\n");
        return;
    }

    data = fs_file_data();
    if (data[0] != 'M' || data[1] != 'A') {
        vga_write("Bad program magic.\r\n");
        return;
    }

    entry = (unsigned int)data[2] | ((unsigned int)data[3] << 8) |
            ((unsigned int)data[4] << 16) | ((unsigned int)data[5] << 24);
    bss = (unsigned int)data[6] | ((unsigned int)data[7] << 8) |
          ((unsigned int)data[8] << 16) | ((unsigned int)data[9] << 24);

    /* Sanity-check the image header BEFORE reserving memory, otherwise a
       corrupt entry offset turns into a wild jump. */
    if (entry >= (unsigned int)size) {
        vga_write("Bad entry offset.\r\n");
        return;
    }
    /* Overflow-safe form: `size + bss` computed in one go can wrap on
       32-bit, which would let a huge bss slip past the limit and then
       zero-fill far past the allocation. */
    if (bss > PROG_MAX_IMAGE ||
        (unsigned int)size > PROG_MAX_IMAGE - bss) {
        vga_write("Program too large (max ");
        {
            char num[12];
            int n = 0;
            unsigned long v = PROG_MAX_IMAGE;
            do { num[n++] = (char)('0' + v % 10); v /= 10; } while (v);
            for (i = n - 1; i >= 0; i--)
                vga_putc(num[i]);
        }
        vga_write(" bytes).\r\n");
        return;
    }

    /* Load into managed pool memory, never at a fixed address: the kernel
       image occupies 0x10000..__kernel_end and the old fixed
       PROG_LOAD_ADDR (0x20000) sat INSIDE it, so `run` overwrote the
       running kernel's own code as soon as it copied a few bytes.  The
       pool starts at 0x2E0000, well clear of both the kernel image and
       the fixed GUI back buffer.  See memory.h. */
    need_pages = ((unsigned int)size + bss + 0xFFFu) >> 12;
    dst = (unsigned char *)mem_alloc_pages(need_pages);
    if (dst == 0) {
        vga_write("Out of memory loading program.\r\n");
        return;
    }

    for (i = 0; i < size; i++)
        dst[i] = data[i];
    for (i = 0; i < (int)bss; i++)
        dst[size + i] = 0;

    /* Run it.  There is no user mode and no address-space isolation yet:
       the program shares the kernel's flat 32-bit address space and
       reaches the kernel only through the ring-0 int 0x80 gate (see
       syscall.h and ABI32.md).  Returns normally when it calls SYS_EXIT
       or falls off its end. */
    ((void (*)(void))(dst + entry))();

    mem_free_pages((unsigned int)dst, need_pages);
}

static void cmd_mouse(void) {
    shell_print_dec(mouse_x);
    vga_putc(',');
    shell_print_dec(mouse_y);
    vga_write(" buttons=");
    shell_print_dec(mouse_read_buttons());
    vga_write("\r\n");
}

static void cmd_video(void) {
    if (!video_active()) {
        vga_write("text mode (VGA B8000)\r\n");
        return;
    }
    vga_write("framebuffer ");
    shell_print_dec(video_width());
    vga_putc('x');
    shell_print_dec(video_height());
    vga_putc('x');
    shell_print_dec(video_bpp());
    vga_write("\r\n");
}

static void cmd_mem(void) {
    mem_stats_t s;

    mem_stats(&s);
    vga_write("Mem: ");
    shell_print_dec(s.pages_total);
    vga_write(" pages (");
    shell_print_dec(s.pages_total / 256u);
    vga_write(" MB), free ");
    shell_print_dec(s.pages_free);
    vga_write(" pages, heap in use ");
    shell_print_dec(s.heap_used);
    vga_write(" B\r\n");
}

static void cmd_date(void) {
    int y, mo, d, h, mi, s;

    time_get(&y, &mo, &d, &h, &mi, &s);

    shell_print_dec((unsigned int)y);
    vga_putc('-');
    shell_print_dec2(mo);
    vga_putc('-');
    shell_print_dec2(d);
    vga_write("\r\n");
}

static void cmd_time(void) {
    int y, mo, d, h, mi, s;

    time_get(&y, &mo, &d, &h, &mi, &s);

    shell_print_dec2(h);
    vga_putc(':');
    shell_print_dec2(mi);
    vga_putc(':');
    shell_print_dec2(s);
    vga_write("\r\n");
}

static int shell_hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static const char *const color_names[] = {
    "black", "blue", "green", "cyan", "red", "magenta", "brown",
    "light-gray", "dark-gray", "light-blue", "light-green",
    "light-cyan", "light-red", "light-magenta", "yellow", "white",
    0
};

static void shell_parse_color(const char *s, unsigned int *color) {
    int r1, r2, g1, g2, b1, b2;

    if (!s || !*s)
        return;
    /* full 6-digit hex RRGGBB */
    r1 = shell_hex_digit(s[0]);
    r2 = shell_hex_digit(s[1]);
    g1 = shell_hex_digit(s[2]);
    g2 = shell_hex_digit(s[3]);
    b1 = shell_hex_digit(s[4]);
    b2 = shell_hex_digit(s[5]);
    if (r1 >= 0 && r2 >= 0 && g1 >= 0 && g2 >= 0 && b1 >= 0 && b2 >= 0) {
        *color = (unsigned int)((r1 << 4 | r2) << 16 |
                                (g1 << 4 | g2) << 8 |
                                (b1 << 4 | b2));
        return;
    }
    /* 3-digit hex shorthand RGB: f00 -> ff0000 */
    if (s[3] == '\0' && r1 >= 0 && g1 >= 0 && b1 >= 0) {
        r2 = r1 << 4 | r1;
        g2 = g1 << 4 | g1;
        b2 = b1 << 4 | b1;
        *color = (unsigned int)((r2 << 16) | (g2 << 8) | b2);
        return;
    }
    if (shell_strcmp(s, "red") == 0)
        *color = 0xFF0000;
    else if (shell_strcmp(s, "green") == 0 || shell_strcmp(s, "lime") == 0)
        *color = 0x00FF00;
    else if (shell_strcmp(s, "blue") == 0)
        *color = 0x0000FF;
    else if (shell_strcmp(s, "white") == 0)
        *color = 0xFFFFFF;
    else if (shell_strcmp(s, "yellow") == 0)
        *color = 0xFFFF00;
    else if (shell_strcmp(s, "cyan") == 0 || shell_strcmp(s, "aqua") == 0)
        *color = 0x00FFFF;
    else if (shell_strcmp(s, "orange") == 0)
        *color = 0xFFA500;
    else if (shell_strcmp(s, "pink") == 0)
        *color = 0xFFC0CB;
    else if (shell_strcmp(s, "purple") == 0 || shell_strcmp(s, "fuchsia") == 0)
        *color = 0xFF00FF;
    else if (shell_strcmp(s, "silver") == 0)
        *color = 0xC0C0C0;
    else if (shell_strcmp(s, "gray") == 0 || shell_strcmp(s, "grey") == 0)
        *color = 0x808080;
    else if (shell_strcmp(s, "navy") == 0)
        *color = 0x000080;
    else if (shell_strcmp(s, "teal") == 0)
        *color = 0x008080;
    else if (shell_strcmp(s, "olive") == 0)
        *color = 0x808000;
    else if (shell_strcmp(s, "maroon") == 0)
        *color = 0x800000;
}

static void cmd_rect(const char *arg) {
    int x, y, w, h;
    unsigned int color = 0xAA0000;

    if (!video_active()) {
        vga_write("No framebuffer.\r\n");
        return;
    }

    /* parse: x y w h [color] */
    x = shell_atoi(arg);
    arg = shell_next_arg(arg);
    y = shell_atoi(arg);
    arg = shell_next_arg(arg);
    w = shell_atoi(arg);
    arg = shell_next_arg(arg);
    h = shell_atoi(arg);
    arg = shell_next_arg(arg);

    if (x < 0 || y < 0 || w < 1 || h < 1) {
        vga_write("Usage: rect <x> <y> <w> <h> [color]\r\n");
        return;
    }

    shell_parse_color(arg, &color);

    video_rect(x, y, w, h, color);
    vga_write("Rect drawn.\r\n");
}

static void cmd_line(const char *arg) {
    int x0, y0, x1, y1;
    unsigned int color = 0x00FF00;

    if (!video_active()) {
        vga_write("No framebuffer.\r\n");
        return;
    }

    /* parse: x0 y0 x1 y1 [color] */
    x0 = shell_atoi(arg);
    arg = shell_next_arg(arg);
    y0 = shell_atoi(arg);
    arg = shell_next_arg(arg);
    x1 = shell_atoi(arg);
    arg = shell_next_arg(arg);
    y1 = shell_atoi(arg);
    arg = shell_next_arg(arg);

    shell_parse_color(arg, &color);

    video_line(x0, y0, x1, y1, color);
    vga_write("Line drawn.\r\n");
}

static void cmd_frame(const char *arg) {
    int x, y, w, h;
    unsigned int color = 0x00FFFF;

    if (!video_active()) {
        vga_write("No framebuffer.\r\n");
        return;
    }

    /* parse: x y w h [color] */
    x = shell_atoi(arg);
    arg = shell_next_arg(arg);
    y = shell_atoi(arg);
    arg = shell_next_arg(arg);
    w = shell_atoi(arg);
    arg = shell_next_arg(arg);
    h = shell_atoi(arg);
    arg = shell_next_arg(arg);

    if (x < 0 || y < 0 || w < 2 || h < 2) {
        vga_write("Usage: frame <x> <y> <w> <h> [color]\r\n");
        return;
    }

    shell_parse_color(arg, &color);

    video_frame(x, y, w, h, color);
    vga_write("Frame drawn.\r\n");
}

static void cmd_circle(const char *arg) {
    int cx, cy, r;
    unsigned int color = 0x0000FF;

    if (!video_active()) {
        vga_write("No framebuffer.\r\n");
        return;
    }

    /* parse: cx cy r [color] */
    cx = shell_atoi(arg);
    arg = shell_next_arg(arg);
    cy = shell_atoi(arg);
    arg = shell_next_arg(arg);
    r = shell_atoi(arg);
    arg = shell_next_arg(arg);

    if (cx < 0 || cy < 0 || r < 1) {
        vga_write("Usage: circle <cx> <cy> <r> [color]\r\n");
        return;
    }

    shell_parse_color(arg, &color);

    video_circle(cx, cy, r, color);
    vga_write("Circle drawn.\r\n");
}

static unsigned int grad_rgb(unsigned int i, unsigned int n) {
    /* rainbow sweep across [0,n): red->yellow->green->cyan->blue->magenta */
    unsigned int seg, f, v;

    seg = i * 6 / n;
    if (seg > 5)
        seg = 5;
    f = i * 6 - seg * n;
    v = f * 255 / n;
    switch (seg) {
    case 0:
        return (255u << 16) | (v << 8);              /* red -> yellow */
    case 1:
        return ((255 - v) << 16) | (255u << 8);      /* yellow -> green */
    case 2:
        return (255u << 8) | v;                      /* green -> cyan */
    case 3:
        return (v << 16) | 255u;                     /* cyan -> blue */
    case 4:
        return (255u << 16) | ((255 - v) << 8);      /* blue -> magenta */
    default:
        return (255u << 16) | (v << 8);              /* magenta -> red */
    }
}

static void cmd_grad(const char *arg) {
    int x, y, w, h, dx;

    if (!video_active()) {
        vga_write("No framebuffer.\r\n");
        return;
    }

    /* parse: x y w h */
    x = shell_atoi(arg);
    arg = shell_next_arg(arg);
    y = shell_atoi(arg);
    arg = shell_next_arg(arg);
    w = shell_atoi(arg);
    arg = shell_next_arg(arg);
    h = shell_atoi(arg);

    if (x < 0 || y < 0 || w < 2 || h < 1) {
        vga_write("Usage: grad <x> <y> <w> <h>\r\n");
        return;
    }

    for (dx = 0; dx < w; dx++)
        video_rect(x + dx, y, 1, h, grad_rgb((unsigned int)dx, (unsigned int)w));
    vga_write("Gradient drawn.\r\n");
}

static void cmd_color(const char *arg) {
    int i, hi, lo;

    if (!*arg || shell_strcmp(arg, "reset") == 0) {
        shell_attr = 0x07;
        vga_set_attr(shell_attr);
        vga_write("Color set.\r\n");
        return;
    }

    for (i = 0; color_names[i] != 0; i++) {
        if (shell_strcmp(arg, color_names[i]) == 0) {
            shell_attr = (unsigned char)i;
            vga_set_attr(shell_attr);
            vga_write("Color set.\r\n");
            return;
        }
    }

    /* 1-2 hex digits (e.g. 0A, F) */
    hi = shell_hex_digit(arg[0]);
    if (hi >= 0 && arg[1] == '\0') {
        shell_attr = (unsigned char)hi;
        vga_set_attr(shell_attr);
        vga_write("Color set.\r\n");
        return;
    }
    if (hi >= 0) {
        lo = shell_hex_digit(arg[1]);
        if (lo >= 0 && arg[2] == '\0') {
            shell_attr = (unsigned char)(hi << 4 | lo);
            vga_set_attr(shell_attr);
            vga_write("Color set.\r\n");
            return;
        }
    }

    vga_write("Usage: color <name|hex|reset>\r\n");
}

static void shell_dispatch(char *line) {
    char *cmd = line;
    char *rest = line;

    while (*rest && *rest != ' ')
        rest++;
    if (*rest == ' ') {
        *rest++ = '\0';
        while (*rest == ' ')
            rest++;
    }

    if (*cmd == '\0')
        return;

    if (shell_strcmp(cmd, "help") == 0)
        cmd_help();
    else if (shell_strcmp(cmd, "version") == 0 || shell_strcmp(cmd, "ver") == 0)
        cmd_version();
    else if (shell_strcmp(cmd, "cls") == 0 || shell_strcmp(cmd, "clear") == 0)
        vga_clear();
    else if (shell_strcmp(cmd, "echo") == 0)
        cmd_echo(rest);
    else if (shell_strcmp(cmd, "dir") == 0 || shell_strcmp(cmd, "ls") == 0)
        cmd_dir();
    else if (shell_strcmp(cmd, "type") == 0)
        cmd_type(rest);
    else if (shell_strcmp(cmd, "create") == 0 || shell_strcmp(cmd, "touch") == 0)
        cmd_create(rest);
    else if (shell_strcmp(cmd, "write") == 0)
        cmd_write(rest);
    else if (shell_strcmp(cmd, "del") == 0 || shell_strcmp(cmd, "delete") == 0 ||
             shell_strcmp(cmd, "rm") == 0)
        cmd_delete(rest);
    else if (shell_strcmp(cmd, "run") == 0)
        cmd_run(rest);
    else if (shell_strcmp(cmd, "date") == 0)
        cmd_date();
    else if (shell_strcmp(cmd, "time") == 0)
        cmd_time();
    else if (shell_strcmp(cmd, "color") == 0)
        cmd_color(rest);
    else if (shell_strcmp(cmd, "mouse") == 0)
        cmd_mouse();
    else if (shell_strcmp(cmd, "video") == 0)
        cmd_video();
    else if (shell_strcmp(cmd, "mem") == 0)
        cmd_mem();
    else if (shell_strcmp(cmd, "rect") == 0)
        cmd_rect(rest);
    else if (shell_strcmp(cmd, "line") == 0)
        cmd_line(rest);
    else if (shell_strcmp(cmd, "frame") == 0)
        cmd_frame(rest);
    else if (shell_strcmp(cmd, "circle") == 0)
        cmd_circle(rest);
    else if (shell_strcmp(cmd, "grad") == 0)
        cmd_grad(rest);
    else if (shell_strcmp(cmd, "mangoWin") == 0)
        gui_enter();
    else if (shell_strcmp(cmd, "fc") == 0)
        fc_enter();
    else if (shell_strcmp(cmd, "halt") == 0 || shell_strcmp(cmd, "shutdown") == 0)
        cmd_halt();
    else if (shell_strcmp(cmd, "reboot") == 0)
        cmd_reboot();
    else {
        vga_write("Unknown command: ");
        vga_write(cmd);
        vga_write("\r\n");
    }
}

static void shell_read_line(char *buf, int max) {
    int len = 0;
    int key;
    unsigned int spins = 0;

    for (;;) {
        key = kbd_get_key();
        if (!key) {
            /* heartbeat: proves the polling loop is alive and how fast
               it spins (each marker ~= 2M empty polls) */
            if (++spins == 2000000u) {
                spins = 0;
                serial_write("SHELL: poll alive\r\n");
            }
            mouse_poll();
            shell_pause();
            proc_yield();
            continue;
        }

        if (key == KEY_ENTER) {
            vga_write("\r\n");
            buf[len] = '\0';
            return;
        }
        if (key == KEY_BACKSPACE) {
            if (len > 0) {
                len--;
                vga_write("\b \b");
            }
            continue;
        }

        if ((key == KEY_TAB || key == KEY_ESC) || key < 0x20)
            continue;
        if (len >= max - 1)
            continue;

        buf[len++] = (char)key;
        buf[len] = '\0';
        vga_putc((char)key);
    }
}

void shell_run(void) {
    char line[LINE_MAX];

    serial_write("SHELL: enter\r\n");
    fs_init();
    serial_write("SHELL: fs_init done\r\n");
    mouse_init();
    serial_write("SHELL: mouse_init done\r\n");
    kbd_init();
    serial_write("SHELL: kbd_init done, polling keys\r\n");

    for (;;) {
        vga_set_attr(shell_attr);
        vga_write(SHELL_PROMPT);

        shell_read_line(line, LINE_MAX);
        shell_dispatch(line);
    }
}