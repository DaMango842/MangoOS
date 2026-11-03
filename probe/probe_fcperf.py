"""FC emulator frame-time benchmark.

Boots, types "fc", enables the per-frame component timer (fcr), and
collects the reported frame times (T= us, where 16667 us == 60 fps).

Usage: python probe_fcperf.py [label]
"""
import subprocess, time, socket, sys, re

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG = r"D:\Project\MangoOS\image\disk.img"
LABEL = sys.argv[1] if len(sys.argv) > 1 else "run"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 50.0

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

t_enter = time.time()
keys("fc"); mon("sendkey ret", wait=0.5)
data = drain(6)
if "FC: start" not in data:
    print("FC did NOT start. serial was:")
    print("\n".join(l for l in data.splitlines() if not l.strip().startswith("KBD:")))
    ser.close(); mon("quit", 0.5); p.kill(); sys.exit(1)
print("FC started")
drain(1)

# Deliberately NOT using the "fcr" per-frame timer: it prints a line every
# frame over 38400-baud serial, which costs ~10 ms of UART time per frame
# and would dominate the very thing it is trying to measure.  The built-in
# "FC perf:" line reports one 1-second window instead, which is enough.
data = drain(SECONDS)

PERF = re.compile(r"^FC perf: n=(\d+) cpu=(\d+) ppu=(\d+) rnd=(\d+)")
A = re.compile(r"fps=(\d+)")
samples, fpss = [], []
for ln in data.splitlines():
    s = ln.strip()
    mm = PERF.match(s)
    if mm:
        samples.append(tuple(int(x) for x in mm.groups()))
    ma = A.search(s)
    if ma:
        fpss.append(int(ma.group(1)))

if not samples:
    print("no 'FC perf' samples captured; serial was:")
    for l in data.splitlines():
        s = l.strip()
        if s and not s.startswith("KBD:"):
            print("   " + s[:120])
else:
    n = len(samples)
    smp = [s[0] for s in samples]
    cpu = [s[1] for s in samples]
    ppu = [s[2] for s in samples]
    rnd = [s[3] for s in samples]
    tot = [s[1] + s[2] + s[3] for s in samples]
    agg = lambda v: sum(v) / len(v)
    print("[%s] windows=%d  measurable-frames/window: mean=%.1f" % (LABEL, n, agg(smp)))
    # n= is the count of MEASURABLE frames (frames slower than the 54.9 ms
    # PIT wrap are excluded by the instrumentation fix), so the true frame
    # rate comes from the emulator's own fps counter.
    print("      TRUE fps  : mean=%.1f min=%d max=%d   (target 60)"
          % (agg(fpss) if fpss else 0, min(fpss) if fpss else 0, max(fpss) if fpss else 0))
    print("      frame total: mean=%.0fus spread=%dus   (budget 16667us)"
          % (agg(tot), max(tot) - min(tot)))
    print("      cpu  (6502 + PPU dot FSM) : mean=%7.0fus  %4.1f%%  spread=%d"
          % (agg(cpu), 100 * agg(cpu) / agg(tot), max(cpu) - min(cpu)))
    print("      ppu  (fc_ppu_render)      : mean=%7.0fus  %4.1f%%  spread=%d"
          % (agg(ppu), 100 * agg(ppu) / agg(tot), max(ppu) - min(ppu)))
    print("      rnd  (bar + scaled blit) : mean=%7.0fus  %4.1f%%  spread=%d"
          % (agg(rnd), 100 * agg(rnd) / agg(tot), max(rnd) - min(rnd)))
    print("      raw fps series: %s" % fpss)

ser.close(); mon("quit", 0.5)
try: p.wait(timeout=8)
except subprocess.TimeoutExpired: p.kill()
print("DONE")