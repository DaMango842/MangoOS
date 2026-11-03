import subprocess, time, os, socket

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"
MON_PORT = 4600

if os.path.exists(DBG): os.remove(DBG)
if os.path.exists(r"D:\Project\MangoOS\probe\shot.ppm"): os.remove(r"D:\Project\MangoOS\probe\shot.ppm")

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
    "rect 50 50 300 200 ff0000",
    "rect 100 100 200 100 0000ff",
    "line 50 50 350 250 00ff00",
    "line 350 50 50 250 ffffff",
    "line 50 250 350 50 ffff00",
]
for text in cmds:
    for ch in text:
        key = {" ":"spc", ".":"dot"}.get(ch, ch)
        telnet_cmd("sendkey " + key)
        time.sleep(0.12)
    telnet_cmd("sendkey ret")
    time.sleep(0.8)
time.sleep(1)
print("=== xp fb 0x000000 ===")
print(telnet_cmd("xp /16bx 0xFD000000"))
print("=== xp fb 0x1000 ===")
print(telnet_cmd("xp /16bx 0xFD001000"))
print("=== xp fb 0x10000 ===")
print(telnet_cmd("xp /16bx 0xFD010000"))
print("=== screendump ===")
print(telnet_cmd("screendump D:\\Project\\MangoOS\\probe\\shot.ppm"))
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