import sys

d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]

for pat_name, pat in [
    ("STA $0770", bytes([0x8D, 0x70, 0x07])),
    ("LDA $0770", bytes([0xAD, 0x70, 0x07])),
    ("INC $0770", bytes([0xEE, 0x70, 0x07])),
    ("STA $0772", bytes([0x8D, 0x72, 0x07])),
    ("LDA $0772", bytes([0xAD, 0x72, 0x07])),
    ("STA $0710", bytes([0x8D, 0x10, 0x07])),
    ("LDA $0710", bytes([0xAD, 0x10, 0x07])),
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