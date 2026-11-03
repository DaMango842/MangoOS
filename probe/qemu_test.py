import subprocess, time, os, sys
if os.path.exists(r'D:\Project\MangoOS\probe\debug5.log'):
    os.remove(r'D:\Project\MangoOS\probe\debug5.log')
p = subprocess.Popen([r'D:\Softwares\qemu\qemu-system-i386.exe',
  '-drive', r'file=D:\Project\MangoOS\image\disk.img,format=raw',
  '-rtc', 'base=localtime', '-boot', 'c', '-m', '16', '-vga', 'std',
  '-display', 'none', '-monitor', 'tcp:127.0.0.1:4450,server,nowait',
  '-debugcon', 'file:D:\Project\MangoOS\probe\debug5.log'],
  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
time.sleep(5)
rc = p.poll()
err = p.stderr.read().decode('latin1', 'replace') if p.stderr else ''
print('exited:', rc)
print('err:', err[:300])
if os.path.exists('D:\Project\MangoOS\probe\debug5.log'):
    print('dbg:', open('D:\Project\MangoOS\probe\debug5.log', 'rb').read()[:80])
p.terminate()