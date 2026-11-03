import subprocess, time, os

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
SER  = r"C:/Users/yuzhe/AppData/Local/Temp/opencode/serial.log"

for p in (SER,):
    if os.path.exists(p):
        try: os.remove(p)
        except OSError: pass

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "none",
     "-serial", f"file:{SER}"],
    stderr=subprocess.DEVNULL,
)

time.sleep(15)
if os.path.exists(SER):
    print("=== SERIAL LOG ===")
    print(open(SER, "rb").read().decode("latin1", "replace"))
else:
    print("no serial log")

proc.terminate()
try: proc.wait(timeout=3)
except Exception: proc.kill()