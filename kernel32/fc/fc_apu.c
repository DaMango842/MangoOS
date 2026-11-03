// kernel32/fc/fc_apu.c - 2A03 APU: Pulse 1 (minimal for SMB1 music)
#include <stdbool.h>
#include "fc.h"
#include "sound/sound.h"

#define PULSE1_DUTY_TABLE_SIZE 8
static const uint8_t pulse_duty[4][PULSE1_DUTY_TABLE_SIZE] = {
    {0,1,0,0,0,0,0,0},  // 12.5%
    {0,1,1,0,0,0,0,0},  // 25%
    {0,1,1,1,1,0,0,0},  // 50%
    {1,1,1,1,1,1,0,0},  // 75%
};

/* Pulse 1 phase-step numerator: CPU (1789773 Hz) * 2^23 / 44100, folded
   to a 32-bit constant at COMPILE time.  The 64-bit divide must NOT run
   at runtime -- that would emit a __udivdi3 call, and this -nostdlib
   kernel does not link libgcc (the previous (uint64_t) expression broke
   the link).  Value ~340446805, fits uint32.  See apu_render_samples(). */
#define PULSE1_STEP_NUM ((uint32_t)((1789773ULL << 23) / 44100ULL))

struct pulse_state {
    uint16_t period;        // 11-bit (0x000-0x7FF)
    uint8_t duty;           // 0-3
    uint8_t length_ctr;     // 5-bit
    uint8_t envelope_vol;   // 4-bit current volume
    uint8_t envelope_div;   // envelope divider period
    uint8_t envelope_cnt;   // envelope counter
    uint8_t start_flag;     // envelope start flag
    bool constant_vol;      // constant volume / envelope disable
    bool length_halt;       // length counter halt (constant_vol doubles as this)
    uint8_t sweep_shift;    // sweep shift count
    uint8_t sweep_period;   // sweep divider period
    uint8_t sweep_cnt;      // sweep divider counter
    bool sweep_enable;      // sweep enable
    bool sweep_negate;      // sweep negate (complement)
    bool sweep_reload;      // sweep reload flag
    uint32_t phase_acc;     // phase accumulator (24.8 fixed point)
    int16_t output;         // current output (-1..1 scaled)
};

/* ---- diagnostics ----------------------------------------------------
   apu.c's once-per-second serial line needs to prove WHERE a wrong pitch
   comes from.  It compares the frequency this renderer actually produced
   (edges/2 per second of audio) against the frequency the period register
   implies (CPU/(16*(period+1))).  Matching numbers mean the 2A03 side is
   in tune and any detune must come from playback.  The note-trigger count
   doubles as a tempo probe: a melody at ~7 notes/s looks nothing like one
   stalled at 0.5 notes/s. */
static unsigned dbg_edges;              /* duty transitions, last render */
static unsigned dbg_edges_samples;      /* samples the edges cover */
static unsigned dbg_triggers;           /* $4003 writes (note starts) */

static struct pulse_state pulse1;

/* ---- frame counter / IRQ ---------------------------------------------
   $4017 picks the sequence length and the IRQ inhibit; $4015 bit 6
   reports the frame IRQ and reading it clears the flag.  With the PPU NMI
   this is the only interrupt a NROM cartridge has, and it is what
   AccuracyCoin's sync code waits for while it runs with NMI disabled.
   SMB1 always writes $4017 with the inhibit bit set, so leaving this
   unwired never affected it. */
static unsigned char frame_irq;         /* $4015 bit 6, cleared on read */
static unsigned char frame_inhibit;     /* $4017 bit 6 */
static unsigned char frame_mode5;       /* $4017 bit 7 */
static unsigned char frame_step;        /* quarters since the sequence began */

static const uint8_t length_table[32] = {
    10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
    12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30
};

static void pulse1_write(uint16_t addr, uint8_t v) {
    switch (addr & 3) {
    case 0: // $4000: duty, envelope
        pulse1.duty = (v >> 6) & 3;
        pulse1.length_halt = pulse1.constant_vol = (v & 0x20) != 0;
        pulse1.envelope_div = v & 0x0F;
        pulse1.envelope_vol = 0xF;
        pulse1.start_flag = 1;
        break;
    case 1: // $4001: sweep
        pulse1.sweep_enable = (v & 0x80) != 0;
        pulse1.sweep_period = (v >> 4) & 7;
        pulse1.sweep_negate = (v & 0x08) != 0;
        pulse1.sweep_shift = v & 7;
        pulse1.sweep_reload = 1;
        break;
    case 2: // $4002: timer low
        pulse1.period = (pulse1.period & 0x700) | v;
        break;
    case 3: // $4003: length + timer high
        dbg_triggers++;
        pulse1.length_ctr = length_table[v >> 3];
        pulse1.period = (pulse1.period & 0xFF) | ((v & 7) << 8);
        pulse1.envelope_vol = 0xF;
        pulse1.start_flag = 1;
        break;
    }
}

static uint8_t pulse1_read(uint16_t addr) {
    (void)addr;
    return 0;
}

static void envelope_clock(void) {
    if (pulse1.start_flag) {
        pulse1.envelope_vol = 0xF;
        pulse1.envelope_cnt = pulse1.envelope_div;
        pulse1.start_flag = 0;
    } else if (pulse1.envelope_cnt == 0) {
        pulse1.envelope_cnt = pulse1.envelope_div;
        if (pulse1.envelope_vol > 0)
            pulse1.envelope_vol--;
    } else {
        pulse1.envelope_cnt--;
    }
}

static void length_clock(void) {
    if (!pulse1.length_halt && pulse1.length_ctr > 0)
        pulse1.length_ctr--;
}

static void sweep_clock(void) {
    if (pulse1.sweep_reload) {
        pulse1.sweep_cnt = pulse1.sweep_period;
        pulse1.sweep_reload = 0;
    } else if (pulse1.sweep_cnt == 0) {
        pulse1.sweep_cnt = pulse1.sweep_period;
        if (pulse1.sweep_enable && pulse1.sweep_shift > 0 && pulse1.period >= 8 && pulse1.period <= 0x7FF) {
            uint16_t change = pulse1.period >> pulse1.sweep_shift;
            if (pulse1.sweep_negate)
                change = -change;
            uint16_t new_period = (int16_t)pulse1.period + (int16_t)change;
            if (new_period >= 8 && new_period <= 0x7FF)
                pulse1.period = new_period;
        }
    } else {
        pulse1.sweep_cnt--;
    }
}

static void pulse1_clock_quarter_frame(void) {
    envelope_clock();
}

static void pulse1_clock_half_frame(void) {
    length_clock();
    sweep_clock();
}

void apu_wr8(uint16_t addr, uint8_t v) {
    switch (addr) {
    case 0x4000: case 0x4001: case 0x4002: case 0x4003:
        pulse1_write(addr, v);
        break;

    case 0x4015:
        /* channel enables: clearing bit 0 silences pulse 1 immediately
           (the length counter is zeroed, not just halted) */
        if (!(v & 1u))
            pulse1.length_ctr = 0;
        break;

    case 0x4017:
        frame_mode5 = (v >> 7) & 1u;
        frame_inhibit = (v >> 6) & 1u;
        frame_step = 0;
        if (frame_inhibit) {
            frame_irq = 0;               /* setting bit 6 clears the flag */
            fc.irq_pending = 0;
        }
        if (frame_mode5 && !frame_inhibit) {
            /* documented: this write clocks the sequence immediately and
               suppresses that step's IRQ */
            apu_frame_half();
        }
        break;

    default:
        break;
    }
}

uint8_t apu_rd8(uint16_t addr) {
    if ((addr & 0x1Fu) == 0x15u) {       /* $4015 status: reading clears
                                            the frame IRQ flag */
        uint8_t v = 0;

        if (pulse1.length_ctr > 0)
            v |= 0x01u;
        if (frame_irq) {
            v |= 0x40u;
            frame_irq = 0;
            fc.irq_pending = 0;
        }
        return v;
    }
    return 0;
}

void apu_frame_quarter(void) {
    pulse1_clock_quarter_frame();
    frame_step++;
    if (frame_mode5) {
        if (frame_step >= 5u)
            frame_step = 0;
        return;                          /* 5-step mode generates no IRQ */
    }
    if (frame_step >= 4u) {
        frame_step = 0;
        if (!frame_inhibit) {
            /* 4-step mode: the only IRQ source a NROM cart has.  It is
               LEVEL-sensitive and stays asserted until $4015 is read. */
            frame_irq = 1;
            fc.irq_pending = 1;
        }
    }
}

void apu_frame_half(void) {
    pulse1_clock_half_frame();
}

/* Render `samples` into buf.  period/length/envelope only change on the
   frame-counter clocks, which run in apu_render_frame BEFORE this call,
   so they are constant across the whole buffer: hoist the divide and the
   silence guards out of the loop (720 divides/frame -> 1).

   Phase step for freq = CPU / (16*(period+1)) with an 8-step duty table
   and a 2^27 phase period:
       step = CPU * 2^27 / (16*(period+1)*44100)
            = PULSE1_STEP_NUM / (period+1)          (see the #define)
   The compile-time numerator keeps the runtime path a single 32-bit
   divide.  The old 32-bit (0x1000000u * 1789773u) overflowed by four
   orders of magnitude and wrapped to a ~0.3 Hz inaudible square wave. */
int apu_render_samples(int16_t *buf, int samples) {
    uint32_t step;
    uint8_t vol;
    int16_t amp;
    int i, edges = 0;
    int16_t s, prev = 0;

    dbg_edges = 0;
    dbg_edges_samples = 0;
    if (pulse1.length_ctr == 0 || pulse1.period < 8) {
        for (i = 0; i < samples; i++)
            buf[i] = 0;
        return samples;
    }
    step = PULSE1_STEP_NUM / (uint32_t)(pulse1.period + 1u);
    vol = pulse1.constant_vol ? pulse1.envelope_div : pulse1.envelope_vol;
    /* bipolar square centered on 0.  The old unipolar 0..+7680 carried a
       large DC offset (~+3840): it biased the output and made the step to
       silence at each single-shot block boundary click.  +/-(vol<<8) keeps
       the same peak-to-peak loudness with zero DC. */
    amp = (int16_t)(vol << 8);
    for (i = 0; i < samples; i++) {
        pulse1.phase_acc += step;
        s = pulse_duty[pulse1.duty][(pulse1.phase_acc >> 24) & 7]
            ? amp : (int16_t)-amp;
        if (i && s != prev)
            edges++;
        prev = s;
        buf[i] = s;
    }
    dbg_edges = (unsigned)edges;
    dbg_edges_samples = (unsigned)samples;
    return samples;
}

/* ---- diagnostic accessors for apu.c's once-per-second serial line ---- */
unsigned apu_dbg_period(void) { return pulse1.period; }
unsigned apu_dbg_duty(void)   { return pulse1.duty; }
unsigned apu_dbg_len(void)    { return pulse1.length_ctr; }
unsigned apu_dbg_vol(void) {
    return pulse1.constant_vol ? pulse1.envelope_div : pulse1.envelope_vol;
}

/* frequency the last render actually produced: (edges/2) cycles over
   (samples/44100) seconds.  0 when the last render was silent. */
unsigned apu_dbg_meas_hz(void) {
    if (!dbg_edges_samples)
        return 0;
    return (unsigned)(((unsigned long)(dbg_edges / 2u) * 44100ul)
                      / dbg_edges_samples);
}

unsigned apu_dbg_triggers(void) {
    unsigned n = dbg_triggers;

    dbg_triggers = 0;
    return n;
}