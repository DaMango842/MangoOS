import sys

d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]
for i in range(0x82, 0x900):
    if prg[i] == 0x40:
        print("RTI at %04X" % (i + 0x8000))