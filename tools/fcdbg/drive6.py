import socket, time, subprocess, os

qemu = r"D:\Softwares\qemu\qemu-system-i386.exe"
logdir = r"D:\Project\MangoOS\tools\fcdbg"
outf = os.path.join(logdir, "qemu_out.txt")
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

time.sleep(2)
s = socket.create_connection(("127.0.0.1", 4444), timeout=30)
recv_loop(s, 12)
def send(x, wait):
    s.sendall(x.encode())
    recv_loop(s, wait)

send("fc\r", 10)                  # enter the emulator, let the test ROM init
send("fcm 370\r", 2)              # reset register dump
send("fcm 3F0\r", 2)              # magic marker
send("fcdbg\r", 2)                # PC + last PCs
send("fcs\r", 2)                  # full state
send("fcm 360\r", 2)              # PPU readback flag
send("fcm 320\r", 3)              # nametable readback buffer
send("fcm 340\r", 3)              # palette readback buffer
send("fcp\r", 2)                  # palette RAM
send("fcdbg\r", 2)                # PC again (should be at $80DF)
send("fcq\r", 2)
recv_loop(s, 2)
s.close()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()
out.close()
txt = "".join(log)
open(os.path.join(logdir, "qemu_serial.txt"), "w").write(txt)
print("done, got", len(txt), "chars")