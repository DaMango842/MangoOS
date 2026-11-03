import os

path = r"D:\Project\MangoOS\image\disk.img"
data = open(path, "rb").read()

print("=== LBA5 (superblock) ===")
print(repr(data[5*512:5*512+32]))
print("  root_lba field @+10:", int.from_bytes(data[5*512+10:5*512+12], "little"), " data_lba @+14:", int.from_bytes(data[5*512+14:5*512+16], "little"))
print("=== LBA6 (root dir) ===")
print(repr(data[6*512:6*512+64]))
print("=== LBA7 ===")
print(repr(data[7*512:7*512+32]))
print("=== BootMap LBA1 ===")
print(repr(data[512:512+32]))
