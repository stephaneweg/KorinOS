#!/usr/bin/env python3
"""echo.py -- le serveur de l'essai wsock.exe (WINSOCK de win16) : chaque connexion reçoit « ECHO: » suivi de ce
qu'elle a envoyé, puis la connexion est fermée.

    python3 -I tools/win16/tests/echo.py [port]        (7777 par défaut ; 127.0.0.1 seulement)"""
import socket
import sys
import threading


def serve(c):
    with c:
        c.settimeout(10)
        try:
            data = c.recv(1024)
            c.sendall(b'ECHO:' + data)
        except OSError:
            pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 7777
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('127.0.0.1', port))
    s.listen(8)
    print('echo.py : 127.0.0.1:%d' % port, flush=True)
    while True:
        c, _ = s.accept()
        threading.Thread(target=serve, args=(c,), daemon=True).start()


if __name__ == '__main__':
    main()
