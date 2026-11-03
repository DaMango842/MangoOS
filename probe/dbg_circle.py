import subprocess, time, os, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
DUMP = r"D:\Project\MangoOS\probe\dbg_circle.ppm"
for f in (DUMP, r"D:\Project\MangoOS\probe\debug.log"):
    if os.path.exists(f): os.remove(f)

proc = subprocess.Popen(
    [QEMU, "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-monitor", "tcp:127.0.0.1:4607,server,nowait",
     "-debugcon", "file:D:\\Project\\MangoOS\\probe\\debug.log"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

class Mon:
    def __init__(self, port):
        for _ in range(80):
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
                break
            except OSError:
                time.sleep(0.2)
        else:
            raise RuntimeError("no monitor")
        self.s.settimeout(1)
        self.drain()

    def drain(self):
        try:
            while self.s.recv(4096): pass
        except socket.timeout:
            pass

    def cmd(self, c, wait=2.0):
        self.s.sendall((c + "\r\n").encode())
        buf = b""
        end = time.time() + wait
        while time.time() < end:
            try:
                chunk = self.s.recv(4096)
                if not chunk: break
                buf += chunk
                if b"(qemu)" in buf: break
            except socket.timeout:
                pass
        return buf.decode("latin1", "replace")

time.sleep(8)
mon = Mon(4607)
for ch in "circle 400 340 80 00ff00":
    mon.cmd("sendkey " + ch, wait=1.5)
    time.sleep(0.25)
mon.cmd("sendkey ret", wait=1.5)
time.sleep(1.5)
mon.cmd(f"screendump {DUMP}", wait=3)
time.sleep(1.5)

raw = open(DUMP, "rb").read()
head = raw.split(b"\n255\n")[0]
body = raw[len(head) + len(b"255\n"):]
W = 800

def px(x, y):
    i = (y * W + x) * 3
    b, r, g = body[i], body[i + 1], body[i + 2]
    return (r, g, b)

print("column x=400, y=250..440:")
for y in range(250, 441):
    p = px(400, y)
    if p != (0, 0, 0):
        print(f"  y={y}: {p}")
print("row y=340, x=315..490:")
for x in range(315, 491):
    p = px(x, 340)
    if p != (0, 0, 0):
        print(f"  x={x}: {p}")
print("probe points:")
for (x, y) in [(400, 260), (400, 259), (400, 261), (480, 340), (400, 420), (400, 419), (400, 421)]:
    print(f"  ({x},{y}): {px(x, y)}")

mon.cmd("quit", wait=1)
proc.wait(timeout=8)
print("DONE")