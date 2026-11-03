import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
DUMP = r"D:\Project\MangoOS\probe\gui1.ppm"
if os.path.exists(DUMP): os.remove(DUMP)
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4630,server,nowait",
     "-monitor", "tcp:127.0.0.1:4631,server,nowait"],
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

def mon_cmd(mon, c, wait=2.0):
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
    return buf.decode("latin1", "replace").replace("\x1b[K\x1b[D", "")

time.sleep(8)
ser = conn(4630)
mon = conn(4631)
W = 800

def shot(name):
    d = rf"D:\Project\MangoOS\probe\{name}.ppm"
    mon_cmd(mon, f"screendump {d}", wait=3)
    time.sleep(1.2)
    return d

def body_of(d):
    return open(d, "rb").read().split(b"\n255\n", 1)[1]

def px(body, x, y):
    i = (y * W + x) * 3
    return (body[i], body[i + 1], body[i + 2])

def chk(label, got, exp):
    print(f"{'OK' if got == exp else 'FAIL'} {label}: {got} expect {exp}")

# --- enter MangoWin ---
ser.sendall(b"mangoWin\r\n")
time.sleep(2.5)
d = shot("gui1")
b = body_of(d)
chk("bg color", px(b, 30, 30), (0x14, 0x16, 0x1E))
chk("win1 title bar", px(b, 200, 95), (0x1F, 0x3F, 0xBF))
chk("win1 body", px(b, 200, 200), (0x2A, 0x2F, 0x3A))
chk("win1 close btn", px(b, 470, 95), (0xB0, 0x20, 0x20))
chk("win2 title bar", px(b, 400, 265), (0x1F, 0x3F, 0xBF))
chk("win2 body", px(b, 400, 400), (0x2A, 0x2F, 0x3A))
chk("win1 border", px(b, 140, 200), (0xCF, 0xCF, 0xCF))

# --- inject a new window over serial ---
ser.sendall(b"gui win 100 420 200 130 Test\r\n")
time.sleep(1.5)
b = body_of(shot("gui2"))
chk("win3 title bar", px(b, 140, 427), (0x1F, 0x3F, 0xBF))
chk("win3 close btn", px(b, 292, 427), (0xB0, 0x20, 0x20))

# --- click close button of win1 via monitor mouse ---
mon_cmd(mon, "mouse_move 72 -202", wait=1)
mon_cmd(mon, "mouse_button 1", wait=1)
time.sleep(0.3)
mon_cmd(mon, "mouse_button 0", wait=1)
time.sleep(1.5)

# --- exit back to CLI via serial ---
ser.sendall(b"exit\r\n")
time.sleep(1.5)
d = shot("gui_exit")
b = body_of(d)
# console restored: text rows back, GUI gone
def lit(x, y):
    return any(body_of(d)[(y * W + x) * 3: (y * W + x) * 3 + 3])
trow2 = sum(1 for x in range(0, 600) if lit(x, 2 * 16 + 8))
print(f"row2 lit = {trow2} (text row present after exit)")
chk("console bg black", px(b, 500, 550), (0, 0, 0))

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")