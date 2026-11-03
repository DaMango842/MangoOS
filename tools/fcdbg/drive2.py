import socket, time, subprocess

qemu = r"D:\Softwares\qemu\qemu-system-i386.exe"
outf = r"C:\Users\yuzhe\AppData\Local\Temp\opencode\qemu_out.txt"
args = [qemu, "-hda", r"D:\Project\MangoOS\image\disk.img",
        "-rtc", "base=localtime", "-boot", "c",
        "-serial", "tcp:127.0.0.1:4444,server,nowait",
        "-m", "16", "-display", "none", "-vga", "std"]
out = open(outf, "wb")
p = subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT)

log = []

def recv_loop(s, dur):
    s.settimeout(0.5)
    end = time.time() + dur
    while time.time() < end:
        try:
            b = s.recv(65536)
            if b:
                log.append(b.decode("latin1"))
        except socket.timeout:
            pass

time.sleep(3)
s = socket.create_connection(("127.0.0.1", 4444), timeout=30)

recv_loop(s, 20)                 # boot + self-tests
def send(x, wait):
    s.sendall(x.encode())
    recv_loop(s, wait)

send("help\r", 3)
send("fc\r", 8)
send("fcs\r", 3)
send("fcv 0\r", 4)
send("fcp\r", 3)
send("fcs\r", 3)
send("fcbtn 10\r", 5)
send("fcs\r", 3)
send("fcbtn 0\r", 3)
send("fcs\r", 3)
send("fcq\r", 3)
recv_loop(s, 3)
s.close()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()
out.close()
open(r"C:\Users\yuzhe\AppData\Local\Temp\opencode\qemu_serial.txt", "w").write("".join(log))
print("done, got", sum(len(x) for x in log), "chars")