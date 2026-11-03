import subprocess, time, os, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
MON_PORT = 4610
SER_PORT = 4611
DUMP = r"D:\Project\MangoOS\probe\serial.ppm"
W, H = 800, 600
for f in (DUMP, r"D:\Project\MangoOS\probe\debug.log"):
    if os.path.exists(f): os.remove(f)

proc = subprocess.Popen(
    [QEMU, "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", f"tcp:127.0.0.1:{SER_PORT},server,nowait",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
     "-debugcon", "file:D:\\Project\\MangoOS\\probe\\debug.log"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def conn(port):
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("no port " + str(port))

def slurp(s, wait=1.0):
    buf = b""
    end = time.time() + wait
    while time.time() < end:
        try:
            chunk = s.recv(4096)
            if not chunk: break
            buf += chunk
        except socket.timeout:
            break
    return buf

def shot(mon):
    mon.sendall(b"screendump " + DUMP.encode() + b"\r\n")
    slurp(mon, 1.5)
    time.sleep(1.0)

def load_ppm():
    body = open(DUMP, "rb").read().split(b"\n255\n", 1)[1]
    return body if len(body) >= W * H * 3 else None

def px(body, x, y):
    i = (y * W + x) * 3
    b, r, g = body[i], body[i + 1], body[i + 2]
    return (r, g, b)

time.sleep(7)
ser = conn(SER_PORT)
print("serial up:", slurp(ser, 1.5)[:80])
mon = conn(MON_PORT)

def has_color(body, cx, cy, c, r=2):
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if px(body, cx + dx, cy + dy) == c:
                return True
    return False

tests = [
    ("frame", "frame 80 200 320 240 ff0000",
     lambda b: all(has_color(b, x, y, (255, 0, 0), 1)
                   for x, y in [(240, 200), (80, 300), (399, 300), (240, 439), (399, 439)])),
    ("circle", "circle 400 340 80 00ff00",
     lambda b: all(has_color(b, x, y, (0, 255, 0), 2)
                   for x, y in [(400, 260), (480, 340), (400, 420)])
               and px(b, 400, 340) == (0, 0, 0)),
    ("grad", "grad 0 500 800 40",
     lambda b: has_color(b, 0, 510, (255, 0, 0), 1)
               and has_color(b, 400, 510, (0, 0, 255), 1)
               and has_color(b, 266, 510, (2, 255, 0), 1)),
]

for name, text, check in tests:
    for attempt in range(3):
        ser.sendall((text + "\r\n").encode())
        time.sleep(2.0)
        shot(mon)
        body = load_ppm()
        ok = False
        try:
            ok = body is not None and check(body)
        except Exception:
            ok = False
        print(f"{name}: attempt {attempt + 1} -> {'OK' if ok else 're-sending'}")
        if ok:
            break
    else:
        print(f"{name}: FAILED after 3 attempts")

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")