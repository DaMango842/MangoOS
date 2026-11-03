import subprocess, time, socket, sys, os

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
    m_move(x - cur_mx, y - cur_my)

def click(x, y):
    # move in int8 steps, then press + release
    m_abs(x, y)
    mon_cmd(mon, "mouse_button 1", wait=0.4)
    time.sleep(0.2)
    mon_cmd(mon, "mouse_button 0", wait=0.4)
    time.sleep(0.5)

def drag_title_to(px1, py1, px2, py2):
    # press at (px1,py1), drag to (px2,py2) in int8 steps, release
    m_abs(px1, py1)
    mon_cmd(mon, "mouse_button 1", wait=0.4)
    time.sleep(0.2)
    m_move(px2 - cur_mx, py2 - cur_my)
    mon_cmd(mon, "mouse_button 0", wait=0.4)
    time.sleep(0.5)

BG = (0x14, 0x16, 0x1E)
TITLE = (0x1F, 0x3F, 0xBF)
WINBG = (0x2A, 0x2F, 0x3A)
BTN = (0x9A, 0x9A, 0x9A)

ser.sendall(b"mangoWin\r\n")
time.sleep(1.5)
g = shot("w0")
chk("win1 title", near(px(g, 200, 95), TITLE), True)
chk("btn Print face", near(px(g, 235, 134), BTN), True)
chk("btn Quit face", near(px(g, 325, 134), BTN), True)
chk("win2 btn Close face", near(px(g, 430, 304), BTN), True)

# 1) click Print -> serial output "btn: Print"
ser2 = serial_drain(ser, 0)
click(200, 134)
time.sleep(0.5)
out = serial_drain(ser, 1)
chk("Print clicked (serial out)", "btn: Print" in out, True)

# 2) drag win1 by its title bar (200,95) -> (300,145)
#    window should move (140,90) -> (240,140)
drag_title_to(200, 95, 300, 145)
d = shot("w1_drag")
print("  debug: (300,145)=", px(d, 300, 145), " (200,95)=", px(d, 200, 95),
      " (150,100)=", px(d, 150, 100), " (250,150)=", px(d, 250, 150),
      " (240,140)=", px(d, 240, 140))
chk("win1 moved (title at new pos)", near(px(d, 300, 145), TITLE), True)
chk("win1 old spot is bg", near(px(d, 200, 95), BG), True)
chk("win1 btn moved too", near(px(d, 300, 189), BTN), True)

# 3) click win2 Close button -> win2 gone
click(400, 304)
e = shot("w2_closed")
chk("win2 closed (about title gone)", near(px(e, 400, 265), BG), True)
chk("win1 still alive", near(px(e, 300, 145), TITLE), True)

# 4) click win1 Quit button -> win1 gone, GUI exits
click(300, 189)
f = shot("quit")
chk("win1 gone", near(px(f, 300, 145), (0, 0, 0)), True)

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")