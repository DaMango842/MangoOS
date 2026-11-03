import subprocess, time, os, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
MON_PORT = 4612
SER_PORT = 4613
for f in (r"D:\Project\MangoOS\probe\debug.log",):
    if os.path.exists(f): os.remove(f)

proc = subprocess.Popen(
    [QEMU, "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", f"tcp:127.0.0.1:{SER_PORT},server,nowait",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
     "-debugcon", "file:D:\\Project\\MangoOS\\probe\\debug.log"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def conn(port):
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("no port " + str(port))

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
    return buf.decode("latin1", "replace")

time.sleep(7)
ser = conn(SER_PORT)
mon = conn(MON_PORT)

ser.sendall(b"rect 0 0 20 20 ff0000\r\n")
time.sleep(2.0)
ser.sendall(b"frame 80 200 320 240 00ff00\r\n")
time.sleep(2.0)

print("xp rect(0,0) 32bpp:", mon_cmd(mon, "xp /4w 0xFD000000"))
print("xp frame(80,200):", mon_cmd(mon, "xp /4w 0xFD009C540"))
print("xp frame(240,200):", mon_cmd(mon, "xp /4w 0xFD009C7C0"))

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")