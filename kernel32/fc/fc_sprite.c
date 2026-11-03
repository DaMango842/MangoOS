// kernel32/fc/fc_sprite.c - sprite pixel decoding and per-scanline
// hardware-style evaluation (first 8 in-range sprites into secondary
// OAM + overflow flag).  Sprite-0 hit stays in fc_ppu.c: it needs the
// background fetch and split-scroll context.

#include "fc.h"

/* pattern row of the sprite in OAM slot `slot` at scanline y.  Returns 0
   when the sprite is not in range (or sits at X=255, which the 2C02 never
   draws nor hits with); otherwise fills *pack with the 8 pixels (2 bits
   each, pixel 0 in bits 14-15) and *sx with the sprite X coordinate */
int fc_sprite_pixels(int slot, int y, int spr16, unsigned short *pack,
                     unsigned int *sx) {
    const unsigned char *s = fc.oam + slot * 4u;
    int syv = s[0];
    int tile = s[1];
    int attr = s[2];
    int sxp = s[3];
    int sprite_h = spr16 ? 16 : 8;
    int ry;
    int flip_h = (attr & 0x40u) != 0;
    int flip_v = (attr & 0x80u) != 0;

    if ((unsigned int)syv >= 240u)
        return 0;
    if (y < syv + 1 || y >= syv + 1 + sprite_h)
        return 0;
    if (sxp >= 255)
        return 0;
    if (spr16) {
        /* 8x16 sprites: pattern table from tile bit 0, not $2000 bit 3.
           Bit 0=0 -> $0000, Bit 0=1 -> $1000. */
        if (tile & 1)
            tile = (int)(256u | ((unsigned int)tile & 0xFEu));
        else
            tile = (int)((unsigned int)tile & 0xFEu);
    } else {
        /* 8x8 sprites: pattern table from $2000 bit 3 */
        tile |= (fc.ppu_ctrl & 0x08u) ? 256 : 0;
    }
    ry = y - (syv + 1);
    if (flip_v)
        ry = sprite_h - 1 - ry;
    if (ry >= 8) {
        /* lower half of an 8x16 sprite: partner tile (toggle bit 0) */
        if (!spr16)
            return 0;
        ry -= 8;
        tile ^= 1;
    }
    *pack = fc_tiles_pack((unsigned int)tile, (unsigned int)ry,
                          flip_h, flip_v);
    *sx = (unsigned int)sxp;
    return 1;
}

/* select the first 8 in-range sprites for one scanline (OAM order), raise
   the overflow flag on a 9th, and hand back the selected OAM indices.
   Returns the number of selected sprites.

   READ-ONLY with respect to the shared evaluation state: it no longer
   clears fc.sec_oam, writes fc.sec_oam_count, or touches eval_n/eval_m/
   eval_state.  Those belong to the dot-driven FSM in fc_ppu.c
   (eval_dot_inline), which is the only writer.  The renderer does not
   need secondary OAM at all -- fc_sprite_pixels() reads fc.oam directly
   via the index returned in sel[] -- so the copies here were pure
   clobbering of the CPU-visible $2004 state. */
int fc_sprite_eval(unsigned int y, int spr16, unsigned char *sel) {
    int sel_cnt = 0;
    int n = 0;
    unsigned int x;
    int sprite_h = spr16 ? 16 : 8;
    int ly = (int)y;

    for (x = 0; x < 64 && n < 8; x++) {
        const unsigned char *s = fc.oam + x * 4u;

        if (ly < s[0] + 1 || ly >= s[0] + 1 + sprite_h)
            continue;

        /* selected in OAM order; the pixel decoder takes the OAM index */
        sel[sel_cnt++] = (unsigned char)x;
        n++;
    }

    /* a 9th in-range sprite sets the overflow flag */
    if (n == 8) {
        for (; x < 64; x++) {
            const unsigned char *s = fc.oam + x * 4u;

            if (ly >= s[0] + 1 && ly < s[0] + 1 + sprite_h) {
                fc.ppu_status |= 0x20u;
                break;
            }
        }
    }
    return sel_cnt;
}