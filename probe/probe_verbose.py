"""Dump the FC_TRACE_EVAL_VERBOSE per-call state stream to stdout.

Usage: python probe_verbose.py <seconds> <outfile>
"""
import subprocess, time, socket, sys, re, io

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG = r"D:\Project\MangoOS\image\disk.img"
SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
OUT = sys.argv[2] if len(sys.argv) > 2 else r"C:\Users\yuzhe\AppData\Local\Temp\opencode\v.txt"

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

def keys(s):
    for c in s:
        mon("sendkey shift-%s" % c.lower() if c.isupper() else "sendkey %s" % c, wait=0.09)
        time.sleep(0.05)

b = b""; end = time.time() + 180
while b"SHELL: poll alive" not in b and time.time() < end:
    try:
        ch = ser.recv(4096)
        if ch: b += ch
    except socket.timeout: pass
if b"SHELL: poll alive" not in b:
    sys.exit("shell never ready")
drain(1.5)

keys("fc"); mon("sendkey ret", wait=0.5)
buf = drain(2)
if "FC: start" not in buf:
    buf += drain(4)
buf += drain(SECONDS)
ser.close(); mon("quit", 0.5)
try: p.wait(timeout=8)
except subprocess.TimeoutExpired: p.kill()

with io.open(OUT + ".raw", "w", encoding="utf-8", errors="replace") as f:
    f.write(buf)

# verbose lines are pure whitespace-separated integers
rows = []
for ln in buf.splitlines():
    t = ln.strip()
    if not t:
        continue
    parts = t.split()
    if len(parts) == 14 and all(x.isdigit() for x in parts):
        rows.append(" ".join(parts))

with io.open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(rows))
print("rows:", len(rows), "->", OUT)
print("raw lines:", len(buf.splitlines()), "->", OUT + ".raw")