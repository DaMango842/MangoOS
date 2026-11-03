data = open(r'D:\Project\MangoOS\probe\in_asm.log', 'rb').read().decode('latin1', 'replace').splitlines()
i = None
for n, l in enumerate(data):
    if '0x0008bc' in l:
        i = n
        break
print('first zero-run line index:', i, 'of', len(data))
print('--- 50 lines before ---')
for l in data[max(0, i - 50):i]:
    print(l)