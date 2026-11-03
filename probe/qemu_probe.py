import subprocess, time, sys, os, threading

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
LOW  = r"C:/Users/yuzhe/AppData/Local/Temp/opencode/low.bin"
VGA  = r"C:/Users/yuzhe/AppData/Local/Temp/opencode/vga.bin"
SER  = r"C:/Users/yuzhe/AppData/Local/Temp/opencode/serial.log"

for p in (LOW, VGA, SER):
    if os.path.exists(p):
        try: os.remove(p)
        except OSError: pass

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "stdio",
     "-serial", f"file:{SER}"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    text=False, bufsize=0,
)

monitor_lines = []
def reader():
    while True:
        chunk = proc.stdout.read(1)
        if not chunk:
            break
        monitor_lines.append(chunk.decode("latin1", "replace"))
threading.Thread(target=reader, daemon=True).start()

def send(cmd):
    proc.stdin.write(cmd.encode() + b"\n")
    proc.stdin.flush()

def dump(addr, size, dst, tries=10):
    send(f"pmemsave {addr:x} {size:x} {dst}")
    for _ in range(tries):
        time.sleep(0.5)
        if os.path.exists(dst) and os.path.getsize(dst) == size:
            return
    raise RuntimeError("dump failed " + dst)

def hexd(data, off, n):
    return " ".join(f"{data[off+i]:02x}" for i in range(n))

def screen():
    data = open(VGA, "rb").read(0x2000)
    lines = []
    for r in range(25):
        row = data[r*160:(r+1)*160]
        txt = "".join(chr(row[i]) if row[i] != 0 else " " for i in range(0, 160, 2))
        st = txt.rstrip()
        if st.strip(): lines.append(st)
    return lines

def search(sub):
    return [l for l in monitor_lines if sub.lower() in l.lower()]

time.sleep(10)
dump(0, 0x40000, LOW)
dump(0, 0x2000, VGA)
print("=== BOOT MAP @0x3000 ===")
low = open(LOW, "rb").read()
print("  magic:", low[0x3000:0x300c].decode("latin1", "replace"))
print("  s2_lba:", hexd(low, 0x3006, 4), "s2_sec:", hexd(low, 0x300A, 4), "fs_super:", hexd(low, 0x300E, 4))
print("  0x0520 pass:", hexd(low, 0x0520, 8))
print("  stage2@0x7E00:", hexd(low, 0x7E00, 16))
print("  kernel@0x10000:", hexd(low, 0x10000, 16))
print("  super magic@0x30000:", low[0x30000:0x30008].decode("latin1", "replace"))
print("=== VGA SCREEN ===")
print(screen())

send("info registers")
time.sleep(1)
print("=== MONITOR registers ===")
lines = [l for l in monitor_lines if l.startswith(("EAX=", "EBX=", "ECX=", "EDX=", "EIP=", "CS ", "EFL=", "CR3"))]
print("\n".join(lines) or "(none captured)")

proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()