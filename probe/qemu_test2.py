import subprocess, time, os
err = r'D:\Project\MangoOS\probe\qemu_err2.txt'
p = subprocess.Popen([r'D:\Softwares\qemu\qemu-system-i386.exe',
  '-drive', 'file=D:/Project/MangoOS/image/disk.img,format=raw',
  '-rtc', 'base=localtime', '-boot', 'c', '-m', '16', '-vga', 'std',
  '-display', 'none', '-monitor', 'tcp:127.0.0.1:4451,server,nowait',
  '-serial', 'none'],
  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(6)
print('exited:', p.poll())
if p.poll() is None:
    print('RUNNING - monitor port 4451')
    open(r'D:\Project\MangoOS\probe\qemu_pid.txt', 'w').write(str(p.pid))
else:
    print('process exited early')