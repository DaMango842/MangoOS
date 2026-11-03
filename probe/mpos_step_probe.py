import subprocess, time, socket, sys

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
            return int(x), int(y)
    return None

ser.sendall(b"mangoWin\r\n")
time.sleep(1.5)
serial_drain(ser, 0.5)

def step(name, cmd):
    mon_cmd(mon, cmd)
    time.sleep(0.4)
    pos = query_mpos()
    print(f"{name}: {cmd!r} -> {pos}")
    return pos

pos = step("init", "mouse_move 50 50")
pos = step("0,38", "mouse_move 0 38")
pos = step("0,-38", "mouse_move 0 -38")
pos = step("1,0", "mouse_move 1 0")
pos = step("0,1", "mouse_move 0 1")
pos = step("-1,0", "mouse_move -1 0")
pos = step("0,-1", "mouse_move 0 -1")
pos = step("5,0", "mouse_move 5 0")
pos = step("0,5", "mouse_move 0 5")

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
p.wait(timeout=8)
print("DONE")