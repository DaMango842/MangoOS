import subprocess, time, os

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"

if os.path.exists(DBG):
    try: os.remove(DBG)
    except OSError: pass

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "none",
     "-debugcon", f"file:{DBG}"],
    stderr=subprocess.DEVNULL,
)

time.sleep(15)
print("=== DEBUGCON LOG ===")
if os.path.exists(DBG):
    print(open(DBG, "rb").read().decode("latin1", "replace"))
else:
    print("(no file)")

proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()