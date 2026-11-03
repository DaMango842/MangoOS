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

def drain(s, sec):
    out = b""
    end = time.time() + sec
    while time.time() < end:
        try:
            chunk = s.recv(4096)
            if chunk: out += chunk
        except socket.timeout:
            pass
    return out

ser = conn(4632)
mon = conn(4633)
time.sleep(6)
drain(ser, 30)
time.sleep(5)
drain(ser, 2)

ser.sendall(b"fc\r\n")
time.sleep(1)
out = drain(ser, 4)
print("fc start:", "FC: start" in out.decode("latin1", "replace"))

time.sleep(3)
mon.sendall(b"screendump probe\\fc_m1.png\n")
time.sleep(1.5)

ser.sendall(b"fcbtn 01\r\n")   # hold Right
time.sleep(3)
ser.sendall(b"fcbtn 00\r\n")
time.sleep(1)
mon.sendall(b"screendump probe\\fc_m2.png\n")
time.sleep(1.5)

ser.sendall(b"fcq\r\n")
time.sleep(1)
out = drain(ser, 4)
print("fc exit:", "FC: exit" in out.decode("latin1", "replace"))

ser.close(); mon.close(); p.kill()
print("DONE")