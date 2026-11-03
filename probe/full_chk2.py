import sys
from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")
img = Image.open(r"D:\Project\MangoOS\probe\full.png").convert("RGB")
SX, SY, OFF = 112, 135, 25

def chk(label, cx, cy, exp):
    got = img.getpixel((SX + cx, SY + OFF + cy))
    ok = "OK " if got == exp else "FAIL"
    print(f"{ok} {label}: full({SX+cx},{SY+OFF+cy}) = {got} expect {exp}")

chk("frame top (250,100)", 250, 100, (255, 0, 0))
chk("frame left (100,150)", 100, 150, (255, 0, 0))
chk("frame bottom (250,299)", 250, 299, (255, 0, 0))
chk("frame right (399,200)", 399, 200, (255, 0, 0))
chk("frame corner (399,299)", 399, 299, (255, 0, 0))
chk("circle top (400,171)", 400, 171, (0, 255, 0))
chk("circle right (480,250)", 480, 250, (0, 255, 0))
chk("circle bottom (400,329)", 400, 329, (0, 255, 0))
chk("circle center (400,250)", 400, 250, (0, 0, 0))
chk("grad head (0,510)", 0, 510, (255, 0, 0))
chk("grad green (266,510)", 266, 510, (8, 255, 0))
chk("grad blue (400,510)", 400, 510, (0, 0, 255))