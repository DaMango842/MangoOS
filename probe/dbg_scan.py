import sys
sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\dbg_circle.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
W = 800

def lit(x, y):
    b, r, g = body[(y * W + x) * 3], body[(y * W + x) * 3 + 1], body[(y * W + x) * 3 + 2]
    return 1 if (r or g or b) else 0

for row in range(0, 12):
    y0 = row * 16
    print(f"row{row:2d} lit:", sum(lit(x, y0 + 8) for x in range(0, 640)))

for row in range(10, 30):
    y0 = row * 16
    cnt = sum(lit(x, y0 + 8) for x in range(0, 800))
    if cnt:
        print(f"row{row:2d} (y={y0}) lit count={cnt}")

print("grafix presence: red any:", any(
    body[(y * W + x) * 3 + 1] > 200 for y in range(200, 440) for x in range(80, 500)))
print("green any:", any(
    body[(y * W + x) * 3 + 2] > 200 for y in range(200, 440) for x in range(300, 500)))