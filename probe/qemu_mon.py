import subprocess, time, threading

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"

p = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-display", "none", "-monitor", "stdio",
     "-debugcon", "file:" + r"D:\Project\MangoOS\probe\dbg3.log"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def reader():
    while True:
        line = p.stdout.readline()
        if not line:
            break
        print(line.decode("latin1", "replace").rstrip())
threading.Thread(target=reader, daemon=True).start()

time.sleep(9)
for cmd in ["xp /48bx 0x10000\n", "xp /24bx 0x7e00\n", "info registers\n"]:
    p.stdin.write(cmd.encode())
    p.stdin.flush()
    time.sleep(0.7)
time.sleep(1)
p.terminate()
try: p.wait(timeout=3)
except Exception: p.kill()

print("=== debugcon ===")
try:
    print(open(r"D:\Project\MangoOS\probe\dbg3.log", "rb").read().decode("latin1", "replace"))
except OSError as e:
    print("no", e)