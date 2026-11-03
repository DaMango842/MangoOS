import subprocess, time, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4618,server,nowait",
     "-monitor", "tcp:127.0.0.1:4619,server,nowait"],
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
ser = conn(4618)
mon = conn(4619)

for cmd in [b"video\r\n", b"rect 0 0 20 20 ff0000\r\n", b"frame 80 200 320 240 00ff00\r\n"]:
    ser.sendall(cmd)
    time.sleep(1.5)

time.sleep(1)
print("A rect(0,0):", mon_cmd(mon, "xp /4w 0xFD000000").splitlines()[-3:])
print("B rect(19,19):", mon_cmd(mon, "xp /4w 0xFD000000+19*3200+19*4"))
print("C framebottom:", mon_cmd(mon, "xp /4w 0xFD0709000"))
ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")