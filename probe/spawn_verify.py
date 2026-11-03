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

out = b""
end = time.time() + 12
while time.time() < end:
    try:
        chunk = ser.recv(4096)
        if chunk: out += chunk
    except socket.timeout:
        pass

txt = out.decode("latin1", "replace")
lines = txt.splitlines()
print("full output:")
for ln in lines:
    print("  " + ln)

kids = [f"C{i} done" for i in range(20)]
print("OK" if all(k in lines for k in kids) else "FAIL",
      f"all 20 children ran ({len([l for l in lines if l.startswith('C') and l.endswith(' done')])} done lines)")
print("OK" if "SPAWN FAIL" not in lines else "FAIL", "no SPAWN FAIL")
print("OK" if "spawner done" in lines else "FAIL", "spawner finished (slot/stack reuse OK)")

demos = [l for l in lines if l in {f"{x}:{k}" for x in "AB" for k in range(5)}]
alt = [l[0] for l in demos]
print("OK" if alt == ["A","B"]*5 else f"FAIL alternation: {alt}", "demo alternation")

ser.close()
p.kill()
print("DONE")