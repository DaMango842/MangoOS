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
mon = conn(4633)
time.sleep(6)

boot = b""
end = time.time() + 30
while b"kernel loaded" not in boot and time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: boot += chunk
    except socket.timeout:
        pass
time.sleep(5)  # demos/spawner finish, shell takes over
drain = b""
end = time.time() + 3
while time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: drain += chunk
    except socket.timeout:
        pass

ser.sendall(b"mangoWin\r\n")
time.sleep(3)
out = b""
end = time.time() + 6
while time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: out += chunk
    except socket.timeout:
        pass
txt = out.decode("latin1", "replace")
print("gui stats seen:", "gui fps=" in txt)

mon.sendall(b"screendump probe\\desk.png\n")
time.sleep(1.5)
print("screendump sent")
ser.close(); mon.close(); p.kill()
print("DONE")