import subprocess, time, socket, sys

sys.stdout.reconfigure(encoding="utf-8")

p = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4632,server,nowait",
     "-monitor", "tcp:127.0.0.1:4633,server,nowait"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL)

def conn(port):
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("no port")

ser = conn(4632)
time.sleep(6)

boot = b""
end = time.time() + 30
while b"kernel loaded" not in boot and time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: boot += chunk
    except socket.timeout:
        pass
if not boot:
    print("no serial data; aborting")
    p.kill(); sys.exit(1)

# collect until the shell prompt appears (demo processes have exited)
out = boot
end = time.time() + 15
while b"Mango> " not in out and time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: out += chunk
    except socket.timeout:
        pass

text = out.decode("latin1", "replace")
lines = [ln for ln in text.splitlines() if len(ln) == 3 and ln[1] == ":"]
print("demo lines:", lines)

ok_ab = True
for k in range(5):
    if not (f"A:{k}" in lines and f"B:{k}" in lines):
        ok_ab = False
valid = {"A:0", "A:1", "A:2", "A:3", "A:4", "B:0", "B:1", "B:2", "B:3", "B:4"}
alt = [ln[0] for ln in lines if ln in valid]
exp = ["A", "B"] * 5
print(f"{'OK' if ok_ab else 'FAIL'} both demos printed 5 lines each")
print(f"{'OK' if alt == exp else 'FAIL'} strict A/B alternation: {alt}")

# after the demos exited, serial goes quiet (the shell talks to the VGA,
# not the serial port) - no further demo output must appear
time.sleep(4)
rest = b""
end = time.time() + 3
while time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: rest += chunk
    except socket.timeout:
        pass
print(("OK" if b"A:" not in rest and b"B:" not in rest else "FAIL") +
      " no demo output after exit (serial quiet)")

ser.close()
p.kill()
print("DONE")