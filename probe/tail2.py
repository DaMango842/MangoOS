import re
data = open(r'D:\Project\MangoOS\probe\in_asm.log', 'rb').read().decode('latin1', 'replace').splitlines()

seq = []   # (addr, first_line)
cur = None
for l in data:
    m = re.match(r'0x([0-9a-f]+):', l)
    if not m:
        continue
    addr = int(m.group(1), 16)
    if addr >= 0xF0000 and addr < 0x100000:
        continue  # skip BIOS ROM execution spam
    if "bios" in l:
        continue
    if cur != addr:
        cur = addr
        seq.append((addr, l))

print('last 45 executed basic-blocks (mangled, non-ROM):')
for a, l in seq[-45:]:
    print('%08x  %s' % (a, l))