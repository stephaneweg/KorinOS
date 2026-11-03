"""ircd.py -- un petit serveur IRC d'essai pour l'app irc (src/apps/irc), sans compte ni reseau.

  python -I tools/nettest/ircd.py [--port 6667] [--log fichier]

Sous QEMU l'invite joint le PC en 10.0.2.2. Le serveur comprend NICK, USER, JOIN, PART, PRIVMSG, NOTICE, TOPIC,
NAMES, LIST, MODE, PING, QUIT, et a deux salons, #armpi et #test, avec des habitants simules : alice (operatrice),
bob (voix), robot. Pour faire parler le serveur pendant un essai, envoyer une ligne sur le port + 1 (controle) :
  say <#canal|pseudo> <de> <texte>       un message
  me <#canal> <de> <texte>               une action
  join <#canal> <de>                     quelqu'un entre
  part <#canal> <de> <raison>            quelqu'un sort
  nick <ancien> <nouveau>                un habitant change de pseudo
  mode <#canal> <de> +o <pseudo>         un mode
  kick <#canal> <de> <pseudo> <raison>   une expulsion
  raw <ligne>                            vers tous les clients, telle quelle
  drop                                   ferme tous les clients
Chaque ligne recue d'un client va dans le journal (--log), pour verifier ce que l'app envoie.
"""
import argparse
import socket
import threading
import time

NAME = "irc.test"
TOPICS = {"#armpi": "Le salon d'essai d'ARMPI", "#test": ""}
HOME = {"#armpi": [("alice", "@"), ("bob", "+"), ("robot", "")], "#test": [("robot", "")]}
EXTRA = [("#vide", 0, ""), ("#linux", 212, "[+nt] Tout sur Linux"), ("#cafe", 7, "Discussions libres")]

lock = threading.Lock()
clients = []
chans = {c: dict((n, m) for n, m in HOME[c]) for c in HOME}
log = None


def note(s):
    print(s, flush=True)
    if log:
        log.write(s + "\n")
        log.flush()


class Client:
    def __init__(self, sock):
        self.s = sock
        self.nick = None
        self.user = None
        self.done = False

    def send(self, line):
        try:
            self.s.sendall(line.encode("utf-8", "surrogateescape") + b"\r\n")
        except OSError:
            pass

    def num(self, n, text):
        self.send(":%s %03d %s %s" % (NAME, n, self.nick or "*", text))

    def me(self):
        return "%s!u@armpi" % self.nick

    def welcome(self):
        self.done = True
        self.num(1, ":Welcome to the test network, %s" % self.nick)
        self.num(2, ":Your host is %s" % NAME)
        self.num(3, ":This server was created today")
        self.num(4, "%s test-1 iow nt" % NAME)
        self.num(5, "CHANTYPES=# NICKLEN=30 :are supported by this server")
        self.num(375, ":- %s Message of the day -" % NAME)
        self.num(372, ":- Bienvenue sur le serveur d'essai, été comme hiver")
        self.num(376, ":End of /MOTD command.")

    def names(self, ch):
        who = " ".join(m + n for n, m in chans[ch].items())
        self.num(353, "= %s :%s" % (ch, who))
        self.num(366, "%s :End of /NAMES list." % ch)

    def join(self, ch):
        with lock:
            members = chans.setdefault(ch, {})
            members[self.nick] = ""
        to_chan(ch, ":%s JOIN :%s" % (self.me(), ch))
        t = TOPICS.get(ch, "")
        if t:
            self.num(332, "%s :%s" % (ch, t))
        else:
            self.num(331, "%s :No topic is set" % ch)
        self.names(ch)

    def part(self, ch, why):
        to_chan(ch, ":%s PART %s :%s" % (self.me(), ch, why))
        with lock:
            chans.get(ch, {}).pop(self.nick, None)

    def on_line(self, raw):
        note("<%s> %s" % (self.nick or "?", raw))
        parts = raw.split(" :", 1)
        words = parts[0].split()
        if not words:
            return
        cmd = words[0].upper()
        rest = parts[1] if len(parts) > 1 else ""
        arg = words[1:]
        if cmd == "NICK" and arg:
            n = arg[0]
            if n in [c.nick for c in clients if c is not self] or any(n in m for m in chans.values() if self.nick not in m):
                self.num(433, "%s :Nickname is already in use" % n)
                return
            if self.done:
                old = self.me()
                for ch, m in chans.items():
                    if self.nick in m:
                        m[n] = m.pop(self.nick)
                self.send(":%s NICK :%s" % (old, n))
                self.nick = n
            else:
                self.nick = n
                if self.user:
                    self.welcome()
        elif cmd == "USER":
            self.user = arg[0] if arg else "u"
            if self.nick and not self.done:
                self.welcome()
        elif cmd == "PING":
            self.send(":%s PONG %s :%s" % (NAME, NAME, rest or (arg[0] if arg else "")))
        elif cmd == "JOIN" and arg:
            for ch in arg[0].split(","):
                self.join(ch)
        elif cmd == "PART" and arg:
            self.part(arg[0], rest or "")
        elif cmd == "PRIVMSG" and arg:
            target = arg[0]
            if target.startswith("#"):
                for c in clients:
                    if c is not self and c.nick in chans.get(target, {}):
                        c.send(":%s PRIVMSG %s :%s" % (self.me(), target, rest))
                if rest.startswith("!echo "):
                    to_chan(target, ":robot!u@armpi PRIVMSG %s :%s" % (target, rest[6:]))
            elif target in ("alice", "bob", "robot"):
                self.send(":%s!u@armpi PRIVMSG %s :Bien recu : %s" % (target, self.nick, rest))
            else:
                for c in clients:
                    if c.nick == target:
                        c.send(":%s PRIVMSG %s :%s" % (self.me(), target, rest))
                        break
                else:
                    self.num(401, "%s :No such nick/channel" % target)
        elif cmd == "TOPIC" and arg:
            if len(parts) > 1:
                TOPICS[arg[0]] = rest
                to_chan(arg[0], ":%s TOPIC %s :%s" % (self.me(), arg[0], rest))
            else:
                self.num(332, "%s :%s" % (arg[0], TOPICS.get(arg[0], "")))
        elif cmd == "NAMES" and arg:
            self.names(arg[0])
        elif cmd == "LIST":
            self.num(321, "Channel :Users  Name")
            for ch, m in chans.items():
                self.num(322, "%s %d :%s" % (ch, len(m), TOPICS.get(ch, "")))
            for ch, n, t in EXTRA:
                self.num(322, "%s %d :%s" % (ch, n, t))
            self.num(323, ":End of /LIST")
        elif cmd == "MODE":
            pass
        elif cmd == "QUIT":
            for ch, m in chans.items():
                m.pop(self.nick, None)
            self.send("ERROR :Closing link")
            raise ConnectionError
        else:
            self.num(421, "%s :Unknown command" % cmd)


def to_chan(ch, line):
    for c in list(clients):
        if c.nick in chans.get(ch, {}):
            c.send(line)


def serve(c):
    f = b""
    try:
        while True:
            d = c.s.recv(4096)
            if not d:
                break
            f += d
            while b"\n" in f:
                ln, f = f.split(b"\n", 1)
                c.on_line(ln.rstrip(b"\r").decode("utf-8", "surrogateescape"))
    except (OSError, ConnectionError):
        pass
    note("(client %s parti)" % c.nick)
    with lock:
        if c in clients:
            clients.remove(c)
        for m in chans.values():
            m.pop(c.nick, None)
    c.s.close()


def control(line):
    w = line.split(" ", 3)
    op = w[0]
    if op == "say":
        target, who, text = w[1], w[2], w[3]
        msg = ":%s!u@armpi PRIVMSG %s :%s" % (who, target, text)
        if target.startswith("#"):
            to_chan(target, msg)
        else:
            for c in clients:
                if c.nick == target:
                    c.send(msg)
    elif op == "me":
        to_chan(w[1], ":%s!u@armpi PRIVMSG %s :\x01ACTION %s\x01" % (w[2], w[1], w[3]))
    elif op == "join":
        chans[w[1]][w[2]] = ""
        to_chan(w[1], ":%s!u@armpi JOIN :%s" % (w[2], w[1]))
    elif op == "part":
        to_chan(w[1], ":%s!u@armpi PART %s :%s" % (w[2], w[1], w[3] if len(w) > 3 else ""))
        chans[w[1]].pop(w[2], None)
    elif op == "nick":
        for m in chans.values():
            if w[1] in m:
                m[w[2]] = m.pop(w[1])
        for c in list(clients):
            c.send(":%s!u@armpi NICK :%s" % (w[1], w[2]))
    elif op == "mode":
        a = line.split(" ")
        to_chan(a[1], ":%s!u@armpi MODE %s %s" % (a[2], a[1], " ".join(a[3:])))
    elif op == "kick":
        a = line.split(" ", 4)
        to_chan(a[1], ":%s!u@armpi KICK %s %s :%s" % (a[2], a[1], a[3], a[4] if len(a) > 4 else ""))
        chans[a[1]].pop(a[3], None)
    elif op == "raw":
        for c in list(clients):
            c.send(line[4:])
    elif op == "drop":
        for c in list(clients):
            try:
                c.s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def control_server(port):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(4)
    while True:
        c, _ = s.accept()
        d = c.makefile("r", encoding="utf-8")
        for ln in d:
            ln = ln.strip()
            if ln:
                note("(controle) " + ln)
                try:
                    control(ln)
                except (IndexError, KeyError) as e:
                    note("(controle : %r)" % e)
        c.close()


def main():
    global log
    a = argparse.ArgumentParser()
    a.add_argument("--port", type=int, default=6667)
    a.add_argument("--log")
    o = a.parse_args()
    if o.log:
        log = open(o.log, "w", encoding="utf-8", errors="backslashreplace")
    threading.Thread(target=control_server, args=(o.port + 1,), daemon=True).start()
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", o.port))
    s.listen(8)
    note("ircd d'essai : port %d, controle %d" % (o.port, o.port + 1))
    while True:
        c, _ = s.accept()
        cl = Client(c)
        with lock:
            clients.append(cl)
        threading.Thread(target=serve, args=(cl,), daemon=True).start()


if __name__ == "__main__":
    main()
