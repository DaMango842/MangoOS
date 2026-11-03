import re
raw = open(r"D:\Project\MangoOS\tools\fcdbg\qemu_bench.txt", encoding="latin1").read()
evts = re.findall(r"\[([+-]?\d+\.\d+)\] (.*)\n", raw)
stream = "".join(t for _, t in evts)
for pat in ["B f=", "ST", "F=", "fcr", "fcs", "fcq", "e 1"]:
    print(pat, stream.count(pat))
i = stream.find("ST")
if i >= 0:
    print("ST context:", repr(stream[max(0, i - 40):i + 120]))