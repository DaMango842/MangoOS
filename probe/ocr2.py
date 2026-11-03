import sys, re

sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\gfx.ppm", "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
w = 800

src = open(r"D:\Project\MangoOS\kernel32\font16.c", encoding="utf-8",
           errors="replace").read()
i = src.find("font8x16")
nums = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", src[i:].split(";")[0])]


def decode_cell(x0, y0):
    best = (-9999, None)
    lit_px = []
    for gy in range(16):
        for gx in range(8):
            p = ((y0 + gy) * w + (x0 + gx)) * 3
            lit_px.append(body[p] | body[p + 1] | body[p + 2])
    for code in list(range(0x20, 0x7F)) + [0x00]:
        g = nums[code * 16:(code + 1) * 16]
        score = 0
        for i in range(128):
            on = g[i // 8] & (0x80 >> (i % 8))
            if bool(on) == bool(lit_px[i]):
                score += 1
        if score > best[0]:
            best = (score, code)
    return chr(best[1]) if best[0] >= 90 else "?"


for row in range(0, 20):
    y0 = row * 16
    txt = ""
    for col in range(0, 60):
        c = decode_cell(col * 8, y0)
        if c == "?":
            if txt and txt[-1] == " ":
                break
        txt += c
    print(f"row{row:2d} |{txt}|")