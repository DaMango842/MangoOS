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

recv_loop(s, 25)                 # boot
def send(x, wait):
    s.sendall(x.encode())
    recv_loop(s, wait)

send("fc\r", 10)                 # launch emulator, title draws
send("fcs\r", 4)                 # title state
send("fcbtn 10\r", 10)           # press Start -> world transition
send("fcs\r", 4)
send("fcv 0\r", 8)               # world nametable
send("fco\r", 4)                 # OAM (sprites)
send("fcbtn 1\r", 6)             # hold Right
send("fcs\r", 4)
send("fcbtn 0\r", 2)
send("fcs\r", 4)
send("fcq\r", 3)
recv_loop(s, 3)
s.close()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()
out.close()
txt = "".join(log)
open(r"C:\Users\yuzhe\AppData\Local\Temp\opencode\qemu_serial.txt", "w").write(txt)
print("done, got", len(txt), "chars")