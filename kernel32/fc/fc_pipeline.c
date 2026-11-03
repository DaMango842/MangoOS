// kernel32/fc/fc_pipeline.c - Per-dot 2C02 PPU pipeline state machine.
//
// Tracks the exact internal state of the PPU at every dot so that
// register reads ($2002 / $2004 / $2007) observe the same values real
// hardware would produce at that instant.  Visual output still comes
// from the batch renderer in fc_ppu_render(); this module supplements
// it with cycle-accurate internal state.
//
//   Background fetch sequence (repeats every 8 dots):
//     dots 1-2   NT byte
//     dots 3-4   AT byte
//     dots 5-6   pattern plane 0
//     dots 7-8   pattern plane 1  -> shift registers reload at end
//
//   Loopy register updates:
//     hori(v) += 1     at dots 8,16,...,256, 328, 336
//     vert(v) += 1     at dot 256
//     hori(t) -> v     at dot 257
//     vert(t) -> v     at dots 280-304 of pre-render line
//
//   VBlank flag:  set at (241,1)  cleared at (261,1)
//   NMI:          asserted on VBlank rising edge when $2000 bit 7 = 1
//
//   Sprite-0 hit: raised when an opaque sprite-0 pixel overlaps an
//                 opaque background pixel (dots 2-255, both enabled).
//
// STATUS: HALF-WIRED.  Only fc_pipeline_reset() is reachable — fc_ppu.c
// calls it per frame to zero the dot-driven evaluation state.  The rest of
// the module, above all fc_pipeline_tick() itself, is unreachable: it is
// never called from anywhere.
//
// The reason is that its loopy register updates write fc.ppu_addr
// directly, which corrupts the batch renderer's interpretation of the
// scroll registers (see fc_ppu.c:237), so driving it per dot degrades the
// picture instead of refining it.  Until that interaction is resolved,
// keep this module reference-only: it still ships inside the kernel image
// (ar + ld pull whole objects, and fc_ppu.o references fc_pipeline_reset),
// so it costs code size but must not be stepped.

#include "fc.h"

extern void fc_cpu_nmi(void);

/* ---- background pipeline ---- */

static unsigned short p_bg_lo;         /* pattern plane 0 shifter */
static unsigned short p_bg_hi;         /* pattern plane 1 shifter */
static unsigned short p_at_lo;         /* attribute bit 0 shifter */
static unsigned short p_at_hi;         /* attribute bit 1 shifter */
static unsigned char  p_nt_byte;       /* nametable latch */
static unsigned char  p_at_byte;       /* attribute latch */
static unsigned char  p_pt_lo;         /* pattern plane 0 latch */
static unsigned char  p_pt_hi;         /* pattern plane 1 latch */

/* ---- sprite output units (loaded during dots 257-320) ---- */

typedef struct {
    unsigned char lo;                  /* remaining pattern bits, plane 0 */
    unsigned char hi;                  /* remaining pattern bits, plane 1 */
    unsigned char attr;                /* palette 2b | priority | flip H */
    unsigned char x;                   /* coarse X counter (counts down) */
} spr_unit_t;

static spr_unit_t p_spr[8];

/* ---- derived state exposed to the rest of the emulator ---- */

unsigned char fc_pipe_last_bg_pix;     /* last opaque bg pixel (0-3) */
unsigned char fc_pipe_last_spr_pix;    /* last sprite pixel at same dot */
int fc_pipe_sprite0_hit_dot;           /* dot of the sprite-0 hit, or -1 */

/* ---- helpers ---- */

#define RENDERING_LINE(sl) ((sl) < 240u || (sl) == 261u)
#define VISIBLE_LINE(sl)   ((sl) < 240u)

static int rendering_on(void) {
    return (fc.ppu_mask & 0x18u) != 0;
}

/* loopy hori(v): increment coarse X, rolling into nametable X */
static void loopy_hori_inc(void) {
    unsigned int v = fc.ppu_addr;
    if ((v & 0x001Fu) == 31u) {        /* coarse X == 31: wrap */
        v &= ~0x001Fu;                 /* coarse X = 0 */
        v ^=  0x0400u;                 /* flip horizontal NT bit */
    } else {
        v += 1;                        /* coarse X += 1 */
    }
    fc.ppu_addr = (unsigned short)v;
}

/* loopy vert(v): increment fine Y, rolling into coarse Y, then NT Y */
static void loopy_vert_inc(void) {
    unsigned int v = fc.ppu_addr;
    if ((v & 0x7000u) != 0x7000u) {
        v += 0x1000u;                  /* fine Y += 1 */
    } else {
        v &= ~0x7000u;                 /* fine Y = 0 */
        unsigned int cy = (v & 0x03E0u) >> 5;
        if (cy == 29u) {
            cy = 0u;                   /* coarse Y wraps */
            v ^= 0x0800u;              /* flip vertical NT bit */
        } else if (cy == 31u) {
            cy = 0u;                   /* attribute garbage range */
        } else {
            cy += 1u;
        }
        v = (v & ~0x03E0u) | (cy << 5);
    }
    fc.ppu_addr = (unsigned short)v;
}

/* copy hori(t) into hori(v) */
static void loopy_hori_copy(void) {
    fc.ppu_addr = (unsigned short)(
        (fc.ppu_addr & ~0x041Fu) |
        (fc.ppu_latch_t &  0x041Fu));
}

/* copy vert(t) into vert(v) */
static void loopy_vert_copy(void) {
    fc.ppu_addr = (unsigned short)(
        (fc.ppu_addr & ~0x7BE0u) |
        (fc.ppu_latch_t &  0x7BE0u));
}

/* ---- background fetch (called at even dots within groups of 8) ---- */

static void bg_fetch(unsigned int scanline, unsigned int dot) {
    /* dot phase within the 8-dot group (0..7) */
    unsigned int ph = (dot - 1u) & 7u;
    unsigned int v = fc.ppu_addr;

    switch (ph) {
    case 0: case 1: {
        /* NT fetch */
        unsigned int nt_base = 0x2000u + (v & 0x0C00u);
        unsigned int cy = (v >> 5) & 0x1Fu;
        unsigned int cx =  v        & 0x1Fu;
        unsigned int addr = nt_base + (cy << 5) + cx;
        p_nt_byte = fc_vram_read((unsigned short)addr);
        break;
    }
    case 2: case 3: {
        /* AT fetch */
        unsigned int nt_base = 0x2000u + (v & 0x0C00u);
        unsigned int cy = (v >> 5) & 0x1Fu;
        unsigned int cx =  v        & 0x1Fu;
        unsigned int addr = nt_base + 0x3C0u +
                            ((cy >> 2) << 3) + (cx >> 2);
        unsigned int shift = ((cy & 2u) << 1) | (cx & 2u);
        p_at_byte = (unsigned char)(
            (fc_vram_read((unsigned short)addr) >> shift) & 3u);
        break;
    }
    case 4: case 5: {
        /* pattern plane 0 */
        unsigned int bg_pt = (fc.ppu_ctrl & 0x10u) ? 0x1000u : 0u;
        unsigned int fine_y = (v >> 12) & 7u;
        unsigned int addr = bg_pt + ((unsigned int)p_nt_byte << 4) + fine_y;
        p_pt_lo = fc_vram_read((unsigned short)addr);
        break;
    }
    case 6: case 7: {
        /* pattern plane 1; reload shifters at end of dot 8 */
        unsigned int bg_pt = (fc.ppu_ctrl & 0x10u) ? 0x1000u : 0u;
        unsigned int fine_y = (v >> 12) & 7u;
        unsigned int addr = bg_pt + ((unsigned int)p_nt_byte << 4) + fine_y + 8u;
        p_pt_hi = fc_vram_read((unsigned short)addr);

        if (ph == 7u) {
            /* reload shift registers: new tile enters low byte */
            p_bg_lo = (unsigned short)((p_bg_lo & 0xFF00u) | p_pt_lo);
            p_bg_hi = (unsigned short)((p_bg_hi & 0xFF00u) | p_pt_hi);
            p_at_lo = (unsigned short)((p_at_lo & 0xFF00u) |
                                       ((p_at_byte & 1u) ? 0xFFu : 0x00u));
            p_at_hi = (unsigned short)((p_at_hi & 0xFF00u) |
                                       ((p_at_byte & 2u) ? 0xFFu : 0x00u));
        }
        break;
    }
    }
}

/* ---- sprite fetch (dots 257-320, 8 sprites × 8 dots) ---- */

static void spr_fetch(unsigned int scanline, unsigned int dot) {
    unsigned int unit = (dot - 257u) >> 3;
    unsigned int ph   = (dot - 257u) & 7u;
    unsigned int spr_ht = (fc.ppu_ctrl & 0x20u) ? 16u : 8u;
    unsigned int y, row, tile, attr, addr;
    unsigned char lo = 0, hi = 0;

    /* secondary OAM holds up to 8 entries × 4 bytes */
    if (unit < fc.sec_oam_count && (unit * 4u + 3u) < 32u) {
        const unsigned char *s = &fc.sec_oam[unit * 4u];
        y    = s[0];
        tile = s[1];
        attr = s[2];

        row = scanline - y;
        if (row >= spr_ht) row = 0;    /* out of range: blank */
        if (attr & 0x80u)              /* vertical flip */
            row = spr_ht - 1u - row;

        if (spr_ht == 16u) {
            /* 8x16 sprites: bank select in tile bit 0 */
            unsigned int bank = (tile & 1u) ? 0x1000u : 0u;
            unsigned int idx  = tile & 0xFEu;
            if (row >= 8u) {
                idx += 1u;
                row -= 8u;
            }
            addr = bank + (idx << 4) + row;
        } else {
            unsigned int bg_pt = (fc.ppu_ctrl & 0x08u) ? 0x1000u : 0u;
            addr = bg_pt + (tile << 4) + row;
        }

        lo = fc_vram_read((unsigned short)addr);
        hi = fc_vram_read((unsigned short)(addr + 8u));

        if (attr & 0x40u) {            /* horizontal flip */
            unsigned char rlo = 0, rhi = 0, b;
            for (b = 0; b < 8; b++) {
                rlo = (unsigned char)(rlo << 1) | (unsigned char)((lo >> b) & 1u);
                rhi = (unsigned char)(rhi << 1) | (unsigned char)((hi >> b) & 1u);
            }
            lo = rlo;
            hi = rhi;
        }
    }

    if (ph == 7u) {
        /* load the sprite output unit */
        p_spr[unit].lo   = lo;
        p_spr[unit].hi   = hi;
        p_spr[unit].attr = (unit < fc.sec_oam_count && (unit*4u+2u) < 32u)
                          ? fc.sec_oam[unit * 4u + 2u] : 0u;
        p_spr[unit].x    = (unit < fc.sec_oam_count && (unit*4u+0u) < 32u)
                          ? fc.sec_oam[unit * 4u + 3u] : 0xFFu;
    }
}

/* ---- pixel output for one dot (dots 1-256 of visible lines) ---- */

static void pixel_out(unsigned int scanline, unsigned int dot) {
    unsigned int x = dot - 1u;
    unsigned int fx = fc.ppu_latch_x;          /* fine X */
    unsigned int bg_bit_pos = 15u - fx;
    unsigned int bg_lo = (p_bg_lo >> bg_bit_pos) & 1u;
    unsigned int bg_hi = (p_bg_hi >> bg_bit_pos) & 1u;
    unsigned int bg_idx = (bg_hi << 1) | bg_lo;
    unsigned int at_bit_pos = 15u - fx;
    unsigned int at_lo = (p_at_lo >> at_bit_pos) & 1u;
    unsigned int at_hi = (p_at_hi >> at_bit_pos) & 1u;
    unsigned int pal_row = (at_hi << 1) | at_lo;
    int left8_mask = (x < 8u) &&
                     ((fc.ppu_mask & 0x06u) != 0x06u);
    unsigned int spr_idx = 0, spr_pal = 0, spr_prio = 0;
    int spr_is_zero = 0;
    int i;

    /* left-column masking */
    if (left8_mask) {
        if (!(fc.ppu_mask & 0x02u)) bg_idx = 0;
        if (!(fc.ppu_mask & 0x04u)) { spr_idx = 0; goto bg_only; }
    }

    /* find first active sprite pixel (lowest OAM index wins) */
    for (i = 0; i < 8; i++) {
        if (p_spr[i].x > 0u) continue;     /* not yet reached screen X */
        unsigned int sidx =
            (((p_spr[i].hi >> 7) & 1u) << 1) | ((p_spr[i].lo >> 7) & 1u);
        if (sidx == 0) continue;           /* transparent pixel */
        spr_idx  = sidx;
        spr_pal  = (p_spr[i].attr & 3u);
        spr_prio = (p_spr[i].attr & 0x20u) ? 1u : 0u;
        spr_is_zero = (i == 0 &&
                       (fc.sec_oam[0] != 0xFFu));
        break;                             /* first non-transparent wins */
    }

bg_only:
    /* sprite-0 hit detection */
    if (spr_is_zero && spr_idx != 0 && bg_idx != 0 &&
        !(fc.ppu_status & 0x40u) && !left8_mask &&
        (fc.ppu_mask & 0x18u) == 0x18u &&
        dot >= 2u && dot <= 255u &&
        !(scanline == 261u)) {
        fc.ppu_status |= 0x40u;
        fc.sprite0_hit = 1;
        fc.sprite0_hit_row = scanline;
        fc_pipe_sprite0_hit_dot = (int)dot;
    }

    fc_pipe_last_bg_pix  = (unsigned char)bg_idx;
    fc_pipe_last_spr_pix = (unsigned char)spr_idx;

    /* shift registers advance every dot */
    p_bg_lo <<= 1;
    p_bg_hi <<= 1;
    p_at_lo <<= 1;
    p_at_hi <<= 1;

    /* decrement sprite X counters */
    for (i = 0; i < 8; i++) {
        if (p_spr[i].x > 0u && p_spr[i].x != 0xFFu)
            p_spr[i].x--;
        else if (p_spr[i].x == 0u) {
            /* active sprite: shift pattern bits */
            p_spr[i].lo = (unsigned char)(p_spr[i].lo << 1);
            p_spr[i].hi = (unsigned char)(p_spr[i].hi << 1);
        }
    }
}

/* ---- main per-dot tick ---- */

void fc_pipeline_tick(void) {
    unsigned int sl = fc.ppu_scanline;
    unsigned int dt = fc.ppu_dot + 1u;     /* 1-based dot within scanline */

    /* --- advance dot counter --- */
    fc.ppu_dot++;
    if (fc.ppu_dot >= 341u) {
        fc.ppu_dot = 0;
        fc.ppu_scanline++;
        if (fc.ppu_scanline >= 262u)
            fc.ppu_scanline = 0;
        /* skipped dot 340 on odd frames when rendering (cycle jitter) */
        if (fc.ppu_scanline == 0u && rendering_on() && (fc.frame_odd & 1u))
            fc.ppu_dot = 0;                /* hardware skips dot 339→0 */
    }

    /* --- VBlank flag: batch model (set in fc_ppu_render at end,
           cleared at start).  Per-dot VBlank breaks the batch NMI
           injection in fc_run_frame() which fires at scanline 0 when
           the per-dot flag has already been cleared at (261,1).
           The pipeline still provides per-dot sprite-0 hit, loopy
           register updates and BG fetch state. --- */

    /* --- rendering lines --- */
    if (!RENDERING_LINE(sl) || !rendering_on())
        return;

    /* background fetch + shift register update */
    if (dt <= 256u || (dt >= 321u && dt <= 336u))
        bg_fetch(sl, dt);

    /* sprite evaluation: delegate to existing FSM */
    if (dt <= 256u || (dt >= 257u && dt <= 320u))
        fc_eval_dot_public(sl, dt);

    /* sprite fetch */
    if (dt >= 257u && dt <= 320u)
        spr_fetch(sl, dt);

    /* pixel output */
    if (VISIBLE_LINE(sl) && dt >= 1u && dt <= 256u)
        pixel_out(sl, dt);

    /* loopy v updates */
    if (dt == 256u) loopy_vert_inc();
    if (dt == 257u) loopy_hori_copy();
    if (sl == 261u && dt >= 280u && dt <= 304u) loopy_vert_copy();
    if ((dt <= 256u && (dt & 7u) == 0u) ||
        dt == 328u || dt == 336u)
        loopy_hori_inc();

    /* reload shifters at start-of-line prefetch (dots 329, 337) */
    if (dt == 329u || dt == 337u) {
        p_bg_lo = (unsigned short)((p_bg_lo & 0xFF00u) | p_pt_lo);
        p_bg_hi = (unsigned short)((p_bg_hi & 0xFF00u) | p_pt_hi);
    }
}

/* ---- reset ---- */

void fc_pipeline_reset(void) {
    int i;
    p_bg_lo = p_bg_hi = p_at_lo = p_at_hi = 0;
    p_nt_byte = p_at_byte = p_pt_lo = p_pt_hi = 0;
    for (i = 0; i < 8; i++) {
        p_spr[i].lo = p_spr[i].hi = 0;
        p_spr[i].attr = 0;
        p_spr[i].x = 0xFFu;
    }
    fc_pipe_last_bg_pix = 0;
    fc_pipe_last_spr_pix = 0;
    fc_pipe_sprite0_hit_dot = -1;
}