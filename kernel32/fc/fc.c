// kernel32/fc/fc.c - fullscreen NES game mode for MangoOS.
//
// Usage: shell command "fc" (graphics mode required), Esc returns to the
// shell. Serial commands while running:
//   fcbtn <hex>   force joypad #1 state (scripted verification)
//   fcq           quit the emulator
// The top of the screen shows a status bar with the ROM name and FPS.

#include "fc.h"
#include "../render.h"
#include "../gui.h"
#include "../keyboard.h"
#include "../font16.h"

#define FC_BAR_H 20

extern int proc_yield(void);

static void fc_fps_tick(unsigned long frame_us);

static unsigned int frame_no;
static unsigned int fps_disp;           /* fps over the last ~1s window */
static int bench_on;                /* per-frame component timing (fcr) */
static unsigned int bench_left;
static int bar_drawn;                   /* status bar repainted only on change */
static unsigned int bar_fps_last = 0xFFFFFFFFu;
/* per-second frame cost breakdown in us: emulation (CPU+PPU) versus the
   surface render + blit.  Printed with the fps line so a slow frame gets
   attributed instead of guessed. */
static unsigned long perf_cpu_us, perf_ppu_us, perf_render_us;
static unsigned long perf_frames;
static int quit_flag;
static unsigned int override_mask;
static int override_on;

/* watch $0770 (SMB1 game-mode variable): record every change with the
   frame number, printed by fcdbg so scripts can see mode transitions */
static unsigned char mode_hist_val[24];
static unsigned int mode_hist_frame[24];
static unsigned int mode_hist_cnt;
static unsigned char last_mode;

static void mode_watch(void) {
    unsigned char m = fc.ram[0x770];

    if (m != last_mode) {
        if (mode_hist_cnt < 24) {
            mode_hist_val[mode_hist_cnt] = m;
            mode_hist_frame[mode_hist_cnt] = frame_no;
            mode_hist_cnt++;
        }
        last_mode = m;
    }
}

static void bar_clear(unsigned int bg) {
    unsigned int *dst = back;
    int i;

    for (i = 0; i < FC_BAR_H * 800; i++)
        dst[i] = bg;
}

static void bar_text(int x, int y, const char *s, unsigned int fg,
                     unsigned int bg) {
    while (*s) {
        const unsigned char *g = &font8x16[(unsigned int)*s * 16u];
        int gy, gx;

        for (gy = 0; gy < 16; gy++)
            for (gx = 0; gx < 8; gx++)
                back[(unsigned int)(y + gy) * 800u + (unsigned int)(x + gx)] =
                    (g[gy] & (0x80u >> (unsigned int)gx)) ? fg : bg;
        s++;
        x += 8;
    }
}

/* serial commands for the emulator loop */
extern unsigned int fc_trace_count(void);
extern unsigned short fc_trace_lookup(unsigned int n);
extern void fc_trace_reset(void);
extern unsigned int fc_ppu_wr_count(unsigned int reg);
extern unsigned short fc_ppu_wr_lookup(unsigned int reg, unsigned int n);

static const char hex[] = "0123456789ABCDEF";

/* decimal digits of v appended to p; advances *i */
static void put_dec(char *p, int *i, unsigned long v) {
    char t[16];
    int n = 0;

    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        p[(*i)++] = t[--n];
}
static unsigned int trace_frames = 0;      /* "fct <n>": trace next n frames */

static void app_dec(char *b, int *i, unsigned long v) {
    char tmp[12];
    int t = 0;

    do { tmp[t++] = hex[v % 10]; v /= 10; } while (v);
    while (t) b[(*i)++] = tmp[--t];
}

static void app_hex8(char *b, int *i, unsigned int v) {
    b[(*i)++] = hex[(v >> 4) & 15];
    b[(*i)++] = hex[v & 15];
}

static void app_hex16(char *b, int *i, unsigned int v) {
    b[(*i)++] = hex[(v >> 12) & 15];
    b[(*i)++] = hex[(v >> 8) & 15];
    b[(*i)++] = hex[(v >> 4) & 15];
    b[(*i)++] = hex[v & 15];
}

/* "fcs": extended machine state for title-screen diagnosis */
static void fc_dump_state(void) {
    char b[480];
    int i = 0;
    int k;

    b[i++] = '\r'; b[i++] = '\n'; b[i++] = 'S'; b[i++] = 'T';
    b[i++] = ' '; b[i++] = 'f'; b[i++] = '=';
    app_dec(b, &i, frame_no);
    b[i++] = ' '; b[i++] = 'p'; b[i++] = 'c'; b[i++] = '=';
    app_hex16(b, &i, fc.pc);
    b[i++] = ' '; b[i++] = 'a'; b[i++] = '=';
    app_hex8(b, &i, fc.a);
    b[i++] = ' '; b[i++] = 'x'; b[i++] = '=';
    app_hex8(b, &i, fc.x);
    b[i++] = ' '; b[i++] = 'y'; b[i++] = '=';
    app_hex8(b, &i, fc.y);
    b[i++] = ' '; b[i++] = 'p'; b[i++] = '=';
    app_hex8(b, &i, fc.p);
    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'C'; b[i++] = 'T'; b[i++] = 'R';
    b[i++] = '=';
    app_hex8(b, &i, fc.ppu_ctrl);
    b[i++] = ' '; b[i++] = 'M'; b[i++] = 'S'; b[i++] = 'K';
    b[i++] = '=';
    app_hex8(b, &i, fc.ppu_mask);
    b[i++] = ' '; b[i++] = 'S'; b[i++] = 'T'; b[i++] = 'S';
    b[i++] = '=';
    app_hex8(b, &i, fc.ppu_status);
    b[i++] = ' '; b[i++] = 'S'; b[i++] = 'X'; b[i++] = '=';
    app_hex8(b, &i, fc.ppu_scroll_x);
    b[i++] = ' '; b[i++] = 'S'; b[i++] = 'Y'; b[i++] = '=';
    app_hex8(b, &i, fc.ppu_scroll_y);
    b[i++] = ' '; b[i++] = 'T'; b[i++] = 'X'; b[i++] = '=';
    app_hex8(b, &i, fc.scroll_top_x);
    b[i++] = ' '; b[i++] = 'T'; b[i++] = 'Y'; b[i++] = '=';
    app_hex8(b, &i, fc.scroll_top_y);
    b[i++] = ' '; b[i++] = 'S'; b[i++] = 'P'; b[i++] = 'L';
    b[i++] = '=';
    app_dec(b, &i, fc.sprite0_hit_row);
    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'A'; b[i++] = 'D'; b[i++] = 'R';
    b[i++] = '=';
    app_hex16(b, &i, fc.ppu_addr & 0x3FFFu);
    b[i++] = ' '; b[i++] = 'L'; b[i++] = 'A'; b[i++] = 'T';
    b[i++] = '=';
    app_hex8(b, &i, fc.ppu_addr_latch);
    b[i++] = ' '; b[i++] = 'B'; b[i++] = 'U'; b[i++] = 'F';
    b[i++] = '=';
    app_hex8(b, &i, fc.ppu_read_buf);
    b[i++] = ' '; b[i++] = 'N'; b[i++] = 'M'; b[i++] = 'I';
    b[i++] = '=';
    app_dec(b, &i, fc.nmi_count);
    b[i++] = ' '; b[i++] = 'C'; b[i++] = 'Y';
    b[i++] = '=';
    app_dec(b, &i, fc.cycles);
    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'W';
    app_dec(b, &i, fc_ppu_wr_count(0));
    b[i++] = ' ';
    app_dec(b, &i, fc_ppu_wr_count(1));
    b[i++] = ' ';
    app_dec(b, &i, fc_ppu_wr_count(2));
    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'V'; b[i++] = 'A'; b[i++] = 'R';
    b[i++] = ' '; b[i++] = '7'; b[i++] = '7'; b[i++] = '0'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x770]);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '7'; b[i++] = '2'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x772]);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '3'; b[i++] = 'C'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x73C]);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '4'; b[i++] = 'E'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x74E]);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '7'; b[i++] = '3'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x773]);
    b[i++] = ' '; b[i++] = '6'; b[i++] = 'F'; b[i++] = 'C'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x6FC]);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '4'; b[i++] = 'A'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x74A]);
    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'P'; b[i++] = 'P'; b[i++] = 'U';
    for (k = 8; k >= 1; k--) {
        unsigned int n = fc_ppu_wr_count(1);
        unsigned int m = fc_ppu_wr_count(2);
        unsigned int v;

        b[i++] = ' ';
        if (n >= (unsigned int)k)
            v = fc_ppu_wr_lookup(1, n - (unsigned int)k);
        else
            v = 0xFFFFu;
        app_hex16(b, &i, v);
        b[i++] = ':';
        if (m >= (unsigned int)k)
            v = fc_ppu_wr_lookup(2, m - (unsigned int)k);
        else
            v = 0xFFFFu;
        app_hex16(b, &i, v);
    }
    b[i++] = '\r'; b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

/* "fct <n>": per-frame state line while the game logic runs */
static void fc_dump_trace(void) {
    char b[220];
    int i = 0;
    int k;

    b[i++] = 'T'; b[i++] = 'R'; b[i++] = ' '; b[i++] = 'f'; b[i++] = '=';
    app_dec(b, &i, frame_no);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '7'; b[i++] = '0'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x0770]);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '7'; b[i++] = '2'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x0772]);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '3'; b[i++] = 'C'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x073C]);
    b[i++] = ' '; b[i++] = '7'; b[i++] = '7'; b[i++] = '3'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x0773]);
    b[i++] = ' '; b[i++] = '6'; b[i++] = 'F'; b[i++] = 'C'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x06FC]);
    b[i++] = ' '; b[i++] = '4'; b[i++] = 'A'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x074A]);
    b[i++] = ' '; b[i++] = 'A'; b[i++] = '2'; b[i++] = '=';
    app_hex8(b, &i, fc.ram[0x07A2]);
    b[i++] = ' '; b[i++] = 'P'; b[i++] = 'L'; b[i++] = '=';
    app_hex8(b, &i, fc.pad_latch[0]);
    b[i++] = ' '; b[i++] = 'P'; b[i++] = 'S'; b[i++] = '=';
    app_hex8(b, &i, fc.pad_shift[0]);
    b[i++] = ' '; b[i++] = 'A'; b[i++] = 'D'; b[i++] = 'R'; b[i++] = '=';
    app_hex16(b, &i, fc.ppu_addr & 0x3FFFu);
    b[i++] = ' '; b[i++] = 'W';
    app_dec(b, &i, fc_ppu_wr_count(0));
    b[i++] = ' '; app_dec(b, &i, fc_ppu_wr_count(1));
    b[i++] = ' '; app_dec(b, &i, fc_ppu_wr_count(2));
    b[i++] = ' '; b[i++] = 'B';
    for (k = 0; k < 16; k++) {
        b[i++] = ' ';
        app_hex8(b, &i, fc.ram[0x0300 + k]);
    }
    b[i++] = '\r'; b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

/* "fcv <0|1>": dump nametable + attribute table of table 0 or 1 */
static void fc_dump_vram(unsigned int table) {
    static char b[3700];
    const unsigned char *nt = fc.vram + (table & 1u) * 0x400u;
    unsigned int row, col;
    int i = 0;

    b[i++] = '\r'; b[i++] = '\n';
    for (row = 0; row < 32; row++) {
        b[i++] = 'V'; b[i++] = hex[table & 1u]; b[i++] = ':';
        app_hex16(b, &i, row * 32u);
        b[i++] = ':';
        for (col = 0; col < 32; col++) {
            unsigned int v = nt[row * 32u + col];

            b[i++] = ' ';
            app_hex8(b, &i, v);
        }
        b[i++] = '\r'; b[i++] = '\n';
    }
    b[i++] = 'A'; b[i++] = hex[table & 1u]; b[i++] = ':';
    for (col = 0; col < 64; col++) {
        unsigned int v = nt[0x3C0u + col];

        if ((col & 15) == 0) {
            b[i++] = '\r'; b[i++] = '\n';
            b[i++] = ' '; b[i++] = ' ';
        }
        b[i++] = ' ';
        app_hex8(b, &i, v);
    }
    b[i++] = '\r'; b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

/* "fcp": dump the 32-entry palette RAM */
static void fc_dump_pal(void) {
    char b[300];
    int i = 0;
    int k;

    b[i++] = '\r'; b[i++] = '\n'; b[i++] = 'P'; b[i++] = 'A'; b[i++] = 'L';
    for (k = 0; k < 32; k++) {
        if ((k & 7) == 0) {
            b[i++] = '\r'; b[i++] = '\n';
        }
        b[i++] = ' ';
        app_hex8(b, &i, (unsigned int)k);
        b[i++] = ':';
        app_hex8(b, &i, fc.palette[k]);
    }
    b[i++] = '\r'; b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

/* "fco": dump the 64 OAM sprites */
static void fc_dump_oam(void) {
    char b[1400];
    int i = 0;
    int k;

    b[i++] = '\r'; b[i++] = '\n'; b[i++] = 'O'; b[i++] = 'A'; b[i++] = 'M';
    for (k = 0; k < 64; k++) {
        const unsigned char *s = fc.oam + k * 4;

        b[i++] = '\r'; b[i++] = '\n';
        app_hex8(b, &i, (unsigned int)k);
        b[i++] = ':';
        b[i++] = ' '; app_hex8(b, &i, s[0]);
        b[i++] = ' '; app_hex8(b, &i, s[1]);
        b[i++] = ' '; app_hex8(b, &i, s[2]);
        b[i++] = ' '; app_hex8(b, &i, s[3]);
    }
    b[i++] = '\r'; b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

static void fc_dump_oam_debug(void) {
    char b[1600];
    int i = 0;
    int k;

    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'O'; b[i++] = 'A'; b[i++] = 'M'; b[i++] = 'D'; b[i++] = 'B'; b[i++] = 'G';
    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'O'; b[i++] = 'A'; b[i++] = 'M'; b[i++] = 'A'; b[i++] = 'D'; b[i++] = 'R'; b[i++] = '='; 
    app_hex8(b, &i, fc.oam_addr);
    b[i++] = ' '; b[i++] = 'E'; b[i++] = 'V'; b[i++] = 'A'; b[i++] = 'L'; b[i++] = '='; 
    app_hex8(b, &i, fc.eval_state);
    b[i++] = ' '; b[i++] = 'N'; b[i++] = '='; 
    app_hex8(b, &i, (unsigned int)fc.eval_n);
    b[i++] = ' '; b[i++] = 'M'; b[i++] = '='; 
    app_hex8(b, &i, fc.eval_m);
    b[i++] = ' '; b[i++] = 'C'; b[i++] = 'N'; b[i++] = 'T'; b[i++] = '='; 
    app_hex8(b, &i, fc.sec_oam_count);
    b[i++] = ' '; b[i++] = 'O'; b[i++] = 'V'; b[i++] = 'F'; b[i++] = '='; 
    app_hex8(b, &i, fc.overflow_check);
    b[i++] = ' '; b[i++] = 'O'; b[i++] = 'D'; b[i++] = 'D'; b[i++] = '='; 
    app_hex8(b, &i, fc.eval_odd_cycle);
    b[i++] = '\r'; b[i++] = '\n';

    b[i++] = 'S'; b[i++] = 'E'; b[i++] = 'C'; b[i++] = ':'; b[i++] = '\r'; b[i++] = '\n';
    for (k = 0; k < 8; k++) {
        b[i++] = ' '; app_hex8(b, &i, k);
        b[i++] = ':';
        b[i++] = ' '; app_hex8(b, &i, fc.sec_oam[k * 4 + 0]);
        b[i++] = ' '; app_hex8(b, &i, fc.sec_oam[k * 4 + 1]);
        b[i++] = ' '; app_hex8(b, &i, fc.sec_oam[k * 4 + 2]);
        b[i++] = ' '; app_hex8(b, &i, fc.sec_oam[k * 4 + 3]);
        b[i++] = '\r'; b[i++] = '\n';
    }
    b[i++] = 'P'; b[i++] = 'R'; b[i++] = 'I'; b[i++] = 'M'; b[i++] = ':'; b[i++] = '\r'; b[i++] = '\n';
    for (k = 0; k < 64; k++) {
        const unsigned char *s = fc.oam + k * 4;
        b[i++] = ' '; app_hex8(b, &i, (unsigned int)k);
        b[i++] = ':';
        b[i++] = ' '; app_hex8(b, &i, s[0]);
        b[i++] = ' '; app_hex8(b, &i, s[1]);
        b[i++] = ' '; app_hex8(b, &i, s[2]);
        b[i++] = ' '; app_hex8(b, &i, s[3]);
        if ((k & 7) == 7) {
            b[i++] = '\r'; b[i++] = '\n';
        }
    }
    b[i] = '\0';
    serial_write(b);
}

/* "fctr": the last 64 executed PCs, oldest first.  When a ROM spins in a
   loop (blank screen, frozen registers) this shows the loop's shape -- an
   LDA $2002/BPL pair means it waits for the vblank flag, a DEY/BNE chain
   is a calibrated delay, and so on -- which beats guessing. */
static void fc_dump_trace64(void) {
    char b[48];
    int i = 0;
    unsigned int tn = fc_trace_count();
    int k;

    b[i++] = '\r'; b[i++] = '\n';
    for (k = 63; k >= 0; k--) {
        unsigned int val = (tn > (unsigned int)k)
                         ? fc_trace_lookup(tn - 1u - (unsigned int)k)
                         : 0u;

        b[i++] = hex[(val >> 12) & 15];
        b[i++] = hex[(val >> 8) & 15];
        b[i++] = hex[(val >> 4) & 15];
        b[i++] = hex[val & 15];
        b[i++] = ' ';
        if ((k & 7) == 0) {
            b[i++] = '\r'; b[i++] = '\n'; b[i] = '\0';
            serial_write(b);
            i = 0;
        }
    }
    if (i) {
        b[i++] = '\r'; b[i++] = '\n'; b[i] = '\0';
        serial_write(b);
    }
}

static void fc_dump(void) {
    char b[160];
    int i = 0;
    unsigned long val;
    int k;

    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'D'; b[i++] = 'B'; b[i++] = 'G';
    b[i++] = ' '; b[i++] = 'f'; b[i++] = 'r'; b[i++] = 'a'; b[i++] = 'm';
    b[i++] = 'e'; b[i++] = '=';
    val = frame_no;
    {
        char tmp[10];
        int t = 0;
        do { tmp[t++] = hex[val % 10]; val /= 10; } while (val);
        while (t) b[i++] = tmp[--t];
    }
    b[i++] = ' '; b[i++] = 'p'; b[i++] = 'c'; b[i++] = '=';
    val = fc.pc;
    b[i++] = hex[(val >> 12) & 15]; b[i++] = hex[(val >> 8) & 15];
    b[i++] = hex[(val >> 4) & 15]; b[i++] = hex[val & 15];
    b[i++] = ' '; b[i++] = 'a'; b[i++] = '='; b[i++] = hex[fc.a >> 4];
    b[i++] = hex[fc.a & 15];
    b[i++] = ' '; b[i++] = 'x'; b[i++] = '='; b[i++] = hex[fc.x >> 4];
    b[i++] = hex[fc.x & 15];
    b[i++] = ' '; b[i++] = 'y'; b[i++] = '='; b[i++] = hex[fc.y >> 4];
    b[i++] = hex[fc.y & 15];
    b[i++] = ' '; b[i++] = 's'; b[i++] = 'p'; b[i++] = '='; b[i++] = hex[fc.sp >> 4];
    b[i++] = hex[fc.sp & 15];
    b[i++] = ' '; b[i++] = 'c'; b[i++] = 't'; b[i++] = 'r'; b[i++] = 'l';
    b[i++] = '='; b[i++] = hex[fc.ppu_ctrl >> 4]; b[i++] = hex[fc.ppu_ctrl & 15];
    b[i++] = ' '; b[i++] = 'm'; b[i++] = 'a'; b[i++] = 's'; b[i++] = 'k';
    b[i++] = '='; b[i++] = hex[fc.ppu_mask >> 4]; b[i++] = hex[fc.ppu_mask & 15];
    b[i++] = ' '; b[i++] = 's'; b[i++] = 'x'; b[i++] = '='; b[i++] = hex[fc.ppu_scroll_x >> 4];
    b[i++] = hex[fc.ppu_scroll_x & 15];
    b[i++] = ' '; b[i++] = 's'; b[i++] = 'y'; b[i++] = '='; b[i++] = hex[fc.ppu_scroll_y >> 4];
    b[i++] = hex[fc.ppu_scroll_y & 15];
    b[i++] = ' '; b[i++] = 'p'; b[i++] = '0'; b[i++] = '='; b[i++] = hex[fc.palette[0] >> 4];
    b[i++] = hex[fc.palette[0] & 15];
    b[i++] = ' '; b[i++] = 'p'; b[i++] = '1'; b[i++] = '='; b[i++] = hex[fc.palette[1] >> 4];
    b[i++] = hex[fc.palette[1] & 15];
    b[i++] = ' '; b[i++] = 'p'; b[i++] = '2'; b[i++] = '='; b[i++] = hex[fc.palette[2] >> 4];
    b[i++] = hex[fc.palette[2] & 15];
    b[i++] = ' '; b[i++] = 'p'; b[i++] = '3'; b[i++] = '='; b[i++] = hex[fc.palette[3] >> 4];
    b[i++] = hex[fc.palette[3] & 15];
    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'M'; b[i++] = 'O'; b[i++] = 'D'; b[i++] = 'E';
    for (k = 0; k < (int)mode_hist_cnt; k++) {
        unsigned int f = mode_hist_frame[k];
        char tmp[6];
        int t = 0;

        do { tmp[t++] = hex[f % 10]; f /= 10; } while (f);
        b[i++] = ' ';
        while (t) b[i++] = tmp[--t];
        b[i++] = ':';
        b[i++] = hex[mode_hist_val[k] >> 4];
        b[i++] = hex[mode_hist_val[k] & 15];
    }
    b[i++] = '\r'; b[i++] = '\n';
    b[i++] = 'T'; b[i++] = 'R';
    for (k = 8; k >= 1; k--) {
        unsigned int tn = fc_trace_count();

        b[i++] = ' ';
        if (tn >= (unsigned int)k) {
            val = fc_trace_lookup(tn - k);
        } else {
            val = 0;
        }
        b[i++] = hex[(val >> 12) & 15]; b[i++] = hex[(val >> 8) & 15];
        b[i++] = hex[(val >> 4) & 15]; b[i++] = hex[val & 15];
    }
    b[i++] = '\r'; b[i++] = '\n';
    b[i] = '\0';
    serial_write(b);
}

static void fc_serial_poll(void) {
    static char line[24];
    static int n = 0;
    int c;

    while ((c = serial_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            line[n] = '\0';
            n = 0;
            if (line[0] == 'f' && line[1] == 'c' && line[2] == 'b' &&
                line[3] == 't' && line[4] == 'n') {
                const char *p = line + 5;
                unsigned int m = 0;

                while (*p == ' ')
                    p++;
                while ((*p >= '0' && *p <= '9') ||
                       (*p >= 'a' && *p <= 'f') ||
                       (*p >= 'A' && *p <= 'F')) {
                    unsigned int d;

                    if (*p >= '0' && *p <= '9')
                        d = (unsigned int)(*p - '0');
                    else if (*p >= 'a' && *p <= 'f')
                        d = (unsigned int)(*p - 'a' + 10);
                    else
                        d = (unsigned int)(*p - 'A' + 10);
                    m = m * 16u + d;
                    p++;
                }
                override_mask = m;
                override_on = 1;
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'd' &&
                       line[3] == 'b' && line[4] == 'g') {
                fc_dump();
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 's') {
                fc_dump_state();
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'v') {
                const char *p = line + 3;
                unsigned int t = 0;

                while (*p == ' ')
                    p++;
                if (*p >= '0' && *p <= '1')
                    t = (unsigned int)(*p - '0');
                fc_dump_vram(t);
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'p' &&
                       line[3] == 'w') {
                /* dump the last 64 $2007 writes: addr=value pairs */
                char b[1200];
                int i = 0;
                unsigned int total = fc_pw_count();
                unsigned int start = (total > 64) ? (total - 64) : 0;
                unsigned int n;

                b[i++] = '\r'; b[i++] = '\n';
                b[i++] = 'P'; b[i++] = 'W'; b[i++] = ' ';
                app_dec(b, &i, total);
                for (n = start; n < total; n++) {
                    unsigned short a;
                    unsigned char v;

                    fc_pw_lookup(n, &a, &v);
                    if (((n - start) & 7u) == 0) {
                        b[i++] = '\r'; b[i++] = '\n';
                        app_dec(b, &i, n);
                        b[i++] = ':';
                    }
                    b[i++] = ' ';
                    app_hex16(b, &i, a);
                    b[i++] = '=';
                    app_hex8(b, &i, v);
                }
                b[i++] = '\r'; b[i++] = '\n';
                b[i] = '\0';
                serial_write(b);
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 's' &&
                       line[3] == 'h') {
                /* dump the 256x240x4 framebuffer surface raw over serial
                   (BGRA little-endian); ~64 s at 38400 baud */
                const unsigned char *p =
                    (const unsigned char *)fc_surface;
                unsigned long total = 256UL * 240UL * 4UL;
                unsigned long n;

                for (n = 0; n < total; n++)
                    serial_putc((char)p[n]);
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'p') {
                fc_dump_pal();
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'o' &&
                       line[3] == 'a' && line[4] == 'm') {
                /* MUST be tested before the 3-letter "fco" below, which
                   would otherwise swallow every fco* command */
                fc_dump_oam_debug();
                fc_oam_dump_log();
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'o') {
                fc_dump_oam();
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 't' &&
                       line[3] == 'r') {
                fc_dump_trace64();
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 't') {
                const char *p = line + 3;
                unsigned int tn = 0;

                while (*p == ' ')
                    p++;
                while (*p >= '0' && *p <= '9')
                    tn = tn * 10u + (unsigned int)(*p++ - '0');
                trace_frames = tn;
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'r') {
                const char *p = line + 3;
                unsigned int bn = 30;

                while (*p == ' ')
                    p++;
                if (*p >= '0' && *p <= '9') {
                    bn = 0;
                    while (*p >= '0' && *p <= '9')
                        bn = bn * 10u + (unsigned int)(*p++ - '0');
                }
                bench_on = 1;
                bench_left = bn;
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'm') {
                const char *p = line + 4;
                unsigned long a = 0;
                unsigned int n = 16;
                int i, k;
                char b[96];

                while (*p == ' ')
                    p++;
                while ((*p >= '0' && *p <= '9') ||
                       (*p >= 'a' && *p <= 'f') ||
                       (*p >= 'A' && *p <= 'F')) {
                    unsigned int d;

                    if (*p >= '0' && *p <= '9')
                        d = (unsigned int)(*p - '0');
                    else if (*p >= 'a' && *p <= 'f')
                        d = (unsigned int)(*p - 'a' + 10);
                    else
                        d = (unsigned int)(*p - 'A' + 10);
                    a = a * 16u + d;
                    p++;
                }
                while (*p == ' ')
                    p++;
                if (*p >= '0' && *p <= '9') {
                    n = 0;
                    while (*p >= '0' && *p <= '9') {
                        n = n * 10u + (unsigned int)(*p - '0');
                        p++;
                    }
                }
                if (n > 32)
                    n = 32;
                i = 0;
                b[i++] = '\r'; b[i++] = '\n'; b[i++] = 'M';
                b[i++] = 'E'; b[i++] = 'M';
                for (k = 0; k < (int)n; k++) {
                    unsigned int v;

                    if ((k & 7) == 0) {
                        b[i++] = '\r'; b[i++] = '\n';
                        b[i++] = hex[(a >> 12) & 15];
                        b[i++] = hex[(a >> 8) & 15];
                        b[i++] = hex[(a >> 4) & 15];
                        b[i++] = hex[a & 15];
                        b[i++] = ':';
                    }
                    v = fc.ram[(a + (unsigned int)k) & 0x7FFu];
                    b[i++] = ' ';
                    b[i++] = hex[v >> 4];
                    b[i++] = hex[v & 15];
                }
                b[i++] = '\r'; b[i++] = '\n';
                b[i] = '\0';
                serial_write(b);
            } else if (line[0] == 'f' && line[1] == 'c' && line[2] == 'q') {
                quit_flag = 1;
            }
        } else if (n < 23) {
            line[n++] = (char)c;
        }
    }
}

static void reset_machine(void) {
    fc_trace_reset();
    fc_mem_init();
    fc_mem_reset();
    fc_ppu_reset();
    fc_cpu_reset();
    apu_register();
}

/* run one frame: sample input, render, run the CPU, blit */
static void run_frame(void) {
    unsigned int t0 = pit_now_export();
    unsigned int tr, tc;
    unsigned long t_ppu;

    if (override_on)
        fc.pad_latch[0] = override_mask;
    else
        fc_input_sample();

    fc.cycles = 0;

    /* reset the PPU timeline to the pre-render start of the frame */
    fc.ppu_scanline = 0;
    fc.ppu_dot = 0;
    fc_ppu_advance(0);

    fc_ppu_render();
    t_ppu = pit_us_since_export(t0);      /* input sampling + surface render */
    fc.scroll_top_flag = 1;
    fc.ppu_addr_latch = 0;               /* force a clean $2005 pair: a pair
                                            split across frames desyncs the
                                            top-scroll capture and later
                                            breaks split/hit detection */              /* next $2005 pair is the
                                            pre-sprite-0-hit scroll: the top
                                            rows of the frame stay fixed */
    tr = pit_now_export();
    /* batch NMI injection: preserves NMI/IRQ timing for AccuracyCoin
       #84 (NMI Overlap IRQ). Per-dot VBlank flag is in fc_ppu_advance. */
    if (fc.ppu_ctrl & 0x80u)
        fc_cpu_nmi();

    /* advance the PPU timeline after each executed instruction so the
       sprite evaluation FSM matches the 2C02 scanline/dot timing */
    {
        unsigned int target = FC_CPU_CYCLES_PER_FRAME;
        unsigned int sprite0_fired = 0;

        while (fc.cycles < target) {
            unsigned int step_cycles;

            /* NMI/IRQ dispatch lives here: the old batch runner serviced
               them inside fc_cpu_run; the per-instruction loop must do it
               explicitly or an NMI-driven game waits forever */
            fc_cpu_take_nmi();
            fc_cpu_take_irq();

            step_cycles = fc_cpu_step();

            fc.cycles += step_cycles;
            fc_ppu_advance(step_cycles);

            if (!sprite0_fired && fc.sprite0_hit &&
                fc.ppu_scanline == fc.sprite0_hit_row) {
                fc.ppu_status |= 0x40u;
                fc.sprite0_hit = 0;
                sprite0_fired = 1;
            } else if (!sprite0_fired && !fc.sprite0_hit &&
                       fc.ppu_scanline > 140u &&
                       (fc.ppu_mask & 0x18u) == 0x18u) {
                /* safety net: a frame whose detection failed must never
                   leave the game polling $2002 bit6 forever */
                fc.ppu_status |= 0x40u;
                sprite0_fired = 1;
            }
        }
    }
    mode_watch();
    tc = pit_now_export();
    {
        unsigned long whole = pit_us_since_export(t0);

        /* pit_us_since_export() can only measure deltas shorter than one
           wrap of the free-running PIT counter (65536 / 1193182 = 54.9 ms).
           A frame slower than that wraps an unknown number of times, and
           the helper then returns a small, entirely bogus number instead
           of signalling failure.  Feeding that into the averages produced
           wildly inconsistent cpu/ppu/rnd splits (multi-second "frame
           times"), which is how a real but modest optimisation was first
           mistaken for a 40% win.  Drop unmeasurable frames from the
           statistics instead of reporting nonsense; perf_frames only
           counts the frames that contributed a valid sample. */
        if (whole <= PIT_FREE_RUN_PERIOD_US && whole >= t_ppu) {
            perf_ppu_us += t_ppu;             /* surface render */
            perf_cpu_us += whole - t_ppu;     /* 6502 + PPU dot timeline */
            perf_render_us += pit_us_since_export(tc);
            perf_frames++;
        }
    }
    if (trace_frames) {
        trace_frames--;
        fc_dump_trace();
    }

    /* status bar: its only changing content is the fps number (once a
       second), so repaint it then instead of clearing 16K pixels and
       re-comparing them in the blit every single frame */
    if (!bar_drawn || bar_fps_last != fps_disp) {
        bar_drawn = 1;
        bar_fps_last = fps_disp;
        bar_clear(0x1E222Cu);
        bar_text(8, 2, "FC: NES Cartridge (NROM)", 0xFFFFFFu, 0x1E222Cu);
        {
            char buf[24];
            int i = 0;
            unsigned int fps = fps_disp;

            buf[i++] = 'f';
            buf[i++] = 'p';
            buf[i++] = 's';
            buf[i++] = ' ';
            if (fps >= 100)
                buf[i++] = (char)('0' + fps / 100 % 10);
            if (fps >= 10)
                buf[i++] = (char)('0' + fps / 10 % 10);
            buf[i++] = (char)('0' + fps % 10);
            buf[i] = '\0';
            bar_text(800 - 60, 2, buf, 0xFFFFFFu, 0x1E222Cu);
        }
    }

    ren_fc_flush();
    if (bench_on && bench_left) {
        char b[80];
        int i = 0;
        unsigned long r = pit_us_since_export(t0);
        unsigned long c = pit_us_since_export(tr);
        unsigned long p = pit_us_since_export(tc);

        b[i++] = 'B'; b[i++] = ' '; b[i++] = 'f'; b[i++] = '=';
        put_dec(b, &i, frame_no);
        b[i++] = ' '; b[i++] = 'R'; b[i++] = '=';
        put_dec(b, &i, r);
        b[i++] = ' '; b[i++] = 'C'; b[i++] = '=';
        put_dec(b, &i, c);
        b[i++] = ' '; b[i++] = 'P'; b[i++] = '=';
        put_dec(b, &i, p);
        b[i++] = ' '; b[i++] = 'T'; b[i++] = '=';
        put_dec(b, &i, r + c + p);
        b[i++] = '\r'; b[i++] = '\n'; b[i] = '\0';
        serial_write(b);
        bench_left--;
        if (bench_left == 0)
            bench_on = 0;
    }
    frame_no++;
    /* pace to 60 fps: yield-wait until the 16.6 ms frame budget is
       spent.  A tight spin would starve QEMU's main loop under TCG
       (the PIT clock freezes and the wait never ends), so each round
       gives the scheduler and the other processes a turn */
    {
        unsigned long elapsed;

        for (;;) {
            elapsed = pit_us_since_export(t0);
            if (elapsed >= 16600ul)
                break;
            proc_yield();
        }
        /* hand the MEASURED frame time to the APU: it renders exactly as
           many 44100 Hz samples as this frame consumed, so production
           tracks the DAC instead of slowly falling behind it */
        apu_tick_frame(elapsed);
        fc_fps_tick(elapsed);
    }
}

/* honest fps from accumulated PIT time.  The old version read the RTC
   every frame (~25 port I/O ops -> VM exits under TCG) and could spin
   ~244us in the RTC update-in-progress window once a second, nudging an
   already-heavy frame past the 16.6ms budget (the "sometimes drops a
   frame" symptom).  run_frame now hands us the measured frame time, so
   the fps window costs zero I/O.  Frames are paced to >=16.6ms, so a
   per-frame delta only wraps the 16-bit PIT below ~18fps; clamp those. */
static void fc_fps_tick(unsigned long frame_us) {
    static unsigned long win_us;
    static unsigned long win_frames;

    if (frame_us < 16600ul)
        frame_us = 16600ul;              /* PIT wrapped: frame was slower */
    win_us += frame_us;
    win_frames++;
    if (win_us >= 1000000ul) {           /* one second of frames elapsed */
        unsigned long f = win_frames * 1000000ul / win_us;

        fps_disp = f > 999 ? 999 : (unsigned int)f;
        win_us = 0;
        win_frames = 0;
        if (perf_frames) {
            char b[96];
            int i = 0;

            b[i++] = 'F'; b[i++] = 'C'; b[i++] = ' ';
            b[i++] = 'p'; b[i++] = 'e'; b[i++] = 'r'; b[i++] = 'f';
            b[i++] = ':';
            b[i++] = ' '; b[i++] = 'n'; b[i++] = '=';
            put_dec(b, &i, perf_frames);
            b[i++] = ' '; b[i++] = 'c'; b[i++] = 'p'; b[i++] = 'u'; b[i++] = '=';
            put_dec(b, &i, perf_cpu_us / perf_frames);
            b[i++] = ' '; b[i++] = 'p'; b[i++] = 'p'; b[i++] = 'u'; b[i++] = '=';
            put_dec(b, &i, perf_ppu_us / perf_frames);
            b[i++] = ' '; b[i++] = 'r'; b[i++] = 'n'; b[i++] = 'd'; b[i++] = '=';
            put_dec(b, &i, perf_render_us / perf_frames);
            b[i++] = '\r'; b[i++] = '\n'; b[i] = '\0';
            serial_write(b);
            perf_cpu_us = 0;
            perf_ppu_us = 0;
            perf_render_us = 0;
            perf_frames = 0;
        }
    }
}

/* same number the status bar shows; the APU serial line prints it so a
   slow emulator cannot hide behind the fixed 60-frame diagnostic window */
unsigned fc_fps_get(void) {
    return fps_disp;
}

/* unattended framebuffer capture: magic + raw BGRA surface over serial */
static void fc_dump_surface(void) {
    static const char magic[8] = { 'F', 'C', 'S', 'H', '0', '1' };
    const unsigned char *p = (const unsigned char *)fc_surface;
    unsigned long n;
    int i;

    for (i = 0; i < 6; i++)
        serial_putc(magic[i]);
    for (n = 0; n < 256UL * 240UL * 4UL; n++)
        serial_putc((char)p[n]);
}

void fc_enter(void) {
    if (!video_active() || video_width() != 800 ||
        video_height() != 600 || video_bpp() != 32) {
        serial_write("FC: no video (need 800x600x32)\r\n");
        return;
    }

    vga_snapshot();
    back = (unsigned int *)0x100000u;
    ren_fc_init();
    /* run_frame paces itself with pit_us_since_export, which only works
       while the PIT free-runs in mode 2 (54.9 ms wrap).  timer_init leaves
       it in 100 Hz mode 3 (10 ms wrap), so without this the 16.6 ms wait
       never completes and the first frame hangs forever when "fc" is
       started straight from the shell (mangoWin happened to set it up).
       fc_enter's exit path calls stats_shutdown() to give the channel
       back, so quitting the emulator leaves the 100 Hz tick alive. */
    stats_init();
    quit_flag = 0;
    override_on = 0;
    frame_no = 0;
    fps_disp = 0;
    bar_drawn = 0;                      /* force one bar repaint */
    bar_fps_last = 0xFFFFFFFFu;
    mode_hist_cnt = 0;
    last_mode = 0xFF;
    kbd_clear_state();

    reset_machine();
    serial_write("FC: start\r\n");

    while (!quit_flag) {
        int key = kbd_get_key();

        if (key == 0x1B)                 /* Esc: back to the shell */
            break;

        run_frame();
        fc_serial_poll();
        proc_yield();
        __asm__ volatile("pause");
    }
    apu_stream_stop();                   /* the auto-init DMA ring would
                                            otherwise loop the last 133 ms
                                            forever after we leave */
    vga_restore();
    stats_shutdown();                    /* give PIT ch0 back so the
                                            100 Hz tick / proc_tick resume */
    serial_write("FC: exit\r\n");
}