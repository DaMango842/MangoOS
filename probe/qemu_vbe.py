import subprocess, time, os, socket

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"
PPM  = r"D:\Project\MangoOS\probe\vbe.ppm"
MON_PORT = 4444

if os.path.exists(DBG): os.remove(DBG)
if os.path.exists(PPM): os.remove(PPM)

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
     "-debugcon", f"file:{DBG}"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

def telnet_cmd(cmd):
    try:
        with socket.create_connection(("127.0.0.1", MON_PORT), timeout=5) as s:
            s.sendall(b"\r\n")
            s.settimeout(0.5)
            try:
                s.recv(8192)
            except socket.timeout:
                pass
            s.sendall((cmd + "\r\n").encode())
            data = b""
            s.settimeout(1.5)
            try:
                while True:
                    chunk = s.recv(8192)
                    if not chunk:
                        break
                    data += chunk
            except socket.timeout:
                pass
            return data.decode("latin1", "replace")
    except Exception as e:
        return f"(monitor error: {e})"

time.sleep(20)
print("=== MONITOR: code at kernel entry 0x10000 ===")
print(telnet_cmd("xp /32bx 0x10000"))
print("=== MONITOR: code at vga_enable_fb ===")
print(telnet_cmd("xp /32bx 0x102d0"))
print("=== MONITOR: state ===")
print(telnet_cmd("xp /16bx 0x15530"))
print("=== MONITOR: xp fb ===")
print(telnet_cmd("xp /64bx 0xFD000000"))
print("=== MONITOR: screendump ===")
print(telnet_cmd("screendump " + PPM.replace("\\", "/")))
time.sleep(1)

print("=== DEBUGCON ===")
if os.path.exists(DBG):
    print(open(DBG, "rb").read().decode("latin1", "replace"))
else:
    print("(no file)")

print("=== PPM CHECK ===")
if not os.path.exists(PPM):
    print("screendump produced no file")
else:
    data = open(PPM, "rb").read()
    print("bytes:", len(data))
    # PPM header: "P6\n<w> <h>\n255\n"
    nl = data.index(b"\n255\n")
    header = data[:nl + 5].decode("latin1")
    print("header:", header)
    dims = header.splitlines()[1].split()
    w, h = int(dims[0]), int(dims[1])
    print(f"size: {w}x{h}")
    px = data[data.index(b"\n255\n") + 5:]
    # top banner rows (kernel paints a test strip near the bottom; check both)
    from collections import Counter
    def row_stats(y):
        seg = px[y * 3 * w:(y + 1) * 3 * w]
        c = Counter()
        for off in range(0, len(seg), 30):
            c[tuple(seg[off:off + 3])] += 1
        return c.most_common(3)
    print("bottom-row palette:", row_stats(h - 1))
    print("row h-2 palette:", row_stats(h - 2))
    print("top-row palette:", row_stats(0))

try:
    telnet_cmd("quit")
except Exception:
    pass
proc.wait(timeout=5)
try: proc.terminate()
except Exception: pass