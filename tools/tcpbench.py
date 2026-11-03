"""Banc du reseau d'ARMPI, cote PC (serveur : SD0:/bin/tcpbench, port 5001).

  python -I tools/tcpbench.py [HOTE[:PORT]] [tours] [secondes]

Chaque tour mesure l'aller-retour (200 echos d'un octet sur une connexion), puis le debit
dans les deux sens. Le Wi-Fi est tres variable : plusieurs tours, et la mediane.
"""
import socket
import statistics
import struct
import sys
import time

host, port = "192.168.1.20", 5001
if len(sys.argv) > 1:
    host, _, p = sys.argv[1].partition(":")
    port = int(p) if p else port
tours = int(sys.argv[2]) if len(sys.argv) > 2 else 5
duree = float(sys.argv[3]) if len(sys.argv) > 3 else 5


def conn(cmd):
    fin = time.time() + 120
    while True:
        try:
            s = socket.create_connection((host, port), 5)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            s.settimeout(5)
            s.sendall(cmd)
            return s
        except OSError:
            if time.time() > fin:
                raise
            time.sleep(1)


NST_BYTES = 136
F = ("rx_n", "rx_t", "tx_n", "tx_t", "polls", "empty", "nocred", "link", "stack", "rtx", "ooo", "ooofull",
     "rxdrop", "txdrop", "freq", "frtx", "probes")


def stats():
    try:
        s = conn(b"I")
        b = b""
        while len(b) < NST_BYTES:
            d = s.recv(NST_BYTES - len(b))
            if not d:
                break
            b += d
        s.close()
        return dict(zip(F, struct.unpack("<17Q", b))) if len(b) == NST_BYTES else None
    except OSError:
        return None


def diff(a, b, secs):
    if not a or not b:
        return "(compteurs illisibles)"
    d = {k: b[k] - a[k] for k in F}
    link = "%.1f Mb/s, economie d'energie %d" % ((b["link"] & 0xFFFF) / 2, b["link"] >> 16)
    us = 1e6 / (b["freq"] or 54000000)
    return (link + " ;  %5d tr/s lues (%.0f us), %5d tr/s ecrites (%.0f us), tours %d/s (%d %% a vide), sans credit %d/s,"
            " pile %d/s, RTO %d, rapides %d, sondes %d, hors ordre %d (perdus %d), anneaux pleins rx %d tx %d" % (
                d["rx_n"] / secs, d["rx_t"] * us / max(d["rx_n"], 1), d["tx_n"] / secs, d["tx_t"] * us / max(d["tx_n"], 1),
                d["polls"] / secs, 100 * d["empty"] / max(d["polls"], 1), d["nocred"] / secs, d["stack"] / secs,
                d["rtx"], d["frtx"], d["probes"], d["ooo"], d["ooofull"], d["rxdrop"], d["txdrop"]))


def echo(n=200):
    s = conn(b"E")
    t = []
    for _ in range(n):
        a = time.perf_counter()
        s.sendall(b"x")
        s.recv(16)
        t.append((time.perf_counter() - a) * 1000)
    s.close()
    return sorted(t)


def recv():
    s = conn(b"S" + bytes([max(1, round(duree))]))
    n, a = 0, time.perf_counter()
    try:
        while True:
            d = s.recv(65536)
            if not d:
                break
            n += len(d)
    except OSError:
        pass
    dt = time.perf_counter() - a
    s.close()
    return n / 1e6 / dt


def send():
    s = conn(b"R")
    b = bytes(65536)
    n, a = 0, time.perf_counter()
    try:
        while time.perf_counter() - a < duree:
            n += s.send(b)
    except OSError:
        pass
    dt = time.perf_counter() - a
    s.close()
    return n / 1e6 / dt


med, p90, rx, tx = [], [], [], []
for i in range(tours):
    try:
        e = echo()
        med.append(statistics.median(e))
        p90.append(e[int(len(e) * .9)])
        a = stats()
        rx.append(recv())
        print(" Pi envoie :", diff(a, stats(), duree), flush=True)
        time.sleep(1)
        a = stats()
        tx.append(send())
        print(" Pi recoit :", diff(a, stats(), duree), flush=True)
        time.sleep(1)
        print("tour %d : aller-retour %.1f ms (90 %% %.1f) ; le Pi envoie %.2f, recoit %.2f Mo/s"
              % (i + 1, med[-1], p90[-1], rx[-1], tx[-1]), flush=True)
    except OSError as x:
        print("tour %d perdu : %r" % (i + 1, x), flush=True)
if med:
    print("MEDIANES : aller-retour %.1f ms, le Pi envoie %.2f, recoit %.2f Mo/s (%d tours)"
          % (statistics.median(med), statistics.median(rx), statistics.median(tx), len(med)))
