"""Read the bootinfo block stage2/vbe.asm writes, to see if a linear
framebuffer was actually negotiated.

  0x50A word status (1 = LFB active)
  0x50E dword fb address
  0x512 word width / 0x514 word height / 0x516 word pitch / 0x518 byte bpp
  0x500 word boot drive, 0x502 dword fs_base
"""
import subprocess, time, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG = r"D:\Project\MangoOS\image\disk.img"

EXTRA = sys.argv[1:] or []

p = subprocess.Popen(
    [QEMU, "-drive", "file=%s,format=raw" % IMG, "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4632,server,nowait",
     "-monitor", "tcp:127.0.0.1:4633,server,nowait"] + EXTRA,
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)

def conn(port):
    for _ in range(150):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1.5); return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("port")

ser, m = conn(4632), conn(4633)

def mon(c, wait=1.2):
    try: m.sendall((c + "\r\n").encode())
    except OSError: return ""
    buf = b""; end = time.time() + wait
    while time.time() < end:
        try:
            ch = m.recv(4096)
            if not ch: break
            buf += ch
            if b"(qemu)" in buf: break
        except socket.timeout: pass
    return buf.decode("latin1", "replace")

def drain(sec):
    d = b""; end = time.time() + sec
    while time.time() < end:
        try:
            ch = ser.recv(4096)
            if ch: d += ch
        except socket.timeout: pass
    return d.decode("latin1", "replace")

b = b""; end = time.time() + 60
while b"shell loaded" not in b and time.time() < end:
    try:
        ch = ser.recv(4096)
        if ch: b += ch
    except socket.timeout: pass
print("booted:", b"shell loaded" in b)
time.sleep(2); drain(1)

print("--- bootinfo 0x500..0x51F (16 words) ---")
print(mon("xp/16xh 0x500").strip())
print("--- interpret ---")
w = mon("xp/16xh 0x500")
vals = []
for tok in w.split():
    try: vals.append(int(tok, 16))
    except ValueError: pass
if len(vals) >= 16:
    print("  status(0x50A) =", vals[5])
    fb = vals[6] | (vals[7] << 16)
    print("  fb addr(0x50E)= 0x%08X" % fb)
    print("  width (0x512) =", vals[9])
    print("  height(0x514) =", vals[10])
    print("  pitch (0x516) =", vals[11])
    print("  bpp   (0x518) =", hex(vals[12] & 0xFF))

ser.close(); mon("quit", 0.5)
try: p.wait(timeout=8)
except subprocess.TimeoutExpired: p.kill()
print("DONE")