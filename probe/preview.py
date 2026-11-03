import re
d = open(r'D:\Project\MangoOS\probe\shot.ppm', 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', d)
w, h = int(m.group(1)), int(m.group(2))
px = d[m.end():]

# 100x60 downsampled: map color -> single char
def chof(r, g, b):
    if r > 200 and g < 80 and b < 80: return 'R'
    if b > 200 and r < 80 and g < 80: return 'B'
    if g > 200 and r < 80 and b < 80: return 'G'
    if r > 200 and g > 200 and b < 80: return 'Y'
    if r > 200 and g > 200 and b > 200: return 'W'
    if r < 60 and g < 60 and b < 60: return '.'
    return 't'

cw, ch = 8, 10
out = []
for yy in range(0, h, ch):
    row = []
    for xx in range(0, w, cw):
        r = g = b = n = 0
        for y in range(yy, min(yy + ch, h)):
            for x in range(xx, min(xx + cw, w)):
                i = (y * w + x) * 3
                r += px[i]; g += px[i+1]; b += px[i+2]; n += 1
        row.append(chof(r // n, g // n, b // n))
    out.append(''.join(row))
print('\n'.join(out))