import sys
sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\serial.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + 5:]
print("head:", head[:60], "body:", len(body))
W = 800

def px(x, y):
    i = (y * W + x) * 3
    return (body[i], body[i + 1], body[i + 2])

reds = 0
for y in range(190, 450):
    for x in range(60, 420):
        p = px(x, y)
        if p[0] > 200 and p[1] < 100 and p[2] < 100:
            reds += 1
print("red pixels in frame region:", reds)
greens = 0
for y in range(240, 440):
    for x in range(300, 500):
        p = px(x, y)
        if p[1] > 200 and p[0] < 100 and p[2] < 100:
            greens += 1
print("green pixels in circle region:", greens)
print("px(240,200):", px(240, 200))
print("px(400,260):", px(400, 260))
print("px(400,340):", px(400, 340))
print("px(0,510):", px(0, 510))
print("px(400,510):", px(400, 510))