import sys

d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]

for pat_name, pat in [
    ("INC $0E", bytes([0xE6, 0x0E])),
    ("LDA $0E", bytes([0xA5, 0x0E])),
    ("STA $0E", bytes([0x85, 0x0E])),
    ("LDA $0F", bytes([0xA5, 0x0F])),
    ("INC $0F", bytes([0xE6, 0x0F])),
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