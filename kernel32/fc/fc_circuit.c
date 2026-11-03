// kernel32/fc/fc_circuit.c - Circuit-level 2C02 PPU behavior simulation.
//
// Models the analog/digital circuit behaviors that exist below the
// register-level abstraction:
//
//   Open bus       - PPU data bus retains the last value written to
//                    any PPU register.  $2002 bits 0-4 return this.
//
//   OAM DRAM corruption - writing OAMADDR ($2003) during rendering
//     changes the DRAM row selector mid-access without precharge,
//     causing the previous row's 8 bytes to be implicitly written
//     to the new row (the "row copy" hardware bug).
//
//   OAM attribute byte decay - attribute bytes (OAMADDR & 3 == 2)
//     have bits 2-4 physically missing from the DRAM cells; they
//     always read back as 0 through $2004 on PPUs that support it.
//
//   VRAM read buffer decay - back-to-back $2007 reads may return
//     stale data from the buffer because the refill lags the read
//     by 1-2 PPU cycles.

#include "fc.h"

/* ---- open bus ---- */

static unsigned char circuit_open_bus;

/* last value driven onto the PPU-CPU data bus */
unsigned char fc_circuit_open_bus(void) {
    return circuit_open_bus;
}

/* any PPU register write drives the bus */
void fc_circuit_bus_write(unsigned char v) {
    circuit_open_bus = v;
}

/* ---- OAM DRAM row corruption ---- */

/* Simulate the hardware behavior when OAMADDR ($2003) is written
   during rendering: the DRAM row selector changes mid-access without
   the columns being precharged first, causing the previous row's
   data to be implicitly written to the new row.

   On real hardware this copies all 9 bytes (8 OAM1 + 1 OAM2) from
   the old row to the new row.  We model the 8 OAM1 bytes; the OAM2
   byte is not directly accessible through $2004. */
void fc_circuit_oam_corrupt(unsigned char old_addr, unsigned char new_addr) {
    unsigned char old_row = old_addr >> 3;
    unsigned char new_row = new_addr >> 3;
    int i;

    if (old_row == new_row)
        return;                          /* same row: no corruption */

    /* Copy all 8 OAM1 bytes from old row to new row */
    for (i = 0; i < 8; i++) {
        unsigned int src = (unsigned int)old_row * 8u + (unsigned int)i;
        unsigned int dst = (unsigned int)new_row * 8u + (unsigned int)i;
        if (dst < 256u && src < 256u)
            fc.oam[dst] = fc.oam[src];
    }
}

/* ---- OAM attribute byte read masking ---- */

/* Attribute bytes (OAMADDR & 3 == 2) have bits 2-4 physically
   missing from the DRAM.  They always read back as 0 through $2004
   on PPU revisions that support OAMDATA reads (RP2C02G and later). */
unsigned char fc_circuit_oam_attr_mask(unsigned char v) {
    return (unsigned char)(v & 0xE3u);   /* keep bits 7-5, 1-0; zero 4-2 */
}

/* ---- initialization ---- */

void fc_circuit_reset(void) {
    circuit_open_bus = 0;
}