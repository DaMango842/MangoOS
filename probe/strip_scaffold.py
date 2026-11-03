"""Remove the temporary measurement scaffolding from fc_ppu.c:
  - FC_MONOLITHIC block + eval_monolithic()
  - FC_LEGACY_DOTLOOP branch in the dot loop
Keeps the split FSM and the FC_TRACE_EVAL oracle."""
import io, re, sys

P = r"D:\Project\MangoOS\kernel32\fc\fc_ppu.c"
s = io.open(P, encoding="utf-8", errors="surrogateescape").read()
orig = s

# 1. drop the monolithic reference block (comment .. #endif before dispatcher)
s = re.sub(
    r"/\* =+ monolithic reference FSM =+.*?#endif\s*\n\n(?=/\* Dispatcher for a single dot)",
    "", s, flags=re.S)

# 2. drop the FC_LEGACY_DOTLOOP branch inside the loop
s = s.replace(
"""#ifdef FC_LEGACY_DOTLOOP
            eval_dot_inline(fc.ppu_scanline, fc.ppu_dot + 1u);
#else
""", "")
s = s.replace(
"""                eval_tail_phase(dot);
#endif
""", """                eval_tail_phase(dot);
""")

# 3. drop the FC_MONOLITHIC loop variant, keep the split one
s = re.sub(
    r"#ifdef FC_MONOLITHIC\n"
    r"        if \(rendering && \(fc\.ppu_scanline < 240u \|\| fc\.ppu_scanline == 261u\)\)\n"
    r"            eval_monolithic\(fc\.ppu_scanline, fc\.ppu_dot \+ 1u\);\n"
    r"        else\n"
    r"            fc\.eval_state = 0;\n"
    r"#else\n", "", s)
s = s.replace("""            fc.eval_state = 0;
        }
#endif
    }
""", """            fc.eval_state = 0;
        }
    }
""")

# 4. tidy the loop comment (no longer mentions a legacy switch)
s = s.replace(
"""    /* The loop is deliberately UNCHANGED from the original: same dot
       counter, same wrap, same `fc.ppu_dot + 1u` dot numbering (which runs
       2,3,...,341,1 within a scanline - do not "fix" that to 1..341, the
       $2004 stress test in AccuracyCoin is calibrated against it).
       The only change is that the FSM dispatch is written out inline so
       the 149 dots per scanline with no work to do cost a couple of
       compares instead of a call into eval_dot_inline.part.0. */
""", """    /* The loop is deliberately UNCHANGED from the original: same dot
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
""")

io.open(P, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
print("removed", len(orig) - len(s), "chars")
for pat in ("FC_MONOLITHIC", "eval_monolithic", "FC_LEGACY_DOTLOOP"):
    print("  %-20s remaining: %d" % (pat, s.count(pat)))