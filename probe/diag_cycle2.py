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

def mon_cmd(mon, c, wait=5.0):
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
    time.sleep(0.4)
    return buf

def clean(r):
    r = r.replace(b"\x1b[K", b"").replace(b"\x1b[D", b"")
    lines = [l.strip() for l in r.decode("latin1", "replace").splitlines()
             if l.strip() and not l.startswith("(qemu)")
             and not l.startswith("QEMU")]
    return " | ".join(lines)

ser = conn(4632)
mon = conn(4633)
time.sleep(10)
print("[CLI] fb dword:", clean(mon_cmd(mon, "xp /1wx 0x295a0")))
print("[CLI] fb_w/h/pitch/bpp:", clean(mon_cmd(mon, "xp /4wx 0x295a8")))
print("[CLI] fb_ready:", clean(mon_cmd(mon, "xp /1bx 0x295b4")))
ser.sendall(b"mangoWin\r\n")
time.sleep(2.5)
print("[GUI] fb dword:", clean(mon_cmd(mon, "xp /1wx 0x295a0")))
print("[GUI] fb_ready:", clean(mon_cmd(mon, "xp /1bx 0x295b4")))
ser.sendall(b"exit\r\n")
time.sleep(1.5)
print("[EXIT] fb dword:", clean(mon_cmd(mon, "xp /1wx 0x295a0")))
print("[EXIT] fb_ready:", clean(mon_cmd(mon, "xp /1bx 0x295b4")))
ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")