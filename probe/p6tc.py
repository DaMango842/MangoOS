import sys

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\tc.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w, h = 800, 600


def p6(x, y):
    i = (y * w + x) * 3
    return (body[i], body[i + 1], body[i + 2])


print("expected: rect1 RED (255,0,0), rect2 GREEN (0,255,0), rect3 BLUE (0,0,255), rect4 YELLOW, rect5 WHITE")
for x, y in [(20, 20), (70, 20), (120, 20), (170, 20), (220, 20)]:
    print(f"P6 at ({x},{y}) = {p6(x, y)}")