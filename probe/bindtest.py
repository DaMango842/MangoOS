import socket, sys
for port in (4450, 4451, 4452, 4446, 4447):
    s = socket.socket()
    try:
        s.bind(('127.0.0.1', port))
        print(port, 'bind OK by python')
    except Exception as e:
        print(port, 'bind FAIL:', e)
    s.close()