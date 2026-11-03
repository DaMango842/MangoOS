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

def mon_cmd(mon, c, wait=1.0):
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
    time.sleep(0.1)
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

cur_mx, cur_my = 400, 288

def m_move(dx, dy):
    global cur_mx, cur_my
    dx = int(dx); dy = int(dy)
    while abs(dx) > 100 or abs(dy) > 100:
        sx = 100 if dx > 0 else -100
        sy = 100 if dy > 0 else -100
        if abs(dx) <= 100: sx = dx
        if abs(dy) <= 100: sy = dy
        mon_cmd(mon, f"mouse_move {sx} {sy}")
        time.sleep(0.05)
        dx -= sx; dy -= sy
    mon_cmd(mon, f"mouse_move {dx} {dy}")
    time.sleep(0.05)
    cur_mx += dx; cur_my += dy

def m_abs(x, y):
    m_move(x - cur_mx, y - cur_my)

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
    out = serial_drain(ser, 1.2)
    for ln in out.splitlines():
        if ln.startswith("mpos "):
            x, y = ln[5:].split(",")
            return int(x), int(y), out
    return None, None, out

ser.sendall(b"mangoWin\r\n")
time.sleep(1.5)
ser2 = serial_drain(ser, 0.5)

print("initial (expect 400,288):", query_mpos()[:2])
m_move(50, 30)
print("after +50,+30 (expect 450,318):", query_mpos()[:2])
m_move(-25, 10)
print("after -25,+10 (expect 425,328):", query_mpos()[:2])
m_abs(200, 120)
print("m_abs(200,120):", query_mpos()[:2])
time.sleep(0.5)
m_abs(200, 158)
pos = query_mpos()
print("m_abs(200,158):", pos[:2])
print("  raw serial:", repr(pos[2]))
time.sleep(0.5)
m_abs(700, 500)
pos = query_mpos()
print("m_abs(700,500):", pos[:2])
print("  raw serial:", repr(pos[2]))
time.sleep(0.5)

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
p.wait(timeout=8)
print("DONE")