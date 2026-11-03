import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4632,server,nowait",
     "-monitor", "tcp:127.0.0.1:4633,server,nowait"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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

time.sleep(8)
ser = conn(4632)
mon = conn(4633)
W = 800

def shot(name):
    d = rf"D:\Project\MangoOS\probe\c_{name}.ppm"
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
time.sleep(2.5)
g = shot("gui")
chk("win1 title", px(g, 200, 95), TITLE)
chk("win2 title", px(g, 400, 265), TITLE)

# click close of win1: (400,288) -> (472,98)
mon_cmd(mon, "mouse_move 72 -95", wait=1)
mon_cmd(mon, "mouse_move 0 -95", wait=1)
mon_cmd(mon, "mouse_button 1", wait=1)
time.sleep(0.4)
mon_cmd(mon, "mouse_button 0", wait=1)
time.sleep(1.5)
a = shot("after_win1")
chk("win1 closed (bg)", px(a, 200, 95), BG)
chk("win2 still alive", px(a, 400, 265), TITLE)
chk("still in GUI", px(a, 30, 30), BG)

# click close of win2: (472,98) -> (612,268)
mon_cmd(mon, "mouse_move 140 85", wait=1)
mon_cmd(mon, "mouse_move 0 85", wait=1)
mon_cmd(mon, "mouse_button 1", wait=1)
time.sleep(0.4)
mon_cmd(mon, "mouse_button 0", wait=1)
time.sleep(1.5)
b = shot("after_win2")
chk("win2 closed (bg)", px(b, 400, 265), BG)
chk("no title left", px(b, 200, 95), BG)
# GUI should have exited -> console black screen (no GUI bg)
chk("GUI exited (bg black)", px(b, 30, 30), (0, 0, 0))
print("DONE")