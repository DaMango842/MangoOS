import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")

p = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4632,server,nowait",
     "-monitor", "tcp:127.0.0.1:4633,server,nowait",
     "-debugcon", "file:D:\\Project\\MangoOS\\probe\\dbg2.log"],
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

ser = conn(4632)
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

time.sleep(3)
ser.close()
p.kill()
time.sleep(1)
try:
    with open(r"D:\Project\MangoOS\probe\dbg2.log", "rb") as f:
        data = f.read()
    txt = data.decode("latin1", "replace")
    idx = txt.find("kK")
    tail = txt[idx + 2:]
    print("dbg tail:", repr(tail[:300]))
    print("S-picks:", tail.count("S"), "Z-reclaims:", tail.count("Z"))
except OSError:
    print("dbg unreadable")
print("DONE")