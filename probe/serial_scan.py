import sys
sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\serial.ppm", "rb").read()
body = raw.split(b"\n255\n", 1)[1]
W = 800

def lit(x, y):
    i = (y * W + x) * 3
    return 1 if any(body[i:i + 3]) else 0

print("text rows:")
for row in range(0, 14):
    y0 = row * 16
    c = sum(lit(x, y0 + 8) for x in range(0, 700))
    if c:
        print(f"  row{row} y={y0} lit={c}")
print("red any:", any(body[(y * W + x) * 3 + 1] > 200 for y in range(200, 460) for x in range(60, 420)))
print("green any:", any(body[(y * W + x) * 3 + 2] > 200 for y in range(200, 460) for x in range(300, 500)))
for y in range(200, 440):
    for x in range(60, 500):
        b, r, g = body[(y * W + x) * 3], body[(y * W + x) * 3 + 1], body[(y * W + x) * 3 + 2]
        if r > 200 or g > 200:
            print(f"  color at ({x},{y}) = ({r},{g},{b})")
            break
    else:
        continue
    break