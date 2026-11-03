"""Collect the FC_TRACE_EVAL equivalence checksums (deterministic trigger)."""
import subprocess, time, socket, sys, re

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG = r"D:\Project\MangoOS\image\disk.img"
SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 45.0

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
    print("shell never ready"); p.kill(); sys.exit(1)
drain(1.5)

keys("fc"); mon("sendkey ret", wait=0.5)
# Keep everything from here on: the EV stream starts almost immediately,
# and discarding the first few seconds shifts the comparison.
buf = drain(2)
if "FC: start" not in buf:
    buf += drain(4)
if "FC: start" not in buf:
    print("FC did not start"); ser.close(); mon("quit", 0.5); p.kill(); sys.exit(1)

buf += drain(SECONDS)
evs = re.findall(r"E=([0-9A-F]{8})\s+(\d+)", buf)
for v, n in evs:
    print("%s %s" % (n, v))
print("count:", len(evs), file=sys.stderr)

ser.close(); mon("quit", 0.5)
try: p.wait(timeout=8)
except subprocess.TimeoutExpired: p.kill()