import sys
from PIL import Image
from collections import Counter

sys.stdout.reconfigure(encoding="utf-8")
img = Image.open(r"D:\Project\MangoOS\probe\full.png").convert("RGB")
W, H = img.size
print("size", W, H)
for p in [(112, 135), (400, 300), (400, 160), (500, 400), (112, 160),
          (912, 760), (960, 540)]:
    print(p, img.getpixel(p))
c = Counter()
for y in range(0, H, 16):
    for x in range(0, W, 16):
        c[img.getpixel((x, y))] += 1
print("top:", c.most_common(5))