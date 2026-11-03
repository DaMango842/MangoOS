"""Type a command via real PS/2 keys, dump ALL serial output + screendump."""
import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG = r"D:\Project\MangoOS\image\disk.img"
CMD = sys.argv[1] if len(sys.argv) > 1 else "mangoWin"
WAIT = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0

p = subprocess.Popen(
    [QEMU, "-drive", "file=%s,format=raw" % IMG, "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4632,server,nowait",
     "-monitor", "tcp:127.0.0.1:4633,server,nowait"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)

def conn(port):
    for _ in range(150):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1); return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("port")

ser, m = conn(4632), conn(4633)

def mon(c, wait=0.5):
    try: m.sendall((c + "\r\n").encode())
    except OSError: return
    end = time.time() + wait
    while time.time() < end:
        try:
            if b"(qemu)" in m.recv(4096): break
        except socket.timeout: pass

def drain(sec):
    d = b""; end = time.time() + sec
    while time.time() < end:
        try:
            ch = ser.recv(4096)
            if ch: d += ch
        except socket.timeout: pass
    return d.decode("latin1", "replace")

b = b""; end = time.time() + 180
while b"SHELL: poll alive" not in b and time.time() < end:
    try:
        ch = ser.recv(4096)
        if ch: b += ch
    except socket.timeout: pass
if b"SHELL: poll alive" not in b:
    print("shell never ready"); p.kill(); sys.exit(1)
print("shell ready")
drain(1.5)

print("=== typing: %r ===" % CMD)
for ch in CMD:
    # the shell's dispatcher is case sensitive ("mangoWin"), so uppercase
    # letters must go out as shift-<key>
    if ch.isupper():
        mon("sendkey shift-%s" % ch.lower(), wait=0.10)
    else:
        mon("sendkey %s" % ch, wait=0.10)
    time.sleep(0.06)
mon("sendkey ret", wait=0.6)

data = drain(WAIT)
print("=== serial after Enter (non-KBD lines) ===")
for ln in data.splitlines():
    s = ln.strip()
    if s and not s.startswith("KBD:"):
        print("   " + s)

out = r"D:\Project\MangoOS\probe\p_chk.ppm"
if os.path.exists(out): os.remove(out)
mon("screendump %s" % out, wait=5)
for _ in range(20):
    if os.path.exists(out) and os.path.getsize(out) == 1440015: break
    time.sleep(0.3)
raw = open(out, "rb").read().split(b"\n255\n", 1)[1]
def px(x, y):
    i = (y * 800 + x) * 3
    return (raw[i], raw[i+1], raw[i+2])
print("=== screen ===")
print("  (5,5)      ", px(5, 5))
print("  (400,585)  ", px(400, 585), "(taskbar)")
print("  (400,500)  ", px(400, 500), "(desktop)")
print("  (200,95)   ", px(200, 95), "(win title)")
print("  (400,300)  ", px(400, 300))
ser.close(); mon("quit", 0.5)
try: p.wait(timeout=8)
except subprocess.TimeoutExpired: p.kill()
print("DONE")