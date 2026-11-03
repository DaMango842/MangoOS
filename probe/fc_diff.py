import sys
from PIL import Image

a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
pa, pb = a.load(), b.load()
w, h = a.size
diffs = {}
for y in range(60, 540):
    for x in range(144, 656):
        if pa[x, y] != pb[x, y]:
            diffs.setdefault(y // 8, 0)
            diffs[y // 8] += 1
print("changed px:", sum(diffs.values()))
print("row bands:", sorted(diffs.items())[:20])