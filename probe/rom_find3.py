import sys

d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]

for pat_name, pat in [
    ("LDA $4016", bytes([0xAD, 0x16, 0x40])),
    ("STA $4016", bytes([0x8D, 0x16, 0x40])),
    ("LDA $4017", bytes([0xAD, 0x17, 0x40])),
    ("STA $4017", bytes([0x8D, 0x17, 0x40])),
]:
    hits = []
    i = 0
    while True:
        i = prg.find(pat, i)
        if i < 0:
            break
        hits.append(i + 0x8000)
        i += 1
    print("%s: %s" % (pat_name, ["%04X" % h for h in hits]))