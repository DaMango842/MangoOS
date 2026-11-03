import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]
for a in range(0x8000, 0x8030):
    print("%04X: %02X" % (a, prg[a - 0x8000]))
print("---")
print("NMI vec:", hex(prg[0x7FFA] | prg[0x7FFB] << 8),
      "RESET:", hex(prg[0x7FFC] | prg[0x7FFD] << 8),
      "IRQ:", hex(prg[0x7FFE] | prg[0x7FFF] << 8))