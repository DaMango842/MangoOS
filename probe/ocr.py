import sys, re

sys.stdout.reconfigure(encoding="utf-8")
w, h = 800, 600
data = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb")
assert data.readline().strip() == b"P6"
while True:
    line = data.readline().strip()
    if line and not line.startswith(b"#"):
        break
px = data.read()

src = open(r"D:\Project\MangoOS\kernel32\font16.c", encoding="utf-8",
           errors="replace").read()
i = src.find("font8x16")
tbl = src[i:].split(";")[0]
nums = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", tbl)]
print("glyphs parsed:", len(nums) // 16)


def glyph(code):
    return nums[code * 16:(code + 1) * 16]


def decode_cell(x0, y0, lit_bg=False):
    best = None
    for code in list(range(0x21, 0x7F)) + [0x00, 0x20]:
        g = glyph(code)
        score = 0
        for gy in range(16):
            bits = g[gy]
            for gx in range(8):
                on = bits & (0x80 >> gx)
                i = ((y0 + gy) * w + (x0 + gx)) * 3
                lit = px[i] | px[i + 1] | px[i + 2]
                if lit and not lit_bg:
                    # glyph lit matches screen lit
                    if on:
                        score += 1
                    else:
                        score -= 1
                elif lit and lit_bg:
                    if not on:
                        score += 1
                    else:
                        score -= 1
        if best is None or score > best[0]:
            best = (score, code)
    return chr(best[1]) if best and best[0] >= 60 else "?"


for row in range(0, 21):
    y0 = row * 16
    line_txt = ""
    for col in range(80):
        c = decode_cell(col * 8, y0, lit_bg=(row < 3))
        line_txt += c
        if c == "?":
            break
    print(f"{row:2d}: {line_txt}")