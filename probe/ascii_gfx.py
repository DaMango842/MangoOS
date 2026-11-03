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


def col(x, y):
    i = (y * w + x) * 3
    r, g, b = px[i + 2], px[i + 1], px[i]
    if r > 200 and g < 100 and b < 100:
        return "R"
    if g > 200 and r < 100 and b < 100:
        return "G"
    if b > 200 and r < 100 and g < 100:
        return "B"
    if r > 200 and g > 200 and b < 100:
        return "Y"
    if r > 170 and g > 170 and b > 170:
        return "W"
    if r | g | b:
        return "."
    return " "


for y in range(0, 600, 6):
    row = "".join(col(x, y) for x in range(0, 800, 6))
    print(f"{y:3d} |{row}|")