import subprocess, time, os, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"
MON_PORT = 4606
DUMP = r"D:\Project\MangoOS\probe\demo.ppm"
W, H = 800, 600

for f in (DBG, DUMP, r"D:\Project\MangoOS\probe\full.png"):
    if os.path.exists(f): os.remove(f)

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-vga", "std",
     "-display", "gtk",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
     "-debugcon", f"file:{DBG}"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

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
            while self.s.recv(4096):
                pass
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

def type_cmd(mon, text, per_char=0.3):
    for ch in text:
        key = {" ":"spc", ".":"dot"}.get(ch, ch)
        mon.cmd("sendkey " + key, wait=1.5)
        time.sleep(per_char)
    mon.cmd("sendkey ret", wait=1.5)
    time.sleep(1.2)

def shot(mon):
    mon.cmd(f"screendump {DUMP}", wait=3)
    time.sleep(1.5)

def load_ppm():
    raw = open(DUMP, "rb").read()
    head = raw.split(b"\n255\n")[0]
    body = raw[len(head) + len(b"255\n"):]
    if len(body) < W * H * 3:
        print("!! short dump", len(body))
        return None
    return body

def pixel(body, x, y):
    i = (y * W + x) * 3
    b, r, g = body[i], body[i + 1], body[i + 2]
    return (r, g, b)

def red_at(body, pts):
    body = load_ppm()
    return all(body and (lambda p: p[0] > 200 and p[1] < 100 and p[2] < 100)(pixel(body, x, y)) for x, y in pts)

tests = [
    ("frame", "frame 80 200 320 240 ff0000",
     [(240, 200), (80, 300), (399, 439)], "red frame"),
    ("circle", "circle 400 340 80 00ff00",
     [(400, 260), (480, 340), (400, 420)], "green circle"),
    ("grad", "grad 0 500 800 40",
     [(0, 510), (266, 510), (400, 510)], "rainbow bar"),
]

time.sleep(8)
mon = Mon(MON_PORT)
for name, text, pts, what in tests:
    for attempt in range(3):
        type_cmd(mon, text)
        shot(mon)
        body = load_ppm()
        ok = False
        if name == "frame":
            ok = body and all(pixel(body, x, y)[0] > 200 and pixel(body, x, y)[1] < 100 and pixel(body, x, y)[2] < 100 for x, y in pts)
        elif name == "circle":
            ok = body and all(pixel(body, x, y)[1] > 200 and pixel(body, x, y)[0] < 100 and pixel(body, x, y)[2] < 100 for x, y in pts)
        else:
            if body:
                r = pixel(body, 0, 510)
                b = pixel(body, 400, 510)
                ok = r[0] > 200 and r[1] < 100 and r[2] < 100 and b[2] > 200 and b[0] < 100 and b[1] < 100
        print(f"{name}: attempt {attempt + 1} -> {'OK' if ok else 're-sending'}")
        if ok:
            break
    else:
        print(f"{name}: FAILED after 3 attempts")

time.sleep(1)
r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                    "-File", r"D:\Project\MangoOS\probe\screen_qemu.ps1"],
                   capture_output=True, text=True, timeout=60)
print("window:", r.stdout.strip())
open(r"D:\Project\MangoOS\probe\window_pos.txt", "w").write(r.stdout.strip())
time.sleep(2)
try: mon.cmd("quit", wait=1)
except Exception: pass
proc.wait(timeout=8)
try: proc.terminate()
except Exception: pass
print("DONE")