"""mailsrv.py -- un petit serveur de courrier d'essai pour l'app mail (src/apps/mail), stdlib seule, sans compte reel.

  python -I tools/nettest/mailsrv.py [--dir build/try/mailsrv]         le serveur
  python -I tools/nettest/mailsrv.py seed <dossier> [--verify]         accounts.ini, secrets, key d'essai (vers 10.0.2.2)

Il n'ecoute que sur 127.0.0.1 ; sous QEMU l'invite joint le PC en 10.0.2.2.

  IMAP   1143 (clair, STARTTLS)   1993 (TLS direct)     SPECIAL-USE, UIDPLUS, LITERAL+, IDLE ; MOVE sur 1143 seulement :
                                                        1993 passe donc par COPY + \\Deleted + UID EXPUNGE
  POP3   1110 (clair, STLS)       1995 (TLS direct)
  SMTP   1587 (clair, STARTTLS)   1465 (TLS direct)
  1999   controle, une ligne : "deliver" (un message de plus dans INBOX), "drop" (ferme les clients)

Compte : testuser / testpass. Le certificat auto-signe est fait par l'openssl de WSL dans --dir (hors de git).
Le journal (--dir/mailsrv.log) dit chaque commande recue, mots de passe masques ; les messages recus par SMTP et APPEND
vont dans --dir/recv/ en .eml.
"""
import argparse
import base64
import email
import email.utils
import os
import re
import socket
import ssl
import struct
import subprocess
import sys
import threading
import time
import zlib

USER, PASSWORD = "testuser", "testpass"
lock = threading.RLock()
clients = []
LOG = None
RECV = None
CTX = None
serial = [0]

JPEG_B64 = "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAA0JCgsKCA0LCgsODg0PEyAVExISEyccHhcgLikxMC4pLSwzOko+MzZGNywtQFdBRkxOUlNSMj5aYVpQYEpRUk//2wBDAQ4ODhMREyYVFSZPNS01T09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0//wAARCAA8AHgDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDMooor2zyAorV07RZbpFmmbyom5Ax8zD+n1/StePRLBUCtEzkfxM5yfyxXmYjNsNRlyt3fkdVPBVaivt6nJ0V09xoFpIMwl4Wxxg7h+R5/WsC9sprGUJMByMqy9DWuFzGhiXaD17MmthalLWS0K9FFFdxzhRRRQAUUUUAFFFFABRRRQAVq6FYLdTtLMm6KPoD0Zv6//qrKrrNBRV0qNlGC5Zm9znH8gK8zNsRKjhm47vQ6sFTVSrr01NGiiiviz3wqG6toruBoZlyp6HuD6ipqKqMnBqUXZoTSaszhp4Xt53hkGGQ4P+NMAzWp4jRV1IMowXjDN7nJH8gKy1r7b6zKWEVZaNpHj4XDwljFSmrrX8mG33o2+9Oorz/rtf8Am/BHv/2XhP5Pxf8AmN2+9G33p1FH12v/ADfgg/svCfyfi/8AMbt96NvvTqD0qoYys5JOX4IiplmFUG1Hp3f+YyiiivdPkwrp/Ds4ksDCcbom6Adjz/PNcxViwvJLG5E0YB4wynuPSuHMcK8TQcFvujowtb2VRSex2tFQ2tzFdwLNC2VPUdwfQ1NXxEouDcZKzR9AmmroKKKy9X1UWamGAg3BH4IPU+/t/k6UKE681TprUipUjTjzSMbXJxPqb4wVjGwED06/qTVBaSlWvsq9JUsJ7NdEvzR5mXTc8bGT63/JjqKKK8U+tCiiigAoPSig9Kun8SM6v8OXoxlFFFfTHwgUUUUASQTy28gkhkZGHcGtGPX71UCsInI/iZTk/kRWVRWFXC0a2tSKZpCtOHwuxoXGs304wJBEpHSMY/XrWfRRVUqNOkrU4pEzqSm7ydwoBxRRVygprllsOnUlTlzQdmLuNG40lFY/VaP8p0f2hif52LuNG40lFH1Wj/KH9oYn+di7jRk0lFNYaiteUHj8S1ZzYUUUVuch/9k="


def log(*a):
    s = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    with lock:
        LOG.write(s + "\n")
        LOG.flush()


def keep(kind, raw):
    with lock:
        serial[0] += 1
        n = "%s-%03d.eml" % (kind, serial[0])
        with open(os.path.join(RECV, n), "wb") as f:
            f.write(raw)
    log("RECU", kind, n, len(raw), "octets")


def png(w=64, h=40):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    rows = b""
    for y in range(h):
        rows += b"\0" + bytes(c for x in range(w) for c in ((x * 4) % 256, (y * 6) % 256, 160))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def b64(b):
    s = base64.b64encode(b).decode()
    return "\r\n".join(s[i:i + 76] for i in range(0, len(s), 76))


def rfcdate(days=0, hours=0):
    return email.utils.formatdate(time.time() - days * 86400 - hours * 3600, localtime=False)


mid = [100]


def hdr(frm, to, subj, days=0, hours=0, extra="", msgid=None):
    mid[0] += 1
    return "From: %s\r\nTo: %s\r\nSubject: %s\r\nDate: %s\r\nMessage-ID: <%s@test.local>\r\nMIME-Version: 1.0\r\n%s" % (
        frm, to, subj, rfcdate(days, hours), msgid or "m%d" % mid[0], extra)


ME = "Test User <testuser@test.local>"
TEXT8 = "Content-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: 8bit\r\n"


def m_text():
    subj = "=?UTF-8?B?" + base64.b64encode("Réunion de février : ça avance".encode()).decode() + "?="
    return (hdr("=?UTF-8?Q?Ren=C3=A9_L=C3=A9vy?= <rene@test.local>", ME, subj, 0, 1, TEXT8) + "\r\n"
            "Bonjour,\r\n\r\nLe budget d'été est validé : 1 250 €, déjà payé à l'hôtel.\r\nVoir https://example.test/page pour le détail.\r\n\r\n"
            "> Ancienne ligne citée\r\n\r\nÀ bientôt,\r\nRené\r\n").encode("utf-8")


def m_html():
    b, r = "=_alt1", "=_rel1"
    html = ('<html><head><style>h1{color:#c0392b}.box{border:1px solid #888;padding:6px;background:#eef}</style></head><body>'
            '<h1>Lettre d&eacute;t&eacute;</h1><p>Un paragraphe en <b>gras</b>, en <i>italique</i>, avec un <a href="https://example.test/x">lien</a>.</p>'
            '<div class="box">Une bo&icirc;te stylée.</div><ul><li>Un</li><li>Deux</li></ul>'
            '<p><img src="cid:logo1" width="128" height="80"> <img src="cid:photo1"></p>'
            '<table border="1"><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table></body></html>')
    return (hdr('"Lettre Info" <news@test.local>', ME, "Lettre HTML avec images", 0, 3, 'Content-Type: multipart/related; boundary="%s"\r\n' % r) + "\r\n"
            "--%s\r\nContent-Type: multipart/alternative; boundary=\"%s\"\r\n\r\n"
            "--%s\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nVersion texte de la lettre.\r\n"
            "--%s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n%s\r\n"
            "--%s--\r\n"
            "--%s\r\nContent-Type: image/png\r\nContent-ID: <logo1>\r\nContent-Disposition: inline\r\nContent-Transfer-Encoding: base64\r\n\r\n%s\r\n"
            "--%s\r\nContent-Type: image/jpeg\r\nContent-ID: <photo1>\r\nContent-Disposition: inline\r\nContent-Transfer-Encoding: base64\r\n\r\n%s\r\n"
            "--%s--\r\n" % (r, b, b, b, html, b, r, b64(png()), r, JPEG_B64, r)).encode("utf-8")


def m_attach():
    b = "=_mix1"
    return (hdr("Marie Dubois <marie@test.local>", ME + ", Paul <paul@test.local>", "Pieces jointes", 0, 5, 'Content-Type: multipart/mixed; boundary="%s"\r\n' % b) + "\r\n"
            "--%s\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nDeux fichiers ci-joints.\r\n"
            "--%s\r\nContent-Type: text/plain; name=\"notes.txt\"\r\nContent-Disposition: attachment; filename=\"notes.txt\"\r\nContent-Transfer-Encoding: base64\r\n\r\n%s\r\n"
            "--%s\r\nContent-Type: image/png\r\nContent-Disposition: attachment; filename=\"=?UTF-8?B?%s?=\"\r\nContent-Transfer-Encoding: base64\r\n\r\n%s\r\n"
            "--%s--\r\n" % (b, b, b64("contenu des notes\n".encode()), b, base64.b64encode("dégradé.png".encode()).decode(), b64(png()), b)).encode("utf-8")


def m_latin1():
    return (hdr("=?ISO-8859-1?Q?Andr=E9?= <andre@test.local>", ME, "=?ISO-8859-1?Q?Caf=E9_cr=E8me?=", 1, 2,
                "Content-Type: text/plain; charset=iso-8859-1\r\nContent-Transfer-Encoding: quoted-printable\r\n") + "\r\nUn caf=E9 cr=E8me, s'il vous pla=EEt.\r\n").encode()


def m_wide():
    cells = "".join("<td>Colonne %d</td>" % i for i in range(8))
    return (hdr("Promo <promo@test.local>", ME, "Table large et image distante", 2, 0, "Content-Type: text/html; charset=utf-8\r\n") + "\r\n"
            '<html><body><table width="900" bgcolor="#ffeecc"><tr>' + cells + '</tr><tr><td colspan="8"><p>Un texte large dans une table de 900 pixels, '
            'plus large que le volet.</p><img src="http://10.0.2.2:1999/x.png" width="200" height="60"></td></tr></table></body></html>\r\n').encode("utf-8")


def m_thread():
    a = (hdr("Bob <bob@test.local>", ME, "Projet Pi", 3, 2, TEXT8, "fil1") + "\r\nPremier message du fil.\r\n").encode("utf-8")
    b = (hdr("Alice <alice@test.local>", "Bob <bob@test.local>, " + ME, "Re: Projet Pi", 2, 20,
             "In-Reply-To: <fil1@test.local>\r\nReferences: <fil1@test.local>\r\n" + TEXT8, "fil2") + "\r\nDeuxième message du fil.\r\n> Premier message du fil.\r\n").encode("utf-8")
    c = (hdr("Bob <bob@test.local>", ME, "Re: Projet Pi", 0, 7,
             "In-Reply-To: <fil2@test.local>\r\nReferences: <fil1@test.local> <fil2@test.local>\r\n" + TEXT8, "fil3") + "\r\nTroisième message du fil.\r\n").encode("utf-8")
    return a, b, c


class Box:
    def __init__(self, name, attrs=()):
        self.name, self.attrs = name, attrs
        self.msgs = []          # {uid, flags, raw}
        self.next = 1

    def add(self, raw, flags=()):
        self.msgs.append({"uid": self.next, "flags": set(flags), "raw": raw})
        self.next += 1


BOXES = {}
POP = []


def seed_boxes():
    for n, a in (("INBOX", ()), ("Sent", ("\\Sent",)), ("Drafts", ("\\Drafts",)), ("Archive", ("\\Archive",)), ("Junk", ("\\Junk",)), ("Trash", ("\\Trash",)),
                 ("Projets", ()), ("Projets/2026", ()), ("Caf&AOk-", ())):
        BOXES[n] = Box(n, a)
    t = m_thread()
    i = BOXES["INBOX"]
    i.add(t[0], ("\\Seen",))
    i.add(m_wide())
    i.add(m_latin1(), ("\\Seen", "\\Flagged"))
    i.add(t[1])
    i.add(m_attach())
    i.add(m_html())
    i.add(t[2])
    i.add(m_text())
    BOXES["Sent"].add((hdr(ME, "Bob <bob@test.local>", "Re: Projet Pi", 1, 0, "In-Reply-To: <fil2@test.local>\r\nReferences: <fil1@test.local> <fil2@test.local>\r\n" + TEXT8) + "\r\nMa réponse dans le fil.\r\n").encode("utf-8"), ("\\Seen",))
    BOXES["Projets"].add((hdr("Carl <carl@test.local>", ME, "Dans un dossier", 4, 0, "Content-Type: text/plain\r\n") + "\r\nCe message vit dans Projets.\r\n").encode())
    POP.append({"uidl": "pop-a", "raw": (hdr("=?UTF-8?Q?Zo=C3=A9?= <zoe@test.local>", ME, "Par POP3", 0, 2, TEXT8) + "\r\nUn message relevé par POP3, été.\r\n.Ligne qui commence par un point.\r\n").encode("utf-8"), "del": False})
    POP.append({"uidl": "pop-b", "raw": m_attach(), "del": False})
    POP.append({"uidl": "pop-c", "raw": m_html(), "del": False})


class Line:
    def __init__(self, s):
        self.s, self.buf = s, b""
        self.s.settimeout(600)

    def recv(self):
        d = self.s.recv(65536)
        if not d:
            raise ConnectionError
        return d

    def line(self):
        while b"\n" not in self.buf:
            self.buf += self.recv()
        l, self.buf = self.buf.split(b"\n", 1)
        return l.rstrip(b"\r")

    def n(self, k):
        while len(self.buf) < k:
            self.buf += self.recv()
        d, self.buf = self.buf[:k], self.buf[k:]
        return d

    def put(self, b):
        self.s.sendall(b if isinstance(b, bytes) else b.encode("utf-8", "surrogateescape"))

    def tls(self):
        self.buf = b""
        self.s = CTX.wrap_socket(self.s, server_side=True)
        self.s.settimeout(600)


def q(s):
    if s is None:
        return "NIL"
    s = str(s)
    if '"' in s or "\\" in s or "\r" in s or "\n" in s or any(ord(c) > 126 for c in s):
        b = s.encode("utf-8", "surrogateescape")
        return "{%d}\r\n" % len(b) + s
    return '"%s"' % s


def addrs(v):
    if not v:
        return "NIL"
    out = ""
    for n, a in email.utils.getaddresses([str(v)]):
        mb, _, hs = a.partition("@")
        out += "(%s NIL %s %s)" % (q(n) if n else "NIL", q(mb), q(hs))
    return "(" + out + ")" if out else "NIL"


def envelope(m):
    frm = addrs(m["From"])
    rt = addrs(m["Reply-To"]) if m["Reply-To"] else frm
    return "(%s %s %s %s %s %s %s %s %s %s)" % (q(m["Date"]), q(m["Subject"]), frm, frm, rt, addrs(m["To"]), addrs(m["Cc"]), "NIL", q(m["In-Reply-To"]), q(m["Message-ID"]))


def payload(p):
    v = p.get_payload()
    return v.encode("utf-8", "surrogateescape") if isinstance(v, str) else b""


def bodystructure(p):
    if p.is_multipart():
        return "(" + "".join(bodystructure(x) for x in p.get_payload()) + ' "%s")' % p.get_content_subtype().upper()
    t, s = p.get_content_maintype(), p.get_content_subtype()
    params = p.get_params() or []
    pl = []
    for k, v in params[1:]:
        pl += [q(k.upper()), q(v)]
    prm = "(" + " ".join(pl) + ")" if pl else "NIL"
    raw = payload(p)
    cid = p["Content-ID"]
    enc = (p["Content-Transfer-Encoding"] or "7bit").upper()
    disp = p["Content-Disposition"]
    ds = "NIL"
    if disp:
        v = disp.split(";")[0].strip()
        fn = p.get_param("filename", header="content-disposition")
        ds = "(%s %s)" % (q(v), "(" + q("FILENAME") + " " + q(fn) + ")" if fn else "NIL")
    base = "%s %s %s %s NIL %s %d" % (q(t), q(s), prm, q(cid) if cid else "NIL", q(enc), len(raw))
    if t == "text":
        return "(%s %d NIL %s NIL)" % (base, raw.count(b"\n"), ds)
    return "(%s NIL %s NIL)" % (base, ds)


def section(m, sec):
    p = m
    for k in sec.split("."):
        i = int(k) - 1
        p = p.get_payload()[i] if p.is_multipart() else p
    return payload(p)


def flags_s(fl):
    return " ".join(sorted(fl))


def fetch_items(box, msg, seq, items):
    raw = msg["raw"]
    m = email.message_from_bytes(raw)
    out = []
    i = items.strip()
    if i.startswith("("):
        i = i[1:-1]
    toks = re.findall(r"BODY(?:\.PEEK)?\[[^\]]*\](?:<\d+\.\d+>)?|RFC822\.SIZE|[A-Za-z0-9.\-]+", i)
    lits = {}
    for t in toks:
        u = t.upper()
        if u == "UID":
            out.append("UID %d" % msg["uid"])
        elif u == "FLAGS":
            out.append("FLAGS (%s)" % flags_s(msg["flags"]))
        elif u == "INTERNALDATE":
            out.append('INTERNALDATE "%s"' % time.strftime("%d-%b-%Y %H:%M:%S +0000", time.gmtime()))
        elif u == "RFC822.SIZE":
            out.append("RFC822.SIZE %d" % len(raw))
        elif u == "ENVELOPE":
            out.append("ENVELOPE " + envelope(m))
        elif u == "BODYSTRUCTURE":
            out.append("BODYSTRUCTURE " + bodystructure(m))
        elif u.startswith("BODY"):
            sec = t[t.index("[") + 1:t.index("]")]
            part = t[t.index("<") + 1:t.index(">")] if "<" in t else None
            if sec.upper().startswith("HEADER.FIELDS"):
                v = m["References"]
                d = ("References: %s\r\n\r\n" % v).encode() if v else b"\r\n"
                name = "BODY[HEADER.FIELDS (REFERENCES)]"
            else:
                d = raw if sec == "" else section(m, sec)
                name = "BODY[%s]" % sec
            if part:
                a, n = part.split(".")
                d = d[int(a):int(a) + int(n)]
                name += "<%s>" % a
            out.append(name + " {%d}\r\n" % len(d))
            lits[len(out) - 1] = d
    b = ("* %d FETCH (" % seq).encode()
    for k, x in enumerate(out):
        if k:
            b += b" "
        b += x.encode("utf-8", "surrogateescape")
        if k in lits:
            b += lits[k]
    return b + b")\r\n"


def uidset(s, box, byseq=False):
    """les messages d'un ensemble : "3,5:7", "4:*" (n:* donne au moins le dernier, comme le veut la RFC)"""
    ms = box.msgs
    if not ms:
        return []
    keys = list(range(1, len(ms) + 1)) if byseq else [x["uid"] for x in ms]
    mx = keys[-1]
    sel = set()
    for p in s.split(","):
        if ":" in p:
            a, b = p.split(":")
            a = mx if a == "*" else int(a)
            b = mx if b == "*" else int(b)
            lo, hi = min(a, b), max(a, b)
            sel |= {k for k in keys if lo <= k <= hi}
        else:
            sel.add(mx if p == "*" else int(p))
    return [(i + 1, x) for i, x in enumerate(ms) if (i + 1 if byseq else x["uid"]) in sel]


def imap(c, port, tls):
    def caps():
        s = "IMAP4rev1 AUTH=PLAIN SASL-IR SPECIAL-USE UIDPLUS LITERAL+ IDLE"
        if not tls[0] and port == 1143:
            s += " STARTTLS"
        if port == 1143:
            s += " MOVE"
        return s
    c.put("* OK [CAPABILITY %s] mailsrv pret\r\n" % caps())
    box = None
    authed = False
    while True:
        l = c.line().decode("utf-8", "surrogateescape")
        tag, _, rest = l.partition(" ")
        cmd, _, arg = rest.partition(" ")
        up = cmd.upper()
        log("IMAP", port, tag, up if up in ("LOGIN", "AUTHENTICATE") else rest[:150])
        if up == "CAPABILITY":
            c.put("* CAPABILITY %s\r\n%s OK\r\n" % (caps(), tag))
        elif up == "STARTTLS":
            c.put(tag + " OK commence\r\n")
            c.tls()
            tls[0] = True
        elif up == "LOGIN":
            p = re.findall(r'"((?:[^"\\]|\\.)*)"|(\S+)', arg)
            u, w = [a or b for a, b in p][:2]
            authed = u == USER and w == PASSWORD
            c.put(tag + (" OK connecte\r\n" if authed else " NO [AUTHENTICATIONFAILED] mauvais mot de passe\r\n"))
        elif up == "AUTHENTICATE":
            t = arg.split(" ")
            d = t[1] if len(t) > 1 else None
            if d is None:
                c.put("+ \r\n")
                d = c.line().decode()
            v = base64.b64decode(d).split(b"\0")
            authed = len(v) == 3 and v[1].decode() == USER and v[2].decode() == PASSWORD
            c.put(tag + (" OK connecte\r\n" if authed else " NO [AUTHENTICATIONFAILED] mauvais mot de passe\r\n"))
        elif up == "LOGOUT":
            c.put("* BYE\r\n%s OK\r\n" % tag)
            return
        elif up == "NOOP":
            c.put(tag + " OK\r\n")
        elif not authed:
            c.put(tag + " NO connecte-toi d'abord\r\n")
        elif up == "LIST":
            with lock:
                for b in BOXES.values():
                    c.put('* LIST (%s) "/" %s\r\n' % (" ".join(b.attrs), q(b.name)))
            c.put(tag + " OK\r\n")
        elif up in ("SELECT", "EXAMINE"):
            box = BOXES.get(arg.strip().strip('"'))
            if not box:
                c.put(tag + " NO pas de tel dossier\r\n")
                continue
            c.put("* %d EXISTS\r\n* 0 RECENT\r\n* OK [UIDVALIDITY 42]\r\n* OK [UIDNEXT %d]\r\n* FLAGS (\\Seen \\Answered \\Flagged \\Deleted \\Draft)\r\n%s OK [READ-WRITE]\r\n" % (len(box.msgs), box.next, tag))
        elif up == "FETCH":
            s, _, it = arg.partition(" ")
            with lock:
                for seq, m in uidset(s, box, True):
                    c.put(fetch_items(box, m, seq, it))
            c.put(tag + " OK\r\n")
        elif up == "UID":
            sub, _, a2 = arg.partition(" ")
            sub = sub.upper()
            s, _, it = a2.partition(" ")
            if sub == "FETCH":
                with lock:
                    for seq, m in uidset(s, box):
                        c.put(fetch_items(box, m, seq, it))
                c.put(tag + " OK\r\n")
            elif sub == "STORE":
                op, _, fl = it.partition(" ")
                fset = set(fl.strip("() ").split())
                with lock:
                    for seq, m in uidset(s, box):
                        if op.startswith("+"):
                            m["flags"] |= fset
                        elif op.startswith("-"):
                            m["flags"] -= fset
                        log("IMAP FLAGS", box.name, m["uid"], flags_s(m["flags"]))
                c.put(tag + " OK\r\n")
            elif sub in ("MOVE", "COPY"):
                d = BOXES.get(it.strip().strip('"'))
                if not d:
                    c.put(tag + " NO [TRYCREATE] pas de tel dossier\r\n")
                    continue
                with lock:
                    for seq, m in reversed(uidset(s, box)):
                        d.add(m["raw"], m["flags"])
                        log("IMAP", sub, box.name, m["uid"], "->", d.name)
                        if sub == "MOVE":
                            box.msgs.remove(m)
                            c.put("* %d EXPUNGE\r\n" % seq)
                c.put(tag + " OK\r\n")
            elif sub == "EXPUNGE":
                with lock:
                    for seq, m in reversed(uidset(s, box)):
                        if "\\Deleted" in m["flags"]:
                            box.msgs.remove(m)
                            log("IMAP EFFACE", box.name, m["uid"])
                            c.put("* %d EXPUNGE\r\n" % seq)
                c.put(tag + " OK\r\n")
            else:
                c.put(tag + " BAD\r\n")
        elif up == "EXPUNGE":
            with lock:
                for i in range(len(box.msgs) - 1, -1, -1):
                    if "\\Deleted" in box.msgs[i]["flags"]:
                        del box.msgs[i]
                        c.put("* %d EXPUNGE\r\n" % (i + 1))
            c.put(tag + " OK\r\n")
        elif up == "APPEND":
            m = re.match(r'"?([^"]+)"? (\(.*?\) )?\{(\d+)(\+?)\}$', arg)
            n, fl, sz, lit = m.group(1), m.group(2), int(m.group(3)), m.group(4)
            if not lit:
                c.put("+ vas-y\r\n")
            raw = c.n(sz)
            c.line()
            with lock:
                BOXES[n].add(raw, (fl or "").strip("() ").split())
            keep("append-" + n, raw)
            c.put(tag + " OK\r\n")
        elif up == "IDLE":
            c.put("+ j'attends\r\n")
            c.line()
            c.put(tag + " OK\r\n")
        else:
            c.put(tag + " BAD commande inconnue\r\n")


def pop3(c, port, tls):
    c.put("+OK mailsrv POP3\r\n")
    user = None
    authed = False
    while True:
        l = c.line().decode("utf-8", "surrogateescape")
        cmd, _, arg = l.partition(" ")
        up = cmd.upper()
        log("POP3", port, up, "" if up in ("PASS", "AUTH") else arg)
        live = [(i + 1, m) for i, m in enumerate(POP) if not m["del"]]
        if up == "CAPA":
            c.put("+OK\r\nUSER\r\nUIDL\r\nTOP\r\nSASL PLAIN\r\n" + ("STLS\r\n" if port == 1110 and not tls[0] else "") + ".\r\n")
        elif up == "STLS":
            c.put("+OK commence\r\n")
            c.tls()
            tls[0] = True
        elif up == "USER":
            user = arg
            c.put("+OK\r\n")
        elif up == "PASS":
            authed = user == USER and arg == PASSWORD
            c.put("+OK\r\n" if authed else "-ERR [AUTH] mauvais mot de passe\r\n")
        elif up == "AUTH":
            v = base64.b64decode(arg.split(" ")[1]).split(b"\0")
            authed = len(v) == 3 and v[1].decode() == USER and v[2].decode() == PASSWORD
            c.put("+OK\r\n" if authed else "-ERR [AUTH] mauvais mot de passe\r\n")
        elif up == "QUIT":
            c.put("+OK\r\n")
            return
        elif not authed:
            c.put("-ERR connecte-toi\r\n")
        elif up == "STAT":
            c.put("+OK %d %d\r\n" % (len(live), sum(len(m["raw"]) for _, m in live)))
        elif up == "LIST":
            c.put("+OK\r\n" + "".join("%d %d\r\n" % (i, len(m["raw"])) for i, m in live) + ".\r\n")
        elif up == "UIDL":
            c.put("+OK\r\n" + "".join("%d %s\r\n" % (i, m["uidl"]) for i, m in live) + ".\r\n")
        elif up == "RETR":
            s = re.sub(rb"(?m)^\.", b"..", POP[int(arg) - 1]["raw"])
            c.put(b"+OK\r\n" + s + (b"" if s.endswith(b"\r\n") else b"\r\n") + b".\r\n")
        elif up == "DELE":
            POP[int(arg) - 1]["del"] = True
            log("POP3 EFFACE", arg)
            c.put("+OK\r\n")
        else:
            c.put("-ERR inconnu\r\n")


def smtp(c, port, tls):
    c.put("220 mailsrv SMTP\r\n")
    rcpt = []
    frm = None
    authed = False
    while True:
        l = c.line().decode("utf-8", "surrogateescape")
        cmd = l.split(" ")[0].upper()
        log("SMTP", port, "AUTH" if cmd == "AUTH" else l[:120])
        if cmd == "EHLO":
            s = "250-mailsrv\r\n250-SIZE 30000000\r\n250-8BITMIME\r\n"
            if port == 1587 and not tls[0]:
                s += "250-STARTTLS\r\n"
            c.put(s + "250 AUTH PLAIN LOGIN\r\n")
        elif cmd == "HELO":
            c.put("250 mailsrv\r\n")
        elif cmd == "STARTTLS":
            c.put("220 commence\r\n")
            c.tls()
            tls[0] = True
        elif cmd == "AUTH":
            v = base64.b64decode(l.split(" ")[2]).split(b"\0")
            authed = len(v) == 3 and v[1].decode() == USER and v[2].decode() == PASSWORD
            c.put("235 ok\r\n" if authed else "535 mauvais mot de passe\r\n")
        elif cmd == "MAIL":
            if not authed:
                c.put("530 connecte-toi\r\n")
                continue
            frm = l
            rcpt = []
            c.put("250 ok\r\n")
        elif cmd == "RCPT":
            rcpt.append(l)
            c.put("250 ok\r\n")
        elif cmd == "DATA":
            c.put("354 vas-y\r\n")
            lines = []
            while True:
                x = c.line()
                if x == b".":
                    break
                lines.append(x[1:] if x.startswith(b".") else x)
            raw = b"\r\n".join(lines) + b"\r\n"
            log("SMTP", frm, rcpt)
            keep("smtp", raw)
            c.put("250 recu\r\n")
        elif cmd == "RSET":
            c.put("250 ok\r\n")
        elif cmd == "QUIT":
            c.put("221 au revoir\r\n")
            return
        else:
            c.put("500 inconnu\r\n")


def serve(port, fn, direct):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(8)

    def one(k):
        c = Line(k)
        clients.append(c)
        tls = [False]
        try:
            if direct:
                c.tls()
                tls[0] = True
            fn(c, port, tls)
        except (ConnectionError, OSError, ssl.SSLError, socket.timeout) as e:
            log("FIN", port, repr(e)[:100])
        except Exception as e:
            import traceback
            log("ERREUR", port, repr(e))
            log(traceback.format_exc())
        try:
            k.close()
        except OSError:
            pass
        if c in clients:
            clients.remove(c)

    while True:
        k, _ = s.accept()
        log("CONNEXION", port)
        threading.Thread(target=one, args=(k,), daemon=True).start()


def control(c, port, tls):
    n = 0
    while True:
        l = c.line().decode()
        if l == "deliver":
            n += 1
            with lock:
                BOXES["INBOX"].add((hdr("Nouveau <new@test.local>", ME, "Nouveau courrier %d" % n, 0, 0, TEXT8) + "\r\nArrivé pendant l'essai.\r\n").encode("utf-8"))
            c.put("ok\r\n")
        elif l == "drop":
            for x in list(clients):
                if x is not c:
                    x.s.close()
            c.put("ok\r\n")


# ---- les comptes semes : accounts.ini, secrets (AES-256-GCM, comme src/apps/mail/accounts.h, en Python pur) ----------------
SBOX = []


def _sbox():
    p = q_ = 1
    t = [0] * 256
    while True:
        p = p ^ ((p << 1) & 255) ^ (0x1B if p & 0x80 else 0)
        q_ ^= (q_ << 1) & 255
        q_ ^= (q_ << 2) & 255
        q_ ^= (q_ << 4) & 255
        if q_ & 0x80:
            q_ ^= 0x09
        rot = lambda v, k: ((v << k) | (v >> (8 - k))) & 255
        t[p] = q_ ^ rot(q_, 1) ^ rot(q_, 2) ^ rot(q_, 3) ^ rot(q_, 4) ^ 0x63
        if p == 1:
            break
    t[0] = 0x63
    return t


def xt(a):
    return ((a << 1) ^ 0x1B) & 255 if a & 0x80 else a << 1


def aes_keys(k):
    w = [list(k[i:i + 4]) for i in range(0, 32, 4)]
    rc = 1
    for i in range(8, 60):
        t = list(w[i - 1])
        if i % 8 == 0:
            t = [SBOX[t[1]] ^ rc, SBOX[t[2]], SBOX[t[3]], SBOX[t[0]]]
            rc = xt(rc)
        elif i % 8 == 4:
            t = [SBOX[x] for x in t]
        w.append([a ^ b for a, b in zip(w[i - 8], t)])
    return [sum(w[i:i + 4], []) for i in range(0, 60, 4)]


def aes_block(rk, b):
    s = [x ^ y for x, y in zip(b, rk[0])]
    for r in range(1, 15):
        s = [SBOX[x] for x in s]
        s = [s[(i + 4 * (i % 4)) % 16] for i in range(16)]
        if r != 14:
            o = []
            for c in range(4):
                a = s[c * 4:c * 4 + 4]
                o += [xt(a[0]) ^ xt(a[1]) ^ a[1] ^ a[2] ^ a[3], a[0] ^ xt(a[1]) ^ xt(a[2]) ^ a[2] ^ a[3], a[0] ^ a[1] ^ xt(a[2]) ^ xt(a[3]) ^ a[3], xt(a[0]) ^ a[0] ^ a[1] ^ a[2] ^ xt(a[3])]
            s = o
        s = [x ^ y for x, y in zip(s, rk[r])]
    return bytes(s)


def gcm(key, iv, plain):
    rk = aes_keys(key)
    h = int.from_bytes(aes_block(rk, bytes(16)), "big")

    def mul(x):
        z, v = 0, h
        for i in range(128):
            if x >> (127 - i) & 1:
                z ^= v
            v = (v >> 1) ^ (0xE1 << 120) if v & 1 else v >> 1
        return z
    ct = b""
    for i in range(0, len(plain), 16):
        ks = aes_block(rk, iv + struct.pack(">I", i // 16 + 2))
        ct += bytes(a ^ b for a, b in zip(plain[i:i + 16], ks))
    y = 0
    for i in range(0, len(ct), 16):
        y = mul(y ^ int.from_bytes(ct[i:i + 16].ljust(16, b"\0"), "big"))
    y = mul(y ^ (len(ct) * 8))
    tag = bytes(a ^ b for a, b in zip(y.to_bytes(16, "big"), aes_block(rk, iv + struct.pack(">I", 1))))
    return ct, tag


def seed(d, verify, only):
    """des comptes d'essai vers 10.0.2.2 : a1 IMAP en TLS direct, a2 IMAP par STARTTLS, a3 POP3 en clair ; only : "a1,a3" """
    SBOX[:] = _sbox()
    os.makedirs(d, exist_ok=True)
    A = [("a1", "Essai TLS", 0x1A73E8, "imap", 1993, "tls", 1465, "tls"), ("a2", "Essai STARTTLS", 0x188038, "imap", 1143, "starttls", 1587, "starttls"), ("a3", "Essai POP3", 0xE37400, "pop3", 1110, "none", 1587, "starttls")]
    o = "# comptes d'essai de tools/nettest/mailsrv.py\n"
    plain = b""
    for i, l, c, k, ip, isec, op, osec in A:
        if only and i not in only:
            continue
        o += "\n[account]\nid = %s\nemail = testuser@test.local\nname = Test User\nlabel = %s\nkind = %s\nprovider = other\nauth = password\n" % (i, l, k)
        o += "in_host = 10.0.2.2\nin_port = %d\nin_security = %s\nin_user = testuser\nout_host = 10.0.2.2\nout_port = %d\nout_security = %s\nout_user = \n" % (ip, isec, op, osec)
        o += "verify = %d\npop_keep = 1\npop_days = 0\ncolour = #%06X\ncheck_minutes = 0\nsignature = \n" % (1 if verify else 0, c)
        plain += ("%s\tin\t%s\n" % (i, PASSWORD)).encode()
    key, iv = os.urandom(32), os.urandom(12)
    ct, tag = gcm(key, iv, plain)
    with open(os.path.join(d, "accounts.ini"), "w", newline="\n") as f:
        f.write(o)
    with open(os.path.join(d, "key"), "wb") as f:
        f.write(key)
    with open(os.path.join(d, "secrets"), "wb") as f:
        f.write(b"ONYXMAIL1" + iv + ct + tag)


def main():
    global LOG, RECV, CTX
    if len(sys.argv) > 2 and sys.argv[1] == "seed":
        only = [x for x in sys.argv[3:] if not x.startswith("--")]
        seed(sys.argv[2], "--verify" in sys.argv, ",".join(only))
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="build/try/mailsrv")
    a = ap.parse_args()
    os.makedirs(a.dir, exist_ok=True)
    RECV = os.path.join(a.dir, "recv")
    os.makedirs(RECV, exist_ok=True)
    LOG = open(os.path.join(a.dir, "mailsrv.log"), "a", encoding="utf-8", errors="replace")
    crt, key = os.path.join(a.dir, "mailsrv.crt"), os.path.join(a.dir, "mailsrv.key")
    if not os.path.exists(crt):
        d = a.dir.replace("\\", "/")
        subprocess.check_call(["wsl", "openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes", "-keyout", d + "/mailsrv.key",
                               "-out", d + "/mailsrv.crt", "-days", "30", "-subj", "/CN=10.0.2.2", "-addext", "subjectAltName=IP:10.0.2.2"], stderr=subprocess.DEVNULL)
    CTX = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    CTX.load_cert_chain(crt, key)
    seed_boxes()
    log("DEMARRE")
    for port, fn, direct in ((1143, imap, False), (1993, imap, True), (1110, pop3, False), (1995, pop3, True), (1587, smtp, False), (1465, smtp, True), (1999, control, False)):
        threading.Thread(target=serve, args=(port, fn, direct), daemon=True).start()
    while True:
        time.sleep(3600)


main()
