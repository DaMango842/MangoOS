import sys
from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")
img = Image.open(r"D:\Project\MangoOS\probe\full.png").convert("RGB")

pos = open(r"D:\Project\MangoOS\probe\window_pos.txt").read()
t = pos.split()
SX = int(t[t.index("SCREEN_X") + 1])
SY = int(t[t.index("SCREEN_Y") + 1])
CH = int(t[t.index("CLIENT_H") + 1])
OFF = 25 if CH > 600 else 0
print("window at", SX, SY, "OFF", OFF)


def chk(label, cx, cy, exp):
    got = img.getpixel((SX + cx, SY + OFF + cy))
    ok = "OK " if got == exp else "FAIL"
    print(f"{ok} {label}: {got} expect {exp}")


chk("frame top (240,200)", 240, 200, (255, 0, 0))
chk("frame left (80,300)", 80, 300, (255, 0, 0))
chk("frame bottom (240,439)", 240, 439, (255, 0, 0))
chk("frame right (399,300)", 399, 300, (255, 0, 0))
chk("frame corner (399,439)", 399, 439, (255, 0, 0))
chk("circle top (400,260)", 400, 260, (0, 255, 0))
chk("circle right (480,340)", 480, 340, (0, 255, 0))
chk("circle bottom (400,420)", 400, 420, (0, 255, 0))
chk("circle center (400,340)", 400, 340, (0, 0, 0))
chk("grad head (0,510)", 0, 510, (255, 0, 0))
chk("grad blue (400,510)", 400, 510, (0, 0, 255))

img.crop((SX, SY + OFF, SX + 800, SY + OFF + 600)).save(
    r"D:\Project\MangoOS\probe\real_view.png")
print("saved real_view.png")