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
        seq.append(l)
print('\n'.join(seq[-30:]))