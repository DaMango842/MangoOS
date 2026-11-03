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
    d = rf"D:\Project\MangoOS\probe\t_{name}.ppm"
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
        rx, ry = tx, ty
        time.sleep(0.3)
    m_abs(x1, y1)
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

C_ACT = (31, 63, 191)     # C_TITLE / C_TB_ACT
C_INA = (74, 90, 128)     # C_TITLE_INA
C_BTN = (58, 65, 80)      # C_TB_BTN
C_MIN = (35, 39, 51)      # C_TB_MIN
C_BG  = (20, 22, 30)

ser.sendall(b"mangoWin\r\n")
time.sleep(1.5)
serial_drain(ser, 0.5)

# --- 1. initial state: win2 (About, created last) is active ---
g = shot("init")
chk("win1 title inactive", region_has(g, 144, 93, 208, 109, C_INA), True)
chk("win2 title active", region_has(g, 324, 263, 388, 279, C_ACT), True)
chk("tb btn1 normal", region_has(g, 6, 572, 90, 592, C_BTN), True)
chk("tb btn2 active", region_has(g, 104, 572, 188, 592, C_ACT), True)

# --- 2. click taskbar btn1 -> win1 activates ---
click(50, 582)
g = shot("act1")
chk("win1 title now active", region_has(g, 144, 93, 208, 109, C_ACT), True)
chk("win2 title now inactive", region_has(g, 324, 263, 388, 279, C_INA), True)
chk("tb btn1 active", region_has(g, 6, 572, 90, 592, C_ACT), True)
chk("tb btn2 normal", region_has(g, 104, 572, 188, 592, C_BTN), True)

# --- 3. click active btn1 again -> minimize ---
click(50, 582)
g = shot("min")
chk("win1 hidden (bg at title)", px(g, 200, 100) == C_BG, True)
chk("tb btn1 minimized", region_has(g, 6, 572, 90, 592, C_MIN), True)
chk("win2 auto-activated", region_has(g, 324, 263, 388, 279, C_ACT), True)

# --- 4. click minimized btn1 -> restore + activate ---
click(50, 582)
g = shot("rest")
chk("win1 visible again", region_has(g, 144, 93, 208, 109, C_ACT), True)
chk("tb btn1 active again", region_has(g, 6, 572, 90, 592, C_ACT), True)

# --- 5. drag win1 title to the bottom edge: title must stop at y=552 ---
drag_title_to(200, 100, 400, 580)
d = shot("dragdown")
chk("title pinned above taskbar",
    region_has(d, 150, 552, 450, 568, C_ACT), True)
chk("taskbar row still taskbar color",
    px(d, 200, 590) == (30, 34, 44), True)

# --- 6. desktop icon click activates mangoWin (win1) ---
click(350, 270)               # activate win2 via its title
click(40, 40)                 # click the desktop icon
g = shot("icon")
chk("icon click activates win1", region_has(g, 150, 552, 450, 568, C_ACT), True)
chk("win2 inactive after icon", region_has(g, 324, 263, 388, 279, C_INA), True)

# --- 7. exit command leaves the GUI ---
ser.sendall(b"exit\r\n")
time.sleep(1.5)
out2 = serial_drain(ser, 3.0)
chk("gui stats stopped (exited)", "gui fps=" not in out2, True)

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
p.wait(timeout=8)
print("DONE")