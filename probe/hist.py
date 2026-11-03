import re, sys
d = open(r'D:\Project\MangoOS\probe\shot.ppm', 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', d)
w = int(m.group(1)); px = d[m.end():]
c = {}
for i in range(0, len(px), 3):
    k = (px[i], px[i+1], px[i+2])
    c[k] = c.get(k, 0) + 1
out = []
for k, n in sorted(c.items(), key=lambda kv: -kv[1]):
    out.append('%.8s %d' % (str(k), n))
open(r'D:\Project\MangoOS\probe\vec.txt', 'w').write('\n'.join(out))
print(len(c), 'distinct colors')