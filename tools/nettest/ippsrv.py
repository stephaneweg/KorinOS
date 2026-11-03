"""ippsrv.py -- une fausse imprimante IPP pour l'essai de printd (src/sys/printd), sans papier ni reseau.

  python -I tools/nettest/ippsrv.py [--port 8631] [--dir build/ippjobs] [--pdf] [--busy N] [--stop] [--log fichier]

Sous QEMU l'invite joint le PC en 10.0.2.2 : l'adresse a taper dans l'applet est 10.0.2.2:8631. Le serveur repond a
Get-Printer-Attributes (comme une imprimante IPP Everywhere : PWG Raster, A4 et Lettre, 300 ppp, couleur, encre), a
Print-Job, Get-Job-Attributes, Cancel-Job. Chaque document recu va dans --dir : job<n>.pwg, ou job<n>.pdf si le travail
est en PDF, et pour le PWG Raster chaque page decodee en job<n>-p<k>.png, avec ses en-tetes dans le journal.
  --pdf     ne prend que application/pdf (printd envoie alors un PDF)
  --busy N  un travail reste « en cours » N interrogations
  --stop    printer-state = stopped (Verifier le dit)
  --none    ne prend ni PWG Raster ni PDF (printd doit refuser l'imprimante)
N'ouvre rien vers l'exterieur : ecoute sur le PC seul.
"""
import argparse
import os
import socket
import struct
import threading
import zlib

TAGS = {"int": 0x21, "bool": 0x22, "enum": 0x23, "res": 0x32, "range": 0x33, "text": 0x41, "name": 0x42, "key": 0x44,
        "uri": 0x45, "charset": 0x47, "lang": 0x48, "mime": 0x49}

args = None
jobs = {}
next_id = [100]
lock = threading.Lock()


def note(s):
    print(s, flush=True)
    if args and args.log:
        with open(args.log, "a", encoding="utf-8") as f:
            f.write(s + "\n")


def attr(tag, name, val):
    n = name.encode()
    return bytes([TAGS[tag]]) + struct.pack(">H", len(n)) + n + struct.pack(">H", len(val)) + val


def s_(tag, name, *vals):
    o = attr(tag, name, vals[0].encode())
    for v in vals[1:]:
        o += attr(tag, "", v.encode())
    return o


def i_(tag, name, *vals):
    o = attr(tag, name, struct.pack(">i", vals[0]))
    for v in vals[1:]:
        o += attr(tag, "", struct.pack(">i", v))
    return o


def operation_head(status, rid):
    return struct.pack(">BBHI", 2, 0, status, rid) + b"\x01" + s_("charset", "attributes-charset", "utf-8") + s_("lang", "attributes-natural-language", "fr")


def printer_attrs():
    f = ["application/pdf"] if args.pdf else ["image/jpeg"] if args.none else ["image/pwg-raster", "application/pdf"]
    o = b"\x04"
    o += s_("name", "printer-name", "Fausse imprimante") + s_("text", "printer-make-and-model", "ARMPI Faux IPP 3000")
    o += i_("enum", "printer-state", 5 if args.stop else 3) + s_("key", "printer-state-reasons", "media-empty-error" if args.stop else "none")
    o += s_("mime", "document-format-supported", *f)
    o += attr("bool", "color-supported", b"\x01")
    o += attr("range", "copies-supported", struct.pack(">ii", 1, 99))
    o += i_("enum", "print-quality-supported", 3, 4, 5)
    o += s_("key", "sides-supported", "one-sided")
    o += s_("key", "media-supported", "iso_a4_210x297mm", "na_letter_8.5x11in", "iso_a5_148x210mm")
    o += s_("key", "media-default", "iso_a4_210x297mm")
    for m in ("left", "right", "top"):
        o += i_("int", "media-%s-margin-supported" % m, 296)
    o += i_("int", "media-bottom-margin-supported", 1270)
    o += s_("key", "pwg-raster-document-type-supported", "sgray_8", "srgb_8")
    o += attr("res", "pwg-raster-document-resolution-supported", struct.pack(">iiB", 300, 300, 3))
    o += s_("name", "marker-names", "Noir", "Couleur") + i_("int", "marker-levels", 72, 41)
    return o


def parse(body):
    op = struct.unpack(">H", body[2:4])[0]
    rid = struct.unpack(">I", body[4:8])[0]
    i = 8
    at = {}
    last = ""
    while i < len(body):
        t = body[i]
        i += 1
        if t == 3:
            break
        if t < 0x10:
            continue
        nl = struct.unpack(">H", body[i:i + 2])[0]
        i += 2
        n = body[i:i + nl].decode("utf-8", "replace")
        i += nl
        vl = struct.unpack(">H", body[i:i + 2])[0]
        i += 2
        v = body[i:i + vl]
        i += vl
        if n:
            last = n
        at.setdefault(last, []).append((t, v))
    return op, rid, at, body[i:]


def text(at, name, d=""):
    return at[name][0][1].decode("utf-8", "replace") if name in at else d


def png(path, w, h, rgb, bpp):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * bpp:(y + 1) * w * bpp]

    def ch(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2 if bpp == 3 else 0, 0, 0, 0)))
        f.write(ch(b"IDAT", zlib.compress(bytes(raw), 1)) + ch(b"IEND", b""))


def decode_pwg(path, base):
    d = open(path, "rb").read()
    if d[:4] != b"RaS2":
        note("  PWG : signature fausse " + repr(d[:4]))
        return
    i = 4
    page = 0
    while i + 1796 <= len(d):
        h = d[i:i + 1796]
        i += 1796
        page += 1
        be = lambda o: struct.unpack(">I", h[o:o + 4])[0]
        w, hh, bpp, line, cs = be(372), be(376), be(388) // 8, be(392), be(400)
        name = h[1732:1796].split(b"\0")[0].decode()
        note("  page %d : %d x %d, %d octets par pixel, espace %d, %d ppp, papier %s, %d pages, qualite %d" % (page, w, hh, bpp, cs, be(276), name, be(452), be(484)))
        out = bytearray(w * hh * bpp)
        y = 0
        while y < hh:
            rep = d[i] + 1
            i += 1
            row = bytearray()
            while len(row) < line:
                c = d[i]
                i += 1
                if c < 128:
                    row += d[i:i + bpp] * (c + 1)
                    i += bpp
                else:
                    n = 257 - c
                    row += d[i:i + n * bpp]
                    i += n * bpp
            if len(row) != line:
                note("  ligne %d : %d octets au lieu de %d" % (y, len(row), line))
                return
            for k in range(rep):
                out[y * line:(y + 1) * line] = row
                y += 1
        png("%s-p%d.png" % (base, page), w, hh, bytes(out), bpp)
    note("  %d page(s), %d octets lus sur %d" % (page, i, len(d)))


def handle_ipp(body):
    op, rid, at, doc = parse(body)
    if op == 0x0B:
        note("Get-Printer-Attributes")
        return operation_head(0, rid) + printer_attrs() + b"\x03"
    if op == 0x02:
        with lock:
            jid = next_id[0]
            next_id[0] += 1
            jobs[jid] = {"polls": 0, "state": 5}
        fmt = text(at, "document-format")
        ext = "pdf" if fmt == "application/pdf" else "pwg"
        os.makedirs(args.dir, exist_ok=True)
        base = os.path.join(args.dir, "job%d" % jid)
        with open(base + "." + ext, "wb") as f:
            f.write(doc)
        note("Print-Job %d : %r, %s, %d octets, media=%s copies=%s qualite=%s couleur=%s" % (jid, text(at, "job-name"), fmt, len(doc), text(at, "media"),
             struct.unpack(">i", at["copies"][0][1])[0] if "copies" in at else 1, struct.unpack(">i", at["print-quality"][0][1])[0] if "print-quality" in at else "-", text(at, "print-color-mode")))
        if ext == "pwg":
            try:
                decode_pwg(base + ".pwg", base)
            except Exception as e:
                note("  PWG illisible : %r" % (e,))
        return operation_head(0, rid) + b"\x02" + i_("int", "job-id", jid) + i_("enum", "job-state", 5) + s_("key", "job-state-reasons", "none") + b"\x03"
    if op == 0x09:
        jid = struct.unpack(">i", at["job-id"][0][1])[0]
        j = jobs.get(jid)
        if not j:
            return struct.pack(">BBHI", 2, 0, 0x0406, rid) + b"\x03"
        j["polls"] += 1
        if j["state"] == 5 and j["polls"] > args.busy:
            j["state"] = 9
        note("Get-Job-Attributes %d -> %d" % (jid, j["state"]))
        return operation_head(0, rid) + b"\x02" + i_("enum", "job-state", j["state"]) + s_("key", "job-state-reasons", "none") + b"\x03"
    if op == 0x08:
        jid = struct.unpack(">i", at["job-id"][0][1])[0]
        if jid in jobs:
            jobs[jid]["state"] = 7
        note("Cancel-Job %d" % jid)
        return operation_head(0, rid) + b"\x03"
    note("operation inconnue %x" % op)
    return struct.pack(">BBHI", 2, 0, 0x0501, rid) + b"\x03"


def serve(c, a):
    try:
        d = b""
        while b"\r\n\r\n" not in d:
            b = c.recv(65536)
            if not b:
                return
            d += b
        head, rest = d.split(b"\r\n\r\n", 1)
        lines = head.decode("latin-1").split("\r\n")
        h = {l.split(":", 1)[0].lower(): l.split(":", 1)[1].strip() for l in lines[1:] if ":" in l}
        note("%s %s (%s) %s" % (a[0], lines[0], h.get("user-agent", ""), h.get("transfer-encoding", "")))
        body = bytearray()
        if h.get("transfer-encoding", "").lower() == "chunked":
            buf = rest
            while True:
                while b"\r\n" not in buf:
                    buf += c.recv(65536)
                sz, buf = buf.split(b"\r\n", 1)
                n = int(sz, 16)
                if n == 0:
                    break
                while len(buf) < n + 2:
                    b = c.recv(1 << 20)
                    if not b:
                        raise EOFError
                    buf += b
                body += buf[:n]
                buf = buf[n + 2:]
        else:
            n = int(h.get("content-length", "0"))
            body += rest
            while len(body) < n:
                body += c.recv(65536)
        r = handle_ipp(bytes(body))
        c.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(r) + r)
    except Exception as e:
        note("erreur : %r" % (e,))
    finally:
        c.close()


def main():
    global args
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8631)
    p.add_argument("--dir", default="build/ippjobs")
    p.add_argument("--pdf", action="store_true")
    p.add_argument("--none", action="store_true")
    p.add_argument("--stop", action="store_true")
    p.add_argument("--busy", type=int, default=1)
    p.add_argument("--log")
    args = p.parse_args()
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", args.port))
    s.listen(8)
    note("fausse imprimante sur 127.0.0.1:%d (invite : 10.0.2.2:%d)" % (args.port, args.port))
    while True:
        c, a = s.accept()
        threading.Thread(target=serve, args=(c, a), daemon=True).start()


main()
