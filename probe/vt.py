import socket
def tc(cmd, wait=0.6):
    try:
        s = socket.create_connection(('127.0.0.1', 4600), timeout=3)
        s.settimeout(wait)
        s.sendall((cmd + '\r\n').encode())
        d = b''
        try:
            while True:
                c = s.recv(8192)
                if not c:
                    break
                d += c
        except socket.timeout:
            pass
        s.close()
        return d.decode('latin1', 'replace')
    except Exception as e:
        return 'ERR ' + str(e)

# green line midpoint: (200,150) -> offset 150*3200 + 200*4 = 0x75620
print(tc('xp /8bx 0xFD075620'))