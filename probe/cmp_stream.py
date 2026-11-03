"""Compare two checksum streams emitted by probe_evaltrace.py.
Each line is "<callcount> <8 hex checksum>".
"""
import io, re, sys

pat = re.compile(r"^(\d+)\s+([0-9A-F]{8})\s*$")


def load(p):
    out = []
    for ln in io.open(p, encoding="utf-8", errors="replace"):
        m = pat.match(ln.strip())
        if m:
            out.append((int(m.group(1)), m.group(2)))
    return out


a = load(sys.argv[1])
b = load(sys.argv[2])
na = sys.argv[3] if len(sys.argv) > 3 else "A"
nb = sys.argv[4] if len(sys.argv) > 4 else "B"
print("%s: %d records, calls up to %s" % (na, len(a), a[-1][0] if a else "-"))
print("%s: %d records, calls up to %s" % (nb, len(b), b[-1][0] if b else "-"))

da, db = dict(a), dict(b)
common = sorted(set(da) & set(db))
print("shared call counts:", len(common))
if not common:
    print("==> NO OVERLAP")
    sys.exit(2)

bad = [c for c in common if da[c] != db[c]]
if not bad:
    print("==> IDENTICAL at all %d shared call counts" % len(common))
    print("    verified through call %d (~%.1f frames)"
          % (common[-1], common[-1] / 8500.0))
    sys.exit(0)

print("==> DIVERGES")
first = bad[0]
ok = [c for c in common if c < first]
print("   %d of %d shared samples disagree" % (len(bad), len(common)))
if ok:
    print("   last agreeing call count: %d" % ok[-1])
    print("   first bad  call count  : %d  (%s vs %s)" % (first, da[first], db[first]))
    print("   -> divergence lies within calls %d..%d" % (ok[-1], first))
else:
    print("   -> diverges at or before the first shared sample (%d)" % common[0])