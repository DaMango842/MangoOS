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
cmds = [
    "xp /16bx 0x7b00\n",          # DAP packet
    "xp /16bx 0x3000\n",          # superblock buffer
    "xp /16bx 0x3800\n",          # root dir buffer
    "xp /48bx 0x18000\n",         # 1.5MB - check spillover (seg 0x3000 far)
    "xp /16bx 0x9ff00\n",         # stack
]
for cmd in cmds:
    p.stdin.write(cmd.encode()); p.stdin.flush(); time.sleep(0.5)
p.terminate()
try: p.wait(timeout=3)
except Exception: p.kill()