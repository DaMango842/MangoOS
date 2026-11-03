import sys
from PIL import Image

img = Image.open(sys.argv[1]).convert("RGB")
px = img.load()
w, h = img.size

def fc(y, x):
    return px[144 + x * 2, 60 + y * 2]

sky = (0x68, 0x88, 0xFC)
red = (0xE4, 0x5C, 0x10)
green = (0x00, 0xA8, 0x00)
skin = (0xF0, 0xD0, 0xB0)
black = (0x00, 0x00, 0x00)

print("== top band (y 0-30) ==")
c = {}
for y in range(0, 30):
    for x in range(0, 256):
        p = fc(y, x)
        c[p] = c.get(p, 0) + 1
top = sorted(c.items(), key=lambda kv: -kv[1])[:6]
for p, n in top:
    print("  %s: %d" % (("#%02X%02X%02X" % p), n))

print("== ground band (y 190-230) ==")
c = {}
for y in range(190, 230):
    for x in range(0, 256):
        p = fc(y, x)
        c[p] = c.get(p, 0) + 1
top = sorted(c.items(), key=lambda kv: -kv[1])[:6]
for p, n in top:
    print("  %s: %d" % ("#%02X%02X%02X" % p, n))

print("== mario-ish pixel bbox (red|green|skin) ==")
xs, ys = [], []
for y in range(0, 240):
    for x in range(0, 256):
        p = fc(y, x)
        if p == red or p == green or p == skin:
            xs.append(x); ys.append(y)
if xs:
    print("  bbox x %d-%d y %d-%d count %d" % (min(xs), max(xs), min(ys), max(ys), len(xs)))
else:
    print("  none")