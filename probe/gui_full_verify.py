import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")

p = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4632,server,nowait",
     "-monitor", "tcp:127.0.0.1:4633,server,nowait"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL)

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

time.sleep(6)
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
    p.kill(); sys.exit(1)

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

def query_mpos():
    ser.sendall(b"mpos\r\n")
    out = serial_drain(ser, 1.0)
    for ln in out.splitlines():
        if ln.startswith("mpos "):
            x, y = ln[5:].split(",")
            return int(x), int(y)
    return None

def m_move(dx, dy):
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

def click(x, y):
    m_abs(x, y)
    mon_cmd(mon, "mouse_button 1", wait=0.4)
    time.sleep(0.2)
    mon_cmd(mon, "mouse_button 0", wait=0.4)
    time.sleep(0.5)

def drag_title_to(x0, y0, x1, y1):
    m_abs(x0, y0)
    mon_cmd(mon, "mouse_button 1", wait=0.4)
    time.sleep(0.3)
    pos = query_mpos()
    if pos is None:
        pos = (x0, y0)
    rx, ry = pos
    dx, dy = x1 - rx, y1 - ry
    n = max(abs(dx), abs(dy)) // 6 + 1
    for i in range(n):
        tx = rx + dx // n
        ty = ry + dy // n
        m_move(tx - rx, ty - ry)
        rx, ty_old = tx, ty
        ry = ty
        time.sleep(0.3)
    m_abs(x1, y1)           # final closed-loop correction (drag follows)
    mon_cmd(mon, "mouse_button 0", wait=0.4)
    time.sleep(0.5)

def region_has(b, x0, y0, x1, y1, want, tol=3):
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, bl = px(b, x, y)
            if (abs(r - want[0]) <= tol and abs(g - want[1]) <= tol and
                    abs(bl - want[2]) <= tol):
                return True
    return False

ser.sendall(b"mangoWin\r\n")
time.sleep(1.5)
serial_drain(ser, 0.5)

# --- 1. initial button faces ---
g = shot("init")
# win1 title "MangoWin" white text on blue title bar (144,93)
chk("win1 title bar", region_has(g, 144, 93, 208, 109, (255, 255, 255)), True)
# win1 Print button face (160,146,80,24)
chk("Print face", region_has(g, 164, 148, 236, 168, (154, 154, 154)), True)
chk("Quit face", region_has(g, 256, 148, 328, 168, (154, 154, 154)), True)
# win2 Close face (360,320,80,24)
chk("win2 Close face", region_has(g, 364, 322, 436, 342, (154, 154, 154)), True)

# --- 2. Print button click ---
click(200, 158)
out = serial_drain(ser, 1.2)
chk("Print clicked -> btn: Print", "btn: Print" in out, True)

# --- 3. drag win1 by its title bar ---
# press at (200,95) on the title bar, drop at (300,145)
# anchor: offx=140-200=-60, offy=90-95=-5 -> new win pos (240,140)
drag_title_to(200, 95, 300, 145)
print("  after drag, kernel mouse pos:", query_mpos())
d = shot("drag")
# new title text at (244,143); scan for white pixels in title bar
chk("win1 moved (title at new pos)",
    region_has(d, 244, 143, 308, 159, (255, 255, 255)), True)
# old spot (140,90) now background
chk("old spot is bg", px(d, 140, 90) == (20, 22, 30), True)
# Print button moved along: face (260,196,80,24)
chk("win1 btn moved too",
    region_has(d, 264, 198, 336, 218, (154, 154, 154)), True)

# --- 4. win2 close button (400,332) ---
click(400, 332)
c = shot("closed")
chk("win2 closed (title gone)",
    not region_has(c, 330, 268, 400, 284, (255, 255, 255)), True)
chk("win1 still alive",
    region_has(c, 244, 143, 308, 159, (255, 255, 255)), True)

# --- 5. Quit button closes last window and leaves the GUI ---
# win1 now at (240,140); Quit ctl (112,40) abs (352,196) center (392,208)
click(392, 208)
q = shot("quit")
chk("win1 gone",
    not region_has(q, 244, 143, 308, 159, (255, 255, 255)), True)
time.sleep(1.5)
out2 = serial_drain(ser, 3.0)
chk("gui stats stopped (exited)", "gui fps=" not in out2, True)

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
p.wait(timeout=8)
print("DONE")