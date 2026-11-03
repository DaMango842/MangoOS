import sys
d = open(r'D:\Project\MangoOS\probe\fs11.txt', 'rb').read().decode('latin1')
out = []
i = d.find('DEBUGCON')
out.append('--- DEBUGCON ---')
out.append(d[i:i+600] if i >= 0 else 'n/a')
open(r'D:\Project\MangoOS\probe\sl.txt', 'w', encoding='latin1').write('\r\n'.join(out))