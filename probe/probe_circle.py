import sys
sys.stdout.reconfigure(encoding="utf-8")
from PIL import Image
img = Image.open(r"D:\Project\MangoOS\probe\full.png").convert("RGB")
t = open(r"D:\Project\MangoOS\probe\window_pos.txt").read().split()
SX = int(t[t.index("SCREEN_X") + 1])
SY = int(t[t.index("SCREEN_Y") + 1])
CH = int(t[t.index("CLIENT_H") + 1])
OFF = 25 if CH > 600 else 0

def px(x, y):
    return img.getpixel((SX + x, SY + OFF + y))

print("window", SX, SY, "OFF", OFF)
print("near top (360..440, 254..266):")
for y in range(254, 267):
    row = [px(x, y) for x in range(360, 441, 8)]
    lit = " ".join("G" if c == (0, 255, 0) else "." for c in row)
    print(f"  y={y}: {lit}")
for x in range(390, 411):
    if px(x, 261) == (0, 255, 0):
        print(f"  green at ({x},261)")
print("near bottom (360..440, 414..426):")
for y in range(414, 427):
    row = [px(x, y) for x in range(360, 441, 8)]
    lit = " ".join("G" if c == (0, 255, 0) else "." for c in row)
    print(f"  y={y}: {lit}")
for x in range(390, 411):
    if px(x, 419) == (0, 255, 0):
        print(f"  green at ({x},419)")