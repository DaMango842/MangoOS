import re
d = open(r'D:\Project\MangoOS\probe\shot.ppm', 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', d)
w, h = int(m.group(1)), int(m.group(2))
px = d[m.end():]
pts = []
for i in range(0, len(px), 3):
    if px[i] or px[i+1] or px[i+2]:
        pts.append(((i//3) % w, (i//3)//w, px[i], px[i+1], px[i+2]))
xs = [p[0] for p in pts]
ys = [p[1] for p in pts]
print('count', len(pts), 'x', min(xs), max(xs), 'y', min(ys), max(ys))
c = {}
for p in pts:
    k = (p[2], p[3], p[4])
    c[k] = c.get(k, 0) + 1
print(sorted(c.items(), key=lambda kv: -kv[1])[:6])