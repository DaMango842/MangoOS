import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
DUMP = r"D:\Project\MangoOS\probe\bottom.ppm"
if os.path.exists(DUMP): os.remove(DUMP)
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4626,server,nowait",
     "-monitor", "tcp:127.0.0.1:4627,server,nowait"],
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

time.sleep(8)
ser = conn(4626)
mon = conn(4627)
ser.sendall(b"circle 400 340 80 00ff00\r\n")
time.sleep(2.5)
mon.sendall(b"screendump " + DUMP.encode() + b"\r\n")
time.sleep(2)

def mon_cmd(c, wait=2.0):
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

body = open(DUMP, "rb").read().split(b"\n255\n", 1)[1]
W = 800

def px(x, y):
    i = (y * W + x) * 3
    return (body[i], body[i + 1], body[i + 2])

for y in range(414, 426):
    row = []
    for x in range(392, 409):
        p = px(x, y)
        row.append("G" if p == (0, 255, 0) else ".")
    print(f"y={y}:", "".join(row))
print("top sneek y=258..262, x=398..402:")
for y in range(258, 263):
    print(f"y={y}:", "".join("G" if px(x, y) == (0, 255, 0) else "." for x in range(398, 403)))
ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")