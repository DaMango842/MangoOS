import re
data = open(r'D:\Project\MangoOS\probe\in_asm.log', 'rb').read().decode('latin1', 'replace').splitlines()

# find the first instruction at >= 0x8b000, and print 40 following unique blocks around it
pn = 0
for n, l in enumerate(data):
    m = re.match(r'0x([0-9a-f]+):', l)
    if not m:
        continue
    addr = int(m.group(1), 16)
    if addr >= 0x0008b000:
        print('first block at/after 0x8b000 at line', n)
        print('--- context before ---')
        # print last 30 lines before n that are code (IN blocks) - simpler: print whole previous ~40 lines
        for l2 in data[max(0, n - 45):n]:
            print(l2)
        print('@@@')
        # now print lines n..n+25
        for l2 in data[n:n + 75]:
            print(l2)
        break
else:
    print('no 0x8b block found')

# also: total count of blocks with addr >= 0x0008b000
cnt = 0
for l in data:
    m = re.match(r'0x([0-9a-f]+):', l)
    if m and int(m.group(1), 16) >= 0x0008b000:
        cnt += 1
print('total executed instructions at >=0x8b000:', cnt)