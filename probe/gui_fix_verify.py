import subprocess, time, socket, sys, os, re

sys.stdout.reconfigure(encoding="utf-8")

def start_qemu():
    p = subprocess.Popen(
        [r"D:\Softwares\qemu\qemu-system-i386.exe",
         "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
         "-rtc", "base=localtime", "-boot", "c", "-m", "16",
         "-vga", "std", "-display", "none",
         "-serial", "tcp:127.0.0.1:4632,server,nowait",
         "-monitor", "tcp:127.0.0.1:4633,server,nowait"],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    time.sleep(3)
    return p

proc = start_qemu()

def conn(port):
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("no port")

def mon_cmd(mon, c, wait=2.5):
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
    time.sleep(0.3)
    return buf

# IMPORTANT: connect BEFORE waiting for boot (Windows QEMU chardev
# only forwards bytes to already-established connections)
ser = conn(4632)
mon = conn(4633)
W = 800

time.sleep(10)
boot = b""
end = time.time() + 30
while b"kernel loaded" not in boot and time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: boot += chunk
    except socket.timeout:
        pass
if not boot:
    print("no serial data; aborting; qemu alive:", proc.poll() is None)
    proc.kill(); sys.exit(1)
if b"kernel loaded" not in boot:
    print("banner not found; got:", repr(boot[:200]))
    proc.kill(); sys.exit(1)

def shot(name):
    d = rf"D:\Project\MangoOS\probe\p_{name}.ppm"
    if os.path.exists(d): os.remove(d)
    mon_cmd(mon, f"screendump {d}", wait=4)
    time.sleep(1.5)
    for _ in range(15):
        if os.path.exists(d) and os.path.getsize(d) == 1440015:
            break
        time.sleep(0.3)
    raw = open(d, "rb").read()
    return raw.split(b"\n255\n", 1)[1]

def px(b, x, y):
    i = (y * W + x) * 3
    return (b[i], b[i + 1], b[i + 2])

def chk(label, got, exp):
    print(f"{'OK' if got == exp else 'FAIL'} {label}: {got} expect {exp}")

def near(c1, c2, tol=6):
    return all(abs(a - b) <= tol for a, b in zip(c1, c2))

CURS = (0xE0, 0xE0, 0xE0)
BG = (0x14, 0x16, 0x1E)
WINBG = (0x2A, 0x2F, 0x3A)
TITLE = (0x1F, 0x3F, 0xBF)

# ---- GUI serial burst test: one big pasted blob (multiple commands,
# > 16 bytes) must arrive intact through the RX ring buffer ----
ser.sendall(b"mangoWin\r\n")
time.sleep(1.5)
ser.sendall(b"win 20 20 100 60 QQ\r\n")
time.sleep(1.5)
g = shot("single")
chk("win QQ created (single)", near(px(g, 90, 28), TITLE), True)
burst = b"win 700 20 100 60 RR\r\nwin 20 500 100 60 ZZ\r\n"
ser.sendall(burst)
time.sleep(1.5)
g2 = shot("burst")
chk("win RR created (burst ok)", near(px(g2, 760, 28), TITLE), True)
chk("win ZZ created (burst ok)", near(px(g2, 90, 508), TITLE), True)

# ---- GUI cursor ghost test ----
# mouse starts at (400,288); move to A=(200,150) inside win1
mon_cmd(mon, "mouse_move -100 -69", wait=0.5)
mon_cmd(mon, "mouse_move -100 -69", wait=0.5)
time.sleep(0.8)
a = shot("ghost_a")
chk("cursor drawn at A", near(px(a, 200, 150), CURS), True)
# move away to B=(600,408) inside win2
mon_cmd(mon, "mouse_move 200 129", wait=0.5)
mon_cmd(mon, "mouse_move 200 129", wait=0.5)
time.sleep(0.8)
b = shot("ghost_b")
chk("no ghost at A", near(px(b, 200, 150), WINBG), True)
chk("cursor drawn at B", near(px(b, 600, 408), CURS), True)
# move back to A: must have no trace left at B
mon_cmd(mon, "mouse_move -200 -129", wait=0.5)
mon_cmd(mon, "mouse_move -200 -129", wait=0.5)
time.sleep(0.8)
c = shot("ghost_c")
chk("B restored (no ghost)", near(px(c, 600, 408), WINBG), True)
chk("cursor back at A", near(px(c, 200, 150), CURS), True)

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")