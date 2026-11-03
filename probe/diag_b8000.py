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

def clean(r):
    r = r.replace(b"\x1b[K", b"").replace(b"\x1b[D", b"")
    return r.decode("latin1", "replace")

mon = conn(4633)
time.sleep(9)
print("B8000 (text plane, first 32 bytes):")
print(clean(mon_cmd(mon, "xp /32bx 0xb8000")))
print("B8000 chars->ascii:")
r = mon_cmd(mon, "xp /32bx 0xb8000")
import re
vals = [int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", r.decode("latin1", "replace"))]
chars = "".join(chr(v) if 32 <= v < 127 else "." for v in vals[::2])
print(repr(chars))
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")