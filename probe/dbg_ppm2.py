import sys

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w = 800
for y in range(96, 176):
    row = ""
    for x in range(0, 168):
        i = (y * w + x) * 3
        b, g, r = body[i], body[i + 1], body[i + 2]
        row += "#" if (r | g | b) else "."
    print(f"{y:3d}|{row}|")