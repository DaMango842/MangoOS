import subprocess, time, os

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
LOW  = r"C:/Users/yuzhe/AppData/Local/Temp/opencode/low.bin"

if os.path.exists(LOW):
    try: os.remove(LOW)
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

time.sleep(14)
send(f"pmemsave 0x0 0x20000 {LOW}")
time.sleep(1)
for _ in range(12):
    time.sleep(0.5)
    if os.path.exists(LOW) and os.path.getsize(LOW) == 0x20000:
        break

low = open(LOW, "rb").read()

def hx(o, n):
    return " ".join(f"{low[o+i]:02x}" for i in range(n))

print("stage2@0x7E00 first16:", hx(0x7E00, 16))
print("  as ascii:", low[0x7E00:0x7E40])
print("  'stage2 loaded' str:", low[0x7E00+10:0x7E00+40])
print("kernel@0x10000 first16:", hx(0x10000, 16))
print("0x520 pass:", hx(0x520, 4))
print("0x502 super:", hx(0x502, 4), "0x506 data:", hx(0x506, 4))

proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()