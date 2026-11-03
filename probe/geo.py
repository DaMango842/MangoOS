import sys

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w, h = 800, 600

reds = []
greens = []
for y in range(0, h):
    for x in range(0, w):
        i = (y * w + x) * 3
        r, g, b = body[i + 2], body[i + 1], body[i]
        if r > 200 and g < 100 and b < 100:
            reds.append((x, y))
        elif g > 200 and r < 100 and b < 100:
            greens.append((x, y))
print("RED count:", len(reds), "GREEN count:", len(greens))
print("red x-range:", min(x for x, y in reds), max(x for x, y in reds),
      "y-range:", min(y for x, y in reds), max(y for x, y in reds))
print("green x-range:", min(x for x, y in greens), max(x for x, y in greens),
      "y-range:", min(y for x, y in greens), max(y for x, y in greens))

# split green by columns - find the rectangle edges
cols = {}
for x, y in greens:
    cols.setdefault(x, []).append(y)
xs = sorted(cols)
print("green distinct x:", len(xs))
print("green vertical segments (x, ymin, ymax):")
for x in xs:
    ys = sorted(cols[x])
    if ys[-1] - ys[0] > 50:
        print(f"  x={x} y={ys[0]}..{ys[-1]} n={len(ys)}")