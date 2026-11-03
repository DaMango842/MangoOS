import io, sys

def load(p):
    return [l.strip() for l in io.open(p, encoding="utf-8", errors="replace")
            if l.strip() and len(l.strip()) == 8 and all(c in "0123456789ABCDEF" for c in l.strip())]

a = load(sys.argv[1])
b = load(sys.argv[2])
na, nb = sys.argv[3] if len(sys.argv) > 3 else "A", sys.argv[4] if len(sys.argv) > 4 else "B"
print("%s: %d samples" % (na, len(a)))
print("%s: %d samples" % (nb, len(b)))
print("A:", " ".join(a))
print("B:", " ".join(b))

# The streams are deterministic but a run may capture fewer samples than
# another (wall-clock window), so require prefix-consistency, not equal
# length.
n = min(len(a), len(b))
conflicts = [(i, a[i], b[i]) for i in range(n) if a[i] != b[i]]
if not conflicts:
    extra = abs(len(a) - len(b))
    print("==> IDENTICAL over %d aligned samples%s"
          % (n, " (+%d more in the longer run)" % extra if extra else ""))
else:
    print("==> DIFFER: %d conflicting samples" % len(conflicts))
    for i, x, y in conflicts[:5]:
        print("     sample %d: %s vs %s" % (i, x, y))