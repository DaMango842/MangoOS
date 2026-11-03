"""Diff two FC_TRACE_EVAL_VERBOSE per-call state streams.

Columns: call scanline dot state n m done sec_pos sec_cnt eval_read
         oam_base oam_addr ppu_status checksum
"""
import io, sys

COLS = ["call", "scanline", "dot", "state", "n", "m", "done",
        "sec_pos", "sec_cnt", "eval_read", "oam_base", "oam_addr",
        "status", "checksum"]


def load(p):
    rows = []
    for ln in io.open(p, encoding="utf-8", errors="replace"):
        t = ln.strip()
        if t:
            rows.append(t.split())
    return rows


a = load(sys.argv[1])
b = load(sys.argv[2])
na, nb = sys.argv[3] if len(sys.argv) > 3 else "A", sys.argv[4] if len(sys.argv) > 4 else "B"
print("%s: %d rows, %s: %d rows" % (na, len(a), nb, len(b)))

n = min(len(a), len(b))
first = None
for i in range(n):
    if a[i] != b[i]:
        first = i
        break

if first is None:
    print("==> IDENTICAL over %d per-call records" % n)
    sys.exit(0)

print("==> DIVERGES at call %s" % a[first][0])
lo = max(0, first - 6)
for i in range(lo, min(n, first + 4)):
    mark = " <<<" if i == first else ""
    print("  call %s" % a[i][0])
    print("    %-8s %s" % (na, " ".join(a[i][1:])))
    print("    %-8s %s%s" % (nb, " ".join(b[i][1:]), mark))
    if i == first:
        for c, (x, y) in enumerate(zip(a[i], b[i])):
            if x != y:
                name = COLS[c] if c < len(COLS) else "col%d" % c
                print("      field %-9s %s -> %s" % (name, x, y))
    print()