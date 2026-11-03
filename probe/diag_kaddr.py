import subprocess, time, socket, sys, re

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
# entry signature: fa b0 6b e6 e9  (cli; mov $0x6b,%al; out %al,$0xe9)
for addr in [0x10000, 0x30000, 0x50000, 0x70000, 0x80000, 0x90000, 0x20000, 0x40000]:
    r = clean(mon_cmd(mon, f"xp /8bx 0x{addr:x}"))
    vals = re.findall(r"0x([0-9a-f]{2})", r)[1:9]
    sig = "".join(vals)
    mark = " <== ENTRY?" if sig.startswith("fab06be6") else ""
    print(f"0x{addr:x}: {sig}{mark}")
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")