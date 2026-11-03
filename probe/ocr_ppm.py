import sys, re
sys.stdout.reconfigure(encoding="utf-8")
raw = open(r"D:\Project\MangoOS\probe\serial.ppm", "rb").read()
body = raw.split(b"\n255\n", 1)[1]
W = 800

def lit(x, y):
    i = (y * W + x) * 3
    return 1 if any(body[i:i + 3]) else 0

src = open(r"D:\Project\MangoOS\kernel32\font16.c", encoding="utf-8",
           errors="replace").read()
i = src.find("font8x16")
nums = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", src[i:].split(";")[0])]


def decode_cell(x0, y0):
    best = (-9999, None)
    lit_ = [lit(x0 + gx, y0 + gy) for gy in range(16) for gx in range(8)]
    for code in list(range(0x20, 0x7F)) + [0x00]:
        g = nums[code * 16:(code + 1) * 16]
        score = sum(1 for i in range(128)
                    if bool(g[i // 8] & (0x80 >> (i % 8))) == bool(lit_[i]))
        if score > best[0]:
            best = (score, code)
    return chr(best[1]) if best[0] >= 90 else "?"

for row in range(0, 14):
    y0 = row * 16
    txt = ""
    for col in range(0, 40):
        c = decode_cell(col * 8, y0)
        if c == "?":
            if len(txt) and txt[-1] == " ":
                break
        txt += c
    print(f"row{row:2d} |{txt}|")