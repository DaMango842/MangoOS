import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")

def start_qemu():
    p = subprocess.Popen(
        [r"D:\Softwares\qemu\qemu-system-i386.exe",
         "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
         "-rtc", "base=localtime", "-boot", "c", "-m", "16",
         "-vga", "std", "-display", "none",
         "-serial", "tcp:127.0.0.1:4632,server,nowait",
         "-monitor", "tcp:127.0.0.1:4633,server,nowait",
         "-debugcon", "file:D:\\Project\\MangoOS\\probe\\dbg.log"],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
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
    print("no serial data; aborting")
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

cur_mx, cur_my = 400, 288

def query_mpos():
    ser.sendall(b"mpos\r\n")
    out = serial_drain(ser, 1.0)
    for ln in out.splitlines():
        if ln.startswith("mpos "):
            x, y = ln[5:].split(",")
            return int(x), int(y)
    return None

def m_move(dx, dy):
    global cur_mx, cur_my
    dx = int(dx); dy = int(dy)
    while abs(dx) > 100 or abs(dy) > 100:
        sx = 100 if dx > 0 else -100
        sy = 100 if dy > 0 else -100
        if abs(dx) <= 100: sx = dx
        if abs(dy) <= 100: sy = dy
        mon_cmd(mon, f"mouse_move {sx} {sy}", wait=0.4)
        time.sleep(0.05)
        dx -= sx; dy -= sy
    mon_cmd(mon, f"mouse_move {dx} {dy}", wait=0.4)
    time.sleep(0.05)
    cur_mx += dx; cur_my += dy

def m_abs(x, y):
    """closed-loop: keep sending corrections until the kernel-side mouse
    position matches (QEMU injection loses/desyncs packets otherwise)"""
    for _ in range(10):
        pos = query_mpos()
        if pos is None:
            return
        rx, ry = pos
        if abs(x - rx) <= 1 and abs(y - ry) <= 1:
            return
        m_move(x - rx, y - ry)
    pos = query_mpos()
    if pos:
        cur_mx, cur_my = pos

def click(x, y):
    m_abs(x, y)
    mon_cmd(mon, "mouse_button 1", wait=0.4)
    time.sleep(0.2)
    mon_cmd(mon, "mouse_button 0", wait=0.4)
    time.sleep(0.5)

def region_has_white(b, x0, y0, x1, y1, thresh=200):
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, bl = px(b, x, y)
            if r > thresh and g > thresh and bl > thresh:
                return True
    return False

ser.sendall(b"mangoWin\r\n")
time.sleep(1.5)
time.sleep(1)
g = shot("lbl")
# win1 label "Hello Mango!" at abs (160,118) 96x16
chk("win1 label text visible", region_has_white(g, 160, 118, 256, 134), True)
# win2 label "MangoWin v0.2" at abs (360,288) 96x16
chk("win2 label text visible", region_has_white(g, 360, 288, 456, 304), True)
# win2 label is far from the cursor (400,288): the 9x9 cursor overlaps!
# cursor box = (400,288)-(409,297); label rows 288-303 -> overlap. Move away first.
m_abs(700, 500)
time.sleep(0.5)
h = shot("lbl2")
chk("win2 label text visible (cursor away)", region_has_white(h, 360, 288, 456, 304), True)

# clicking a label must NOT trigger any button action
ser2 = serial_drain(ser, 0)
click(200, 120)
out = serial_drain(ser, 1)
chk("label click fires nothing", "btn:" in out, False)

# button still works
m_abs(200, 158)
time.sleep(0.3)
k = shot("cursor_check")
hits = []
for y in range(0, 600, 1):
    for x in range(0, 800, 1):
        r, g, bl = px(k, x, y)
        if r == 224 and g == 224 and bl == 224:
            hits.append((x, y))
            break
if hits:
    print("  cursor white block near:", hits[:5])
else:
    print("  no cursor-colored pixels found")
click(200, 158)
out2 = serial_drain(ser, 1)
chk("Print button still works", "btn: Print" in out2, True)
if "btn: Print" not in out2:
    print("  debug serial:", repr(out2))
    j = shot("btn_debug")
    print("  debug: (160,146)=", px(j, 160, 146), " (200,158)=", px(j, 200, 158),
          " (235,158)=", px(j, 235, 158))

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
try:
    with open(r"D:\Project\MangoOS\probe\dbg.log", "rb") as f:
        data = f.read()
    dbg_tail = data[-200:].decode("latin1", "replace")
    print("dbg tail:", repr(dbg_tail))
    print("release seen:", "R" in dbg_tail, "trigger seen:", "RT" in dbg_tail)
except OSError:
    print("dbg.log unreadable")
print("DONE")