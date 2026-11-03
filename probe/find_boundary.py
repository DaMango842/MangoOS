import re
data = open(r'D:\Project\MangoOS\probe\in_asm.log', 'rb').read().decode('latin1', 'replace').splitlines()

seq = []
cur = None
for l in data:
    m = re.match(r'0x([0-9a-f]+):', l)
    if not m:
        continue
    addr = int(m.group(1), 16)
    if addr >= 0xF0000 and addr < 0x100000:
        continue
    if cur != addr:
        cur = addr
        seq.append((addr, l))

# find the last block that is NOT a pure-zero 'addb' block
last_real = None
for i, (a, l) in enumerate(seq):
    if '00 00' not in l and 'addb' not in l:
        last_real = i

print('last non-zero block index:', last_real, 'of', len(seq))
for a, l in seq[last_real - 8:last_real + 12]:
    print('%08x  %s' % (a, l))