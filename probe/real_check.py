import sys
from PIL import Image
from collections import Counter

sys.stdout.reconfigure(encoding="utf-8")
img = Image.open(r"D:\Project\MangoOS\probe\full.png").convert("RGB")
W, H = img.size
print("full screen:", W, H)

# QEMU window client area is 800x625 at screen (112,135); content is 800x600,
# menu bar ~25px at top of client area
SX, SY, CW, CH = 112, 135, 800, 625
content = img.crop((SX, SY + 25, SX + 800, SY + 25 + 600))
content.save(r"D:\Project\MangoOS\probe\real_view.png")
print("content 800x600 saved as real_view.png")

c = Counter()
for y in range(0, 600, 3):
    for x in range(0, 800, 3):
        p = content.getpixel((x, y))
        if p != (0, 0, 0):
            c[p] += 1
print("distinct colors:", len(c))
for col, n in c.most_common(10):
    print(col, n)

checks = [
    ("frame top (250,100)", 250, 100, (255, 0, 0)),
    ("frame left (100,150)", 100, 150, (255, 0, 0)),
    ("frame bottom (399,299)", 399, 299, (255, 0, 0)),
    ("circle top (400,171)", 400, 171, (0, 255, 0)),
    ("circle right (480,250)", 480, 250, (0, 255, 0)),
    ("circle bottom (400,329)", 400, 329, (0, 255, 0)),
    ("circle center (400,250)", 400, 250, (0, 0, 0)),
    ("grad head (0,510)", 0, 510, (255, 0, 0)),
    ("grad (266,510)", 266, 510, (8, 255, 0)),
    ("grad (400,510)", 400, 510, (0, 0, 253)),
]
for label, x, y, exp in checks:
    got = content.getpixel((x, y))
    ok = "OK " if got == exp or (exp == (0, 0, 0) and not any(got)) else "FAIL"
    print(f"{ok} {label}: got {got} expect {exp}")