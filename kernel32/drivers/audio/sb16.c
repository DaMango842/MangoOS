// kernel32/drivers/audio/sb16.c - Creative Sound Blaster 16 driver (QEMU semantics)
#include "sound/sound.h"
#include "../../serial.h"
#include <stdint.h>

#define SB16_BASE     0x220

#define DSP_RESET     (SB16_BASE + 0x06)
#define DSP_READ      (SB16_BASE + 0x0A)
#define DSP_WRITE     (SB16_BASE + 0x0C)
#define DSP_WR_STATUS (SB16_BASE + 0x0C)  // read: bit7=1 write buffer full
#define DSP_RD_STATUS (SB16_BASE + 0x0E)  // read: bit7=1 data available, bit1=IRQ8

#define MIXER_INDEX   (SB16_BASE + 0x04)
#define MIXER_DATA    (SB16_BASE + 0x05)

#define DMA_MASK_CH5  0xD4
#define DMA_MODE_CH5  0xD6
#define DMA_FF_CH5    0xD8
/* Page register of the slave 8237: 0x8F/0x8B/0x89/0x8A address channels
   4/5/6/7.  The old 0x89 wrote channel 6's page, so channel 5 kept
   page 0 and every transfer read from physical address 0x0xxxx instead
   of our buffer -- the DAC played low-memory garbage. */
#define DMA_PAGE_CH5  0x8B
#define DMA_ADDR_CH5  0xC4
#define DMA_COUNT_CH5 0xC6

/* 8237 mode byte: channel in bits 1-0, transfer type in 3-2, auto-init
   in bit 4.  Type 00 is VERIFY, which QEMU answers with memset()
   (i8257_dma_read_memory) -- the old 0x51 selected channel 1 in VERIFY
   mode, so nothing we ever rendered reached the DAC.  Type 10 = read =
   memory -> device is what playback needs. */
#define DMA_T_SINGLE  0x49     /* ch1, mem->dev, increment, single */
#define DMA_T_AUTO    0x59     /* same, plus auto-init: rings forever */

#define MIXER_IRQ_STATUS 0x82

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void sb16_wait_write(void) {
    /* bounded: on machines without an SB16 the status bit never clears */
    unsigned int t;
    for (t = 0; t < 200000u; t++)
        if (!(inb(DSP_WR_STATUS) & 0x80))
            return;
}

static void sb16_write_dsp(uint8_t value) {
    sb16_wait_write();
    outb(DSP_WRITE, value);
}

static uint8_t sb16_read_dsp(void) {
    unsigned int t;
    for (t = 0; t < 200000u; t++)
        if (inb(DSP_RD_STATUS) & 0x80)
            return inb(DSP_READ);
    return 0;
}

static int sb16_reset(void) {
    unsigned int t;
    outb(DSP_RESET, 0x01);
    for (t = 0; t < 256u; t++)
        __asm__ volatile("nop");
    outb(DSP_RESET, 0x00);
    /* DSP answers the reset with 0xAA; bounded wait so a machine
       without the card falls through instead of hanging forever */
    for (t = 0; t < 200000u; t++) {
        if (inb(DSP_RD_STATUS) & 0x80) {
            if (inb(DSP_READ) == 0xAA)
                return 0;
        }
    }
    return -1;
}

static uint16_t sb16_rate = 44100;

static void sb16_set_freq(uint16_t freq) {
    sb16_rate = freq;
    sb16_write_dsp(0x41);
    /* QEMU command 0x41 assembles the rate with dsp_get_hilo: because
       dsp_get_data pops in2_data in reverse, the FIRST byte we write
       becomes the HIGH byte.  The old order (low then high) byte-swapped
       44100 (0xAC44) into 0x44AC = 17580 Hz -> every block played ~2.5x
       slower/lower and drained in 40.9ms, so the idle gate dropped most
       frames (the "noisy" symptom).  Note this is the OPPOSITE of the
       0xB0 DMA family, which reads its length low-byte-first. */
    sb16_write_dsp(freq >> 8);
    sb16_write_dsp(freq & 0xFF);
}

static void mixer_write(uint8_t reg, uint8_t value) {
    outb(MIXER_INDEX, reg);
    outb(MIXER_DATA, value);
}

static uint8_t mixer_read(uint8_t reg) {
    outb(MIXER_INDEX, reg);
    return inb(MIXER_DATA);
}

/* program channel 5.  word_count is the length in 16-bit WORDS; the
   chip counts words, so the count register takes word_count-1.  */
static void dma_setup_ch5(uint32_t phys_addr, uint16_t word_count, int autoinit) {
    uint16_t count = (uint16_t)(word_count - 1);

    outb(DMA_MASK_CH5, 0x05);      // mask channel 5 (bit2=1, ch=1)
    outb(DMA_FF_CH5, 0);           // clear address/count flip-flop
    outb(DMA_MODE_CH5, autoinit ? DMA_T_AUTO : DMA_T_SINGLE);
    outb(DMA_PAGE_CH5, (phys_addr >> 16) & 0xFF);
    /* 16-bit channels count WORDS: QEMU does now[ADDR] = base[ADDR] << 1
       (i8257_init_chan with dshift=1), so the register takes phys/2.
       Writing raw bytes doubled the start address on top of the wrong
       page above. */
    outb(DMA_ADDR_CH5, (phys_addr >> 1) & 0xFF);
    outb(DMA_ADDR_CH5, (phys_addr >> 9) & 0xFF);
    outb(DMA_COUNT_CH5, count & 0xFF);
    outb(DMA_COUNT_CH5, count >> 8);
    outb(DMA_MASK_CH5, 0x01);      // unmask channel 5
}

/* arm a single-shot 16-bit DMA playback block; returns size or 0 */
static int sb16_play_arm(const void *buffer, uint32_t size) {
    uint32_t phys = (uint32_t)buffer;
    uint16_t word_count = (uint16_t)(size / 2);
    if (word_count == 0)
        return 0;
    {
        uint16_t dma_len = (uint16_t)(word_count - 1);

        dma_setup_ch5(phys, word_count, 0);

        sb16_write_dsp(0xB0);          // 16-bit DAC, single-cycle, mono.
                                       // bit3 selects ADC(1)/DAC(0): the old
                                       // 0xB8 was ADC (record), which QEMU
                                       // logs as unsupported and never starts
                                       // DMA -> no audio (incl. boot tone).
        sb16_write_dsp(0x10);          // signed mono (bit4=sign, bit5=stereo)
        sb16_write_dsp(dma_len & 0xFF);
        sb16_write_dsp(dma_len >> 8);
    }
    return size;
}

static int sb16_play(const void *buffer, uint32_t size) {
    int r = sb16_play_arm(buffer, size);
    if (r <= 0)
        return r;

    /* one-shot users (test tone) wait for completion here */
    outb(MIXER_INDEX, MIXER_IRQ_STATUS);
    for (volatile int i = 0; i < 10000000; i++) {
        uint8_t val = inb(MIXER_DATA);
        if (val & 0x02) {
            inb(SB16_BASE + 0x0F);     // acknowledge IRQ16
            return r;
        }
        __asm__ volatile("nop");
    }
    /* the block never completed: DMA is not advancing (wrong page/base,
       verify mode, or a stalled DMA channel) */
    serial_write("SB16: one-shot IRQ timeout\r\n");
    return r;
}

static void sb16_stop(void) {
    sb16_write_dsp(0xD5); // halt 16-bit DMA (QEMU: control(s,0))
    outb(DMA_MASK_CH5, 0x05); // mask channel 5
}

static int sb16_init(SoundFormat *fmt) {
    sb16_reset();
    sb16_set_freq(fmt->sample_rate);
    return 0;
}

SoundDevice sb16_device = {
    .name = "Sound Blaster 16",
    .capabilities = 0,
    .init = sb16_init,
    .write = sb16_play,
    .stop = sb16_stop,
};

static int sb16_ready;

void sb16_register(void) {
    if (sb16_reset() != 0) {
        serial_write("SB16: not present\r\n");
        return;
    }
    sb16_ready = 1;
    sound_register_device(&sb16_device);
}

void sb16_test_tone(uint16_t freq, uint16_t ms) {
    if (!sb16_ready)
        return;
    if (freq == 0 || ms == 0)
        return;
    const uint32_t sample_rate = 44100;
    uint32_t samples = (sample_rate * ms) / 1000;
    if (samples > 65535)
        samples = 65535;
    static int16_t buf[65535];
    for (uint32_t i = 0; i < samples; i++) {
        double t = (double)i / sample_rate;
        double angle = 2.0 * 3.1415926535 * freq * t;
        double x = angle;
        double x2 = x * x;
        double x3 = x2 * x;
        double x5 = x3 * x2;
        double x7 = x5 * x2;
        double s = x - x3/6.0 + x5/120.0 - x7/5040.0;
        buf[i] = (int16_t)(32767.0 * s);
    }
    sb16_play(buf, samples * 2);
    serial_write("SB16: test tone done\r\n");
}

/* streaming entry used by the APU driver.  Non-blocking: it just arms the
   next block and returns.  Busy-waiting here stalls the whole cooperative
   kernel (the audio completes on wall-clock time), which froze gameplay to
   ~seconds/frame.  The caller (apu.c) double-buffers, so the block being
   armed is never the one the DMA is currently reading */
static int sb16_stream_pending;

/* Completed 16-bit DMA blocks since the stream started.  MONOTONIC: the
   caller keeps its own last-seen value (apu.c arms the next block the
   moment this advances, so the DAC is always fed and no block is ever
   truncated by a fixed schedule). */
static unsigned sb16_blocks_done;

unsigned sb16_blocks_done_get(void) {
    return sb16_blocks_done;
}

/* 1 when a 16-bit DMA block finished since the last call, acknowledging it
   (mixer 0x82 bit1, cleared by reading port 0x22F).  The APU driver polls
   this every frame: it is what paces the next arm, so the DAC is chained
   seamlessly instead of being restarted on a fixed schedule. */
int sb16_block_irq(void) {
    if (!sb16_ready)
        return 0;
    outb(MIXER_INDEX, MIXER_IRQ_STATUS);
    if (inb(MIXER_DATA) & 0x02) {
        inb(SB16_BASE + 0x0F);
        sb16_blocks_done++;
        return 1;
    }
    return 0;
}

/* Returns 1 when no single-shot block is in flight, acknowledging the
   completion IRQ if the last one just finished.  The APU driver calls
   this BEFORE rendering so it never overwrites apu_frame_buf while the
   DMA is still reading it (that race corrupted the playing block's tail
   and caused the crackling artifact). */
int sb16_stream_idle(void) {
    if (!sb16_ready)
        return 0;
    if (!sb16_stream_pending)
        return 1;
    outb(MIXER_INDEX, MIXER_IRQ_STATUS);
    if (inb(MIXER_DATA) & 0x02) {
        inb(SB16_BASE + 0x0F);         /* ack: previous block finished */
        sb16_stream_pending = 0;
        return 1;
    }
    return 0;
}

int sb16_play_raw(const void *buffer, uint32_t size) {
    if (!sb16_ready)
        return -1;

    /* Ack a completed block's IRQ16 (mixer 0x82 bit1) before restarting.
       The APU driver now double-buffers and arms a block EVERY frame, so we
       must not drop the chunk when the previous block is still draining:
       the alternate buffer guarantees the DMA never reads memory we
       overwrite.  The old "drop if still playing" rule starved the audio
       backend -- 720-sample blocks (16.33 ms) delivered per ~16.6 ms frame
       produced ~1.6% less audio than the 44100 Hz DAC consumed, so QEMU's
       DirectSound buffer drained and underran -> the noise. */
    outb(MIXER_INDEX, MIXER_IRQ_STATUS);
    sb16_block_irq();
    sb16_stream_pending = 1;

    return sb16_play_arm(buffer, size);
}

/* ---- auto-init ring streaming ----------------------------------------
   A 0xB0 single-shot block stops the DMA at every block boundary: QEMU's
   SB_read_DMA calls control(s, 0) when left_till_irq runs out, which
   releases DREQ *and* deactivates the audio backend voice.  At 60 fps
   that is 60 stop/start cycles per second and QEMU's DirectSound buffers
   drained in between ("dsound: warning: Voice is not playing").  The
   0xB6 auto-init command never does that -- it keeps the voice active
   and simply raises an IRQ every `irq_bytes`.

   Ring length and IRQ pace are programmed separately: the 8237 walks its
   own count (the ring), the DSP block size only sets when the IRQ fires.
   The caller refills one IRQ period per completion, so QEMU never has to
   run off the end of what we rendered. */
int sb16_ring_start(const void *ring, uint32_t ring_bytes, uint32_t irq_bytes) {
    uint32_t words = ring_bytes / 2u;
    uint32_t irq_words = irq_bytes / 2u;
    uint16_t count;

    if (!sb16_ready || words == 0u || words > 0x10000ul)
        return 0;
    if (irq_words == 0u || irq_words > words)
        irq_words = words;
    count = (uint16_t)(irq_words - 1u);

    sb16_stop();                   /* drop any block still armed */
    sb16_set_freq(sb16_rate);      /* a DSP reset would have forgotten it */
    dma_setup_ch5((uint32_t)ring, (uint16_t)words, 1);

    sb16_write_dsp(0xB6);          /* 16-bit auto-init DAC, mono */
    sb16_write_dsp(0x10);          /* signed mono (bit4=sign, bit5=stereo) */
    sb16_write_dsp(count & 0xFF);
    sb16_write_dsp(count >> 8);

    outb(MIXER_INDEX, MIXER_IRQ_STATUS);
    if (inb(MIXER_DATA) & 0x02)    /* drop a stale boot-tone IRQ */
        inb(SB16_BASE + 0x0F);
    return 1;
}

/* 1 when one 16-bit DMA period completed since the previous call.  QEMU
   latches the request in mixer 0x82 bit1; port 0x22F acknowledges it
   (and lowers the PIC line).  Under-counting is safe: apu.c only uses
   this as a LOWER bound on how much of the ring was consumed. */
int sb16_ring_irqs(void) {
    return sb16_block_irq();
}

/* stop the stream for good (FC left / back to the shell): the DSP would
   otherwise loop the last ring content forever */
void sb16_halt(void) {
    if (!sb16_ready)
        return;
    sb16_stop();
}
