"""Perf probe: boot MangoOS, type into the real PS/2 keyboard via the QEMU
monitor, and capture the serial 'gui fps=' / FC benchmark lines.

Usage:  python probe_perf.py [gui|fc|shell]
"""
import subprocess, time, socket, sys, os, re

sys.stdout.reconfigure(encoding="utf-8")

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG = r"D:\Project\MangoOS\image\disk.img"
SER_PORT, MON_PORT = 4632, 4633

mode = sys.argv[1] if len(sys.argv) > 1 else "gui"


def start_qemu():
    p = subprocess.Popen(
        [QEMU,
         "-drive", "file=%s,format=raw" % IMG,
         "-rtc", "base=localtime", "-boot", "c", "-m", "16",
         "-vga", "std", "-display", "none",
         "-serial", "tcp:127.0.0.1:%d,server,nowait" % SER_PORT,
         "-monitor", "tcp:127.0.0.1:%d,server,nowait" % MON_PORT],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    time.sleep(3)
    if p.poll() is not None:
        raise RuntimeError("qemu exited early")
    return p


def conn(port):
    for _ in range(150):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("no port %d" % port)


def mon_cmd(mon, c, wait=0.6):
    try:
        mon.sendall((c + "\r\n").encode())
    except OSError:
        return b""
    buf = b""
    end = time.time() + wait
    while time.time() < end:
        try:
            ch = mon.recv(4096)
            if not ch:
                break
            buf += ch
            if b"(qemu)" in buf:
                break
        except socket.timeout:
            pass
    return buf


def drain(ser, seconds):
    data = b""
    end = time.time() + seconds
    while time.time() < end:
        try:
            ch = ser.recv(4096)
            if ch:
                data += ch
        except socket.timeout:
            pass
    return data.decode("latin1", "replace")


def type_text(mon, text):
    """Send each character as a real PS/2 keystroke via the monitor.
    The shell dispatcher is case sensitive, so uppercase needs shift."""
    for ch in text:
        if ch.isupper():
            mon_cmd(mon, "sendkey shift-%s" % ch.lower(), wait=0.10)
        else:
            mon_cmd(mon, "sendkey %s" % ch, wait=0.10)
        time.sleep(0.06)


proc = start_qemu()
ser, mon = conn(SER_PORT), conn(MON_PORT)
print("waiting for shell-ready marker...", flush=True)

# The serial TCP server only keeps one client, so output produced before we
# connect is lost.  "SHELL: poll alive" repeats every ~2M idle polls, so it
# is the reliable "the REPL is up and accepting keys" signal.
READY = b"SHELL: poll alive"
boot = b""
end = time.time() + 180
while READY not in boot and time.time() < end:
    try:
        ch = ser.recv(4096)
        if ch:
            boot += ch
    except socket.timeout:
        pass
if READY not in boot:
    print("NO SHELL. got:", boot.decode("latin1", "replace")[-400:])
    proc.kill(); sys.exit(1)
print("shell ready", flush=True)

time.sleep(2)
drain(ser, 1)

if mode == "gui":
    type_text(mon, "mangoWin")
    mon_cmd(mon, "sendkey ret", wait=0.5)
    time.sleep(3)
    # wiggle the mouse so cursor-update cost is measured too
    for i in range(6):
        mon_cmd(mon, "mouse_move 4 0", wait=0.25); time.sleep(0.05)
    for i in range(6):
        mon_cmd(mon, "mouse_move -4 0", wait=0.25); time.sleep(0.05)
    data = drain(ser, 14)
    lines = [l.strip() for l in data.splitlines() if "gui fps=" in l]
    print("stats lines: %d" % len(lines))
    for ln in lines:
        print("  " + ln)
elif mode == "fc":
    type_text(mon, "fc")
    mon_cmd(mon, "sendkey ret", wait=0.5)
    time.sleep(5)
    drain(ser, 2)
    mon_cmd(mon, "sendkey f", wait=0.2); mon_cmd(mon, "sendkey c", wait=0.2)
    mon_cmd(mon, "sendkey r", wait=0.2); mon_cmd(mon, "sendkey ret", wait=0.5)
    time.sleep(6)
    data = drain(ser, 10)
    for ln in data.splitlines():
        s = ln.strip()
        if s and ("fps" in s or "bench" in s or "FC" in s or "fcr" in s):
            print("  " + s)
else:
    type_text(mon, "help")
    mon_cmd(mon, "sendkey ret", wait=0.5)
    time.sleep(2)
    print(drain(ser, 3))

ser.close()
mon_cmd(mon, "quit", wait=0.5)
try:
    proc.wait(timeout=8)
except subprocess.TimeoutExpired:
    proc.kill()
print("DONE")