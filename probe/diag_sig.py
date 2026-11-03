import sys
sys.stdout.reconfigure(encoding="utf-8")
m = open(r"D:\Project\MangoOS\probe\mem0.bin", "rb").read()
hits = []
i = m.find(b"\xb0\x6b\xe6\xe9")
while i != -1:
    hits.append(i)
    i = m.find(b"\xb0\x6b\xe6\xe9", i + 1)
print("entry sig hits:", [hex(h) for h in hits])
hits2 = []
i = m.find(b"\xc7\x05\xb4\x95\x01\x00\x01\x00\x00\x00\x00")
while i != -1:
    hits2.append(i)
    i = m.find(b"\xc7\x05\xb4\x95\x01\x00\x01\x00\x00\x00\x00", i + 1)
print("fb_ready=1 instr hits:", [hex(h) for h in hits2])