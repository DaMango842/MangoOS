import sys
from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")
img = Image.open(r"D:\Project\MangoOS\probe\full.png").convert("RGB")
for p in [(512, 331), (512, 285), (512, 485), (112+266+0, 160+510),
          (800, 400), (100, 700), (950, 500), (112+40, 135+40)]:
    print(p, img.getpixel(p))