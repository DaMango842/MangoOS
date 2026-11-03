// kernel32/drivers/audio/apu.c - 2A03 APU output: renders Pulse 1 each
// frame and streams the samples into the SB16 driver as an AUTO-INIT DMA
// ring (sb16_ring_*), falling back to single-shot blocks when that never
// runs.  It deliberately bypasses sound.c: sound_play() routes to the
// active device's .write, which for sb16 is the BLOCKING sb16_play
// (busy-waits for the block to drain) -- that would stall the cooperative
// kernel on every one of the 60 fps frames.  kernel.c registers sb16
// BEFORE pcspk, so sb16 owns active_device and sb16_init ->
// sb16_set_freq(44100) runs.
#include "sound/sound.h"
#include "fc/fc.h"
#include "../../serial.h"
#include <stdint.h>

#define APU_SAMPLE_RATE 44100
/* One DMA block PER FRAME restarts the audio backend voice 60 times a
   second: QEMU stops the stream at every 0xB0 block end (control(s, 0))
   and dsound then reports "Voice is not playing".  On a sustained note
   that is audible as a warble.  So we accumulate samples and hand the
   card a longer block exactly when the previous one has been consumed.
   That needs room for up to ~100 ms of audio, and the clamp must never
   cut samples the frame really generated: a frame slower than 18.1 ms
   used to lose its tail, which starved the DAC (choppy, off-pitch
   sounding output on a loaded machine). */
#define APU_MAX_SAMPLES 4410             /* 100 ms at 44100 Hz */
#define APU_BUF_COUNT   4                /* rotating slots, so the block
                                            being filled is never one the
                                            DMA may still be reading */
#define APU_BLOCK_TARGET 2940            /* first block: about four frames
                                            (66 ms).  Later blocks settle
                                            at this length on their own,
                                            because each one is armed when
                                            the previous has been consumed,
                                            i.e. after ~its own duration. */

/* ---- streaming -------------------------------------------------------
   A single-shot 0xB0 block is stopped (and the QEMU audio backend voice
   deactivated) at every 16.6 ms block boundary, so DirectSound drained
   between frames -- 60 stop/start cycles per second.  The 0xB6 auto-init
   command instead wraps a fixed window of our memory forever and only
   raises an IRQ16 every period.

   Ring: APU_HALVES periods of APU_HALF_SAMPLES (16.67 ms at 44100 Hz).
   Each frame we render exactly the samples that frame consumed
   (samples = us * 44100 / 1e6), so production tracks the DAC and the
   pre-filled lead stays constant.  The write cursor only ever touches
   slots the 8237 has already read (lower bound: completed periods),
   never audio that is still queued for playback. */
/* Auto-init ring streaming: IMPLEMENTED BUT OFF BY DEFAULT.
   With 0xB6 the DSP holds DREQ for the whole session and never calls
   control(s, 0), so QEMU's i8257 keeps re-scheduling its DMA bottom
   half on every main-loop pass (status DREQ bit stays set + channel
   unmasked) and SB_read_DMA in auto-init pushes the full -- stale --
   s->audio_free in one go instead of one bounded block.  MangoOS froze
   with that enabled; the single-shot path below is bounded per block
   and is what the emulator ran on before.  Flip to 1 to re-test it. */
#define APU_RING_ENABLE 0

#define APU_HALF_SAMPLES 735             /* 44100 / 60 = 16.67 ms */
#define APU_HALVES       16              /* 266 ms of ring */
#define APU_RING_SAMPLES (APU_HALF_SAMPLES * APU_HALVES)
#define APU_PREFILL      (APU_RING_SAMPLES / 2)   /* 133 ms of lead */

static int16_t apu_ring[APU_RING_SAMPLES];
static unsigned apu_ring_written;        /* samples written since start */
static unsigned apu_ring_done;           /* periods the DMA has consumed */
static unsigned apu_ring_waits;          /* frames waited for the first IRQ */
static int apu_ring_state = APU_RING_ENABLE ? 0 : -1;
                                         /* 0 = idle, 1 = streaming,
                                            -1 = single-shot blocks */

/* single-shot streaming buffers (also used when the ring never runs) */
static int16_t apu_frame_buf[APU_BUF_COUNT][APU_MAX_SAMPLES];
static int apu_buf_cur;
static int apu_blk_fill;                 /* samples in the current slot */
static int apu_blk_inflight;             /* the card's DMA owns a block */
static int apu_blk_stall;                /* frames since the last completion */

static void apu_blk_arm(void);           /* defined after apu_render_frame */

/* provided by drivers/audio/sb16.c; no-op when no card is present */
extern int sb16_play_raw(const void *buffer, uint32_t size);
extern int sb16_stream_idle(void);
extern unsigned sb16_blocks_done_get(void);
extern int sb16_block_irq(void);
extern int sb16_ring_start(const void *ring, uint32_t ring_bytes,
                           uint32_t irq_bytes);
extern int sb16_ring_irqs(void);
extern void sb16_halt(void);

/* ---- temporary audio diagnostics: one serial line per ~60 frames ----
   Pinpoints why the FC stream is unlistenable instead of guessing:
     md   = 2 (auto-init ring) or 1 (single-shot blocks)
     f    = render frames elapsed (always 60 -- the window IS 60 frames)
     fps  = the real frame rate the pacer measured, so f= cannot hide a
            slow emulator: a low value here means every APU clock (frame
            counter, envelopes, note timing) runs slow as well.
     a    = DMA blocks armed in this window (~15: several frames each).
            a ~= q -> every block is consumed before the next is armed
            (no truncation, no gap); a > q -> blocks are being cut short.
     q    = DMA blocks the DAC FINISHED in this window (one IRQ16 each).
     nt   = $4003 writes (note starts) per second: ~6-8 is a normal NES
            tempo, ~1 means the music engine is being starved.
     per  = pulse1 timer period (musical notes ~50..500; 0 or huge means
            the $4000-$4003 decode or the game writes are wrong)
     exp  = the frequency that period implies, 1789773/(16*(per+1));
            hz = the frequency we actually rendered.  exp == hz proves
            the 2A03 side is in tune and any detune is on the playback
            side; exp != hz means the renderer itself is off.
     duty = 0..3, vol = 0..15, len = length counter (0 => silence path) */
extern unsigned apu_dbg_period(void);
extern unsigned apu_dbg_duty(void);
extern unsigned apu_dbg_vol(void);
extern unsigned apu_dbg_len(void);
extern unsigned apu_dbg_meas_hz(void);
extern unsigned apu_dbg_triggers(void);

static unsigned dbg_frames, dbg_armed, dbg_irqs;
static int apu_first_tick = 1;

static void dbg_putu(char *b, int *i, unsigned v) {
    char tmp[12];
    int t = 0;
    if (v == 0) { b[(*i)++] = '0'; return; }
    while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    while (t) { b[(*i)++] = tmp[--t]; }
}

static void dbg_str(char *b, int *i, const char *s) {
    while (*s) b[(*i)++] = *s++;
}

static void apu_dbg_line(void) {
    char b[128];
    int i = 0;
    unsigned per = apu_dbg_period();

    dbg_str(b, &i, "APU md="); dbg_putu(b, &i, apu_ring_state >= 0 ? 2u : 1u);
    dbg_str(b, &i, " f=");    dbg_putu(b, &i, dbg_frames);
    dbg_str(b, &i, " fps=");  dbg_putu(b, &i, fc_fps_get());
    dbg_str(b, &i, " a=");    dbg_putu(b, &i, dbg_armed);
    dbg_str(b, &i, " q=");    dbg_putu(b, &i, dbg_irqs);
    dbg_str(b, &i, " nt=");   dbg_putu(b, &i, apu_dbg_triggers());
    dbg_str(b, &i, " per=");  dbg_putu(b, &i, per);
    /* exp = what the period register implies, hz = what we rendered.
       Equal means the 2A03 side is in tune and any detune is playback. */
    dbg_str(b, &i, " exp=");
    dbg_putu(b, &i, per <= 0x7FFu
                     ? (unsigned)(1789773ul / (16ul * (per + 1ul)))
                     : 0u);
    dbg_str(b, &i, " hz=");   dbg_putu(b, &i, apu_dbg_meas_hz());
    dbg_str(b, &i, " duty="); dbg_putu(b, &i, apu_dbg_duty());
    dbg_str(b, &i, " vol=");  dbg_putu(b, &i, apu_dbg_vol());
    dbg_str(b, &i, " len=");  dbg_putu(b, &i, apu_dbg_len());
    b[i++] = '\r'; b[i++] = '\n'; b[i] = '\0';
    serial_write(b);
}

/* render into the ring at the write cursor, splitting on the wrap */
static void apu_ring_render(int samples) {
    unsigned off = apu_ring_written % (unsigned)APU_RING_SAMPLES;
    int first = APU_RING_SAMPLES - (int)off;

    if (first > samples)
        first = samples;
    apu_render_samples(apu_ring + off, first);
    if (samples > first)
        apu_render_samples(apu_ring, samples - first);
    apu_ring_written += (unsigned)samples;
}

/* returns 1 when this frame's samples went into the ring */
static int apu_ring_feed(int samples) {
    unsigned limit;

    if (apu_ring_state == 0) {
        /* prefill from the ring base, then start the DAC on it (the 8237
           address register is reprogrammed to the base every start, so
           the write cursor and the read cursor line up again) */
        apu_ring_written = 0;
        apu_ring_render(APU_PREFILL);
        if (!sb16_ring_start(apu_ring, APU_RING_SAMPLES * 2u,
                             APU_HALF_SAMPLES * 2u)) {
            apu_ring_state = -1;         /* no card: single-shot path */
            return 0;
        }
        apu_ring_state = 1;
        apu_ring_done = 0;
        apu_ring_waits = 0;
        return 1;
    }

    if (sb16_ring_irqs()) {
        apu_ring_done++;
        dbg_irqs++;
        apu_ring_waits = 0;
    }
    else if (++apu_ring_waits > 90) {    /* 1.5 s of silence: ring never ran */
        sb16_halt();
        apu_ring_state = -1;
        return 0;
    }

    /* The DMA read cursor is somewhere in [done, done+1) periods, so any
       slot below done*periods has definitely been played: staying under
       done*periods + ring size can only overwrite finished audio.  On an
       underrun (cursor past the write cursor) the DAC repeats the start
       of the ring instead of the noise a stopped card makes. */
    limit = apu_ring_done * APU_HALF_SAMPLES + APU_RING_SAMPLES;
    if (apu_ring_written + (unsigned)samples > limit)
        return 0;

    apu_ring_render(samples);
    return 1;
}

static void apu_render_frame(unsigned long frame_us) {
    /* clock the APU frame counter: quarter frames at 240 Hz, half
       frames at 120 Hz -> per 60 Hz frame: 4 envelope steps with the
       length/sweep step interleaved after the 2nd and 4th of them.
       These advance the APU state every frame regardless of streaming. */
    int i, n;

    for (i = 0; i < 4; i++) {
        apu_frame_quarter();
        if (i & 1)
            apu_frame_half();
    }

    /* size the block from the MEASURED frame time so we produce exactly
       as many 44100 Hz samples as the DAC consumed during the frame */
    if (frame_us < 1000ul)
        frame_us = 16600ul;              /* first frame / clock hiccup */
    n = (int)((frame_us * (unsigned long)APU_SAMPLE_RATE) / 1000000ul);
    if (n < 1)
        n = 1;
    if (n > APU_MAX_SAMPLES)
        n = APU_MAX_SAMPLES;

    dbg_frames++;
    if (apu_first_tick) {
        apu_first_tick = 0;
        serial_write(apu_ring_state >= 0
                     ? "APU: stream mode = auto-init ring\r\n"
                     : "APU: stream mode = single-shot blocks\r\n");
    }
    if (apu_ring_state >= 0) {
        if (apu_ring_feed(n))
            dbg_armed++;
    }
    else {
        /* Single-shot streaming, several frames per block.  QEMU STOPS the
           stream (and deactivates the audio voice) at every 0xB0 block
           end, so the next block must be armed within a fraction of a
           millisecond or the boundary becomes an audible hole: arming at
           the next frame boundary was 20 ms of dropout per block, which is
           what made the output unlistenable.  apu_poll() does the chaining
           from inside the CPU loop; this function only tops up the slot. */
        apu_poll();                        /* chain a finished block now */

        if (apu_blk_fill + n > APU_MAX_SAMPLES)
            apu_blk_arm();                 /* slot full: flush what we have */

        apu_render_samples(apu_frame_buf[apu_buf_cur] + apu_blk_fill, n);
        apu_blk_fill += n;

        if (!apu_blk_inflight && apu_blk_fill >= APU_BLOCK_TARGET)
            apu_blk_arm();                 /* a full block is ready */
        else if (apu_blk_inflight && ++apu_blk_stall > 40)
            apu_blk_arm();                 /* card never reported back */
    }

    if (dbg_frames >= 60) {
        apu_dbg_line();
        dbg_frames = 0;
        dbg_armed = 0;
        dbg_irqs = 0;
    }
}

void apu_register(void) {
}

/* hand the accumulated samples to the card and move to the next slot */
static void apu_blk_arm(void) {
    if (sb16_play_raw(apu_frame_buf[apu_buf_cur],
                      (uint32_t)apu_blk_fill * 2u) > 0)
        dbg_armed++;
    apu_buf_cur = (apu_buf_cur + 1) % APU_BUF_COUNT;
    apu_blk_fill = 0;
    apu_blk_inflight = 1;
    apu_blk_stall = 0;
}

/* Polled from the FC CPU loop roughly every millisecond (and once per
   frame).  The DSP raises IRQ16 when a block has been consumed; QEMU has
   already stopped the stream and deactivated the backend voice at that
   point, so chaining the next block here -- not at the next frame -- is
   what keeps the audio continuous.  At most one completion can be
   pending, so nothing can be missed. */
void apu_poll(void) {
    if (apu_ring_state >= 0)
        return;                            /* the ring never stops on its own */
    if (sb16_block_irq()) {
        dbg_irqs++;
        apu_blk_inflight = 0;
        apu_blk_stall = 0;
        /* Deliberately does NOT arm here: the completion IRQ arrives as
           soon as the block has been handed to the audio backend, which
           can be long before it has been played.  Arming on it made each
           block shorter than the last (a=60: one-frame blocks, i.e. 60
           voice restarts per second -- the chopped output).  It only
           clears the in-flight flag; apu_render_frame arms a fresh block
           once APU_BLOCK_TARGET samples have been rendered, and the
           backend's backlog covers the wait. */
    }
}

void apu_tick_frame(unsigned long frame_us) {
    apu_render_frame(frame_us);
}

/* called when the emulator leaves fullscreen: the auto-init ring would
   otherwise keep looping the last 266 ms forever.  Back to the start
   state so a later session begins cleanly. */
void apu_stream_stop(void) {
    if (apu_ring_state == 1)
        sb16_halt();
    apu_ring_state = APU_RING_ENABLE ? 0 : -1;
    apu_blk_fill = 0;
    apu_blk_inflight = 0;
    apu_blk_stall = 0;
}