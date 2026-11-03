import sys
sys.stdout.reconfigure(encoding="utf-8")
W = 800

def load(name):
    raw = open(rf"D:\Project\MangoOS\probe\{name}.ppm", "rb").read()
    return raw.split(b"\n255\n", 1)[1]

def row_count(body, row):
    y0 = row * 16
    return sum(1 for x in range(0, 600)
               if any(body[(y0 * W + x) * 3:(y0 * W + x) * 3 + 3]))

def bg_at(body, x, y):
    i = (y * W + x) * 3
    return (body[i], body[i + 1], body[i + 2])

pre = load("s0_cli")
for n in ["s0_cli", "s1_cli", "s2_cli"]:
    b = load(n)
    print(f"{n}: rows lit = {[row_count(b, r) for r in range(0, 7)]}")
    print(f"   bg(500,550)={bg_at(b, 500, 550)} bg(10,400)={bg_at(b, 10, 400)}")

print("s0==s1 rows0-1:", pre[:2*16*W*3] == load("s1_cli")[:2*16*W*3])
print("s0==s2 rows0-1:", pre[:2*16*W*3] == load("s2_cli")[:2*16*W*3])