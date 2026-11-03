import subprocess, time

qemu = r"D:\Softwares\qemu\qemu-system-i386.exe"
outf = r"C:\Users\yuzhe\AppData\Local\Temp\opencode\qemu_out.txt"
args = [qemu, "-hda", r"D:\Project\MangoOS\image\disk.img",
        "-rtc", "base=localtime", "-boot", "c", "-serial", "stdio",
        "-m", "16", "-display", "none", "-vga", "std"]
out = open(outf, "wb")
p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=out, stderr=subprocess.STDOUT)

def send(s, wait):
    p.stdin.write(s.encode())
    p.stdin.flush()
    time.sleep(wait)

send("help\r", 20)           # boot + self-tests, then a serial command
send("fc\r", 10)
send("fcs\r", 3)
send("fcv 0\r", 4)
send("fcp\r", 3)
send("fcs\r", 3)
send("fcbtn 10\r", 5)
send("fcs\r", 3)
send("fcbtn 0\r", 3)
send("fcs\r", 3)
send("fcq\r", 3)
p.stdin.close()
try:
    p.wait(timeout=15)
except subprocess.TimeoutExpired:
    p.kill()
out.close()
print("done")