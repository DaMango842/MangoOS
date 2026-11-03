import sys
from PIL import Image

img = Image.open(sys.argv[1]).convert("RGB")
px = img.load()

def fc(y, x):
    return px[144 + x * 2, 60 + y * 2]

black = (0, 0, 0)
sky = (0x68, 0x88, 0xFC)

hud = 0
skyc = 0
for y in range(0, 24):
    for x in range(0, 256):
        if fc(y, x) == black:
            hud += 1
for y in range(60, 120):
    for x in range(0, 256):
        if fc(y, x) == sky:
            skyc += 1
print("%s: hud_black=%d/%d sky60-120=%d/%d" % (
    sys.argv[1], hud, 24 * 256, skyc, 60 * 256))