import sys
from collections import Counter

sys.stdout.reconfigure(encoding="utf-8")
w, h = 800, 600
data = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb")
assert data.readline().strip() == b"P6"
line = data.readline().strip()
while line.startswith(b"#"):
    line = data.readline().strip()
print("header:", line)
px = data.read()
c = Counter()
nonblack = 0
for i in range(w * h):
    b, g, r = px[i * 3], px[i * 3 + 1], px[i * 3 + 2]
    if b | g | r:
        c[(r, g, b)] += 1
        nonblack += 1
print("nonblack px:", nonblack, "distinct colors:", len(c))
for col, n in c.most_common(16):
    print(col, n)