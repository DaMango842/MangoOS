# tools/serialcat.py - live viewer for the guest serial port.
# Pairs with 'make run' / 'make run-debug', which now expose the UART on
# tcp:127.0.0.1:4632 instead of stdio (so the terminal no longer eats
# keystrokes destined for the guest PS/2 keyboard).
#
# Usage: start the VM first, then   python tools/serialcat.py
# Ctrl+C to stop watching.

import socket
import sys

HOST = "127.0.0.1"
PORT = 4632

sys.stdout.reconfigure(encoding="latin1", errors="replace")

try:
    s = socket.create_connection((HOST, PORT), timeout=10)
except OSError as e:
    print(f"serialcat: cannot connect to {HOST}:{PORT} ({e})", file=sys.stderr)
    print("serialcat: is the VM running? start it with 'make run' first",
          file=sys.stderr)
    sys.exit(1)

print(f"serialcat: connected to {HOST}:{PORT}, streaming guest serial out")
try:
    while True:
        data = s.recv(4096)
        if not data:
            break
        sys.stdout.write(data.decode("latin1", "replace"))
        sys.stdout.flush()
except KeyboardInterrupt:
    pass
finally:
    s.close()
