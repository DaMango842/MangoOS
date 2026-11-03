import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
DUMP = r"D:\Project\MangoOS\probe\VERIFY.ppm"
if os.path.exists(DUMP): os.remove(DUMP)
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4624,server,nowait",
     "-monitor", "tcp:127.0.0.1:4625,server,nowait"],
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
ser = conn(4624)
mon = conn(4625)

W, H = 800, 600

def shot():
    mon_cmd(mon, f"screendump {DUMP}", wait=3)
    time.sleep(1.5)
    return open(DUMP, "rb").read().split(b"\n255\n", 1)[1]

def px(body, x, y):
    i = (y * W + x) * 3
    return (body[i], body[i + 1], body[i + 2])

def is_r(p): return p[0] > 200 and p[1] < 100 and p[2] < 100
def is_g(p): return p[1] > 200 and p[0] < 100 and p[2] < 100
def is_b(p): return p[2] > 200 and p[0] < 100 and p[1] < 100

def run(name, command, check):
    ser.sendall((command + "\r\n").encode())
    time.sleep(2.5)
    body = shot()
    pts = check(body)
    ok = all(v for v in pts)
    print(f"{name}: {command} -> {'OK' if ok else 'FAIL'}")
    for k, v in pts.items():
        print(f"   {k}: {v}")

def check_frame(b):
    return {
        "frame top (240,200)": is_r(px(b, 240, 200)),
        "frame left (80,300)": is_r(px(b, 80, 300)),
        "frame right (399,300)": is_r(px(b, 399, 300)),
        "frame bottom (240,439)": is_r(px(b, 240, 439)),
    }

run("frame", "frame 80 200 320 240 ff0000", check_frame)
run("circle", "circle 400 340 80 00ff00", lambda b: {
    "circle top": is_g(px(b, 400, 260)),
    "circle right": is_g(px(b, 480, 340)),
    "circle bottom": is_g(px(b, 400, 420)),
    "circle center": px(b, 400, 340) == (0, 0, 0),
})
run("grad", "grad 0 500 800 40", lambda b: {
    "grad head red": px(b, 0, 510) == (255, 0, 0),
    "grad mid green": px(b, 266, 510)[1] > 200 and px(b, 266, 510)[0] < 100,
    "grad right blue": px(b, 400, 510) == (0, 0, 255),
})

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")