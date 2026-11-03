import subprocess, time, os, socket, sys

sys.stdout.reconfigure(encoding="utf-8")
QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"
MON_PORT = 4605

if os.path.exists(DBG): os.remove(DBG)
for f in ("probe/full.png", "probe/window_pos.txt"):
    if os.path.exists(f): os.remove(f)

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-vga", "std",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
     "-debugcon", f"file:{DBG}",
     "-display", "sdl"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

class Mon:
    def __init__(self, port):
        for _ in range(50):
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
                break
            except OSError:
                time.sleep(0.2)
        else:
            raise RuntimeError("no monitor")
        self.s.settimeout(2)
        self.drain()

    def drain(self):
        try:
            while True:
                if not self.s.recv(4096):
                    break
        except socket.timeout:
            pass

    def cmd(self, c, wait=3.0):
        self.s.sendall((c + "\r\n").encode())
        buf = b""
        end = time.time() + wait
        while time.time() < end:
            try:
                chunk = self.s.recv(4096)
                if not chunk:
                    break
                buf += chunk
                if b"(qemu)" in buf:
                    break
            except socket.timeout:
                pass
        return buf.decode("latin1", "replace")

time.sleep(8)
mon = Mon(MON_PORT)
cmds = [
    "frame 100 100 300 200 ff0000",
    "circle 400 250 80 00ff00",
    "grad 0 500 800 40",
]
for text in cmds:
    for ch in text:
        key = {" ":"spc", ".":"dot"}.get(ch, ch)
        mon.cmd("sendkey " + key, wait=1.5)
        time.sleep(0.2)
    mon.cmd("sendkey ret", wait=1.5)
    time.sleep(1.0)
time.sleep(1)

r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                    "-File", r"D:\Project\MangoOS\probe\screen_qemu.ps1"],
                   capture_output=True, text=True, timeout=60)
print("PS:", r.stdout.strip())
open(r"D:\Project\MangoOS\probe\window_pos.txt", "w").write(r.stdout.strip())

try: mon.cmd("quit", wait=1)
except Exception: pass
proc.wait(timeout=5)
try: proc.terminate()
except Exception: pass
print("DONE")