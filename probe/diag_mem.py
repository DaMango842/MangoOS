import subprocess, time, socket, sys

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

def mon_cmd(mon, c, wait=3.0):
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

mon = conn(4633)
time.sleep(9)

print("bootinfo 0x50A:")
print(mon_cmd(mon, "xp /8bx 0x50A"))
print("0x50A as words/dwords:")
print(mon_cmd(mon, "xp /4wx 0x50A"))

# framebuffer usually at 0xFC000000 or 0xF0000000 with -vga std
for addr in ["0xfc000000", "0xf0000000", "0xfd000000"]:
    r = mon_cmd(mon, f"xp /16bx {addr}")
    line = r.decode("latin1", "replace")
    vals = [int(s, 16) for s in line.split() if s.startswith("0x")][1:17]
    nz = sum(1 for v in vals if v)
    print(f"fb {addr}: non-zero bytes {nz}/16")

mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")