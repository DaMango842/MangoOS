import subprocess, time, os, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
MON_PORT = 4608
DUMP = r"D:\Project\MangoOS\probe\final.ppm"
W, H = 800, 600
for f in (DUMP, r"D:\Project\MangoOS\probe\debug.log"):
    if os.path.exists(f): os.remove(f)

proc = subprocess.Popen(
    [QEMU, "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
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

def type_cmd(mon, text, per_char=0.15):
    for ch in text:
        key = {" ":"spc", ".":"dot"}.get(ch, ch)
        mon.cmd("sendkey " + key, wait=1.5)
        time.sleep(per_char)
    mon.cmd("sendkey ret", wait=1.5)
    time.sleep(1.0)

def shot(mon):
    mon.cmd(f"screendump {DUMP}", wait=3)
    time.sleep(1.2)

def load_ppm():
    raw = open(DUMP, "rb").read()
    body = raw.split(b"\n255\n", 1)[1]
    return body if len(body) >= W * H * 3 else None

def px(body, x, y):
    i = (y * W + x) * 3
    b, r, g = body[i], body[i + 1], body[i + 2]
    return (r, g, b)

tests = [
    ("frame", "frame 80 200 320 240 ff0000",
     lambda body: all(px(body, x, y) == (255, 0, 0)
                      for x, y in [(240, 200), (80, 300), (399, 300), (240, 439), (399, 439)])),
    ("circle", "circle 400 340 80 00ff00",
     lambda body: all(px(body, x, y) == (0, 255, 0)
                      for x, y in [(400, 260), (480, 340), (400, 420)])
                  and px(body, 400, 340) == (0, 0, 0)),
    ("grad", "grad 0 500 800 40",
     lambda body: px(body, 0, 510) == (255, 0, 0)
                  and px(body, 400, 510) == (0, 0, 255)
                  and px(body, 266, 510) == (2, 255, 0)),
]

time.sleep(8)
mon = Mon(MON_PORT)
for name, text, check in tests:
    for attempt in range(3):
        type_cmd(mon, text, 0.15)
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

mon.cmd("quit", wait=1)
proc.wait(timeout=8)
print("DONE")