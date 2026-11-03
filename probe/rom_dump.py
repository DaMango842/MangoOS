import sys

d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]
a = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x8050
n = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x30
for off in range(a, a + n):
    print("%04X: %02X" % (off, prg[off - 0x8000]))