// kernel32/fc/fc.h - NES (FC) emulator core for MangoOS
//
// Target: mapper 0 (NROM) cartridges such as Super Mario Bros.
// Timing model: frame-synchronous. Every 1/60 s the emulator renders the
// 256x240 picture from the current PPU state, then runs the 6502 for one
// frame (29780 cycles) with an NMI at the start of the frame. This is
// coarse compared to per-scanline emulation, but NMI-driven games (SMB1)
// only touch PPU registers during vblank, so writes always apply to the
// next rendered frame and the game behaves correctly.

#include <stdint.h>

#include "../video.h"
#include "../vga.h"
#include "../timer.h"
#include "../serial.h"

#ifndef FC_H
#define FC_H

#define FC_CPU_CYCLES_PER_FRAME  29780u   /* NTSC: 262 lines, 3 PPU cyc/CPU cyc */
#define FC_FB_W  256
#define FC_FB_H  240

/* ---- shared machine state (BSS, kernel links below 640 KB) ---- */
struct fc_state {
    /* CPU (2A03) */
    unsigned short pc;
    unsigned char a, x, y, sp, p;
    unsigned int   cycles;          /* cycles consumed in the current frame */
    unsigned char  nmi_pending;     /* set by the frame driver, consumed by CPU */
    unsigned char  irq_pending;     /* level-sensitive IRQ line (gated by I) */
    unsigned char  running;
    unsigned int   nmi_count;       /* NMIs serviced since power-on */

    /* PPU registers as seen by the CPU */
    unsigned char ppu_ctrl;         /* $2000 */
    unsigned char ppu_mask;         /* $2001 */
    unsigned char ppu_status;       /* $2002 (vblank flag bit7) */
    unsigned char oam_addr;         /* $2003 */
    unsigned char ppu_scroll_x, ppu_scroll_y;   /* $2005 */
    unsigned char scroll_top_x, scroll_top_y;   /* first $2005 pair of the
                                                   frame: the pre-sprite-0-hit
                                                   scroll (split-scroll games
                                                   keep the top rows fixed) */
    unsigned char scroll_top_flag;  /* awaiting the first $2005 pair */
    unsigned int  ppu_addr;         /* $2006 latch */
    unsigned char ppu_addr_latch;   /* shared $2005/$2006 write toggle */
    unsigned char ppu_read_buf;     /* $2007 read-ahead */

    /* memory */
    unsigned char ram[0x800];       /* CPU RAM, mirrored $800-$1FFF */
    unsigned char vram[0x2000];     /* 2 x 2 KB nametables (mirrored) */
    unsigned char chr[0x2000];      /* 8 KB pattern tables */
    unsigned char oam[0x100];       /* sprite RAM */
    unsigned char palette[0x20];    /* 2C02 palette RAM */

    /* cart */
    unsigned int  prg_size;         /* bytes ($8000-$FFFF window) */
    unsigned int  chr_size;
    unsigned char mirror_h;         /* iNES bit0: 1 = NT1 via coarse-X overflow */

    /* input */
    unsigned char pad_latch[2];     /* latched 8-bit NES controller state */
    unsigned char pad_shift[2];     /* shifted-out state */
    unsigned char pad_strobe;       /* $4016 bit0 held high: register frozen */
    unsigned char sprite0_hit;      /* this frame's sprite-0 hit (scanline) */
    unsigned int  sprite0_hit_row;  /* scanline the hit was detected at */

    /* sprite evaluation state for $2004 reads during rendering */
    unsigned char sec_oam[32];      /* secondary OAM (8 sprites × 4 bytes) */
    unsigned int  sec_oam_idx;      /* current secondary OAM index during eval */
    unsigned char eval_state;       /* 0=idle, 1=clear(1-64), 2=eval(65-256), 3=fetch(257-320) */
    unsigned int  eval_sprite;      /* primary OAM sprite index being evaluated */
    unsigned char eval_byte;        /* byte within sprite (0-3) */
    unsigned int  eval_virtual_cycle; /* virtual cycle counter for $2004 stress test */
    unsigned int  eval_n;           /* primary OAM index n during evaluation */
    unsigned char eval_m;           /* byte within sprite m during overflow check */
    unsigned char sec_oam_count;    /* number of sprites copied to secondary OAM */
    unsigned char overflow_check;   /* overflow detection phase */
    unsigned int  eval_odd_cycle;   /* odd/even cycle tracker for eval phase */
    unsigned char eval_read;        /* last value read from primary OAM */
    unsigned char eval_oam_base;    /* OAMADDR captured when the scanline's
                                       sprite evaluation began: the 2C02
                                       reads from HERE, so a misaligned
                                       OAMADDR makes every sprite field
                                       land on the wrong byte */
    unsigned char eval_range;       /* current sprite Y in range */
    unsigned char eval_done;        /* all 64 sprites evaluated */
    unsigned char sec_oam_pos;      /* byte write pos in secondary OAM (0-31) */

    unsigned int ppu_scanline;      // 当前 scanline (0-261)
    unsigned int ppu_dot;           // 当前 dot (0-341)
    unsigned int frame_cycles;      // 本帧已执行的 CPU cycle 数
    int          rendering_enabled; // $2001 bit3|bit4

    /* loopy scroll registers (pipeline.c) */
    unsigned short ppu_latch_t;     /* internal t register ($2005/$2006 target) */
    unsigned char  ppu_latch_x;     /* fine X scroll (3 bits) */
    unsigned char  frame_odd;       /* odd/even frame flag (dot skip) */

    /* frame output is written straight into the GUI backbuffer by ppu.c */
};

extern struct fc_state fc;

/* keyboard input helpers used by the FC controller sampler */
int kbd_scan_down(unsigned char sc);
void kbd_copy_state(unsigned char *dst);
void kbd_clear_state(void);
int kbd_get_key(void);

extern const unsigned char fc_rom_data[];
extern const int fc_rom_size;

/* mem.c: the bus itself lives out of line, but the 6502 does 2-4 accesses
   per instruction and under QEMU TCG every cross-TU call is expensive.
   The two hot cases are served inline here: the RAM mirror (zero page,
   stack, game variables) and PRG ROM ($8000+, which is where EVERY opcode
   and operand byte is fetched from).  Everything else falls back to the
   out-of-line bus, which keeps its own branches, so behaviour is identical
   no matter which one a caller reaches. */
unsigned char fc_rd8_slow(unsigned short a);
void fc_wr8_slow(unsigned short a, unsigned char v);
extern const unsigned char *fc_prg;     /* PRG ROM base, set at reset */

static inline unsigned char fc_rd8(unsigned short a) {
    if (a < 0x2000u)
        return fc.ram[a & 0x7FFu];
    if (a >= 0x8000u)
        return fc_prg[(a - 0x8000u) & (fc.prg_size - 1u)];
    return fc_rd8_slow(a);
}

static inline void fc_wr8(unsigned short a, unsigned char v) {
    if (a < 0x2000u) {
        fc.ram[a & 0x7FFu] = v;
        return;
    }
    fc_wr8_slow(a, v);
}

/* oam.c */
void fc_oam_reset(void);
void fc_oam_dump_log(void);      /* last 64 $2004/$2003 accesses (fcoam) */
void fc_oam_begin_render(void);
void fc_oam_end_render(void);
unsigned char fc_oam_read_data(void);
void fc_oam_write_data(unsigned char v);
void fc_oam_dma(unsigned char page);
/* expose eval advance for $2004 stress-test progression */
void fc_eval_advance_cycle(void);

/* cpu.c */
void fc_cpu_reset(void);
unsigned int fc_cpu_step(void);         /* execute one 6502 instruction */
void fc_cpu_run(unsigned int cycles);   /* run up to N cycles; honors nmi_pending */
void fc_cpu_nmi(void);                  /* set pending NMI */
int fc_cpu_take_nmi(void);              /* service pending NMI (per-instr loop) */
int fc_cpu_take_irq(void);              /* service pending IRQ, gated by I */

/* ppu.c */
void fc_ppu_reset(void);
void fc_ppu_advance(unsigned int cpu_cycles);
void fc_ppu_render(void);               /* render current state -> backbuffer */

/* mem.c */
void fc_mem_init(void);
void fc_mem_reset(void);

/* input.c */
void fc_input_sample(void);             /* keyboard -> pad_latch */
void fc_input_override(unsigned int mask);

/* fc.c */
void fc_enter(void);                    /* fullscreen game mode */
void fc_self_test(void);                /* CPU opcode self-test over serial */
unsigned fc_fps_get(void);              /* measured fps (status bar value) */

/* apu.c */
void apu_frame_quarter(void);
void apu_frame_half(void);
int apu_render_samples(int16_t *buf, int samples);
void apu_tick_frame(unsigned long frame_us);
void apu_stream_stop(void);
void apu_poll(void);
void apu_register(void);

/* tiles.c: two CHR reads.  Inlined on purpose -- the background renderer
   calls this once per pixel (61k calls a frame) and under TCG the
   cross-TU call cost more than the work it does. */
static inline void fc_tiles_row(unsigned int tile, unsigned int row,
                                unsigned char *lo, unsigned char *hi) {
    const unsigned char *base = fc.chr + (tile & 511u) * 16u;

    *lo = base[row & 7u];
    *hi = base[(row & 7u) + 8u];
}
unsigned short fc_tiles_pack(unsigned int tile, unsigned int row,
                             int flip_h, int flip_v);

/* mem.c ($2007 write recorder) */
unsigned int fc_pw_count(void);
void fc_pw_lookup(unsigned int n, unsigned short *addr,
                  unsigned char *val);

/* sprite.c */
int fc_sprite_pixels(int slot, int y, int spr16,
                     unsigned short *pack, unsigned int *sx);
int fc_sprite_eval(unsigned int y, int spr16, unsigned char *sel);

/* circuit.c */
unsigned char fc_circuit_open_bus(void);
void fc_circuit_bus_write(unsigned char v);
void fc_circuit_oam_corrupt(unsigned char old_addr, unsigned char new_addr);
unsigned char fc_circuit_oam_attr_mask(unsigned char v);
void fc_circuit_reset(void);

/* pipeline.c */
void fc_pipeline_tick(void);
void fc_pipeline_reset(void);
extern unsigned char fc_pipe_last_bg_pix;
extern unsigned char fc_pipe_last_spr_pix;
extern int fc_pipe_sprite0_hit_dot;
void fc_eval_dot_public(unsigned int scanline, unsigned int dot);

/* mem.c: VRAM read with mirroring (used by the per-dot pipeline) */
unsigned char fc_vram_read(unsigned short addr);

/* video / console / timing / serial helpers reused from the kernel stack.
   Declared in the headers below rather than ad-hoc here. */

#endif /* FC_H */