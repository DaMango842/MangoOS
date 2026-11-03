# probe/kbd_diag.py - keyboard path diagnosis, round 2.
# Boots MangoOS headless and decides between:
#   (a) the shell polling loop died after kbd_init   -> no heartbeat
#   (b) the 8042 controller never delivers keystrokes -> OBF stays 0
#
# Techniques:
#   - serial heartbeat "SHELL: poll alive" printed every ~2M empty polls
#   - QEMU monitor `inb`/`outb` to inspect the 8042 directly:
#       status reg 0x64, command byte (read via 0x20), keyboard echo
#       (0xEE -> device answers 0xEE when alive)
#   - `sendkey` + immediate status read to see if OBF gets set

import subprocess, socket, time, sys, re

sys.stdout.reconfigure(encoding="utf-8")

QEMU   = r"D:\Softwares\qemu\qemu-system-i386.exe"
DISK   = r"D:\Project\MangoOS\image\disk.img"
SPORT  = 4732   # serial
MPORT  = 4733   # monitor

p = subprocess.Popen(
    [QEMU, "-drive", f"file={DISK},format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", f"tcp:127.0.0.1:{SPORT},server,nowait",
     "-monitor", f"tcp:127.0.0.1:{MPORT},server,nowait"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL)

def conn(port):
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(0.5)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"cannot connect to port {port}")

def drain(s, wait=0.0):
    if wait:
        time.sleep(wait)
    out = b""
    while True:
        try:
            c = s.recv(4096)
            if not c:
                break
            out += c
        except socket.timeout:
            return out

def mon(mon_s, cmd, wait=0.35):
    drain(mon_s)
    mon_s.sendall((cmd + "\r\n").encode())
    return drain(mon_s, wait).decode("latin1", "replace")

def mon_inb(mon_s, port):
    """read one guest IO port via the monitor; returns int or None.
    Skips the command echo line (which contains the port number as hex,
    e.g. 'inb 0x64') and parses the value from the remaining lines."""
    r = mon(mon_s, f"inb {port}")
    lines = [l.strip() for l in r.splitlines()
             if l.strip() and not l.strip().startswith("(")]
    if lines and lines[0].startswith("inb"):
        lines = lines[1:]
    for l in reversed(lines):
        m = re.search(r"\b([0-9a-fA-F]{1,2})\b", l)
        if m:
            return int(m.group(1), 16)
    return None

try:
    ser = conn(SPORT)
    mon_s = conn(MPORT)
    drain(mon_s, 1.0)

    print("=== boot output (8s) ===")
    end = time.time() + 8
    boot = b""
    while time.time() < end:
        boot += drain(ser)
    boot_text = boot.decode("latin1", "replace")
    print(repr(boot_text))

    print("\n=== 8042 controller state (before keys) ===")
    st = mon_inb(mon_s, 0x64)
    print(f"status 0x64 = {hex(st) if st is not None else 'READ FAILED'}  (bit0=OBF, bit5=aux)")
    if st is not None and st & 0x01:
        b = mon_inb(mon_s, 0x60)
        print(f"  pending byte in buffer: {hex(b) if b is not None else 'READ FAILED'}")
    # proper CCB read: send controller command 0x20 to port 0x64, then
    # read the byte from 0x60 (a bare read of port 0x20 is unreliable)
    mon(mon_s, "outb 0x64 0x20")
    time.sleep(0.15)
    ccb = mon_inb(mon_s, 0x60)
    if ccb is not None:
        print(f"command byte = 0x{ccb:02X}  bit4(kbd interface disabled)={1 if ccb & 0x10 else 0}"
              f"  bit5(aux interface disabled)={1 if ccb & 0x20 else 0}"
              f"  bit6(translation disabled)={1 if ccb & 0x40 else 0}")
        if ccb & 0x10:
            print("  !! KEYBOARD INTERFACE IS DISABLED in the controller")
    else:
        print("command byte read FAILED")

    print("\n=== keyboard echo test (0xEE) ===")
    # a plain write to 0x60 (no 0xD4 prefix) goes to the KEYBOARD; the
    # device must answer 0xEE when it is alive
    mon(mon_s, "outb 0x60 0xEE")
    time.sleep(0.3)
    st = mon_inb(mon_s, 0x64)
    echo = None
    if st is not None and st & 0x01:
        echo = mon_inb(mon_s, 0x60)
    print(f"after 0xEE: status=0x{st:02X}, response byte={hex(echo) if echo is not None else None}")
    if echo == 0xEE:
        print("  keyboard device ALIVE (echoed)")
    elif echo is not None:
        print(f"  unexpected echo response 0x{echo:02X}")
    else:
        print("  keyboard device did NOT echo (dead/disabled)")
    leftover = drain(ser)
    if leftover:
        print("  (guest consumed something in the meantime: "
              + repr(leftover.decode("latin1", "replace")) + ")")

    print("\n=== sendkey sequence (a, d, m, shift-a) ===")
    # the driver now logs every scancode; after the break-byte
    # normalisation each UP base must equal its DOWN base.
    for k in ("a", "d", "m", "shift-a"):
        mon(mon_s, f"sendkey {k}")
        time.sleep(0.4)

    print("\n=== serial during/after keys (6s, watch heartbeat) ===")
    end = time.time() + 6
    after = b""
    while time.time() < end:
        after += drain(ser)
    text = after.decode("latin1", "replace")
    print(repr(text))

    kbd_lines = [l for l in text.splitlines() if "KBD" in l]
    alive = "poll alive" in text or "poll alive" in boot_text
    print(f"\nKBD trace lines: {len(kbd_lines)}")
    print(f"polling-loop heartbeat seen: {alive}")

    # pair check: every DOWN base should be matched by an identical UP base
    downs, ups = [], []
    for l in kbd_lines:
        m = re.search(r"KBD: (DOWN|UP) 0x([0-9A-Fa-f]{2})", l)
        if m:
            (downs if m.group(1) == "DOWN" else ups).append(int(m.group(2), 16))
    matched = sum(1 for d in downs if ups.count(d) >= downs.count(d))
    print(f"DOWN bases: {[hex(x) for x in downs]}")
    print(f"UP bases:   {[hex(x) for x in ups]}")

    if downs and ups and sorted(downs) == sorted(ups):
        print("\nVERDICT: make/break pairs MATCH -> scancode path healthy.")
    elif kbd_lines:
        print("\nVERDICT: keys reach the driver but DOWN/UP bases differ -> "
              "break-byte encoding still misaligned (see kbd_norm_release).")
    elif alive:
        print("\nVERDICT: polling loop alive but no scancode arrives -> "
              "8042 controller / PS2 keyboard device problem (see state above).")
    else:
        print("\nVERDICT: polling loop DIED after kbd_init -> crash/exception "
              "in the prompt path, or the scheduler never resumes the shell.")
finally:
    p.kill()
    print("DONE")
