from PIL import Image
from collections import Counter

img = Image.open("probe/fc_e0.png").convert("RGB")
px = img.load()


def fc(y, x):
    return px[144 + x * 2, 60 + y * 2]


for band in range(0, 30):
    c = Counter()
    for y in range(band * 8, band * 8 + 8):
        for x in range(0, 256):
            c[fc(y, x)] += 1
    top = c.most_common(3)
    s = " ".join("#%02X%02X%02X:%d" % (p[0], p[1], p[2], n) for p, n in top)
    print("y%03d-%03d %s" % (band * 8, band * 8 + 7, s))