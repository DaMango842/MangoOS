// kernel32/fc/fc_cpu.c - MOS 6502 (2A03) interpreter for the MangoOS NES core.
//
// Complete 256-opcode set (all official + stable unofficial).  NMOS quirks
// relevant to 2A03 games: no BCD (D flag is stored but ADC/SBC ignore it),
// and the unused B flag is managed for PHP/PLP/BRK/RTI.  Cycle counts are
// the NMOS values; page-cross penalties are honored for branches (2/3/4).
// The KIL/JAM opcodes are treated as 1-byte NOPs (the common emulator
// choice), and the unstable LAX #imm / XAA / SHX / SHY / AHX / TAS / LAS
// forms use the widely accepted approximations.

#include "fc.h"

struct fc_state fc;

#define FLAG_N 0x80u
#define FLAG_V 0x40u
#define FLAG_5 0x20u
#define FLAG_B 0x10u
#define FLAG_D 0x08u
#define FLAG_I 0x04u
#define FLAG_Z 0x02u
#define FLAG_C 0x01u

/* ---- flags ---- */
static void set_nz(unsigned char v) {
    fc.p = (unsigned char)((fc.p & ~(FLAG_N | FLAG_Z)) |
                           (v & FLAG_N) | ((v == 0) ? FLAG_Z : 0));
}

/* ---- stack ---- */
/* sp is an 8-bit counter: the effective address is always in
   0x100-0x1FF by construction, and under/overflow wraps modulo 256
   exactly like the 2A03 (the hardware has no stack fault) */
static void push(unsigned char v) {
    fc.ram[0x100u + fc.sp] = v;
    fc.sp--;
}

static unsigned char pop(void) {
    fc.sp++;
    return fc.ram[0x100u + fc.sp];
}

/* ---- program memory ---- */
static unsigned char fetch(void) {
    unsigned char v = fc_rd8(fc.pc);
    fc.pc++;
    return v;
}

static unsigned short fetch16(void) {
    unsigned short lo = fetch();
    unsigned short hi = fetch();
    return (unsigned short)(lo | (hi << 8));
}

/* ---- addressing ---- */
static unsigned short addr_abs(void) {
    return fetch16();
}

static unsigned char addr_zp(void) {
    return fetch();
}

static unsigned char addr_zpx(void) {
    return (unsigned char)(fetch() + fc.x);
}

static unsigned char addr_zpy(void) {
    return (unsigned char)(fetch() + fc.y);
}

static unsigned short addr_abx(void) {
    return (unsigned short)(fetch16() + fc.x);
}

static unsigned short addr_aby(void) {
    return (unsigned short)(fetch16() + fc.y);
}

static unsigned short addr_indx(void) {
    unsigned char zp = (unsigned char)(fetch() + fc.x);
    unsigned short lo = fc_rd8(zp);
    unsigned short hi = fc_rd8((unsigned char)(zp + 1));
    return (unsigned short)(lo | (hi << 8));
}

static unsigned short addr_indy(void) {
    unsigned char zp = fetch();
    unsigned short lo = fc_rd8(zp);
    unsigned short hi = fc_rd8((unsigned char)(zp + 1));
    return (unsigned short)((lo | (hi << 8)) + fc.y);
}

static unsigned short addr_ind(void) {
    unsigned short base = fetch16();
    unsigned short lo = fc_rd8(base);
    unsigned short hi = fc_rd8((unsigned short)((base & 0xFF00u) |
                                                ((base + 1) & 0x00FFu)));
    return (unsigned short)(lo | (hi << 8));
}

/* ---- arithmetic ---- */
static void adc(unsigned char v) {
    unsigned int sum = (unsigned int)fc.a + v + (fc.p & FLAG_C);
    unsigned char r = (unsigned char)sum;

    /* 2A03 has no BCD; the D flag is ignored */
    fc.p = (unsigned char)((fc.p & ~(FLAG_C | FLAG_V | FLAG_N | FLAG_Z)) |
                           ((sum > 0xFF) ? FLAG_C : 0) |
                           (((fc.a ^ r) & (v ^ r) & 0x80) ? FLAG_V : 0) |
                           (r & FLAG_N) | ((r == 0) ? FLAG_Z : 0));
    fc.a = r;
}

static void sbc(unsigned char v) {
    unsigned int sum = (unsigned int)fc.a - v - (1 - (fc.p & FLAG_C));
    unsigned char r = (unsigned char)sum;

    fc.p = (unsigned char)((fc.p & ~(FLAG_C | FLAG_V | FLAG_N | FLAG_Z)) |
                           ((sum <= 0xFF) ? FLAG_C : 0) |
                           (((fc.a ^ r) & (~v ^ r) & 0x80) ? FLAG_V : 0) |
                           (r & FLAG_N) | ((r == 0) ? FLAG_Z : 0));
    fc.a = r;
}

static void cmp(unsigned char a, unsigned char v) {
    unsigned char r = (unsigned char)(a - v);
    fc.p = (unsigned char)((fc.p & ~(FLAG_C | FLAG_N | FLAG_Z)) |
                           ((a >= v) ? FLAG_C : 0) |
                           (r & FLAG_N) | ((r == 0) ? FLAG_Z : 0));
}

static void bit_test(unsigned char v) {
    fc.p = (unsigned char)((fc.p & ~(FLAG_N | FLAG_V | FLAG_Z)) |
                           (v & (FLAG_N | FLAG_V)) |
                           ((fc.a & v) == 0 ? FLAG_Z : 0));
}

/* returns extra cycles: +1 when taken, +1 more on a page cross */
static unsigned int branch(int taken) {
    signed char off = (signed char)fetch();
    if (taken) {
        unsigned short npc = (unsigned short)(fc.pc + off);
        unsigned int extra = ((((unsigned int)fc.pc ^ (unsigned int)npc) & 0xFF00u) != 0) ? 1u : 0u;

        fc.pc = npc;
        return 1u + extra;
    }
    return 0;
}

/* ---- unofficial read-modify-write helpers ---- */
static void rmw_asl(unsigned short ad) {
    unsigned char v = fc_rd8(ad);

    fc.p = (unsigned char)((fc.p & ~FLAG_C) | (v >> 7));
    v = (unsigned char)(v << 1);
    fc_wr8(ad, v);
    set_nz(v);
}

static void rmw_lsr(unsigned short ad) {
    unsigned char v = fc_rd8(ad);

    fc.p = (unsigned char)((fc.p & ~FLAG_C) | (v & 1));
    v = (unsigned char)(v >> 1);
    fc_wr8(ad, v);
    set_nz(v);
}

static void rmw_rol(unsigned short ad) {
    unsigned char c = fc.p & FLAG_C;
    unsigned char v = fc_rd8(ad);

    fc.p = (unsigned char)((fc.p & ~FLAG_C) | (v >> 7));
    v = (unsigned char)((v << 1) | c);
    fc_wr8(ad, v);
    set_nz(v);
}

static void rmw_ror(unsigned short ad) {
    unsigned char c = fc.p & FLAG_C;
    unsigned char v = fc_rd8(ad);

    fc.p = (unsigned char)((fc.p & ~FLAG_C) | (v & 1));
    v = (unsigned char)((v >> 1) | (c << 7));
    fc_wr8(ad, v);
    set_nz(v);
}

static void rmw_slo(unsigned short ad) {
    unsigned char v = fc_rd8(ad);

    fc.p = (unsigned char)((fc.p & ~FLAG_C) | (v >> 7));
    v = (unsigned char)(v << 1);
    fc_wr8(ad, v);
    fc.a |= v;
    set_nz(fc.a);
}

static void rmw_rla(unsigned short ad) {
    unsigned char c = fc.p & FLAG_C;
    unsigned char v = fc_rd8(ad);

    fc.p = (unsigned char)((fc.p & ~FLAG_C) | (v >> 7));
    v = (unsigned char)((v << 1) | c);
    fc_wr8(ad, v);
    fc.a &= v;
    set_nz(fc.a);
}

static void rmw_sre(unsigned short ad) {
    unsigned char v = fc_rd8(ad);

    fc.p = (unsigned char)((fc.p & ~FLAG_C) | (v & 1));
    v = (unsigned char)(v >> 1);
    fc_wr8(ad, v);
    fc.a ^= v;
    set_nz(fc.a);
}

static void rmw_rra(unsigned short ad) {
    unsigned char c = fc.p & FLAG_C;
    unsigned char v = fc_rd8(ad);

    fc.p = (unsigned char)((fc.p & ~FLAG_C) | (v & 1));
    v = (unsigned char)((v >> 1) | (c << 7));
    fc_wr8(ad, v);
    adc(v);
}

static void rmw_dcp(unsigned short ad) {
    unsigned char v = (unsigned char)(fc_rd8(ad) - 1);

    fc_wr8(ad, v);
    cmp(fc.a, v);
}

static void rmw_isc(unsigned short ad) {
    unsigned char v = (unsigned char)(fc_rd8(ad) + 1);

    fc_wr8(ad, v);
    sbc(v);
}

/* ---- instruction execution ---- */
unsigned int fc_cpu_step(void);

/* last-executed PC trace for the serial debugger */
static unsigned short trace_pc[64];
static unsigned int trace_n;

void fc_trace_reset(void) {
    trace_n = 0;
}

unsigned int fc_trace_count(void) {
    return trace_n;
}

unsigned short fc_trace_lookup(unsigned int n) {
    return trace_pc[n & 63u];
}

unsigned int fc_cpu_step(void) {
    unsigned char op = fetch();
    unsigned int cyc = 0;

    trace_pc[trace_n++ & 63u] = (unsigned short)(fc.pc - 1);
    if (trace_n == 0xFFFFFFFFu)
        trace_n = 0;                     /* never happens, keeps it simple */

    switch (op) {
    /* ---- load/store ---- */
    case 0xA9: { unsigned char v = fetch(); fc.a = v; set_nz(v); cyc = 2; break; }
    case 0xA5: { fc.a = fc_rd8(addr_zp()); set_nz(fc.a); cyc = 3; break; }
    case 0xB5: { fc.a = fc_rd8(addr_zpx()); set_nz(fc.a); cyc = 4; break; }
    case 0xAD: { fc.a = fc_rd8(addr_abs()); set_nz(fc.a); cyc = 4; break; }
    case 0xBD: { fc.a = fc_rd8(addr_abx()); set_nz(fc.a); cyc = 4; break; }
    case 0xB9: { fc.a = fc_rd8(addr_aby()); set_nz(fc.a); cyc = 4; break; }
    case 0xA1: { fc.a = fc_rd8(addr_indx()); set_nz(fc.a); cyc = 6; break; }
    case 0xB1: { fc.a = fc_rd8(addr_indy()); set_nz(fc.a); cyc = 5; break; }

    case 0x85: { fc_wr8(addr_zp(), fc.a); cyc = 3; break; }
    case 0x95: { fc_wr8(addr_zpx(), fc.a); cyc = 4; break; }
    case 0x8D: { fc_wr8(addr_abs(), fc.a); cyc = 4; break; }
    case 0x9D: { fc_wr8(addr_abx(), fc.a); cyc = 5; break; }
    case 0x99: { fc_wr8(addr_aby(), fc.a); cyc = 5; break; }
    case 0x81: { fc_wr8(addr_indx(), fc.a); cyc = 6; break; }
    case 0x91: { fc_wr8(addr_indy(), fc.a); cyc = 6; break; }

    case 0xA2: { fc.x = fetch(); set_nz(fc.x); cyc = 2; break; }
    case 0xA6: { fc.x = fc_rd8(addr_zp()); set_nz(fc.x); cyc = 3; break; }
    case 0xB6: { fc.x = fc_rd8(addr_zpy()); set_nz(fc.x); cyc = 4; break; }
    case 0xAE: { fc.x = fc_rd8(addr_abs()); set_nz(fc.x); cyc = 4; break; }
    case 0xBE: { fc.x = fc_rd8(addr_aby()); set_nz(fc.x); cyc = 4; break; }
    case 0x86: { fc_wr8(addr_zp(), fc.x); cyc = 3; break; }
    case 0x96: { fc_wr8(addr_zpy(), fc.x); cyc = 4; break; }
    case 0x8E: { fc_wr8(addr_abs(), fc.x); cyc = 4; break; }

    case 0xA0: { fc.y = fetch(); set_nz(fc.y); cyc = 2; break; }
    case 0xA4: { fc.y = fc_rd8(addr_zp()); set_nz(fc.y); cyc = 3; break; }
    case 0xB4: { fc.y = fc_rd8(addr_zpx()); set_nz(fc.y); cyc = 4; break; }
    case 0xAC: { fc.y = fc_rd8(addr_abs()); set_nz(fc.y); cyc = 4; break; }
    case 0xBC: { fc.y = fc_rd8(addr_abx()); set_nz(fc.y); cyc = 4; break; }
    case 0x84: { fc_wr8(addr_zp(), fc.y); cyc = 3; break; }
    case 0x94: { fc_wr8(addr_zpx(), fc.y); cyc = 4; break; }
    case 0x8C: { fc_wr8(addr_abs(), fc.y); cyc = 4; break; }

    /* ---- transfers ---- */
    case 0xAA: { fc.x = fc.a; set_nz(fc.x); cyc = 2; break; }
    case 0xA8: { fc.y = fc.a; set_nz(fc.y); cyc = 2; break; }
    case 0x8A: { fc.a = fc.x; set_nz(fc.a); cyc = 2; break; }
    case 0x98: { fc.a = fc.y; set_nz(fc.a); cyc = 2; break; }
    case 0xBA: { fc.x = fc.sp; set_nz(fc.x); cyc = 2; break; }
    case 0x9A: { fc.sp = fc.x; cyc = 2; break; }

    /* ---- stack ops ---- */
    case 0x48: { push(fc.a); cyc = 3; break; }
    case 0x68: { fc.a = pop(); set_nz(fc.a); cyc = 4; break; }
    case 0x08: { push((unsigned char)(fc.p | FLAG_B | FLAG_5)); cyc = 3; break; }
    case 0x28: { fc.p = (unsigned char)((pop() & ~FLAG_B) | FLAG_B | FLAG_5); cyc = 4; break; }
    case 0x20: { unsigned short lo = fetch(); unsigned short hi = fetch();
                unsigned short ret = (unsigned short)(fc.pc - 1);
                push((unsigned char)(ret >> 8)); push((unsigned char)ret);
                fc.pc = (unsigned short)(lo | (hi << 8)); cyc = 6; break; }
    case 0x60: { fc.pc = (unsigned short)(pop() | ((unsigned short)pop() << 8)); fc.pc++; cyc = 6; break; }
    case 0x40: { fc.p = (unsigned char)((pop() & ~FLAG_B) | FLAG_B | FLAG_5); fc.pc = (unsigned short)(pop() | ((unsigned short)pop() << 8)); cyc = 6; break; }
    /* BRK pushes the address of the byte after the padding byte (opcode+2) */
    case 0x00: { unsigned short ret = (unsigned short)(fc.pc + 1);
                push((unsigned char)(ret >> 8)); push((unsigned char)ret);
                push((unsigned char)(fc.p | FLAG_B | FLAG_5));
                fc.p |= FLAG_I;
                fc.pc = (unsigned short)(fc_rd8(0xFFFEu) | ((unsigned short)fc_rd8(0xFFFFu) << 8)); cyc = 7; break; }

    /* ---- logical ---- */
    case 0x29: { fc.a &= fetch(); set_nz(fc.a); cyc = 2; break; }
    case 0x25: { fc.a &= fc_rd8(addr_zp()); set_nz(fc.a); cyc = 3; break; }
    case 0x35: { fc.a &= fc_rd8(addr_zpx()); set_nz(fc.a); cyc = 4; break; }
    case 0x2D: { fc.a &= fc_rd8(addr_abs()); set_nz(fc.a); cyc = 4; break; }
    case 0x3D: { fc.a &= fc_rd8(addr_abx()); set_nz(fc.a); cyc = 4; break; }
    case 0x39: { fc.a &= fc_rd8(addr_aby()); set_nz(fc.a); cyc = 4; break; }
    case 0x21: { fc.a &= fc_rd8(addr_indx()); set_nz(fc.a); cyc = 6; break; }
    case 0x31: { fc.a &= fc_rd8(addr_indy()); set_nz(fc.a); cyc = 5; break; }

    case 0x09: { fc.a |= fetch(); set_nz(fc.a); cyc = 2; break; }
    case 0x05: { fc.a |= fc_rd8(addr_zp()); set_nz(fc.a); cyc = 3; break; }
    case 0x15: { fc.a |= fc_rd8(addr_zpx()); set_nz(fc.a); cyc = 4; break; }
    case 0x0D: { fc.a |= fc_rd8(addr_abs()); set_nz(fc.a); cyc = 4; break; }
    case 0x1D: { fc.a |= fc_rd8(addr_abx()); set_nz(fc.a); cyc = 4; break; }
    case 0x19: { fc.a |= fc_rd8(addr_aby()); set_nz(fc.a); cyc = 4; break; }
    case 0x01: { fc.a |= fc_rd8(addr_indx()); set_nz(fc.a); cyc = 6; break; }
    case 0x11: { fc.a |= fc_rd8(addr_indy()); set_nz(fc.a); cyc = 5; break; }

    case 0x49: { fc.a ^= fetch(); set_nz(fc.a); cyc = 2; break; }
    case 0x45: { fc.a ^= fc_rd8(addr_zp()); set_nz(fc.a); cyc = 3; break; }
    case 0x55: { fc.a ^= fc_rd8(addr_zpx()); set_nz(fc.a); cyc = 4; break; }
    case 0x4D: { fc.a ^= fc_rd8(addr_abs()); set_nz(fc.a); cyc = 4; break; }
    case 0x5D: { fc.a ^= fc_rd8(addr_abx()); set_nz(fc.a); cyc = 4; break; }
    case 0x59: { fc.a ^= fc_rd8(addr_aby()); set_nz(fc.a); cyc = 4; break; }
    case 0x41: { fc.a ^= fc_rd8(addr_indx()); set_nz(fc.a); cyc = 6; break; }
    case 0x51: { fc.a ^= fc_rd8(addr_indy()); set_nz(fc.a); cyc = 5; break; }

    case 0x24: { bit_test(fc_rd8(addr_zp())); cyc = 3; break; }
    case 0x2C: { bit_test(fc_rd8(addr_abs())); cyc = 4; break; }

    /* ---- arithmetic ---- */
    case 0x69: { adc(fetch()); cyc = 2; break; }
    case 0x65: { adc(fc_rd8(addr_zp())); cyc = 3; break; }
    case 0x75: { adc(fc_rd8(addr_zpx())); cyc = 4; break; }
    case 0x6D: { adc(fc_rd8(addr_abs())); cyc = 4; break; }
    case 0x7D: { adc(fc_rd8(addr_abx())); cyc = 4; break; }
    case 0x79: { adc(fc_rd8(addr_aby())); cyc = 4; break; }
    case 0x61: { adc(fc_rd8(addr_indx())); cyc = 6; break; }
    case 0x71: { adc(fc_rd8(addr_indy())); cyc = 5; break; }

    case 0xE9: case 0xEB: { sbc(fetch()); cyc = 2; break; }  /* EB: unofficial SBC */
    case 0xE5: { sbc(fc_rd8(addr_zp())); cyc = 3; break; }
    case 0xF5: { sbc(fc_rd8(addr_zpx())); cyc = 4; break; }
    case 0xED: { sbc(fc_rd8(addr_abs())); cyc = 4; break; }
    case 0xFD: { sbc(fc_rd8(addr_abx())); cyc = 4; break; }
    case 0xF9: { sbc(fc_rd8(addr_aby())); cyc = 4; break; }
    case 0xE1: { sbc(fc_rd8(addr_indx())); cyc = 6; break; }
    case 0xF1: { sbc(fc_rd8(addr_indy())); cyc = 5; break; }

    case 0xC9: { cmp(fc.a, fetch()); cyc = 2; break; }
    case 0xC5: { cmp(fc.a, fc_rd8(addr_zp())); cyc = 3; break; }
    case 0xD5: { cmp(fc.a, fc_rd8(addr_zpx())); cyc = 4; break; }
    case 0xCD: { cmp(fc.a, fc_rd8(addr_abs())); cyc = 4; break; }
    case 0xDD: { cmp(fc.a, fc_rd8(addr_abx())); cyc = 4; break; }
    case 0xD9: { cmp(fc.a, fc_rd8(addr_aby())); cyc = 4; break; }
    case 0xC1: { cmp(fc.a, fc_rd8(addr_indx())); cyc = 6; break; }
    case 0xD1: { cmp(fc.a, fc_rd8(addr_indy())); cyc = 5; break; }

    case 0xE0: { cmp(fc.x, fetch()); cyc = 2; break; }
    case 0xE4: { cmp(fc.x, fc_rd8(addr_zp())); cyc = 3; break; }
    case 0xEC: { cmp(fc.x, fc_rd8(addr_abs())); cyc = 4; break; }
    case 0xC0: { cmp(fc.y, fetch()); cyc = 2; break; }
    case 0xC4: { cmp(fc.y, fc_rd8(addr_zp())); cyc = 3; break; }
    case 0xCC: { cmp(fc.y, fc_rd8(addr_abs())); cyc = 4; break; }

    /* ---- inc/dec ---- */
    case 0xE6: { unsigned short ad = addr_zp(); unsigned char v = (unsigned char)(fc_rd8(ad) + 1); fc_wr8(ad, v); set_nz(v); cyc = 5; break; }
    case 0xF6: { unsigned short ad = addr_zpx(); unsigned char v = (unsigned char)(fc_rd8(ad) + 1); fc_wr8(ad, v); set_nz(v); cyc = 6; break; }
    case 0xEE: { unsigned short ad = addr_abs(); unsigned char v = (unsigned char)(fc_rd8(ad) + 1); fc_wr8(ad, v); set_nz(v); cyc = 6; break; }
    case 0xFE: { unsigned short ad = addr_abx(); unsigned char v = (unsigned char)(fc_rd8(ad) + 1); fc_wr8(ad, v); set_nz(v); cyc = 7; break; }
    case 0xC6: { unsigned short ad = addr_zp(); unsigned char v = (unsigned char)(fc_rd8(ad) - 1); fc_wr8(ad, v); set_nz(v); cyc = 5; break; }
    case 0xD6: { unsigned short ad = addr_zpx(); unsigned char v = (unsigned char)(fc_rd8(ad) - 1); fc_wr8(ad, v); set_nz(v); cyc = 6; break; }
    case 0xCE: { unsigned short ad = addr_abs(); unsigned char v = (unsigned char)(fc_rd8(ad) - 1); fc_wr8(ad, v); set_nz(v); cyc = 6; break; }
    case 0xDE: { unsigned short ad = addr_abx(); unsigned char v = (unsigned char)(fc_rd8(ad) - 1); fc_wr8(ad, v); set_nz(v); cyc = 7; break; }
    case 0xCA: { fc.x--; set_nz(fc.x); cyc = 2; break; }
    case 0x88: { fc.y--; set_nz(fc.y); cyc = 2; break; }
    case 0xE8: { fc.x++; set_nz(fc.x); cyc = 2; break; }
    case 0xC8: { fc.y++; set_nz(fc.y); cyc = 2; break; }

    /* ---- shifts: accumulator forms ---- */
    case 0x0A: { fc.p = (unsigned char)((fc.p & ~FLAG_C) | (fc.a >> 7)); fc.a = (unsigned char)(fc.a << 1); set_nz(fc.a); cyc = 2; break; }
    case 0x4A: { fc.p = (unsigned char)((fc.p & ~FLAG_C) | (fc.a & 1)); fc.a = (unsigned char)(fc.a >> 1); set_nz(fc.a); cyc = 2; break; }
    case 0x2A: { unsigned char c = fc.p & FLAG_C; fc.p = (unsigned char)((fc.p & ~FLAG_C) | (fc.a >> 7)); fc.a = (unsigned char)((fc.a << 1) | c); set_nz(fc.a); cyc = 2; break; }
    case 0x6A: { unsigned char c = fc.p & FLAG_C; fc.p = (unsigned char)((fc.p & ~FLAG_C) | (fc.a & 1)); fc.a = (unsigned char)((fc.a >> 1) | (c << 7)); set_nz(fc.a); cyc = 2; break; }

    /* ---- shifts: memory forms ---- */
    case 0x06: { rmw_asl(addr_zp()); cyc = 5; break; }
    case 0x16: { rmw_asl(addr_zpx()); cyc = 6; break; }
    case 0x0E: { rmw_asl(addr_abs()); cyc = 6; break; }
    case 0x1E: { rmw_asl(addr_abx()); cyc = 7; break; }
    case 0x46: { rmw_lsr(addr_zp()); cyc = 5; break; }
    case 0x56: { rmw_lsr(addr_zpx()); cyc = 6; break; }
    case 0x4E: { rmw_lsr(addr_abs()); cyc = 6; break; }
    case 0x5E: { rmw_lsr(addr_abx()); cyc = 7; break; }
    case 0x26: { rmw_rol(addr_zp()); cyc = 5; break; }
    case 0x36: { rmw_rol(addr_zpx()); cyc = 6; break; }
    case 0x2E: { rmw_rol(addr_abs()); cyc = 6; break; }
    case 0x3E: { rmw_rol(addr_abx()); cyc = 7; break; }
    case 0x66: { rmw_ror(addr_zp()); cyc = 5; break; }
    case 0x76: { rmw_ror(addr_zpx()); cyc = 6; break; }
    case 0x6E: { rmw_ror(addr_abs()); cyc = 6; break; }
    case 0x7E: { rmw_ror(addr_abx()); cyc = 7; break; }

    /* ---- jumps/branches ---- */
    case 0x4C: { fc.pc = addr_abs(); cyc = 3; break; }
    case 0x6C: { fc.pc = addr_ind(); cyc = 5; break; }
    case 0x90: { cyc = 2u + branch(!(fc.p & FLAG_C)); break; }
    case 0xB0: { cyc = 2u + branch(fc.p & FLAG_C); break; }
    case 0xF0: { cyc = 2u + branch(fc.p & FLAG_Z); break; }
    case 0xD0: { cyc = 2u + branch(!(fc.p & FLAG_Z)); break; }
    case 0x30: { cyc = 2u + branch(fc.p & FLAG_N); break; }
    case 0x10: { cyc = 2u + branch(!(fc.p & FLAG_N)); break; }
    case 0x50: { cyc = 2u + branch(!(fc.p & FLAG_V)); break; }
    case 0x70: { cyc = 2u + branch(fc.p & FLAG_V); break; }

    /* ---- status ---- */
    case 0x18: { fc.p &= (unsigned char)~FLAG_C; cyc = 2; break; }
    case 0x38: { fc.p |= FLAG_C; cyc = 2; break; }
    case 0x58: { fc.p &= (unsigned char)~FLAG_I; cyc = 2; break; }
    case 0x78: { fc.p |= FLAG_I; cyc = 2; break; }
    case 0xB8: { fc.p &= (unsigned char)~FLAG_V; cyc = 2; break; }
    case 0xD8: { fc.p &= (unsigned char)~FLAG_D; cyc = 2; break; }
    case 0xF8: { fc.p |= FLAG_D; cyc = 2; break; }

    /* ---- NOPs ---- */
    case 0xEA: case 0x1A: case 0x3A: case 0x5A: case 0x7A:
    case 0xDA: case 0xFA: { cyc = 2; break; }
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: { (void)fetch(); cyc = 2; break; }
    case 0x04: case 0x44: case 0x64: { (void)addr_zp(); cyc = 3; break; }
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
    { (void)addr_zpx(); cyc = 4; break; }
    case 0x0C: { (void)addr_abs(); cyc = 4; break; }
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
    { (void)addr_abx(); cyc = 4; break; }

    /* ---- KIL/JAM: treated as 1-byte NOPs ---- */
    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
    case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
    { cyc = 2; break; }

    /* ---- unofficial: SLO (ASL + ORA) ---- */
    case 0x03: { rmw_slo(addr_indx()); cyc = 8; break; }
    case 0x07: { rmw_slo(addr_zp()); cyc = 5; break; }
    case 0x0F: { rmw_slo(addr_abs()); cyc = 6; break; }
    case 0x13: { rmw_slo(addr_indy()); cyc = 8; break; }
    case 0x17: { rmw_slo(addr_zpx()); cyc = 6; break; }
    case 0x1B: { rmw_slo(addr_aby()); cyc = 7; break; }
    case 0x1F: { rmw_slo(addr_abx()); cyc = 7; break; }

    /* ---- unofficial: RLA (ROL + AND) ---- */
    case 0x23: { rmw_rla(addr_indx()); cyc = 8; break; }
    case 0x27: { rmw_rla(addr_zp()); cyc = 5; break; }
    case 0x2F: { rmw_rla(addr_abs()); cyc = 6; break; }
    case 0x33: { rmw_rla(addr_indy()); cyc = 8; break; }
    case 0x37: { rmw_rla(addr_zpx()); cyc = 6; break; }
    case 0x3B: { rmw_rla(addr_aby()); cyc = 7; break; }
    case 0x3F: { rmw_rla(addr_abx()); cyc = 7; break; }

    /* ---- unofficial: SRE (LSR + EOR) ---- */
    case 0x43: { rmw_sre(addr_indx()); cyc = 8; break; }
    case 0x47: { rmw_sre(addr_zp()); cyc = 5; break; }
    case 0x4F: { rmw_sre(addr_abs()); cyc = 6; break; }
    case 0x53: { rmw_sre(addr_indy()); cyc = 8; break; }
    case 0x57: { rmw_sre(addr_zpx()); cyc = 6; break; }
    case 0x5B: { rmw_sre(addr_aby()); cyc = 7; break; }
    case 0x5F: { rmw_sre(addr_abx()); cyc = 7; break; }

    /* ---- unofficial: RRA (ROR + ADC) ---- */
    case 0x63: { rmw_rra(addr_indx()); cyc = 8; break; }
    case 0x67: { rmw_rra(addr_zp()); cyc = 5; break; }
    case 0x6F: { rmw_rra(addr_abs()); cyc = 6; break; }
    case 0x73: { rmw_rra(addr_indy()); cyc = 8; break; }
    case 0x77: { rmw_rra(addr_zpx()); cyc = 6; break; }
    case 0x7B: { rmw_rra(addr_aby()); cyc = 7; break; }
    case 0x7F: { rmw_rra(addr_abx()); cyc = 7; break; }

    /* ---- unofficial: SAX (store A & X) ---- */
    case 0x83: { fc_wr8(addr_indx(), (unsigned char)(fc.a & fc.x)); cyc = 6; break; }
    case 0x87: { fc_wr8(addr_zp(), (unsigned char)(fc.a & fc.x)); cyc = 3; break; }
    case 0x8F: { fc_wr8(addr_abs(), (unsigned char)(fc.a & fc.x)); cyc = 4; break; }
    case 0x97: { fc_wr8(addr_zpy(), (unsigned char)(fc.a & fc.x)); cyc = 4; break; }

    /* ---- unofficial: LAX (LDA + LDX) ---- */
    case 0xA3: { fc.a = fc.x = fc_rd8(addr_indx()); set_nz(fc.a); cyc = 6; break; }
    case 0xA7: { fc.a = fc.x = fc_rd8(addr_zp()); set_nz(fc.a); cyc = 3; break; }
    case 0xAB: { fc.a = fc.x = fetch(); set_nz(fc.a); cyc = 2; break; }
    case 0xAF: { fc.a = fc.x = fc_rd8(addr_abs()); set_nz(fc.a); cyc = 4; break; }
    case 0xB3: { fc.a = fc.x = fc_rd8(addr_indy()); set_nz(fc.a); cyc = 5; break; }
    case 0xB7: { fc.a = fc.x = fc_rd8(addr_zpy()); set_nz(fc.a); cyc = 4; break; }
    case 0xBF: { fc.a = fc.x = fc_rd8(addr_aby()); set_nz(fc.a); cyc = 4; break; }

    /* ---- unofficial: immediate bit tricks ---- */
    case 0x0B: case 0x2B: { fc.a &= fetch(); fc.p = (unsigned char)((fc.p & ~FLAG_C) | (fc.a & FLAG_N)); set_nz(fc.a); cyc = 2; break; }  /* ANC */
    case 0x4B: { fc.a &= fetch(); fc.p = (unsigned char)((fc.p & ~FLAG_C) | (fc.a & 1)); fc.a = (unsigned char)(fc.a >> 1); set_nz(fc.a); cyc = 2; break; }  /* ALR */
    case 0x6B: { unsigned char c = fc.p & FLAG_C; unsigned char r; fc.a &= fetch(); r = (unsigned char)((fc.a >> 1) | (c << 7)); fc.a = r; fc.p = (unsigned char)((fc.p & ~(FLAG_C | FLAG_V)) | ((r >> 6) & 1) | (((r >> 6) ^ (r >> 5)) & 1) | (r & FLAG_N) | ((r == 0) ? FLAG_Z : 0)); cyc = 2; break; }  /* ARR */
    case 0x8B: { fc.a = (unsigned char)((fc.a | 0xEEu) & fc.x & fetch()); set_nz(fc.a); cyc = 2; break; }  /* XAA (unstable) */
    case 0xCB: { unsigned char t = (unsigned char)(fc.a & fc.x); unsigned char imm = fetch(); unsigned char v = (unsigned char)(t - imm); fc.x = v; fc.p = (unsigned char)((fc.p & ~FLAG_C) | (t >= imm ? FLAG_C : 0)); set_nz(v); cyc = 2; break; }  /* SBX */

    /* ---- unofficial: LAS (AND with S) ---- */
    case 0xBB: { fc.sp &= fc_rd8(addr_aby()); fc.a = fc.x = fc.sp; set_nz(fc.a); cyc = 4; break; }

    /* ---- unofficial: SHX / SHY / AHX / TAS (high-byte AND approximations) ---- */
    case 0x9E: { unsigned short ad = addr_aby(); fc_wr8(ad, (unsigned char)(fc.x & ((unsigned char)(ad >> 8) + 1))); cyc = 5; break; }
    case 0x9C: { unsigned short ad = addr_abx(); fc_wr8(ad, (unsigned char)(fc.y & ((unsigned char)(ad >> 8) + 1))); cyc = 5; break; }
    case 0x9F: { unsigned short ad = addr_aby(); fc_wr8(ad, (unsigned char)((fc.a & fc.x) & ((unsigned char)(ad >> 8) + 1))); cyc = 5; break; }
    case 0x93: { unsigned short ad = addr_indy(); fc_wr8(ad, (unsigned char)((fc.a & fc.x) & ((unsigned char)(ad >> 8) + 1))); cyc = 6; break; }
    case 0x9B: { unsigned short ad = addr_aby(); fc.sp = (unsigned char)(fc.a & fc.x); fc_wr8(ad, (unsigned char)(fc.sp & ((unsigned char)(ad >> 8) + 1))); cyc = 5; break; }

    /* ---- unofficial: DCP (DEC + CMP) ---- */
    case 0xC3: { rmw_dcp(addr_indx()); cyc = 8; break; }
    case 0xC7: { rmw_dcp(addr_zp()); cyc = 5; break; }
    case 0xCF: { rmw_dcp(addr_abs()); cyc = 6; break; }
    case 0xD3: { rmw_dcp(addr_indy()); cyc = 8; break; }
    case 0xD7: { rmw_dcp(addr_zpx()); cyc = 6; break; }
    case 0xDB: { rmw_dcp(addr_aby()); cyc = 7; break; }
    case 0xDF: { rmw_dcp(addr_abx()); cyc = 7; break; }

    /* ---- unofficial: ISC/ISB (INC + SBC) ---- */
    case 0xE3: { rmw_isc(addr_indx()); cyc = 8; break; }
    case 0xE7: { rmw_isc(addr_zp()); cyc = 5; break; }
    case 0xEF: { rmw_isc(addr_abs()); cyc = 6; break; }
    case 0xF3: { rmw_isc(addr_indy()); cyc = 8; break; }
    case 0xF7: { rmw_isc(addr_zpx()); cyc = 6; break; }
    case 0xFB: { rmw_isc(addr_aby()); cyc = 7; break; }
    case 0xFF: { rmw_isc(addr_abx()); cyc = 7; break; }

    default: { (void)fetch(); cyc = 2; break; }  /* unreachable safety net */
    }
    return cyc;
}

void fc_cpu_reset(void) {
    fc.a = 0; fc.x = 0; fc.y = 0;
    fc.sp = 0xFD;
    fc.p = FLAG_I;
    fc.pc = (unsigned short)(fc_rd8(0xFFFCu) | ((unsigned short)fc_rd8(0xFFFDu) << 8));
    fc.cycles = 0;
    fc.nmi_count = 0;
    fc.nmi_pending = 0;
    fc.irq_pending = 0;
    fc.running = 1;
}

void fc_cpu_nmi(void) {
    fc.nmi_pending = 1;
}

/* Service a pending NMI between instructions.  Returns 1 when taken.
   (The old batch runner did this inside fc_cpu_run; the per-instruction
   loop in fc.c must call this explicitly every iteration.) */
int fc_cpu_take_nmi(void) {
    if (!fc.nmi_pending)
        return 0;
    fc.nmi_pending = 0;
    fc.nmi_count++;
    push((unsigned char)(fc.pc >> 8));
    push((unsigned char)fc.pc);
    push((unsigned char)(fc.p & ~FLAG_B));
    fc.p |= FLAG_I;
    fc.pc = (unsigned short)(fc_rd8(0xFFFAu) |
                             ((unsigned short)fc_rd8(0xFFFBu) << 8));
    fc.cycles += 7;
    return 1;
}

/* Level-sensitive IRQ between instructions, gated by the I flag. */
int fc_cpu_take_irq(void) {
    if (!fc.irq_pending || (fc.p & FLAG_I))
        return 0;
    fc.irq_pending = 0;
    push((unsigned char)(fc.pc >> 8));
    push((unsigned char)fc.pc);
    push((unsigned char)(fc.p & ~FLAG_B));
    fc.p |= FLAG_I;
    fc.pc = (unsigned short)(fc_rd8(0xFFFEu) |
                             ((unsigned short)fc_rd8(0xFFFFu) << 8));
    fc.cycles += 7;
    return 1;
}

void fc_cpu_run(unsigned int cycles) {
    /* audio polling: the SB16 stops its DMA at every block end, so the
       next block has to be chained within a fraction of a millisecond or
       the boundary is heard as a gap.  One frame (16-20 ms) of latency
       was 20 ms of silence per block -- the chopped, unlistenable output.
       ~2048 cycles is about 1.1 ms of NES time and costs two port reads. */
    unsigned int poll_acc = 0;

    /* caller owns fc.cycles: it resets it at the start of each frame and
       may split the frame budget across several fc_cpu_run() calls so
       mid-frame PPU events (sprite-0 hit) can be raised at the right
       cycle count */

    /* pending NMI runs before the first instruction of the frame */
    if (fc.nmi_pending) {
        fc.nmi_pending = 0;
        fc.nmi_count++;
        push((unsigned char)(fc.pc >> 8));
        push((unsigned char)fc.pc);
        push((unsigned char)(fc.p & ~FLAG_B));
        fc.p |= FLAG_I;
        fc.pc = (unsigned short)(fc_rd8(0xFFFAu) | ((unsigned short)fc_rd8(0xFFFBu) << 8));
        fc.cycles += 7;
    }

    while (fc.cycles < cycles && fc.running) {
        /* IRQ is level-sensitive: honored between instructions while
           the line is high and I is clear. NMI (serviced above) has
           priority when both are pending. The only NROM source is the
           APU frame counter, which is not wired yet; SMB1 runs 5-step
           mode ($4017 = 0x40) and never asserts it. */
        if (fc.irq_pending && !(fc.p & FLAG_I)) {
            fc.irq_pending = 0;
            push((unsigned char)(fc.pc >> 8));
            push((unsigned char)fc.pc);
            push((unsigned char)(fc.p & ~FLAG_B));
            fc.p |= FLAG_I;
            fc.pc = (unsigned short)(fc_rd8(0xFFFEu) |
                                     ((unsigned short)fc_rd8(0xFFFFu) << 8));
            fc.cycles += 7;
        }
        fc.cycles += fc_cpu_step();
        poll_acc += 8u;
        if (poll_acc >= 2048u) {
            poll_acc = 0;
            apu_poll();
        }
    }
    if (fc.cycles >= cycles)
        fc.cycles -= cycles;            /* carry into the next frame */
}
