import sys

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w, h = 800, 600


def p6(x, y):
    i = (y * w + x) * 3
    return tuple(body[i:i + 3])


pts = [(250, 100), (399, 100), (100, 150), (399, 150), (150, 299),
       (250, 299), (100, 100), (399, 299), (400, 171), (480, 250),
       (400, 329), (320, 250), (400, 250), (0, 510), (266, 510),
       (400, 510), (600, 510), (200, 510), (760, 250)]
for p in pts:
    print(f"P6 bytes at {p}: {p6(*p)}  (R={p6(*p)[0]}, G={p6(*p)[1]}, B={p6(*p)[2]})")