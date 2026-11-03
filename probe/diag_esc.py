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
    return buf

time.sleep(8)
ser = conn(4632)
mon = conn(4633)
W = 800

def shot(name):
    d = rf"D:\Project\MangoOS\probe\ts_{name}.ppm"
    if os.path.exists(d): os.remove(d)
    mon_cmd(mon, f"screendump {d}", wait=4)
    time.sleep(2.0)
    for _ in range(20):
        if os.path.exists(d) and os.path.getsize(d) == 1440015:
            break
        time.sleep(0.3)
    return d

def load(name):
    raw = open(rf"D:\Project\MangoOS\probe\ts_{name}.ppm", "rb").read()
    return raw.split(b"\n255\n", 1)[1]

def px(b, x, y):
    i = (y * W + x) * 3
    return (b[i], b[i + 1], b[i + 2])

def rows_lit(b):
    return [sum(1 for x in range(0, 600)
                if any(b[(r * 16 * W + x) * 3:(r * 16 * W + x) * 3 + 3]))
            for r in range(25)]

print("waiting for CLI...")
for attempt in range(40):
    shot("cli0")
    b = load("cli0")
    lit = rows_lit(b)
    nz = sum(1 for r in range(25) if lit[r] > 0)
    if nz > 0:
        print(f"CLI up (attempt {attempt}): rows {[i for i in range(25) if lit[i] > 0]} lit={[lit[i] for i in range(25) if lit[i] > 0][:6]}")
        break
    time.sleep(1)
else:
    print("CLI never appeared on screen (still black)")
print("cli0 (200,95):", px(load("cli0"), 200, 95))

ser.sendall(b"mangoWin\r\n")
time.sleep(2.5)
shot("gui")
g = load("gui")
print("gui (200,95) titlebar:", px(g, 200, 95), "(200,200) body:", px(g, 200, 200))
print("gui (30,30) bg:", px(g, 30, 30))

mon_cmd(mon, "sendkey esc", wait=1.5)
time.sleep(1.5)
shot("esc")
e = load("esc")
print("after-esc (200,95):", px(e, 200, 95), "(500,550):", px(e, 500, 550))
lit = rows_lit(e)
print("after-esc rows lit:", [i for i in range(25) if lit[i] > 0])

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")