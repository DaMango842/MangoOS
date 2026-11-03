import sys
sys.stdout.reconfigure(encoding="utf-8")
for n in ["gui1", "gui_exit"]:
    raw = open(rf"D:\Project\MangoOS\probe\{n}.ppm", "rb").read()
    print(n, "len", len(raw), "head", raw[:60])
    idx = raw.find(b"255\n")
    print("  255 marker at", idx)
    body = raw[idx + 4:]
    print("  body len", len(body), "first bytes", body[:6])
    i = (95 * 800 + 200) * 3
    print("  (200,95)", (body[i], body[i + 1], body[i + 2]))