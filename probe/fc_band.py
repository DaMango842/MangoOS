from PIL import Image
from collections import Counter

img = Image.open("probe/fc_e0.png").convert("RGB")
px = img.load()


def fc(y, x):
    return px[144 + x * 2, 60 + y * 2]


c = Counter()
for y in range(20, 100):
    for x in range(0, 256):
        c[fc(y, x)] += 1
for p, n in c.most_common(8):
    print("#%02X%02X%02X %d" % (p[0], p[1], p[2], n))