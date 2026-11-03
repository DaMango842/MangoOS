// kernel32/fc/fc_tiles.c - CHR pattern access for one 8x8 tile.
// `tile` is a flat 0..511 index whose bit 8 selects the $1000 table;
// callers apply $2000 bit3/bit4 and 8x16 pairing rules, so this module
// stays register-agnostic.

#include "fc.h"

/* fc_tiles_row() lives in fc.h as `static inline`: the background renderer
   calls it once per pixel, so the call has to be inlined away. */

unsigned short fc_tiles_pack(unsigned int tile, unsigned int row,
                             int flip_h, int flip_v) {
    unsigned char lo, hi;
    unsigned int src_r = (unsigned int)(flip_v ? 7 - row : row);
    unsigned short pack = 0;
    int b;

    fc_tiles_row(tile, src_r, &lo, &hi);
    for (b = 0; b < 8; b++) {
        unsigned int bit = (unsigned int)(flip_h ? b : 7 - b);
        unsigned int idx = ((hi >> bit) & 1u) << 1 | ((lo >> bit) & 1u);

        pack |= (unsigned short)(idx << (14u - b * 2u));
    }
    return pack;
}