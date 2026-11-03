// kernel32/fc/fc_ppu.c - 2C02 picture processing: nametable background,
// 64-sprite OAM, palette, scrolling. Renders 256x240 at 1x into
// fc_surface; the renderer upscales it 2x straight into the framebuffer.
//
// The PPU is rendered frame-synchronously: the whole picture is drawn
// from the state the game left at the end of the previous frame, then
// the 6502 runs one frame and its $2005/$2006/$2007 writes shape the
// NEXT picture (exactly how NMI-driven games use the PPU).

#include "fc.h"
#include "../render.h"

extern void fc_cpu_nmi(void);

/* ---- NTSC 2C02 palette (public domain RGB values) ---- */
static const unsigned int nes_pal[64] = {
    0x7C7C7Cu, 0x0000FCu, 0x0000BCu, 0x4428BCu,
    0x940084u, 0xA80020u, 0xA81000u, 0x881400u,
    0x503000u, 0x007800u, 0x006800u, 0x005800u,
    0x004058u, 0x000000u, 0x000000u, 0x000000u,
    0xBCBCBCu, 0x0078F8u, 0x0058F8u, 0x6844FCu,
    0xD800CCu, 0xE40058u, 0xF83800u, 0xE45C10u,
    0xAC7C00u, 0x00B800u, 0x00A800u, 0x00A844u,
    0x008888u, 0x000000u, 0x000000u, 0x000000u,
    0xF8F8F8u, 0x3CBCFCu, 0x6888FCu, 0x9878F8u,
    0xF878F8u, 0xF85898u, 0xF87858u, 0xFCA044u,
    0xF8B800u, 0xB8F818u, 0x58D854u, 0x58F898u,
    0x00E8D8u, 0x787878u, 0x000000u, 0x000000u,
    0xFCFCFCu, 0xA4E4FCu, 0xB8B8F8u, 0xD8B8F8u,
    0xF8B8F8u, 0xF8A4C0u, 0xF0D0B0u, 0xFCE0A8u,
    0xF8D878u, 0xD8F878u, 0xB8F8B8u, 0xB8F8D8u,
    0x00FCFCu, 0xF8D8F8u, 0x000000u, 0x000000u,
};

/* nametable byte index for (row,col). The base nametable comes from
   $2000 bits 0-1; the scroll's coarse overflow flips that bit exactly
   like the 2C02's t-register increment rolling into the
   nametable-select bits (A10 in "vertical mirroring", iNES header
   bit0=1, SMB1: scrolling right past x=256 crosses into the other NT;
   A11 in "horizontal mirroring", header bit0=0, vertical scrollers). */
static unsigned int nt_index(unsigned int row, unsigned int col) {
    unsigned int base = fc.ppu_ctrl & 0x03u;   /* $2000 bits 0-1 */
    unsigned int table;

    if (fc.mirror_h) {                 /* vertical mirroring (horizontal arrangement) */
        /* coarse X bit 5 (col >> 5) selects horizontal nametable */
        table = (base ^ ((col >> 5) & 1u)) & 1u;
    } else {                           /* horizontal mirroring (vertical arrangement) */
        /* coarse Y bit 5 (row >> 5) selects vertical nametable */
        table = ((base >> 1) ^ ((row >> 5) & 1u)) & 1u;
    }

    return table * 0x400u + (row & 31u) * 32u + (col & 31u);
}

/* pattern index (0..3) of the background pixel at screen (y,x) with explicit scroll */
static unsigned int bg_pixel_scroll(unsigned int y, unsigned int x,
                                     unsigned int scroll_x, unsigned int scroll_y) {
    unsigned int world_y = scroll_y + y;
    unsigned int world_x = scroll_x + x;
    unsigned int trow = world_y >> 3;
    unsigned int tcol = world_x >> 3;
    unsigned int idx = nt_index(trow, tcol);
    unsigned int tile = fc.vram[idx];
    unsigned char lo, hi;
    unsigned int yoff = world_y & 7u;
    unsigned int xoff = world_x & 7u;

    fc_tiles_row(tile + ((fc.ppu_ctrl & 0x10u) ? 256u : 0u), yoff, &lo, &hi);
    return ((lo >> (7u - xoff)) & 1u) |
           (((hi >> (7u - xoff)) & 1u) << 1);
}

/* pattern index (0..3) of the background pixel at screen (y,x) */
static unsigned int bg_pixel(unsigned int y, unsigned int x) {
    return bg_pixel_scroll(y, x, (unsigned int)fc.ppu_scroll_x, (unsigned int)fc.ppu_scroll_y);
}

#define PPU_DOTS_PER_CPU_CYCLE 3u
#define DOTS_PER_SCANLINE      341u
#define SCANLINES_PER_FRAME    262u

/* ---- per-dot sprite evaluation FSM ---- */

/* ================= equivalence oracle (TEMPORARY) =================
   Rolls a checksum over every field a CPU instruction can observe, once
   per fc_ppu_advance() call, and prints it on a fixed CALL COUNT.
   No CPU instruction executes inside fc_ppu_advance, so the state it
   leaves behind IS the complete set of observations the emulated program
   can make.  Two builds that agree on this checksum over a whole run are
   therefore observationally identical - which is what proves a rewrite of
   the dot loop did not change emulation.  Build with -DFC_TRACE_EVAL. */
#ifdef FC_TRACE_EVAL
unsigned int fc_eval_trace;
static unsigned long fc_eval_trace_n;
static unsigned long fc_eval_trace_total;
#ifndef FC_TRACE_STEP
#define FC_TRACE_STEP 2500u          /* calls between printed samples */
#endif
#ifndef FC_TRACE_LO
#define FC_TRACE_LO   0ul           /* first call count to print */
#endif
#ifndef FC_TRACE_HI
#define FC_TRACE_HI   0xFFFFFFFFul  /* last call count to print  */
#endif
static void eval_trace_mix(unsigned int v) {
    fc_eval_trace = (fc_eval_trace ^ v) * 16777619u;
}
static void eval_trace_sample(void) {
    int i;
    eval_trace_mix(fc.eval_state);
    eval_trace_mix(fc.eval_n);
    eval_trace_mix(fc.eval_m);
    eval_trace_mix(fc.eval_done);
    eval_trace_mix(fc.eval_range);
    eval_trace_mix(fc.sec_oam_pos);
    eval_trace_mix(fc.sec_oam_count);
    eval_trace_mix(fc.eval_read);
    eval_trace_mix(fc.eval_oam_base);
    eval_trace_mix(fc.oam_addr);
    eval_trace_mix(fc.ppu_status);
    eval_trace_mix(fc.ppu_dot);
    eval_trace_mix(fc.ppu_scanline);
    for (i = 0; i < 32; i++)
        eval_trace_mix(fc.sec_oam[i]);
}
static void eval_trace_print(void) {
    extern void serial_write(const char *);
    static const char hex[] = "0123456789ABCDEF";
    char b[40];
    char t[12];
    unsigned int v = fc_eval_trace;
    unsigned long n = fc_eval_trace_total;
    int i = 0, k;
    b[i++] = 'E'; b[i++] = '=';
    for (k = 0; k < 8; k++)
        b[i++] = hex[(v >> ((7 - k) * 4)) & 0xFu];
    b[i++] = ' ';
    k = 0;
    do { t[k++] = (char)('0' + n % 10ul); n /= 10ul; } while (n);
    while (k > 0)
        b[i++] = t[--k];
    b[i++] = '\r'; b[i++] = '\n'; b[i] = '\0';
    serial_write(b);
}
#endif /* FC_TRACE_EVAL */

/* ---- phases, split so the per-dot hot path can reach the cheap ones
   without a call.  Profiling (probe/probe_fcperf.py) attributed ~15 ms
   of a ~36 ms frame to this FSM, and objdump showed the monolithic
   eval_dot_inline() was NOT actually inlined: GCC emitted a real
   `call eval_dot_inline.part.0` on every one of the ~82k dots per frame.
   Only the 65-256 evaluation does real work; the other 149 dots of a
   scanline are a state assignment plus, at two dots, a side effect. ---- */

/* dots 1-64: secondary OAM clear.  Side effects only at dot 1 (clear) and
   dot 64 (capture OAMADDR). */
static inline void eval_clear_phase(unsigned int dot) {
    fc.eval_state = 1;
    if (dot == 1u) {
        int i;
        for (i = 0; i < 32; i++) fc.sec_oam[i] = 0xFF;
        fc.eval_n = 0;
        fc.eval_m = 0;
        fc.eval_done = 0;
        fc.eval_range = 0;
        fc.sec_oam_pos = 0;
        fc.sec_oam_count = 0;
    }
    if (dot == 64u) {
        /* fallback capture for the normal case; the authoritative one
           happens when the evaluation actually starts (see below) */
        fc.eval_oam_base = fc.oam_addr;
    }
}

/* dots 65-256: the sprite evaluation itself - the only phase with real
   work.  Deliberately NOT inlined: it is a large body and it is only
   reached while evaluation still has something to do, so paying the call
   there is worth it, and keeping it out of line leaves the much hotter
   1-64 / 257-341 dots completely call-free. */
static void eval_eval_phase(unsigned int scanline, unsigned int dot) {
    int h = (fc.ppu_ctrl & 0x20u) ? 16 : 8;

    fc.eval_state = 2;

    if (fc.eval_done)
        return;

    /* wiki (PPU registers, $2003): "If rendering is enabled
       mid-scanline, there are further consequences of an OAMADDR that
       was not set to 0 before OAM sprite evaluation begins at tick
       65. The value of OAMADDR at this tick determines the starting
       address for sprite evaluation for this scanline."
       Capturing it here (and not only at dot 64, which is skipped
       when the PPU was still blank at dot 64) covers exactly the
       mid-scanline-enable case that the $2004 stress test uses. */
    if (fc.eval_n == 0u && fc.eval_m == 0u)
        fc.eval_oam_base = fc.oam_addr;

    if (dot & 1u) {
        /* odd dot: READ primary OAM at current position.
           nesdev wiki, PPU registers, $2003: "The value of OAMADDR at
           this tick [65] determines the starting address for sprite
           evaluation for this scanline ... If OAMADDR is unaligned
           and does not point to the Y position ... whatever it points
           to will be reinterpreted as a Y position ... No more
           sprites will be found once the end of OAM is reached,
           effectively hiding any sprites before the starting
           OAMADDR."  The walk therefore runs base..255 with NO wrap. */
        unsigned int idx = (unsigned int)fc.eval_oam_base +
                           (fc.eval_n << 2) + fc.eval_m;

        if (idx > 0xFFu) {
            fc.eval_done = 1;        /* end of OAM: stop searching */
            return;
        }
        fc.eval_read = fc.oam[idx];
        /* The attribute byte (OAM[n][2]) physically lacks bits 2-4 in
           the DRAM, so the PPU's own internal read -- which is what a
           $2004 read exposes and what gets copied into secondary OAM --
           sees them as 0 too.  Mask it here so the latch, the secondary
           OAM copy and fc_oam_read_data() all agree. */
        if ((idx & 3u) == 2u)
            fc.eval_read = fc_circuit_oam_attr_mask(fc.eval_read);
        if (fc.eval_m == 0) {
            /* Y coordinate: evaluate range */
            fc.eval_range =
                (scanline >= (unsigned int)(fc.eval_read + 1) &&
                 scanline <  (unsigned int)(fc.eval_read + 1 + h) &&
                 fc.eval_read < 240u);
        }
    } else {
        /* even dot: WRITE/PROCESS */
        if (fc.sec_oam_pos < 32u) {
            /* copying to secondary OAM */
            if (fc.eval_m == 0) {
                if (fc.eval_range) {
                    fc.sec_oam[fc.sec_oam_pos++] = fc.eval_read;
                    fc.eval_m = 1;
                } else {
                    fc.eval_n = (fc.eval_n + 1u) & 0x3Fu;
                    if (fc.eval_n == 0u) fc.eval_done = 1;
                }
            } else {
                fc.sec_oam[fc.sec_oam_pos++] = fc.eval_read;
                fc.eval_m++;
                if (fc.eval_m > 3u) {
                    fc.eval_m = 0;
                    fc.eval_n = (fc.eval_n + 1u) & 0x3Fu;
                    fc.sec_oam_count = fc.sec_oam_pos >> 2;
                    if (fc.eval_n == 0u) fc.eval_done = 1;
                }
            }
        } else {
            /* secondary OAM full: overflow check */
            if (fc.eval_m == 0) {
                if (fc.eval_range) {
                    fc.ppu_status |= 0x20u;
                    fc.eval_m = 1;
                } else {
                    /* hardware bug: increment BOTH n and m */
                    fc.eval_n = (fc.eval_n + 1u) & 0x3Fu;
                    fc.eval_m = (fc.eval_m + 1u) & 3u;
                    if (fc.eval_n == 0u) fc.eval_done = 1;
                }
            } else {
                /* reading remaining bytes of overflow sprite */
                fc.eval_m++;
                if (fc.eval_m > 3u) {
                    fc.eval_m = 0;
                    fc.eval_n = (fc.eval_n + 1u) & 0x3Fu;
                    if (fc.eval_n == 0u) fc.eval_done = 1;
                }
            }
        }
    }
}

/* dots 257-320: sprite tile fetch, then dots 321-341: idle pipeline fill. */
static inline void eval_tail_phase(unsigned int dot) {
    if (dot <= 320u) {
        fc.eval_state = 3;
        fc.eval_n = (dot - 257u) >> 3;
        fc.eval_m = (unsigned char)((dot - 257u) & 3u);
        /* hardware: OAMADDR is reset to 0 during the sprite tile
           loading interval (ticks 257-320) of each visible scanline */
        fc.oam_addr = 0;
        return;
    }
    fc.eval_state = 0;
}

/* Dispatcher for a single dot.  fc_ppu_advance() inlines the cheap phases
   itself and calls eval_eval_phase() directly for the working range; this
   wrapper exists for fc_pipeline.c's own per-dot loop (not wired in yet).
   -DFC_LEGACY_DOTLOOP makes fc_ppu_advance() call THIS per dot instead,
   which reproduces the original cost shape (one real call per dot) and is
   used to A/B the optimisation. */
#ifdef FC_LEGACY_DOTLOOP
__attribute__((noinline))
#endif
static void eval_dot_inline(unsigned int scanline, unsigned int dot) {
    if (!fc.rendering_enabled || scanline >= 240u) {
        fc.eval_state = 0;
        return;
    }
    if (dot <= 64u) {
        eval_clear_phase(dot);
        return;
    }
    if (dot <= 256u) {
        eval_eval_phase(scanline, dot);
        return;
    }
    eval_tail_phase(dot);
}

/* kept public for fc_pipeline.c's own per-dot loop (not wired in yet) */
void fc_eval_dot_public(unsigned int scanline, unsigned int dot) {
    eval_dot_inline(scanline, dot);
}

/* advance the PPU dot/scanline timeline by a given number of CPU cycles.
   NOTE: fc_pipeline_tick() is NOT wired in here yet — its loopy register
   updates write fc.ppu_addr directly, which corrupts the batch renderer's
   scroll interpretation.  Proper integration requires the pipeline to use
   its own internal v register synced from $2006 writes. */
void fc_ppu_advance(unsigned int cpu_cycles) {
    unsigned int dots = cpu_cycles * PPU_DOTS_PER_CPU_CYCLE;
    unsigned int d;
    int rendering = fc.rendering_enabled;   /* constant for this call: it is
                                               only recomputed below */

    /* The loop is deliberately UNCHANGED from the original: same dot
       counter, same wrap, same `fc.ppu_dot + 1u` dot numbering (which runs
       2,3,...,341,1 within a scanline - do not "fix" that to 1..341, the
       $2004 stress test in AccuracyCoin is calibrated against it).
       The only change is that the FSM dispatch is written out inline, so
       the 149 dots per scanline that have no work to do cost a couple of
       compares instead of a function call.  objdump on the pre-split code
       showed GCC had split the monolithic eval_dot_inline() into
       eval_dot_inline.part.0 and emitted a real call for it on every one
       of the ~82k dots per frame; that call is gone now.
       Equivalence is checked with -DFC_TRACE_EVAL (see the oracle above)
       and probe/cmp_stream.py: identical checksums over 6.2M calls. */
    for (d = 0; d < dots; d++) {
        unsigned int dot;
        fc.ppu_dot++;
        if (fc.ppu_dot >= DOTS_PER_SCANLINE) {
            fc.ppu_dot = 0;
            fc.ppu_scanline++;
            if (fc.ppu_scanline >= SCANLINES_PER_FRAME)
                fc.ppu_scanline = 0;
        }
        /* visible scanlines + pre-render: run per-dot eval FSM */
                if (rendering && (fc.ppu_scanline < 240u || fc.ppu_scanline == 261u)) {
#ifdef FC_LEGACY_DOTLOOP
            eval_dot_inline(fc.ppu_scanline, fc.ppu_dot + 1u);
#else
            dot = fc.ppu_dot + 1u;
            if (dot <= 64u)
                eval_clear_phase(dot);
            else if (dot <= 256u)
                eval_eval_phase(fc.ppu_scanline, dot);
            else
                eval_tail_phase(dot);
#endif
        } else {
            fc.eval_state = 0;
        }
    }

    fc.rendering_enabled = (fc.ppu_mask & 0x18u) != 0;
#ifdef FC_TRACE_EVAL
    /* Sampled ONCE PER CALL: no CPU instruction executes inside this
       function, so the state left behind here is exactly what the program
       can observe next.  Printed on a fixed call count (not a wall-clock
       window) so the sequence is reproducible across builds and can be
       diffed with probe/cmp_stream.py. */
    eval_trace_sample();
    if (++fc_eval_trace_n >= FC_TRACE_STEP) {
        fc_eval_trace_n = 0;
        fc_eval_trace_total += FC_TRACE_STEP;
        if (fc_eval_trace_total >= FC_TRACE_LO &&
            fc_eval_trace_total <= FC_TRACE_HI)
            eval_trace_print();
    }
#endif
}

/* all PPU state lives in fc_mem_reset(); kept for symmetry */
void fc_ppu_reset(void) {
    fc.ppu_scanline = 0;
    fc.ppu_dot = 0;
    fc.rendering_enabled = 0;
    fc.eval_state = 0;
    fc_pipeline_reset();
}

/* the FC picture renders at 1x (256x240) into this surface; the
   renderer upscales it 2x straight into the framebuffer, so a quarter
   of the pixels are touched compared with drawing 2x into the 800x600
   back buffer */

/* ---- frame renderer ---- */
void fc_ppu_render(void) {
    unsigned int *dst = fc_surface;
    int y, x;
    int bg_on = (fc.ppu_mask & 0x08u) != 0;
    int spr_on = (fc.ppu_mask & 0x10u) != 0;
    int bg_left8 = (fc.ppu_mask & 0x02u) != 0;   /* show bg in left 8 px */
    int spr_left8 = (fc.ppu_mask & 0x04u) != 0;  /* show sprites left 8 px */
    int spr16 = (fc.ppu_ctrl & 0x20u) != 0;
    unsigned int sx = (unsigned int)fc.ppu_scroll_x;
    unsigned int sy = (unsigned int)fc.ppu_scroll_y;
    unsigned int split = FC_FB_H;   /* sprite-0 split: rows above the hit use
                                       the pre-hit scroll; with no hit (or no
                                       split, i.e. the two scrolls agree) the
                                       whole frame scrolls uniformly */
    unsigned int bg_color = nes_pal[fc.palette[0] & 0x3Fu];

    if (fc.sprite0_hit_row != 0 &&
        ((unsigned int)fc.scroll_top_x != sx ||
         (unsigned int)fc.scroll_top_y != sy))
        split = fc.sprite0_hit_row;

    /* pre-render: vblank (bit7), sprite-0 hit (bit6) and sprite
       overflow (bit5) clear at the start of rendering; bits 0-4 are
       open bus and are never touched */
    fc.ppu_status = (unsigned char)(fc.ppu_status & ~0xE0u);

    /* Sprite evaluation -- secondary OAM clear at dot 1, evaluation at
       dots 65-256, fetch at 257-320 -- is owned SOLELY by the dot-driven
       FSM (eval_dot_inline).  The renderer must not touch eval_state or
       sec_oam here, not even when rendering is disabled: the FSM itself
       sets eval_state = 0 for non-rendered scanlines, and a batch clear
       would overwrite the state the CPU observes through $2004. */

    /* background */
    for (y = 0; y < FC_FB_H; y++) {
        unsigned int use_top = (unsigned int)y < split;
        unsigned int sy_eff = use_top ? (unsigned int)fc.scroll_top_y : sy;
        unsigned int sx_eff = use_top ? (unsigned int)fc.scroll_top_x : sx;
        unsigned int world_y = sy_eff + (unsigned int)y;
        unsigned int trow = world_y >> 3;
        unsigned int yoff = world_y & 7u;
        unsigned int *drow = &dst[(unsigned int)y * FC_FB_W];
        unsigned int cur_col = 0xFFFFFFFFu;
        unsigned char lo = 0, hi = 0;
        unsigned int attr = 0;

        for (x = 0; x < FC_FB_W; x++) {
            unsigned int world_x = sx_eff + (unsigned int)x;
            unsigned int tcol = world_x >> 3;
            unsigned int xoff = world_x & 7u;
            unsigned int px;

            if (!bg_on || (x < 8 && !bg_left8)) {
                px = bg_color;
            } else {
                if (tcol != cur_col) {
                    unsigned int idx = nt_index(trow, tcol);
                    unsigned int tile = fc.vram[idx];
                    unsigned int areg = (idx & ~0x3FFu) + 0x3C0u +
                                        ((trow & 31u) / 4u) * 8u +
                                        ((tcol & 31u) / 4u);
                    unsigned int q = ((trow & 31u) & 2u) * 2u +
                                     ((tcol & 31u) & 2u);

                    fc_tiles_row(tile + ((fc.ppu_ctrl & 0x10u) ? 256u : 0u),
                                 yoff, &lo, &hi);
                    attr = (unsigned int)((fc.vram[areg] >> q) & 3u);
                    cur_col = tcol;
                }
                {
                    unsigned int idx = ((hi >> (7u - xoff)) & 1u) << 1 |
                                       ((lo >> (7u - xoff)) & 1u);
                    unsigned int pal;

                    /* NES hardware: color index 0 of ANY palette is
                       transparent -> shows the universal backdrop */
                    if (idx == 0)
                        px = bg_color;
                    else {
                        pal = attr * 4u + idx;
                        px = nes_pal[fc.palette[pal & 0xFu] & 0x3Fu];
                    }
                }
            }
            drow[x] = px;
        }
    }

    /* sprites: two passes per scanline.  Pass 1 evaluates OAM in hardware
       order and keeps the first 8 in-range sprites; sprite-0 hit detection
       lives here (forward order, like the 2C02's evaluation pass).  Pass 2
       draws them in REVERSE selection order so the lowest OAM index sits
       in front (hardware priority).  Evaluation runs whenever rendering is
       enabled (background or sprites); drawing and the sprite-0 hit
       additionally need the sprite bit ($2001 bit 4) */
    if (bg_on || spr_on) {
        for (y = 0; y < FC_FB_H; y++) {
            unsigned char sel[8];
            int sel_cnt;
            unsigned int *drow = &dst[(unsigned int)y * FC_FB_W];

            /* hardware-style evaluation: fills secondary OAM, selects the
               first 8 in-range sprites, raises overflow on a 9th */
            sel_cnt = fc_sprite_eval((unsigned int)y, spr16, sel);

            /* Sprite-0 hit: only when sprite 0 was actually evaluated and
               both layers are enabled; hit if an opaque sprite pixel sits
               on an opaque background pixel (either scroll interpretation) */
            if (sel_cnt > 0 && sel[0] == 0 && spr_on && bg_on) {
                unsigned short s0pack;
                unsigned int s0x;
                int b0;

                if (fc_sprite_pixels(0, y, spr16, &s0pack, &s0x)) {
                    for (b0 = 0; b0 < 8; b0++) {
                        unsigned int s0idx =
                            (s0pack >> (unsigned int)(14 - b0 * 2)) & 3u;
                        unsigned int wx = s0x + (unsigned int)b0;

                        if (!s0idx || wx >= FC_FB_W)
                            continue;
                        /* left-8 masks: a masked sprite pixel, or a
                           masked (backdrop) background pixel, can
                           never produce a hit */
                        if (wx < 8u && !spr_left8)
                            continue;
                        if (wx < 8u && !bg_left8)
                            continue;
                        /* Use correct scroll for this scanline */
                        unsigned int hit_scroll_x = ((unsigned int)y < split) ? (unsigned int)fc.scroll_top_x : sx;
                        unsigned int hit_scroll_y = ((unsigned int)y < split) ? (unsigned int)fc.scroll_top_y : sy;
                        /* hit if opaque under EITHER scroll: the split
                           interpretation can lag one frame, and a missed
                           hit deadlocks split-scroll games */
                        unsigned int bghit =
                            bg_pixel_scroll((unsigned int)y, wx,
                                            hit_scroll_x, hit_scroll_y);
                        if (!bghit &&
                            (hit_scroll_x != sx ||
                             hit_scroll_y != sy))
                            bghit = bg_pixel_scroll((unsigned int)y, wx,
                                                    sx, sy);
                        if (bghit != 0) {
                            /* sticky flag; set only on first hit */
                            if (!fc.sprite0_hit) {
                                fc.sprite0_hit = 1;
                                fc.sprite0_hit_row = (unsigned int)y;
                            }
                        }
                    }
                }
            }

            /* Draw sprites (reverse priority order).  NOTE: this used to
               set fc.eval_state = 3 to "feed" $2004 reads -- that is a
               timing fake which hides real FSM divergence.  $2004 must
               observe the state produced by eval_dot_inline() alone, so
               the renderer no longer writes it. */
            if (spr_on) {

                int r;
                for (r = sel_cnt - 1; r >= 0; r--) {
                    const unsigned char *s =
                        fc.oam + (unsigned int)sel[r] * 4u;
                    unsigned short pack;
                    unsigned int sx;
                    unsigned int pal_idx = (unsigned int)(s[2] & 3u) * 4u;
                    int behind = (s[2] & 0x20u) != 0;
                    int b;
                    unsigned int px;

                    if (!fc_sprite_pixels((int)sel[r], y, spr16, &pack, &sx))
                        continue;
                    for (b = 0; b < 8; b++) {
                        unsigned int idx =
                            (pack >> (unsigned int)(14 - b * 2)) & 3u;

                        if (idx == 0)
                            continue;
                        px = sx + (unsigned int)b;
                        if (px >= FC_FB_W)
                            continue;
                        if (px < 8u && !spr_left8)
                            continue;
                        if (behind) {
                            /* priority sprite: visible only where the
                               background is transparent */
                            if (bg_pixel((unsigned int)y, px) != 0)
                                continue;
                        }
                        drow[px] =
                            nes_pal[fc.palette[0x10u + pal_idx + idx] & 0x3Fu];
                    }
                }
            }
        } /* end scanline loop */
    }

    /* batch VBlank: set at end of render, cleared at start of next
       render.  This matches the batch NMI injection in run_frame(). */
    fc.ppu_status = (unsigned char)(fc.ppu_status | 0x80u);

    /* end of frame: clear the private evaluation state, leaving only the
       rendered output in the framebuffer. The actual $2004 / secondary OAM
       logic is maintained in the dedicated OAM module. */
    fc_oam_end_render();
}