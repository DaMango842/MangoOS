import subprocess, time, threading

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"

p = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "stdio"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def reader():
    while True:
        line = p.stdout.readline()
        if not line:
            break
        print(line.decode("latin1", "replace").rstrip())
threading.Thread(target=reader, daemon=True).start()

time.sleep(9)
# type "ver\r" then "help\r"
for k in ["v", "e", "r", "ret", "h", "e", "l", "p", "ret"]:
    p.stdin.write(f"sendkey {k}\n".encode())
    p.stdin.flush()
    time.sleep(0.15)
time.sleep(1.5)
p.stdin.write(b"xp /160bx 0xb8050\n")
p.stdin.flush()
time.sleep(0.5)
p.stdin.write(b"xp /160bx 0xb8100\n")
p.stdin.flush()
time.sleep(0.5)
p.terminate()
try: p.wait(timeout=3)
except Exception: p.kill()