// kernel32/fc/fc_mem.c - NES CPU bus: RAM, PPU/APU/joypad registers, PRG-ROM.
// Cartridge assumed mapper 0 (NROM) with either 16 KB PRG (mirrored)
// or 32 KB PRG (direct) and 8 KB CHR. The iNES header travels inside
// fc_rom_data so runtime parsing is trivial.

#include "fc.h"

extern const unsigned char fc_rom_data[];
extern const int fc_rom_size;

/* PRG ROM base.  Non-static and readable from fc.h on purpose: every
   opcode/operand fetch comes from here ($8000+), so fc_rd8() serves it
   inline instead of paying a cross-TU call ~20k times per frame. */
const unsigned char *fc_prg;

/* ---- APU registers (no audio device: accept writes, read zeroes) ---- */
extern void apu_wr8(unsigned short a, unsigned char v);
extern unsigned char apu_rd8(unsigned short a);

/* ---- joypad ---- */
extern void fc_input_sample(void);      /* input.c fills fc.pad_latch */

static void pad_write(unsigned char v) {
    /* strobe=1: reload both shift registers and hold them frozen (every
       read repeats button A); strobe=0: free-run, reads shift one bit
       out per access (A,B,Select,Start,Up,Down,Left,Right, then 0s) */
    if (v & 1) {
        fc.pad_strobe = 1;
        fc.pad_shift[0] = fc.pad_latch[0];
        fc.pad_shift[1] = fc.pad_latch[1];
    } else {
        fc.pad_strobe = 0;
    }
}

static unsigned char pad_read(int port) {
    /* The controller shift register is reloaded on strobe=1 and then shifts
       out the controller bits in the order A,B,Select,Start,Up,Down,Left,Right.
       The 8-bit latch is kept in the same bit order as the raw NES controller,
       so the first read after strobe is high must expose bit7. */
    if (fc.pad_strobe) {
        fc.pad_shift[port] = (unsigned char)fc.pad_latch[port];
        return (unsigned char)((fc.pad_shift[port] >> 7) & 1u);
    }
    {
        unsigned char b = (unsigned char)((fc.pad_shift[port] >> 7) & 1u);

        fc.pad_shift[port] = (unsigned char)(fc.pad_shift[port] << 1);
        return b;
    }
}

/* ---- PPU register writes (CPU side) ---- */

/* debug trace of the last $2005/$2006/$2007 CPU writes, exposed to the
   serial debugger (reg 0 = $2005, 1 = $2006, 2 = $2007) */
static unsigned int ppu_wr_cnt[3];
static unsigned short ppu_wr_ring[3][16];

unsigned int fc_ppu_wr_count(unsigned int reg) {
    return ppu_wr_cnt[reg & 3u];
}

unsigned short fc_ppu_wr_lookup(unsigned int reg, unsigned int n) {
    return ppu_wr_ring[reg & 3u][n & 15u];
}

static void ppu_trace(unsigned int reg, unsigned short v) {
    unsigned int n = ppu_wr_cnt[reg]++;

    ppu_wr_ring[reg][n & 15u] = v;
}

/* ---- $2007 write recorder: (VRAM address, value) pairs, last 64 ---- */
static unsigned short pw_addr[64];
static unsigned char pw_val[64];
static unsigned int pw_cnt;

unsigned int fc_pw_count(void) {
    return pw_cnt;
}

/* read VRAM with proper mirroring (used by the per-dot pipeline) */
unsigned char fc_vram_read(unsigned short a) {
    unsigned int addr = a & 0x3FFFu;

    if (addr >= 0x3F00u) {             /* palette RAM */
        unsigned int idx = addr & 0x1Fu;
        if (idx >= 0x10u && (idx & 3u) == 0)
            idx -= 0x10u;              /* sprite palette color 0 mirrors bg */
        return (unsigned char)(fc.palette[idx] & 0x3Fu);
    }
    if (addr >= 0x2000u) {             /* nametables */
        unsigned int nt;
        if (fc.mirror_h)               /* vertical mirroring: A10 selects */
            nt = (unsigned int)((addr & 0x400u) ? 1 : 0);
        else                           /* horizontal mirroring: A11 selects */
            nt = (unsigned int)((addr & 0x800u) ? 1 : 0);
        return fc.vram[nt * 0x400u + (addr & 0x3FFu)];
    }
    /* CHR ROM */
    return fc.chr[addr & 0x1FFFu];
}

void fc_pw_lookup(unsigned int n, unsigned short *addr,
                  unsigned char *val) {
    unsigned int i = n & 63u;

    *addr = pw_addr[i];
    *val = pw_val[i];
}

static void ppu_wr8(unsigned short a, unsigned char v) {
    fc_circuit_bus_write(v);           /* every PPU write drives the bus */
    switch (a & 7) {
    case 0x00: fc.ppu_ctrl = v; break;
    case 0x01: fc.ppu_mask = v; break;
    case 0x02: break;                  /* $2002: status, read-only */
    case 0x03: {
        /* $2003 write during rendering corrupts OAM DRAM: the row
           selector changes mid-access without precharge, copying the
           previous row's data to the new row (circuit-level bug). */
        if (fc.eval_state >= 1 && fc.eval_state <= 3)
            fc_circuit_oam_corrupt(fc.oam_addr, v);
        fc.oam_addr = v;
        fc_circuit_bus_write(v);
        break;
    }
    case 0x04: {
        fc_oam_write_data(v);
        break;
    }
    case 0x05: /* $2005 scroll: first write X, second Y */
        if (!fc.ppu_addr_latch) {
            fc.ppu_scroll_x = v;
            if (fc.scroll_top_flag)
                fc.scroll_top_x = v;
            fc.ppu_addr_latch = 1;
        } else {
            fc.ppu_scroll_y = v;
            if (fc.scroll_top_flag) {
                fc.scroll_top_y = v;
                fc.scroll_top_flag = 0;
            }
            fc.ppu_addr_latch = 0;
        }
        ppu_trace(0, v);
        break;
    case 0x06: /* $2006 address: first write high byte, second low */
        if (!fc.ppu_addr_latch) {
            fc.ppu_addr = (unsigned int)(v & 0x3F) << 8;
            fc.ppu_addr_latch = 1;
        } else {
            fc.ppu_addr |= v;
            fc.ppu_addr_latch = 0;
        }
        ppu_trace(1, (unsigned short)(fc.ppu_addr & 0x3FFFu));
        break;
    case 0x07: {                       /* $2007 data: write into VRAM */
        unsigned int a = fc.ppu_addr & 0x3FFFu;

        pw_addr[pw_cnt & 63u] = (unsigned short)a;
        pw_val[pw_cnt & 63u] = v;
        pw_cnt++;
        ppu_trace(2, (unsigned short)a);
        if (a >= 0x3F00u) {            /* palette RAM (0x3F00-0x3FFF) */
            unsigned int idx = a & 0x1Fu;
            if (idx >= 0x10u && (idx & 3u) == 0)
                idx -= 0x10u;          /* sprite palette color 0 mirrors bg */
            fc.palette[idx] = v & 0x3F;
        } else if (a >= 0x2000u) {     /* nametables */
            unsigned int nt = (a >> 10) & 3u;

            if (fc.mirror_h)           /* vertical mirroring (horizontal arrangement): A10 selects */
                nt = (unsigned int)((a & 0x400u) ? 1 : 0);
            else                       /* horizontal mirroring (vertical arrangement): A11 selects */
                nt = (unsigned int)((a & 0x800u) ? 1 : 0);
            fc.vram[nt * 0x400u + (a & 0x3FFu)] = v;
        } else {
            /* CHR ROM: writes go nowhere */
        }
        fc.ppu_addr = (fc.ppu_addr + (unsigned int)(fc.ppu_ctrl & 4 ? 32 : 1)) & 0x3FFFu;
        break;
    }
    default: break;
    }
}

/* forward declaration */
void fc_eval_advance_cycle(void);

/* ---- PPU register reads (CPU side) ---- */
static unsigned char ppu_rd8(unsigned short a) {
    switch (a & 7) {
    case 0x02: {                       /* $2002: vblank flag + clear on read */
        /* bits 5-6 (sprite-0 hit, overflow) are preserved: only bit7
           clears on read.  Bits 0-4 come from the PPU data bus
           (open bus: last value written to any PPU register). */
        unsigned char s = (unsigned char)(
            (fc.ppu_status & 0xE0u) |
            (fc_circuit_open_bus() & 0x1Fu));
        fc.ppu_status = (unsigned char)(fc.ppu_status & ~0x80u);
        fc.ppu_addr_latch = 0;
        fc_circuit_bus_write(s);
        return s;
    }
    case 0x04: {
        return fc_oam_read_data();
    }
    case 0x07: {                       /* $2007: buffered read */
        unsigned int a = fc.ppu_addr & 0x3FFFu;
        unsigned char v = fc.ppu_read_buf;

        if (a >= 0x3F00u) {            /* palette */
            unsigned int idx = a & 0x1Fu;

            if (idx >= 0x10u && (idx & 3u) == 0)
                idx -= 0x10u;
            v = fc.palette[idx];
            if (fc.ppu_mask & 0x01u)   /* greyscale: lower 4 bits read 0 */
                v = (unsigned char)(v & 0x30u);
            /* the buffer is still updated, from the nametable address
               $2F00 | (v & $FF): the value a subsequent nametable read
               would have returned (AccuracyCoin "PPU Read Buffer") */
            fc.ppu_read_buf = fc.vram[0xF00u + (a & 0xFFu)];
        } else if (a >= 0x2000u) {     /* nametables */
            unsigned int nt = (a >> 10) & 3u;

            if (fc.mirror_h)           /* vertical mirroring: A10 selects */
                nt = (unsigned int)((a & 0x800u) ? 1 : 0);
            else                       /* horizontal mirroring: A11 selects */
                nt = (unsigned int)((a & 0x1000u) ? 1 : 0);
            fc.ppu_read_buf = fc.vram[nt * 0x400u + (a & 0x3FFu)];
        } else {
            /* CHR ROM read: the 2C02 latches the pattern byte into the
               read buffer, just like nametable reads */
            fc.ppu_read_buf = fc.chr[a & 0x1FFFu];
        }
        fc.ppu_addr = (fc.ppu_addr + (unsigned int)(fc.ppu_ctrl & 4 ? 32 : 1)) & 0x3FFFu;
        return v;
    }
    default:
        return 0;
    }
}

/* ---- master bus (out-of-line half; fc_rd8/fc_wr8 in fc.h serve the RAM
   mirror inline and only call in here for everything else) ---- */
unsigned char fc_rd8_slow(unsigned short a) {
    if (a < 0x2000u)
        return fc.ram[a & 0x7FFu];
    if (a < 0x4000u)
        return ppu_rd8((unsigned short)(a & 0x2007u));
    if (a == 0x4016u)
        return (unsigned char)(pad_read(0) | 0x40);
    if (a == 0x4017u)
        return (unsigned char)(pad_read(1) | 0x40);
    if (a >= 0x4000u && a < 0x4020u)
        return apu_rd8(a);
    if (a >= 0x8000u)
        return fc_prg[(a - 0x8000u) & (fc.prg_size - 1u)];
    return 0;
}

void fc_wr8_slow(unsigned short a, unsigned char v) {
    if (a < 0x2000u) {
        fc.ram[a & 0x7FFu] = v;
        return;
    }
    if (a < 0x4000u) {
        ppu_wr8((unsigned short)(a & 0x2007u), v);
        return;
    }
    if (a == 0x4014u) {                /* sprite DMA from RAM page */
        fc_oam_dma(v);
        return;
    }
    if (a == 0x4016u) {
        pad_write(v);
        return;
    }
    if (a >= 0x4000u && a < 0x4020u) {
        apu_wr8(a, v);
        return;
    }
    /* writes below $8000 with no device go nowhere */
}

/* parse the embedded iNES image once at reset */
void fc_mem_init(void) {
    const unsigned char *d = fc_rom_data;
    unsigned int prg16, chr8;
    unsigned int header = 16 + ((d[6] & 0x04u) ? 512u : 0u);

    if (fc_rom_size < 16 || d[0] != 'N' || d[1] != 'E' || d[2] != 'S') {
        fc_prg = fc_rom_data;
        return;
    }
    prg16 = d[4];
    chr8 = d[5];
    fc.mirror_h = (unsigned char)(d[6] & 1);
    fc_prg = d + header;
    if (prg16 == 1) {
        /* 16 KB PRG: mirror into both halves */
        fc.prg_size = 0x4000u;
        fc.chr_size = chr8 * 0x2000u;
    } else {
        fc.prg_size = prg16 * 0x4000u;
        fc.chr_size = chr8 * 0x2000u;
    }
    if (fc.prg_size > 0x8000u)
        fc.prg_size = 0x8000u;
    if (fc.chr_size > 0x2000u)
        fc.chr_size = 0x2000u;

    /* CHR ROM lives after the PRG; SMB1 has 8 KB CHR ROM (no RAM bank) */
    if (fc.chr_size > 0) {
        const unsigned char *chr = d + header + fc.prg_size;

        for (prg16 = 0; prg16 < fc.chr_size; prg16++)
            fc.chr[prg16] = chr[prg16];
    }
}

/* advance sprite evaluation FSM by one cycle for $2004 stress test.
   The 2C02 does not mutate the secondary OAM while the CPU reads it;
   the read stream simply walks the already-built secondary OAM buffer.
   Keeping the pointer logic exact avoids the OAM corruption that causes
   AccuracyCoin to hang in the $2004 stress test. */
void fc_eval_advance_cycle(void) {
    if (fc.eval_state != 2)
        return;

    fc.eval_m++;
    if (fc.eval_m >= 4u) {
        fc.eval_m = 0u;
        fc.eval_n++;
    }

    if (fc.eval_n >= 64u) {
        fc.eval_n = 0u;
        fc.eval_m = 0u;
    }
}

void fc_mem_reset(void) {
    int i;

    for (i = 0; i < 0x800; i++)
        fc.ram[i] = 0;
    for (i = 0; i < 0x2000; i++)
        fc.vram[i] = 0;
    for (i = 0; i < 0x100; i++)
        fc.oam[i] = 0;
    for (i = 0; i < 0x20; i++)
        fc.palette[i] = 0;
    fc.ppu_ctrl = 0;
    fc.ppu_mask = 0;
    fc.ppu_status = 0;
    fc.oam_addr = 0;
    fc.ppu_scroll_x = 0;
    fc.ppu_scroll_y = 0;
    fc.ppu_addr = 0;
    fc.ppu_addr_latch = 0;
    fc.ppu_read_buf = 0;
    fc.pad_latch[0] = 0;
    fc.pad_latch[1] = 0;
    fc.pad_shift[0] = 0;
    fc.pad_shift[1] = 0;
    fc.pad_strobe = 0;
    fc.sprite0_hit = 0;
    fc.sprite0_hit_row = 0;

    for (i = 0; i < 32; i++)
        fc.sec_oam[i] = 0xFF;
    fc.sec_oam_idx = 0;
    fc.eval_state = 0;
    fc.eval_sprite = 0;
    fc.eval_byte = 0;
    fc.eval_virtual_cycle = 0;
    fc.eval_n = 0;
    fc.eval_m = 0;
    fc.sec_oam_count = 0;
    fc.overflow_check = 0;
    fc.eval_odd_cycle = 0;
    fc.eval_read = 0;
    fc.eval_range = 0;
    fc.eval_done = 0;
    fc.sec_oam_pos = 0;
    fc_circuit_reset();
}