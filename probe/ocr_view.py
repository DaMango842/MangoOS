import sys, re
from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")
img = Image.open(r"D:\Project\MangoOS\probe\full.png").convert("RGB")
t = open(r"D:\Project\MangoOS\probe\window_pos.txt").read().split()
SX = int(t[t.index("SCREEN_X") + 1])
SY = int(t[t.index("SCREEN_Y") + 1])
CH = int(t[t.index("CLIENT_H") + 1])
OFF = 25 if CH > 600 else 0

src = open(r"D:\Project\MangoOS\kernel32\font16.c", encoding="utf-8",
           errors="replace").read()
i = src.find("font8x16")
nums = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", src[i:].split(";")[0])]


def decode_cell(x0, y0):
    best = (-9999, None)
    lit = []
    for gy in range(16):
        for gx in range(8):
            p = img.getpixel((SX + x0 + gx, SY + OFF + y0 + gy))
            lit.append(1 if p != (0, 0, 0) else 0)
    for code in list(range(0x20, 0x7F)) + [0x00]:
        g = nums[code * 16:(code + 1) * 16]
        score = 0
        for i in range(128):
            on = 1 if g[i // 8] & (0x80 >> (i % 8)) else 0
            if on == lit[i]:
                score += 1
        if score > best[0]:
            best = (score, code)
    return chr(best[1]) if best[0] >= 90 else "?"


print(f"window at {SX},{SY}")
for row in range(0, 12):
    y0 = row * 16
    txt = ""
    for col in range(0, 40):
        c = decode_cell(col * 8, y0)
        if c == "?":
            if len(txt) and txt[-1] == " ":
                break
        txt += c
    print(f"row{row:2d} |{txt}|")