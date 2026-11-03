import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
DUMP = r"D:\Project\MangoOS\probe\final_render.ppm"
if os.path.exists(DUMP): os.remove(DUMP)
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4628,server,nowait",
     "-monitor", "tcp:127.0.0.1:4629,server,nowait"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def conn(port):
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("no port")

def mon_cmd(mon, c, wait=2.0):
    mon.sendall((c + "\r\n").encode())
    buf = b""
    end = time.time() + wait
    while time.time() < end:
        try:
            chunk = mon.recv(4096)
            if not chunk: break
            buf += chunk
            if b"(qemu)" in buf: break
        except socket.timeout:
            pass
    return buf

time.sleep(8)
ser = conn(4628)
mon = conn(4629)

for cmd in [b"frame 80 200 320 240 ff0000\r\n",
            b"circle 400 340 80 00ff00\r\n",
            b"line 500 100 700 460 ffffff\r\n",
            b"grad 0 500 800 40\r\n"]:
    ser.sendall(cmd)
    time.sleep(2.0)

mon_cmd(mon, f"screendump {DUMP}", wait=3)
time.sleep(2)

body = open(DUMP, "rb").read().split(b"\n255\n", 1)[1]
from PIL import Image
img = Image.frombytes("RGB", (800, 600), body)
img.save(r"D:\Project\MangoOS\probe\final_render.png")
print("saved final_render.png")

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")