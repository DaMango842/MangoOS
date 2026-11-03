import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
DUMP = r"D:\Project\MangoOS\probe\duel.ppm"
if os.path.exists(DUMP): os.remove(DUMP)
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4620,server,nowait",
     "-monitor", "tcp:127.0.0.1:4621,server,nowait"],
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
ser = conn(4620)
mon = conn(4621)

ser.sendall(b"rect 0 0 40 40 ff0000\r\n")
time.sleep(1.5)
print("xp:", [l for l in mon_cmd(mon, "xp /4w 0xFD000000").splitlines() if "0x00" in l])
r = mon_cmd(mon, f"screendump {DUMP}", wait=3)
print("sd-ret:", r.splitlines()[-2:])
time.sleep(1.0)
if os.path.exists(DUMP):
    raw = open(DUMP, "rb").read()
    print("dump bytes:", len(raw), "head:", raw[:30])
    if b"255\n" in raw:
        body = raw.split(b"\n255\n", 1)[1]
        print("body len:", len(body))
        seg = body[:3 * 20 * 5]
        print("first 20x5 px bytes:", seg[:60])
    else:
        print("no 255 marker; dump:", raw[:200])
else:
    print("NO DUMP FILE")
ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")