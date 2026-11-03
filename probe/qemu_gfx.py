import subprocess, time, os, socket

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"
MON_PORT = 4602

if os.path.exists(DBG): os.remove(DBG)
if os.path.exists(r"D:\Project\MangoOS\probe\gfx.ppm"): os.remove(r"D:\Project\MangoOS\probe\gfx.ppm")

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-vga", "std",
     "-display", "none",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
     "-debugcon", f"file:{DBG}"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

def telnet_cmd(cmd, wait=0.8):
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
    "frame 100 100 300 200 ff0000",
    "circle 400 250 80 00ff00",
    "grad 0 500 800 40",
]
for text in cmds:
    for ch in text:
        key = {" ":"spc", ".":"dot"}.get(ch, ch)
        telnet_cmd("sendkey " + key)
        time.sleep(0.12)
    telnet_cmd("sendkey ret")
    time.sleep(0.8)
time.sleep(1)

print("=== xp: frame top edge (250,100) ===")
print(telnet_cmd("xp /4bx 0xFD04E5E8"))
print("=== xp: circle top (400,170) ===")
print(telnet_cmd("xp /4bx 0xFD085400"))
print("=== xp: circle right (480,250) ===")
print(telnet_cmd("xp /4bx 0xFDC3C80"))
print("=== xp: circle bottom (400,330) ===")
print(telnet_cmd("xp /4bx 0xFD102340"))
print("=== xp: circle left (320,250) ===")
print(telnet_cmd("xp /4bx 0xFDC3A80"))
print("=== xp: grad red head (0,510) ===")
print(telnet_cmd("xp /4bx 0xFD066000"))
print("=== xp: grad green (267,510) ===")
print(telnet_cmd("xp /4bx 0xFD06762C"))
print("=== xp: grad blue (400,510) ===")
print(telnet_cmd("xp /4bx 0xFD068640"))
print("=== xp: grad red tail (600,510) ===")
print(telnet_cmd("xp /4bx 0xFD06A560"))
print("=== screendump ===")
print(telnet_cmd("screendump D:\\Project\\MangoOS\\probe\\gfx.ppm"))
time.sleep(2)
print("=== info vga ===")
print(telnet_cmd("info vga"))
print("=== DEBUGCON ===")
if os.path.exists(DBG):
    print(open(DBG, "rb").read().decode("latin1", "replace"))
else:
    print("(no file)")

try: telnet_cmd("quit")
except Exception: pass
proc.wait(timeout=5)
try: proc.terminate()
except Exception: pass
print("DONE")