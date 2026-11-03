import subprocess, time, os
try:
    open(r'D:\Project\MangoOS\probe\qemu_pid.txt', 'w').close()
    os.remove(r'D:\Project\MangoOS\probe\qemu_pid.txt')
except Exception:
    pass
p = subprocess.Popen([r'D:\Softwares\qemu\qemu-system-i386.exe',
  '-drive', 'file=D:/Project/MangoOS/image/disk.img,format=raw',
  '-rtc', 'base=localtime', '-boot', 'c', '-m', '16', '-vga', 'std',
  '-display', 'none', '-monitor', 'tcp:0.0.0.0:4452,server,nowait',
  '-serial', 'none'],
  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
time.sleep(6)
rc = p.poll()
print('exited:', rc)
if rc is None:
    print('RUNNING port 4452')
    open('D:\Project\MangoOS\probe\qemu_pid.txt', 'w').write(str(p.pid))
else:
    err = p.stderr.read().decode('latin1', 'replace')
    print('err:', err[:300])