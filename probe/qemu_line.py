import subprocess, time, os, socket

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"
MON_PORT = 4601

if os.path.exists(DBG): os.remove(DBG)
if os.path.exists(r"D:\Project\MangoOS\probe\shot2.ppm"): os.remove(r"D:\Project\MangoOS\probe\shot2.ppm")

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-vga", "std",
     "-display", "none",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
     "-debugcon", f"file:{DBG}"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

def telnet_cmd(cmd, wait=0.6):
    try:
        with socket.create_connection(("127.0.0.1", MON_PORT), timeout=5) as s:
            s.sendall(b"\r\n")
            s.settimeout(0.3)
            try: s.recv(8192)
            except socket.timeout: pass
            s.sendall((cmd + "\r\n").encode())
            data = b""
            s.settimeout(wait)
            try:
                while True:
                    chunk = s.recv(8192)
                    if not chunk: break
                    data += chunk
            except socket.timeout: pass
            return data.decode("latin1", "replace")
    except Exception as e:
        return f"(monitor error: {e})"

time.sleep(8)
cmds = [
    "line 50 50 350 250 00ff00",     # green diagonal
    "line 350 50 50 250 ffffff",     # white anti-diagonal
    "line 50 250 350 50 ffff00",     # yellow diagonal other way
]
for text in cmds:
    for ch in text:
        key = {" ":"spc", ".":"dot"}.get(ch, ch)
        telnet_cmd("sendkey " + key, 0.1)
        time.sleep(0.1)
    telnet_cmd("sendkey ret")
    time.sleep(0.8)
time.sleep(1)

# sample: pixels on the line paths. fb pitch 3200 (800x600x32)
def off(x, y):
    return 0xFD000000 + y * 3200 + x * 4

print("=== green line samples ===")
for (x, y) in [(50, 50), (140, 110), (200, 150), (300, 210), (350, 250)]:
    print(telnet_cmd("xp /4bx " + hex(off(x, y))))
print("=== white line samples ===")
for (x, y) in [(350, 50), (250, 110), (200, 150), (100, 210), (50, 250)]:
    print(telnet_cmd("xp /4bx " + hex(off(x, y))))