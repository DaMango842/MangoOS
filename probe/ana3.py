import re
d = open(r'D:\Project\MangoOS\probe\shot.ppm', 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', d)
w = int(m.group(1))
px = d[m.end():]
for row in range(4):
    y0 = row * 16
    cur = ''
    for cx in range(100):
        x0 = cx * 8
        glyph = []
        for gy in range(12):
            bits = 0
            for gx in range(8):
                i = ((y0 + gy) * w + x0 + gx) * 3
                if px[i] or px[i+1] or px[i+2]:
                    bits |= 0x80 >> gx
            glyph.append(bits)
        if any(glyph):
            cur += '#'
        else:
            cur += '.'
    print('row', y0, cur[:80])