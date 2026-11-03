import sys

d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]

for pat_name, pat in [
    ("STA $075A", bytes([0x8D, 0x5A, 0x07])),
    ("LDA $075A", bytes([0xAD, 0x5A, 0x07])),
    ("DEC $075A", bytes([0xCE, 0x5A, 0x07])),
    ("STA $0759", bytes([0x8D, 0x59, 0x07])),
    ("LDA $0759", bytes([0xAD, 0x59, 0x07])),
    ("STA $0774", bytes([0x8D, 0x74, 0x07])),
    ("LDA $0774", bytes([0xAD, 0x74, 0x07])),
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