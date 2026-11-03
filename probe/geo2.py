import sys

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w, h = 800, 600

reds = []
greens = []
for y in range(100, 480):
    for x in range(60, 800):
        i = (y * w + x) * 3
        r, g, b = body[i + 2], body[i + 1], body[i]
        if r > 200 and g < 100 and b < 100:
            reds.append((x, y))
        elif g > 200 and r < 100 and b < 100:
            greens.append((x, y))

print("=== RED pixel coords (y 100..480, x 60..800) ===")
for x, y in reds:
    print(f"  ({x},{y})")
print("RED total:", len(reds))
print("=== GREEN pixel coords y 100..380 (graphics zone, excluding frame edges) ===")
frame = [p for p in greens if not (100 <= p[1] <= 299 and (p[0] == 100 or p[0] == 399 or p[1] == 100 or p[1] == 299))]
print("green outside frame outline:", len(frame))
for x, y in frame[:60]:
    print(f"  ({x},{y})")