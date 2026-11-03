import subprocess, time, os, threading

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
LOW  = r"C:/Users/yuzhe/AppData/Local/Temp/opencode/low.bin"
VGA  = r"C:/Users/yuzhe/AppData/Local/Temp/opencode/vga.bin"

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "stdio",
     "-serial", "null"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    text=False, bufsize=0,
)

mon = []
def reader():
    while True:
        c = proc.stdout.read(1)
        if not c: break
        mon.append(c.decode("latin1", "replace"))
threading.Thread(target=reader, daemon=True).start()

def send(cmd):
    proc.stdin.write(cmd.encode() + b"\n")
    proc.stdin.flush()

def wait_file(path, size, tries=12):
    for _ in range(tries):
        time.sleep(0.5)
        if os.path.exists(path) and os.path.getsize(path) == size:
            return True
    return os.path.exists(path)

time.sleep(12)
send(f"pmemsave 0x0 0x40000 {LOW}")
send(f"pmemsave 0xb8000 0x2000 {VGA}")
time.sleep(1)
ok_low = wait_file(LOW, 0x40000)
ok_vga = wait_file(VGA, 0x2000)

if ok_low:
    low = open(LOW, "rb").read()
    def hx(o, n): return " ".join(f"{low[o+i]:02x}" for i in range(n))
    print("LOW ram dump OK")
    print("  bootmap@0x3000: '", low[0x3000:0x3018].decode("latin1", "replace"), "'")
    print("  0x3004 ver:", hx(0x3004, 2), " 0x3006 s2lba:", hx(0x3006, 2), " 0x300A s2sec:", hx(0x300A, 2), " 0x300E fssup:", hx(0x300E, 2))
    print("  0x520 pass:", hx(0x520, 8))
    print("  stage2@0x7E00:", hx(0x7E00, 16))
    print("  kernel@0x10000:", hx(0x10000, 16))
for l in mon:
    if "error" in l.lower():
        print("MONITOR:", l.rstrip())

if ok_vga:
    data = open(VGA, "rb").read(0x2000)
    lines = []
    for r in range(25):
        row = data[r*160:(r+1)*160]
        txt = "".join(chr(row[i]) if row[i] != 0 else " " for i in range(0, 160, 2))
        st = txt.rstrip()
        if st.strip(): lines.append(st)
    print("=== SCREEN ===")
    print("\n".join(lines) if lines else "(blank)")

send("info registers")
time.sleep(1)
regs = [l for l in mon if l.startswith(("EAX=", "EIP=", "CS ", "ESP="))]
print("=== REGS ===")
print("".join(regs)[:1200])

proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()