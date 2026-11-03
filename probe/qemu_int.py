import subprocess, time, os, sys

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
INTLOG = r"D:\Project\MangoOS\probe\int.log"

if os.path.exists(INTLOG):
    try: os.remove(INTLOG)
    except OSError: pass

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "none",
     "-no-reboot", "-d", "int,cpu_reset,guest_errors", "-D", INTLOG,
     "-debugcon", "file:" + r"D:\Project\MangoOS\probe\dbg2.log"],
    stderr=subprocess.DEVNULL,
)

time.sleep(8)
proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()

print("=== DEBUGCON ===")
p = r"D:\Project\MangoOS\probe\dbg2.log"
print(open(p, "rb").read().decode("latin1", "replace") if os.path.exists(p) else "(none)")
print("=== INT LOG (tail) ===")
if os.path.exists(INTLOG):
    data = open(INTLOG, "rb").read().decode("latin1", "replace").splitlines()
    for ln in data[-60:]:
        print(ln)
else:
    print("(no int log)")