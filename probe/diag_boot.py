import subprocess, time, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4632,server,nowait",
     "-monitor", "tcp:127.0.0.1:4633,server,nowait"],
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

def shot(mon, name):
    d = rf"D:\Project\MangoOS\probe\{name}.ppm"
    mon_cmd(mon, f"screendump {d}", wait=4)
    time.sleep(2.0)
    return d

mon = conn(4633)
W = 800

def rows_lit(path):
    raw = open(path, "rb").read()
    body = raw.split(b"\n255\n", 1)[1]
    out = []
    for row in range(0, 25):
        y0 = row * 16
        lit = sum(1 for x in range(0, 600)
                  if any(body[(y0 * W + x) * 3:(y0 * W + x) * 3 + 3]))
        out.append(lit)
    return out

time.sleep(14)
for i in range(3):
    p = shot(mon, f"t{i}")
    lit = rows_lit(p)
    print(f"t{i}: lit={lit[:8]} bg(500,400)=", end="")
    raw = open(p, "rb").read().split(b"\n255\n", 1)[1]
    i2 = (400 * W + 500) * 3
    print((raw[i2], raw[i2 + 1], raw[i2 + 2]))

mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")