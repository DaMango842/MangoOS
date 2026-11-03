// kernel32/fc/fc_oam.c - 修复与优化后的 OAM 核心逻辑

#include "fc.h"

#define FC_OAM_TRACE_LIMIT 64u

/* Ring of the LAST 64 OAM accesses rather than a one-shot log: a test ROM
   fills a one-shot trace long before it reaches the code under
   investigation, so what matters is what happened most recently.  Dump it
   with `fcoam` while the ROM is stuck.  Every entry carries the
   scanline/dot it landed on plus the evaluation position -- without those
   a $2004 sequence cannot be compared against the hardware reference.
   (The dot/scanline are those at the START of the instruction: the PPU
   advances after it.) */
struct fc_oam_log_ent {
    unsigned char op;        /* 'R' read, 'W' write */
    unsigned char addr;
    unsigned char val;
    unsigned char est;
    unsigned char n;
    unsigned char m;
    unsigned int line;
    unsigned int dot;
};

static struct fc_oam_log_ent fc_oam_log[FC_OAM_TRACE_LIMIT];
static unsigned int fc_oam_log_n;        /* accesses recorded so far */

static void fc_oam_trace(unsigned char op, unsigned int addr,
                        unsigned char value, unsigned char eval_state) {
    struct fc_oam_log_ent *e =
        &fc_oam_log[fc_oam_log_n & (FC_OAM_TRACE_LIMIT - 1u)];

    e->op = op;
    e->addr = (unsigned char)addr;
    e->val = value;
    e->est = eval_state;
    e->n = (unsigned char)fc.eval_n;
    e->m = fc.eval_m;
    e->line = fc.ppu_scanline;
    e->dot = fc.ppu_dot;
    fc_oam_log_n++;
}

static int oam_dec(char *b, int i, unsigned int v) {
    char t[8];
    int n = 0;

    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n)
        b[i++] = t[--n];
    return i;
}

static int oam_hex2(char *b, int i, unsigned int v) {
    static const char h[] = "0123456789ABCDEF";

    b[i++] = h[(v >> 4) & 15u];
    b[i++] = h[v & 15u];
    return i;
}

/* oldest first; wired to the `fcoam` command */
void fc_oam_dump_log(void) {
    char b[128];
    unsigned int n = fc_oam_log_n;
    unsigned int start = (n > FC_OAM_TRACE_LIMIT) ? n - FC_OAM_TRACE_LIMIT : 0u;
    unsigned int k;
    int i;

    for (k = start; k < n; k++) {
        const struct fc_oam_log_ent *e =
            &fc_oam_log[k & (FC_OAM_TRACE_LIMIT - 1u)];

        i = 0;
        b[i++] = 'O'; b[i++] = 'A'; b[i++] = 'M'; b[i++] = ' ';
        b[i++] = (char)e->op; b[i++] = ' ';
        b[i++] = 'A'; b[i++] = '=';  i = oam_hex2(b, i, e->addr);
        b[i++] = ' '; b[i++] = 'V'; b[i++] = '='; i = oam_hex2(b, i, e->val);
        b[i++] = ' '; b[i++] = 'S'; b[i++] = '='; i = oam_dec(b, i, e->est);
        b[i++] = ' '; b[i++] = 'L'; b[i++] = '='; i = oam_dec(b, i, e->line);
        b[i++] = ' '; b[i++] = 'D'; b[i++] = '='; i = oam_dec(b, i, e->dot);
        b[i++] = ' '; b[i++] = 'N'; b[i++] = '='; i = oam_dec(b, i, e->n);
        b[i++] = ' '; b[i++] = 'M'; b[i++] = '='; i = oam_dec(b, i, e->m);
        b[i++] = '\r'; b[i++] = '\n'; b[i] = '\0';
        serial_write(b);
    }
}

void fc_oam_reset(void) {
    fc_oam_log_n = 0;
    fc.eval_oam_base = 0;
    for (int i = 0; i < 32; i++)
        fc.sec_oam[i] = 0xFF;

    fc.oam_addr = 0;
    fc.sec_oam_idx = 0;
    fc.eval_state = 0; /* 0: 非渲染期 (VBlank/Disabled) */
    fc.eval_sprite = 0;
    fc.eval_byte = 0;
    fc.eval_virtual_cycle = 0;
    fc.eval_n = 0;
    fc.eval_m = 0;
    fc.sec_oam_count = 0;
    fc.overflow_check = 0;
    fc.eval_odd_cycle = 0;
    fc.sprite0_hit = 0;
    fc.sprite0_hit_row = 0;
}

void fc_oam_begin_render_scanline(void) {
    /* 硬件行为：在可见渲染线开始时，初始化为阶段 1（清空次级OAM） */
    fc.eval_state = 1; 
    for (int i = 0; i < 32; i++)
        fc.sec_oam[i] = 0xFF;
    fc.sec_oam_idx = 0;
    
    fc.eval_n = 0;
    fc.eval_m = 0;
    fc.sec_oam_count = 0;
    fc.overflow_check = 0;
    fc.eval_odd_cycle = 1;
    fc.eval_virtual_cycle = 0;
}

void fc_oam_end_render(void) {
    /* Called by fc_ppu_render() at end of frame.  MUST NOT touch shared
       state:
         - fc.oam_addr is reset by the PPU timeline itself during dots
           257-320 (see eval_dot_inline), and clearing it here overwrote
           the CPU-visible OAMADDR, so a program's $2003/$2004 sequence
           could observe 0 instead of what it wrote;
         - fc.eval_state / eval_n / eval_m are owned by the dot-driven FSM
           and resetting them here desynchronised the pointer the CPU sees
           through $2004 from the pointer the timeline is walking.
       Kept as a documented no-op to preserve the fc.h interface. */
}

unsigned char fc_oam_read_data(void) {
    unsigned char v;

    if (fc.eval_state == 2) {
        /* nesdev wiki PPU_sprite_evaluation, cycles 65-256: "On odd cycles,
           data is read from (primary) OAM.  On even cycles, data is written
           to secondary OAM (unless secondary OAM is full, in which case it
           will read the value in secondary OAM instead)."  And PPU
           registers, $2004: "Reading OAMDATA while the PPU is rendering
           will EXPOSE INTERNAL OAM ACCESSES during sprite evaluation and
           loading."

           So a $2004 read during evaluation must return the value on the
           PPU's internal OAM bus -- the byte the FSM last read from primary
           OAM (fc.eval_read, refreshed on every odd dot by eval_dot_inline)
           -- NOT a fresh read of fc.oam[pos].  The two differ on even dots
           and whenever n/m have already advanced, which is exactly the
           divergence "$2004 stress test" catches when it diffs every read
           against a hardware reference trace.  eval_read already carries
           the attribute-byte DRAM mask (applied where the FSM stores it),
           so no extra masking is needed here.  NOTE: fc_eval_advance_cycle()
           is deliberately NOT called -- the dot timeline in fc_ppu.c is the
           only thing that may advance the evaluation. */
        v = fc.eval_read;
        fc_oam_trace('R', fc.oam_addr, v, fc.eval_state);
        return v;
    }

    if (fc.eval_state == 1) {
        /* 次级 OAM 清空阶段 (dots 1-64): 硬件固定返回 0xFF */
        fc_oam_trace('R', fc.oam_addr, 0xFF, fc.eval_state);
        return 0xFF;
    }

    if (fc.eval_state == 3) {
        /* 精灵图块获取阶段 (dots 257-320): 返回次级 OAM 数据 */
        unsigned int pos = (fc.eval_n * 4u + fc.eval_m) & 0x1Fu;
        v = fc.sec_oam[pos];
        fc_oam_trace('R', fc.oam_addr, v, fc.eval_state);
        return v;
    }

    /* normal (non-rendering) read: primary OAM at OAMADDR, no increment.
       Attribute bytes (addr & 3 == 2) have bits 2-4 physically missing
       from the DRAM — they always read back as 0. */
    v = fc.oam[fc.oam_addr];
    if ((fc.oam_addr & 3u) == 2u)
        v = fc_circuit_oam_attr_mask(v);
    fc_oam_trace('R', fc.oam_addr, v, fc.eval_state);
#ifdef FC_DEBUG_EVAL
    {
        char b[128];
        int n = 0;
        n += sprintf(b + n, "NORMAL RD: state=%u addr=0x%02X val=0x%02X\r\n",
                     fc.eval_state, fc.oam_addr, v);
        serial_write(b);
    }
#endif
    return v;
}

void fc_oam_write_data(unsigned char v) {
    unsigned char addr = fc.oam_addr;

    if (fc.eval_state >= 1 && fc.eval_state <= 3) {
        /* nesdev wiki, PPU registers, $2004:
           "Writes to OAMDATA during rendering (on the pre-render line and
           the visible lines 0-239, provided either sprite or background
           rendering is enabled) do not modify values in OAM, but do
           perform a glitchy increment of OAMADDR, bumping only the high 6
           bits ... it is probably best to completely ignore writes during
           rendering."
           So: no OAM change, no row corruption -- only the sprite index
           (high 6 bits of the address) is bumped. */
        fc.oam_addr = (unsigned char)((addr + 4u) & 0xFCu);
        fc_oam_trace('W', addr, v, fc.eval_state);
        return;
    }

    /* 正常非渲染期间写入：写入并递增地址 */
    fc.oam[addr] = v;
    fc.oam_addr = (unsigned char)((addr + 1u) & 0xFFu);
    fc_oam_trace('W', addr, v, fc.eval_state);
}

void fc_oam_dma(unsigned char page) {
    unsigned int src = (unsigned int)page << 8;
    
    /* 修复：2C02 DMA 以当前的 oam_addr 为写入起点，依次递增写入 256 字节 */
    unsigned char dest_addr = fc.oam_addr;
    
    for (int i = 0; i < 0x100; i++) {
        // 修复：应调用 CPU 总线读函数（假设为 fc_cpu_read），避免 0x7FF 掩码限制卡带 RAM 的 DMA
        unsigned char data = fc_rd8((unsigned short)(src + (unsigned int)i)); 
        fc.oam[dest_addr] = data;
        dest_addr = (unsigned char)((dest_addr + 1u) & 0xFFu);
    }
    
    /* DMA 完毕后，oam_addr 实际上刚好自增了 256 次，保持原值不变 */
    fc.cycles += (page & 1u) ? 513u : 514u;
}
