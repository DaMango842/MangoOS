import sys
sys.stdout.reconfigure(encoding="utf-8")
from PIL import Image
img = Image.open(r"D:\Project\MangoOS\probe\final_render.png").convert("RGB")

def chk(label, x, y, pred):
    p = img.getpixel((x, y))
    print(f"{'OK' if pred(p) else 'FAIL'} {label} @({x},{y}) = {p}")

W, H = img.size
print("size:", W, H)
chk("frame top", 240, 200, lambda p: p == (255, 0, 0))
chk("frame left", 80, 300, lambda p: p == (255, 0, 0))
chk("frame right", 399, 300, lambda p: p == (255, 0, 0))
chk("frame bottom", 240, 439, lambda p: p == (255, 0, 0))
chk("circle top", 400, 260, lambda p: p == (0, 255, 0))
chk("circle right", 480, 340, lambda p: p == (0, 255, 0))
chk("circle bottom", 400, 420, lambda p: p == (0, 255, 0))
chk("circle center", 400, 340, lambda p: p == (0, 0, 0))
chk("line head", 500, 100, lambda p: p == (255, 255, 255))
chk("line mid", 600, 280, lambda p: p == (255, 255, 255))
chk("line tail", 700, 460, lambda p: p == (255, 255, 255))
chk("grad head", 0, 510, lambda p: p == (255, 0, 0))
chk("grad blue", 400, 510, lambda p: p == (0, 0, 255))

ncolors = len(set(img.getdata()))
print("distinct colors:", ncolors)