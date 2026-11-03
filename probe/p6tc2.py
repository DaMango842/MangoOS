import sys

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\tc.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w, h = 800, 600


def p6(x, y):
    i = (y * w + x) * 3
    return (body[i], body[i + 1], body[i + 2])


# banner area overlaps rect1 (0,0)-(40,40); sample the 5 rects
for label, x, y in [("rect1 ff0000:", 20, 20), ("rect2 00ff00:", 70, 20),
                    ("rect3 0000ff:", 120, 20), ("rect4 ffff00:", 170, 20),
                    ("rect5 ffffff:", 220, 20)]:
    print(label, p6(x, y))