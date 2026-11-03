import subprocess, time, os

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
ASMLOG = r"D:\Project\MangoOS\probe\in_asm.log"

if os.path.exists(ASMLOG):
    try: os.remove(ASMLOG)
    except OSError: pass

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "none",
     "-no-reboot", "-d", "in_asm,cpu_reset", "-D", ASMLOG,
     "-debugcon", "file:" + r"D:\Project\MangoOS\probe\dbg2.log"],
    stderr=subprocess.DEVNULL,
)

time.sleep(7)
proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()

with open(r"D:\Project\MangoOS\probe\dbg2.log", "rb") as f:
    d = f.read().decode("latin1", "replace")
print("=== DEBUGCON ===")
print(d)
print("=== total in_asm log lines: ", sum(1 for _ in open(ASMLOG, "rb")), "===")