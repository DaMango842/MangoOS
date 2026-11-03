import sys
sys.stdout.reconfigure(encoding="utf-8")
W = 800
for n in ["gui_exit", "s_boot", "s0_cli"]:
    raw = open(rf"D:\Project\MangoOS\probe\{n}.ppm", "rb").read()
    b = raw.split(b"\n255\n", 1)[1]
    lit = [sum(1 for x in range(0, 600)
               if any(b[(r * 16 * W + x) * 3:(r * 16 * W + x) * 3 + 3]))
           for r in range(25)]
    print(n, lit[:8])