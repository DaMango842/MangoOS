import sys

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w, h = 800, 600

# for each row 100..600 print a compact map: only where colored pixels exist,
# with color letter, 4px stepping
def col(x, y):
    i = (y * w + x) * 3
    r, g, b = body[i + 2], body[i + 1], body[i]
    if r | g | b:
        return "R" if r > 170 and g < 120 and b < 120 else \
               "G" if g > 170 and r < 120 and b < 120 else \
               "B" if b > 170 and r < 120 and g < 120 else \
               "W" if r > 200 and g > 200 and b > 200 else \
               "C" if g > 170 and b > 170 and r < 120 else \
               "Y" if r > 170 and g > 170 and b < 120 else "o"
    return " "

for y in range(90, 410):
    row = "".join(col(x, y) for x in range(0, 800, 4))
    print(f"{y:3d}|{row}|")