import sys

d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]

for pat_name, pat in [
    ("STA $06FC", bytes([0x8D, 0xFC, 0x06])),
    ("STA $06FD", bytes([0x8D, 0xFD, 0x06])),
    ("LDA $06FC", bytes([0xAD, 0xFC, 0x06])),
    ("LDA $06FD", bytes([0xAD, 0xFD, 0x06])),
    ("LDA $06FF", bytes([0xAD, 0xFF, 0x06])),
    ("STA $06FF", bytes([0x8D, 0xFF, 0x06])),
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