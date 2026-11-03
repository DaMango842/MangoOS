import re

raw = open(r"D:\Project\MangoOS\tools\fcdbg\qemu_bench.txt", encoding="latin1").read()

evts = []
for m in re.finditer(r"\[([+-]?\d+\.\d+)\] (.*)\n", raw):
    evts.append((float(m.group(1)), m.group(2)))
stream = "".join(t for _, t in evts)

pos = 0
ts_at = []
for t, frag in evts:
    ts_at.append((pos, t))
    pos += len(frag)


def ts_of(off):
    best = None
    for p, t in ts_at:
        if p <= off:
            best = t
        else:
            break
    return best


bs = []
for m in re.finditer(r"B f=(\d+) R=(\d+) C=(\d+) P=(\d+) T=(\d+)", stream):
    bs.append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)),
               int(m.group(5)), ts_of(m.start())))
print("bench frames:", len(bs))
if bs:
    print("first B ts:", "%.3f" % bs[0][5], " last B ts:", "%.3f" % bs[-1][5])
    dts = [bs[i + 1][5] - bs[i][5] for i in range(len(bs) - 1)]
    print("frame gaps (s):", ["%.3f" % d for d in dts])
    print("host fps: %.1f" % (len(bs) / max(bs[-1][5] - bs[0][5], 1e-6)))
    for b in bs[:4]:
        print("B", b)
    print("...")
    for b in bs[-3:]:
        print("B", b)