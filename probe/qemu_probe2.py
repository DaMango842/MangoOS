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

def dump_screen(tries=12):
    send(f"pmemsave 0xb8000 0x2000 {VGA}")
    for _ in range(tries):
        time.sleep(0.5)
        if os.path.exists(VGA) and os.path.getsize(VGA) == 0x2000:
            return
    return None

def screen():
    data = open(VGA, "rb").read(0x2000)
    lines = []
    for r in range(25):
        row = data[r*160:(r+1)*160]
        txt = "".join(chr(row[i]) if row[i] != 0 else " " for i in range(0, 160, 2))
        st = txt.rstrip()
        if st.strip(): lines.append(st)
    return lines

time.sleep(14)
if dump_screen():
    print("RESULT:"); print("\n".join(screen()))
else:
    print("no screen dump")

proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()