"""ctl.py -- une ligne au port de controle de ircd.py : python -I tools/nettest/ctl.py say #armpi alice bonjour"""
import socket
import sys

s = socket.create_connection(("127.0.0.1", 6668))
s.sendall((" ".join(sys.argv[1:]) + "\n").encode("utf-8"))
s.close()
