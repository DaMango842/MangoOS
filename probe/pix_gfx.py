import sys

sys.stdout.reconfigure(encoding="utf-8")
w, h = 800, 600
data = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb")
assert data.readline().strip() == b"P6"
while True:
    line = data.readline().strip()
    if line and not line.startswith(b"#"):
        break
px = data.read()


def pix(x, y):
    i = (y * w + x) * 3
    return (px[i + 2], px[i + 1], px[i])  # RGB


print("circle candidates (expect green 0,255,0):")
for x, y in [(400, 170), (480, 250), (400, 330), (320, 250),
             (400, 250), (456, 193), (344, 193), (456, 307), (344, 307)]:
    print(f"  ({x},{y}) =", pix(x, y))

print("frame edges (expect red):")
for x, y in [(250, 100), (250, 299), (100, 200), (399, 200), (100, 100), (399, 299)]:
    print(f"  ({x},{y}) =", pix(x, y))

print("gradient bar y=510 (expect pure hues):")
for x in [0, 100, 133, 266, 400, 533, 666, 700, 799]:
    print(f"  x={x} =", pix(x, 510))

print("text zone y=48 sample:", pix(400, 48))