import re
d = open(r'D:\Project\MangoOS\probe\in_asm.log', 'rb').read().decode('latin1', 'replace').splitlines()
seq = []
cur = None
for l in d:
    m = re.match(r'0x([0-9a-f]+):', l)
    if not m:
        continue
    a = int(m.group(1), 16)
    if a >= 0xF0000 and a < 0x100000:
        continue
    if cur != a:
        cur = a
        seq.append((a, l))
# last non-zero block
last = None
for i, (a, l) in enumerate(seq):
    if 'addb' not in l and '00 00' not in l:
        last = i
print('non-zero blocks total:', last + 1, 'of', len(seq))
for a, l in seq[max(0, last - 15):last + 8]:
    print('%08x  %s' % (a, l))