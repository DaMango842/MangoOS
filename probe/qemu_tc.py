import subprocess, time, os, socket

QEMU = r"D:\Softwares\qemu\qemu-system-i386.exe"
IMG  = r"D:\Project\MangoOS\image\disk.img"
DBG  = r"D:\Project\MangoOS\probe\debug.log"
MON_PORT = 4603

if os.path.exists(DBG): os.remove(DBG)
if os.path.exists(r"D:\Project\MangoOS\probe\tc.ppm"):
    os.remove(r"D:\Project\MangoOS\probe\tc.ppm")

proc = subprocess.Popen(
    [QEMU, "-drive", f"file={IMG},format=raw", "-rtc", "base=localtime",
     "-boot", "c", "-m", "16", "-vga", "std",
     "-display", "none",
     "-monitor", f"tcp:127.0.0.1:{MON_PORT},server,nowait",
     "-debugcon", f"file:{DBG}"],
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

def telnet_cmd(cmd, wait=0.8):
    try:
        with socket.create_connection(("127.0.0.1", MON_PORT), timeout=5) as s:
            s.sendall(b"\r\n")
            s.settimeout(0.3)
            try: s.recv(8192)
            except socket.timeout: pass
            s.sendall((cmd + "\r\n").encode())
            data = b""
            s.settimeout(wait)
            try:
                while True:
                    chunk = s.recv(8192)
                    if not chunk: break
                    data += chunk
            except socket.timeout: pass
            return data.decode("latin1", "replace")
    except Exception as e:
        return f"(monitor error: {e})"

time.sleep(8)
cmds = [
    "rect 0 0 40 40 ff0000",
    "rect 50 0 40 40 00ff00",
    "rect 100 0 40 40 0000ff",
    "rect 150 0 40 40 ffff00",
    "rect 200 0 40 40 ffffff",
]
for text in cmds:
    for ch in text:
        key = {" ":"spc", ".":"dot"}.get(ch, ch)
        telnet_cmd("sendkey " + key)
        time.sleep(0.12)
    telnet_cmd("sendkey ret")
    time.sleep(0.8)
time.sleep(1)

# LFB bytes at the middle of each rect (y=20)
for name, addr in [("rect1 ff0000  x=20", "0xFD001400+16"),  # y*3200+x*4 = 64000+80 = 0xFA50
                   ]:
    pass
# offsets: (20,20)-> 20*3200+20*4 = 64080 = 0xFA50
# (70,20)-> 20*3200+70*4 = 64280 = 0xFB18
# (120,20)-> 20*3200+120*4 = 64480 = 0xFBE0
# (170,20)-> 20*3200+170*4 = 64680 = 0xFC A8 -> 0xFD000000+0xFCA8 = 0xFD00FCA8
# (220,20)-> 20*3200+220*4 = 64880 = 0xFD70 -> 0xFD00FD70
print("=== xp rect centers ===")
print(telnet_cmd("xp /4bx 0xFD000FA50"))
print(telnet_cmd("xp /4bx 0xFD00FB18"))
print(telnet_cmd("xp /4bx 0xFD00FBE0"))
print(telnet_cmd("xp /4bx 0xFD00FCA8"))
print(telnet_cmd("xp /4bx 0xFD00FD70"))
print("=== screendump ===")
print(telnet_cmd("screendump D:\\Project\\MangoOS\\probe\\tc.ppm"))
time.sleep(2)
try: telnet_cmd("quit")
except Exception: pass
proc.wait(timeout=5)
try: proc.terminate()
except Exception: pass
print("DONE")