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

ser = conn(4632)
print("ser ok")
mon = conn(4633)
print("mon ok")
time.sleep(10)
data = b""
end = time.time() + 30
while time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: data += chunk
    except socket.timeout:
        pass
print("got", len(data), "bytes:", repr(data[:150]))
p.kill()