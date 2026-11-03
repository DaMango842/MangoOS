import subprocess, time, socket, sys, os

sys.stdout.reconfigure(encoding="utf-8")
proc = subprocess.Popen(
    [r"D:\Softwares\qemu\qemu-system-i386.exe",
     "-drive", "file=D:\\Project\\MangoOS\\image\\disk.img,format=raw",
     "-rtc", "base=localtime", "-boot", "c", "-m", "16",
     "-vga", "std", "-display", "none",
     "-serial", "tcp:127.0.0.1:4632,server,nowait",
     "-monitor", "tcp:127.0.0.1:4633,server,nowait"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def conn(port):
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(1)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("no port")

def mon_cmd(mon, c, wait=2.0):
    mon.sendall((c + "\r\n").encode())
    buf = b""
    end = time.time() + wait
    while time.time() < end:
        try:
            chunk = mon.recv(4096)
            if not chunk: break
            buf += chunk
            if b"(qemu)" in buf: break
        except socket.timeout:
            pass
    return buf

time.sleep(8)
ser = conn(4632)
mon = conn(4633)
W = 800
TEXT_H = 12 * 16

def shot(name):
    d = rf"D:\Project\MangoOS\probe\{name}.ppm"
    mon_cmd(mon, f"screendump {d}", wait=3)
    time.sleep(1.2)
    return open(d, "rb").read().split(b"\n255\n", 1)[1]

def text_region(body):
    return body[:TEXT_H * W * 3]

def same_text(a, b):
    return text_region(a) == text_region(b)

def rows0_1_same(a, b):
    return a[:2 * 16 * W * 3] == b[:2 * 16 * W * 3]

def no_gui_leftovers(body):
    for y in range(0, 600, 2):
        for x in range(0, 800, 2):
            i = (y * W + x) * 3
            if (body[i], body[i + 1], body[i + 2]) == (0x1F, 0x3F, 0xBF):
                return False
    return True

def row_has_text(body, row):
    y0 = row * 16
    return any(any(body[(y0 * W + x) * 3:(y0 * W + x) * 3 + 3])
               for x in range(0, 600))

def in_gui(body):
    i = (95 * W + 200) * 3
    return (body[i], body[i + 1], body[i + 2])

def wait_for_boot():
    for _ in range(120):
        body = shot("s_boot")
        lit = sum(1 for x in range(0, 600)
                  if any(body[(16 * W + x) * 3:(16 * W + x) * 3 + 3]))
        if lit > 0:
            time.sleep(1.0)
            return
    print("WARN: boot not detected")

wait_for_boot()
for _ in range(5):
    pre = shot("s0_cli")
    lit = sum(1 for x in range(0, 600)
              if any(pre[(16 * W + x) * 3:(16 * W + x) * 3 + 3]))
    if lit > 0:
        break
    time.sleep(1.0)
print("CLI snapshot taken")

# ---- scene 1: enter, then Esc quits ----
ser.sendall(b"mangoWin\r\n")
time.sleep(2.5)
g = shot("s1_gui")
print("GUI shown:", "OK" if in_gui(g) == (0x1F, 0x3F, 0xBF) else f"FAIL pixel={in_gui(g)}")
mon_cmd(mon, "sendkey esc", wait=1.5)
time.sleep(1.5)
post1 = shot("s1_cli")
r1 = (rows0_1_same(pre, post1)
      and row_has_text(post1, 2)
      and row_has_text(post1, 3)
      and no_gui_leftovers(post1))
print("Esc exit -> console restored:", "OK" if r1 else "FAIL")

# ---- scene 2: enter, click close button of win1 ----
ser.sendall(b"mangoWin\r\n")
time.sleep(2.5)
g = shot("s2_gui")
print("GUI shown:", "OK" if in_gui(g) == (0x1F, 0x3F, 0xBF) else f"FAIL pixel={in_gui(g)}")
mon_cmd(mon, "mouse_move 72 -100", wait=1)
mon_cmd(mon, "mouse_move 0 -100", wait=1)
mon_cmd(mon, "mouse_move 0 -2", wait=1)
mon_cmd(mon, "mouse_button 1", wait=1)
time.sleep(0.4)
mon_cmd(mon, "mouse_button 0", wait=1)
time.sleep(1.5)
post2 = shot("s2_cli")
r2 = (rows0_1_same(pre, post2)
      and row_has_text(post2, 2)
      and row_has_text(post2, 3)
      and no_gui_leftovers(post2))
print("close-button exit -> console restored:", "OK" if r2 else "FAIL")

ser.close()
mon.sendall(b"quit\r\n")
time.sleep(1)
proc.wait(timeout=8)
print("DONE")