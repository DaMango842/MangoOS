import sys
from PIL import Image
from collections import Counter

img = Image.open(sys.argv[1]).convert("RGB")
w, h = img.size
# FC picture region: (144,60)-(656,540)
crop = img.crop((144, 60, 656, 540))
cnt = Counter(crop.getdata())
print("size:", w, h, "region:", crop.size)
print("top 12 colors:")
for c, n in cnt.most_common(12):
    print("  #%02X%02X%02X  %d" % (c[0], c[1], c[2], n))
# status bar region (0-20)
bar = Counter(img.crop((0, 0, 800, 20)).getdata())
print("bar top3:", bar.most_common(3))