import sys

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w, h = 800, 600


def rgb(x, y):
    # P6 file stores pixels as (B, R, G) on this QEMU build
    i = (y * w + x) * 3
    b, r, g = body[i], body[i + 1], body[i + 2]
    return (r, g, b)


from collections import Counter
c = Counter()
for yy in range(0, h):
    for xx in range(0, w):
        i = (yy * w + xx) * 3
        if body[i] | body[i + 1] | body[i + 2]:
            c[rgb(xx, yy)] += 1

print("distinct colors:", len(c))
for col, n in c.most_common(14):
    print(col, n)

print()
print("geometry checks:")
for label, x, y, exp in [
    ("frame top (250,100)", 250, 100, "RED"),
    ("frame left (100,150)", 100, 150, "RED"),
    ("frame bottom (399,299)", 399, 299, "RED"),
    ("circle top (400,171)", 400, 171, "GREEN"),
    ("circle right (480,250)", 480, 250, "GREEN"),
    ("circle bottom (400,329)", 400, 329, "GREEN"),
    ("circle center (400,250)", 400, 250, "BLACK"),
    ("grad head (0,510)", 0, 510, "RED"),
    ("grad (266,510)", 266, 510, "GREEN"),
    ("grad (400,510)", 400, 510, "BLUE")]:
    print(f"  {label}: {rgb(x, y)}  expect {exp}")