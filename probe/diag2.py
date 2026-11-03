import sys
sys.stdout.reconfigure(encoding="utf-8")

def scan(name):
    try:
        raw = open(rf"D:\Project\MangoOS\probe\{name}.ppm", "rb").read()
    except FileNotFoundError:
        print(name, "MISSING")
        return
    body = raw.split(b"\n255\n", 1)[1]
    W = 800
    blue = []
    red = []
    for y in range(0, 600, 4):
        for x in range(0, 800, 4):
            i = (y * W + x) * 3
            p = (body[i], body[i + 1], body[i + 2])
            if p == (0x1F, 0x3F, 0xBF):
                blue.append((x, y))
            elif p == (0xB0, 0x20, 0x20):
                red.append((x, y))
    print(f"{name}: blue(title) px={len(blue)} red(close) px={len(red)}")
    if blue:
        xs = [p[0] for p in blue]; ys = [p[1] for p in blue]
        print(f"  blue range x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)}")
    if red:
        xs = [p[0] for p in red]; ys = [p[1] for p in red]
        print(f"  red range x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)}")

for n in ["s_boot", "s0_cli", "s1_gui", "s2_gui"]:
    scan(n)