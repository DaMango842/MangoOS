import sys
sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\s1_gui.ppm", "rb").read()
body = raw.split(b"\n255\n", 1)[1]
W = 800

def px(x, y):
    i = (y * W + x) * 3
    return (body[i], body[i + 1], body[i + 2])

print("s1_gui: window1 title(200,95):", px(200, 95))
print("s1_gui: bg(30,30):", px(30, 30))
for row in range(0, 6):
    y0 = row * 16
    lit = sum(1 for x in range(0, 600) if any(body[(y0 * W + x) * 3:(y0 * W + x) * 3 + 3]))
    if lit:
        print(f"text row{row} y={y0}: {lit} lit")

r2 = open(r"D:\Project\MangoOS\probe\s0_cli.ppm", "rb").read()
b2 = r2.split(b"\n255\n", 1)[1]
print("s0_cli text rows:")
for row in range(0, 6):
    y0 = row * 16
    lit = sum(1 for x in range(0, 600) if any(b2[(y0 * W + x) * 3:(y0 * W + x) * 3 + 3]))
    if lit:
        print(f"text row{row} y={y0}: {lit} lit")