import subprocess, time, socket, sys, os, re

sys.stdout.reconfigure(encoding="utf-8")

def start_qemu():
    for attempt in range(5):
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
        if p.poll() is None:
            return p
        print(f"qemu exited early (attempt {attempt}), retrying...")
        time.sleep(2)
    raise RuntimeError("qemu would not start")

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

def serial_drain(ser, seconds):
    data = b""
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = ser.recv(4096)
            if chunk: data += chunk
        except socket.timeout:
            pass
    return data.decode("latin1", "replace")

ser = conn(4632)
mon = conn(4633)
W = 800

time.sleep(10)

# wait until the shell is up
boot = b""
end = time.time() + 30
while b"kernel loaded" not in boot and time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: boot += chunk
    except socket.timeout:
        pass
if not boot:
    print("proc alive:", proc.poll() is None)
    print("no serial data in 30s; killing and aborting")
    proc.kill()
    sys.exit(1)
print("boot banner:", boot.decode("latin1", "replace").splitlines()[0][:60])

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

BG = (0x14, 0x16, 0x1E)
TITLE = (0x1F, 0x3F, 0xBF)

ser.sendall(b"mangoWin\r\n")
time.sleep(1)
serial_drain(ser, 1)
# move the mouse back and forth so cursor updates (cur=...) are measured,
# ending exactly at the starting position (400, 288)
for i in range(5):
    mon_cmd(mon, "mouse_move 5 0", wait=0.4)
    time.sleep(0.05)
for i in range(5):
    mon_cmd(mon, "mouse_move -5 0", wait=0.4)
    time.sleep(0.05)
# wait for two stats lines (2s report period)
data = serial_drain(ser, 6.5)
lines = [l.strip() for l in data.splitlines() if "gui fps=" in l]
print(f"stats lines captured: {len(lines)}")
good = 0
for ln in lines:
    m = re.search(r"gui fps=(\d+) frame=(\d+)us render=(\d+)us max=(\d+)us cur=(\d+)us busy=(\d+)% idle=(\d+)% mem=(\d+)K\+(\d+)K", ln)
    if not m:
        print("unparsed:", ln); continue
    fps, frame, render, mx, cur, busy, idle, mk, mg = map(int, m.groups())
    print(f"  stats: fps={fps} frame={frame}us render={render}us max={mx}us cur={cur}us busy={busy}% idle={idle}% mem={mk}K+{mg}K")
    if render > 0 and cur > 0 and busy + idle == 100 and mk > 0 and mg == 1875:
        good += 1
chk("at least one active-window stats line", good > 0, True)

g = shot("gui")
chk("win1 title", px(g, 200, 95), TITLE)
chk("win2 title", px(g, 400, 265), TITLE)

mon_cmd(mon, "mouse_move 72 -95", wait=1)
mon_cmd(mon, "mouse_move 0 -95", wait=1)
mon_cmd(mon, "mouse_button 1", wait=1)
time.sleep(0.4)
mon_cmd(mon, "mouse_button 0", wait=1)
time.sleep(1.5)
a = shot("after_win1")
chk("win1 closed (bg)", px(a, 200, 95), BG)
chk("win2 still alive", px(a, 400, 265), TITLE)

mon_cmd(mon, "mouse_move 140 85", wait=1)
mon_cmd(mon, "mouse_move 0 85", wait=1)
mon_cmd(mon, "mouse_button 1", wait=1)
time.sleep(0.4)
mon_cmd(mon, "mouse_button 0", wait=1)
time.sleep(1.5)
b = shot("after_win2")
chk("win2 closed + GUI exited", px(b, 200, 95), (0, 0, 0))
chk("no GUI bg", px(b, 30, 30), (0, 0, 0))

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")