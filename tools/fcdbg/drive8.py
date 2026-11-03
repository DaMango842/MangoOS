import socket, time, subprocess, os, sys

qemu = r"D:\Softwares\qemu\qemu-system-i386.exe"
logdir = r"D:\Project\MangoOS\tools\fcdbg"
outf = os.path.join(logdir, "qemu_out.txt")
logf = os.path.join(logdir, "qemu_bench.txt")
args = [qemu, "-hda", r"D:\Project\MangoOS\image\disk.img",
        "-rtc", "base=localtime", "-boot", "c",
        "-serial", "tcp:127.0.0.1:4444,server,nowait",
        "-m", "16", "-display", "none", "-vga", "std"]
out = open(outf, "wb")
p = subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT)

t0 = time.time()
log = open(logf, "w")
def recv_loop(s, dur, tag=""):
    s.settimeout(0.4)
    end = time.time() + dur
    while time.time() < end:
        try:
            b = s.recv(65536)
            if b:
                for ln in b.decode("latin1").split("\r\n"):
                    if ln:
                        log.write("[%+6.2f] %s%s\n" % (time.time() - t0, tag, ln))
                log.flush()
        except socket.timeout:
            pass

time.sleep(2)
s = socket.create_connection(("127.0.0.1", 4444), timeout=30)
recv_loop(s, 8)
def send(x, wait):
    s.sendall(x.encode())
    recv_loop(s, wait)

send("fc\r", 8)
send("fcr 30\r", 10)
send("fcs\r", 2)
send("fcq\r", 2)
recv_loop(s, 2)
s.close()
try:
    p.wait(timeout=8)
except subprocess.TimeoutExpired:
    p.kill()
out.close()
log.close()
print("done")