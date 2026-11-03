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

z = None
for i, (a, l) in enumerate(seq):
    instr = l.split(':', 1)[1] if ':' in l else ''
    if '00 00' in instr or 'addb' in instr or 'addl' in instr or 'ff' in instr:
        if z is None:
            z = i
    else:
        z = None
# monotonic-in-address zero run
print('zero-run from index:', z, '..', len(seq) - 1)
print('--- blocks at z-14 .. z+3 ---')
for a, l in seq[max(0, z - 14):z + 4]:
    print('%08x  %s' % (a, l))