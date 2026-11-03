# probe/addr2sym.py - locate the kernel symbol nearest to a runtime EIP.
# Usage: python probe/addr2sym.py 0x21EA1 [section]

import sys

target = int(sys.argv[1], 16) + 0x10000   # kernel is loaded at 0x10000
section = sys.argv[2] if len(sys.argv) > 2 else ".text"

syms = []
with open(r"d:\Project\MangoOS\probe\syms.txt", errors="ignore") as f:
    for line in f:
        p = line.split()
        if len(p) >= 6 and p[3] == section:
            try:
                syms.append((int(p[0], 16), int(p[4], 16), p[-1]))
            except ValueError:
                pass

syms.sort()
prev = [s for s in syms if s[0] <= target]
nexts = [s for s in syms if s[0] > target]
print(f"target (ELF addr) = 0x{target:X} in {section}")
for a, sz, n in prev[-3:]:
    print(f"  before: 0x{a:X} +{sz} {n}  (target offset +0x{target-a:X})")
for a, sz, n in nexts[:3]:
    print(f"  after:  0x{a:X} +{sz} {n}")
