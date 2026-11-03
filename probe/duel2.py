import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
DUMP1 = r"D:\Project\MangoOS\probe\d2a.ppm"
DUMP2 = r"D:\Project\MangoOS\probe\d2b.ppm"
for f in (DUMP1, DUMP2):
    if os.path.exists(f): os.remove(f)
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4622,server,nowait",
     "-monitor", "tcp:127.0.0.1:4623,server,nowait"],
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
ser = conn(4622)
mon = conn(4623)

ser.sendall(b"circle 400 340 80 00ff00\r\n")
time.sleep(3.0)
print("xp top:", [l for l in mon_cmd(mon, "xp /4w 0xFD00CB840").splitlines() if "0x" in l and ":" in l])
print("xp right:", [l for l in mon_cmd(mon, "xp /4w 0xFD010A180").splitlines() if "0x" in l and ":" in l])
prints = []
for i, d in enumerate((DUMP1, DUMP2), 1):
    r = mon_cmd(mon, f"screendump {d}", wait=3)
    time.sleep(1.5)
    if os.path.exists(d):
        body = open(d, "rb").read().split(b"\n255\n", 1)[1]
        def px(x, y):
            p = (y * 800 + x) * 3
            return (body[p], body[p + 1], body[p + 2])
        print(f"dump{i} exists len={len(body)} px(400,260)={px(400,260)} px(480,340)={px(480,340)}")
    else:
        print(f"dump{i} MISSING")
ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")