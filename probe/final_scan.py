import sys
sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\final.ppm", "rb").read()
body = raw.split(b"\n255\n", 1)[1]
W = 800

def lit(x, y):
    i = (y * W + x) * 3
    return 1 if any(body[i:i + 3]) else 0

for row in range(0, 14):
    y0 = row * 16
    c = sum(lit(x, y0 + 8) for x in range(0, 700))
    if c:
        print(f"row{row} y={y0} lit={c}")
print("green any:", any(body[(y * W + x) * 3 + 2] > 200 for y in range(200, 460) for x in range(300, 500)))
print("red any:", any(body[(y * W + x) * 3 + 1] > 200 for y in range(200, 460) for x in range(60, 420)))