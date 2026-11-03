import re
d = open(r'D:\Project\MangoOS\probe\shot.ppm', 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', d)
w, h = int(m.group(1)), int(m.group(2))
px = d[m.end():]

# for each row, count non-black pixels; print rows that have content
rowinfo = []
for y in range(h):
    base = y * w * 3
    cnt = 0
    for x in range(w):
        i = base + x * 3
        if px[i] or px[i+1] or px[i+2]:
            cnt += 1
    rowinfo.append(cnt)

# find contiguous bands with content
band = []
inb = False
for y in range(h):
    if rowinfo[y] > 0:
        if not inb:
            band.append([y, y])
            inb = True
        else:
            band[-1][1] = y
    else:
        inb = False
print('bands (y ranges with content):', band[:20])

# column profile for first band
if band:
    y0, y1 = band[0]
    colx = [0] * w
    for y in range(y0, y1 + 1):
        for x in range(w):
            i = (y * w + x) * 3
            if px[i] or px[i+1] or px[i+2]:
                colx[x] += 1
    # summarize contiguous columns
    segs = []
    inb = False
    for x in range(w):
        if colx[x] > 0 and not inb:
            segs.append([x, x]); inb = True
        elif colx[x] > 0:
            segs[-1][1] = x
        else:
            inb = False
    print('x segs with content:', segs[:20])

# average channel of band 0
if band:
    y0, y1 = band[0]
    tot = [0, 0, 0]; n = 0
    for y in range(y0, y1+1):
        for x in range(w):
            i = (y*w + x)*3
            if px[i] or px[i+1] or px[i+2]:
                tot[0] += px[i]; tot[1] += px[i+1]; tot[2] += px[i+2]; n += 1
    print('avg RGB of content:', [round(t/n) for t in tot], 'n', n)