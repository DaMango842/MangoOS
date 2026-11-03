import subprocess, time, os, socket

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"
MON_PORT = 4446

if os.path.exists(DBG): os.remove(DBG)

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none",
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
    "create n.txt",
    "write n.txt this is a test file.",
    "del n.txt",
]
for text in cmds:
    print("type:", text)
    for ch in text:
        telnet_cmd("sendkey " + {" ":"spc", ".":"dot"}.get(ch, ch))
        time.sleep(0.15)
    telnet_cmd("sendkey ret")
    time.sleep(1)
time.sleep(3)
print("=== after del: root buf on disk ===")
print(telnet_cmd("xp /16bx " + hex(53 * 512)))
print(telnet_cmd("xp /16bx " + hex(52 * 512)))
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