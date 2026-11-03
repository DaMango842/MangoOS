"""Add ONLY the equivalence oracle to the pristine fc_ppu.c, leaving the
emulation logic byte-identical.  Used as the A/B reference."""
import io, sys, os

ORACLE = r'''
/* ---- equivalence oracle (reference build) ---- */
#ifdef FC_TRACE_EVAL
unsigned int fc_eval_trace;
static unsigned long fc_eval_trace_n;
static unsigned long fc_eval_trace_total;
#ifndef FC_TRACE_STEP
#define FC_TRACE_STEP 2500u
#endif
#ifndef FC_TRACE_LO
#define FC_TRACE_LO   0ul
#endif
#ifndef FC_TRACE_HI
#define FC_TRACE_HI   0xFFFFFFFFul
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
#endif
'''

ANCHOR = "#define SCANLINES_PER_FRAME    262u\n"
HOOK = "    fc.rendering_enabled = (fc.ppu_mask & 0x18u) != 0;\n}\n"

src, dst = sys.argv[1], sys.argv[2]
s = io.open(src, encoding="utf-8", errors="surrogateescape").read()

if ANCHOR not in s:
    sys.exit("anchor not found")
s = s.replace(ANCHOR, ANCHOR + ORACLE, 1)

if s.count(HOOK) != 1:
    sys.exit("hook count = %d" % s.count(HOOK))
s = s.replace(HOOK, """    fc.rendering_enabled = (fc.ppu_mask & 0x18u) != 0;
#ifdef FC_TRACE_EVAL
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
""", 1)

io.open(dst, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
print("wrote reference:", dst)