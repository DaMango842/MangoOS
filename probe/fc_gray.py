import sys
from PIL import Image

img = Image.open(sys.argv[1]).convert("RGB")
px = img.load()
w, h = img.size
gray = (0x7C, 0x7C, 0x7C)
rows = {}
total = 0
minx, miny, maxx, maxy = 10**9, 10**9, -1, -1
for y in range(60, 540):
    for x in range(144, 656):
        if px[x, y] == gray:
            total += 1
            rows[y // 8] = rows.get(y // 8, 0) + 1
            minx = min(minx, x); miny = min(miny, y)
            maxx = max(maxx, x); maxy = max(maxy, y)
print("gray total:", total, "bbox:", (minx, miny, maxx, maxy))
print("row bands (screen y / 8):")
for k in sorted(rows):
    print("  band %d (%d-%d): %d" % (k, max(60, k*8), min(539, k*8+7), rows[k]))