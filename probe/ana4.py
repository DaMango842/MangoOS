import re
d = open(r'D:\Project\MangoOS\probe\shot.ppm', 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', d)
w, h = int(m.group(1)), int(m.group(2))
px = d[m.end():]

# downscale to ~160x120 ascii: cell 5x5
out = []
for y in range(0, h, 5):
    line = []
    for x in range(0, w, 5):
        cnt = 0
        for dy in range(5):
            for dx in range(5):
                xx, yy = x+dx, y+dy
                if xx < w and yy < h:
                    i = (yy*w + xx)*3
                    if px[i] or px[i+1] or px[i+2]:
                        cnt += 1
        line.append('#' if cnt >= 3 else ('.' if cnt else ' '))
    out.append(''.join(line))
print('\n'.join(out))