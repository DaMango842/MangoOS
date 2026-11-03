import sys

d = open("roms/Super Mario Bros. (W) [!].nes", "rb").read()
prg = d[16:16 + 0x8000]

for pat_name, pat in [
    ("JSR 8E5C", bytes([0x20, 0x5C, 0x8E])),
    ("JSR 8E6A", bytes([0x20, 0x6A, 0x8E])),
    ("JSR AEEA", bytes([0x20, 0xEA, 0xAE])),
    ("JSR 8E6A", bytes([0x20, 0x6A, 0x8E])),
    ("JSR B0E6", bytes([0x20, 0xE6, 0xB0])),
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