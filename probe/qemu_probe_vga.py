import subprocess, time, os

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
VGA  = r"C:/Users/yuzhe/AppData/Local/Temp/opencode/vga.bin"

if os.path.exists(VGA):
    try: os.remove(VGA)
    except OSError: pass

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "stdio"],
    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    text=False, bufsize=0,
)

def send(cmd):
    proc.stdin.write(cmd.encode() + b"\n")
    proc.stdin.flush()

def snap(path, tries=10):
    send(f"pmemsave 0xb8000 0x2000 {path}")
    for _ in range(tries):
        time.sleep(0.5)
        if os.path.exists(path) and os.path.getsize(path) == 0x2000:
            return read(path)
    return None

def read(path):
    data = open(path, "rb").read(0x2000)
    lines = []
    for r in range(25):
        row = data[r*160:(r+1)*160]
        txt = "".join(chr(row[i]) if row[i] != 0 else " " for i in range(0, 160, 2))
        st = txt.rstrip()
        if st.strip(): lines.append(st)
    return lines

time.sleep(16)
s = snap(VGA, 12)
print("=== SCREEN ===")
print("\n".join(s) if s else "(no dump)")

proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()