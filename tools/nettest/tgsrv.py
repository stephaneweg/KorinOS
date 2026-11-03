"""tgsrv.py -- un petit serveur MTProto 2.0 d'essai pour l'app telegram (src/apps/telegram), sans compte ni reseau.

  python -I tools/nettest/tgsrv.py [--port 14430] [--log build/try/tgsrv.log] [--ntp] [--bad-salt]
  python -I tools/nettest/tgsrv.py seed <fichier>         ecrit le config.ini de l'essai (hote, port, cle RSA de test)
  python -I tools/nettest/tgsrv.py ctl <ligne>            une commande au port de controle (port + 1)

Tout reste sur 127.0.0.1 (sous QEMU l'invite joint le PC en 10.0.2.2). Bibliotheque standard seule ; AES et RSA ecrits ici.
La cle RSA de test est faite au premier lancement et gardee dans build/try/tgsrv_rsa.txt, les cles d'autorisation dans
build/try/tgsrv_keys.txt ; rien d'autre ne les connait. Le journal ne montre ni cle ni api_hash.

Ce qu'il fait : transport intermediaire 0xeeeeeeee, echange de cle complet (req_pq_multi, req_DH_params,
set_client_DH_params : RSA_PAD dechiffre et verifie, DH 2048 bits), messages chiffres MTProto 2.0 dans les deux sens,
new_session_created, msgs_ack, conteneurs, rpc_result, rpc_error, gzip_packed (les grosses reponses), ping, bad_server_salt
(--bad-salt). Le schema vient de src/apps/telegram/schema.inc : un codec TL generique, comme tl.h.

Le parcours fictif : numero 9996612345 ; le code est 12345 ; 9996620000 demande en plus le mot de passe (SRP) hunter2 ;
9996630000 n'a pas de compte (auth.signUp) ; un autre code : PHONE_CODE_INVALID. Puis un utilisateur fictif (Sam Tester),
Alice, Bob, Iris (pas un contact), un groupe, leurs messages, photos de profil, une photo dans un message (data/demo-lake.jpg).
Alice repond a ce qu'on lui ecrit (typing, puis un message). Commandes du port de controle :
  msg <user_id> <texte>      un message entrant (5001 Alice, 5002 Bob, 5003 Iris)
  typing <user_id>           la marque "ecrit"
  status <user_id> on|off    updateUserStatus
  read <user_id> <max_id>    updateReadHistoryOutbox : les coches lues
  drop                       ferme les connexions
  rekey                      oublie les cles (la prochaine requete recoit -404)
"""
import base64
import gzip
import hashlib
import os
import re
import secrets
import socket
import struct
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SCHEMA = os.path.join(ROOT, "src", "apps", "telegram", "schema.inc")
TRY = os.path.join(ROOT, "build", "try")
RSA_FILE = os.path.join(TRY, "tgsrv_rsa.txt")
KEYS_FILE = os.path.join(TRY, "tgsrv_keys.txt")
PHOTO = os.path.join(ROOT, "src", "apps", "telegram", "data", "demo-lake.jpg")

DH_PRIME = int("c71caeb9c6b1c9048e6c522f70f13f73980d40238e3e21c14934d037563d930f48198a0aa7c14058229493d22530f4dbfa336f6e0ac925139543aed44cce7c3720fd51f69458705ac68cd4fe6b6b13abdc9746512969328454f18faf8c595f642477fe96bb2a941d5bcd1d4ac8cc49880708fa9b378e3c4f3a9060bee67cf9a4a4a695811051907e162753b56b0f6b410dba74d8a84b2a14b3144e0ef1284754fd17ed950d5965b4b9dd46582db1178d169c6bc465b0d6ff9ca3928fef5b9ae4e418fc15e83ebea0f87fa9ff5eed70050ded2849f47bf959d956850ce929851f0d8115f635b105ee2e4e15d04b2454bf6f4fadf034b10403119cd8e3b92fcc5b", 16)
DH_G = 3

log_f = None
lock = threading.RLock()
conns = []
keys = {}                       # key id -> cle d'autorisation


def note(s):
    line = time.strftime("%H:%M:%S ") + s
    print(line, flush=True)
    if log_f:
        log_f.write(line + "\n")
        log_f.flush()


# ---- AES (tables), IGE --------------------------------------------------------------------------------------------

def _aes_tables():
    sbox = [0] * 256
    p = q = 1
    while True:
        p = p ^ ((p << 1) & 255) ^ (0x1B if p & 0x80 else 0)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 255
        if q & 0x80:
            q ^= 0x09
        x = q ^ ((q << 1 | q >> 7) & 255) ^ ((q << 2 | q >> 6) & 255) ^ ((q << 3 | q >> 5) & 255) ^ ((q << 4 | q >> 4) & 255)
        sbox[p] = x ^ 0x63
        if p == 1:
            break
    sbox[0] = 0x63
    inv = [0] * 256
    for i, s in enumerate(sbox):
        inv[s] = i
    return sbox, inv


SBOX, INV = _aes_tables()


def _xt(a):
    return ((a << 1) ^ 0x1B) & 255 if a & 0x80 else a << 1


def _mul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a = _xt(a)
        b >>= 1
    return r


TE = [[0] * 256 for _ in range(4)]
TD = [[0] * 256 for _ in range(4)]
for _i in range(256):
    _s = SBOX[_i]
    _t = (_mul(_s, 2) << 24) | (_s << 16) | (_s << 8) | _mul(_s, 3)
    _v = INV[_i]
    _u = (_mul(_v, 14) << 24) | (_mul(_v, 9) << 16) | (_mul(_v, 13) << 8) | _mul(_v, 11)
    for _k in range(4):
        TE[_k][_i] = _t
        TD[_k][_i] = _u
        _t = ((_t >> 8) | (_t << 24)) & 0xFFFFFFFF
        _u = ((_u >> 8) | (_u << 24)) & 0xFFFFFFFF


def aes_expand(key):
    nk = len(key) // 4
    nr = nk + 6
    w = list(struct.unpack(">%dI" % nk, key))
    rc = 1
    for i in range(nk, 4 * (nr + 1)):
        t = w[i - 1]
        if i % nk == 0:
            t = ((SBOX[(t >> 16) & 255] << 24) | (SBOX[(t >> 8) & 255] << 16) | (SBOX[t & 255] << 8) | SBOX[t >> 24]) ^ (rc << 24)
            rc = _xt(rc)
        elif nk > 6 and i % nk == 4:
            t = (SBOX[t >> 24] << 24) | (SBOX[(t >> 16) & 255] << 16) | (SBOX[(t >> 8) & 255] << 8) | SBOX[t & 255]
        w.append(w[i - nk] ^ t)
    return w, nr


def aes_invkey(w, nr):
    d = [0] * len(w)
    for r in range(nr + 1):
        for c in range(4):
            x = w[4 * (nr - r) + c]
            if 0 < r < nr:
                x = TD[0][SBOX[x >> 24]] ^ TD[1][SBOX[(x >> 16) & 255]] ^ TD[2][SBOX[(x >> 8) & 255]] ^ TD[3][SBOX[x & 255]]
            d[4 * r + c] = x
    return d


def aes_block(ws, nr, a, b, c, d, dec):
    T0, T1, T2, T3 = TD if dec else TE
    B = INV if dec else SBOX
    a ^= ws[0]
    b ^= ws[1]
    c ^= ws[2]
    d ^= ws[3]
    k = 4
    if dec:
        for _ in range(nr - 1):
            a, b, c, d = (T0[a >> 24] ^ T1[(d >> 16) & 255] ^ T2[(c >> 8) & 255] ^ T3[b & 255] ^ ws[k],
                          T0[b >> 24] ^ T1[(a >> 16) & 255] ^ T2[(d >> 8) & 255] ^ T3[c & 255] ^ ws[k + 1],
                          T0[c >> 24] ^ T1[(b >> 16) & 255] ^ T2[(a >> 8) & 255] ^ T3[d & 255] ^ ws[k + 2],
                          T0[d >> 24] ^ T1[(c >> 16) & 255] ^ T2[(b >> 8) & 255] ^ T3[a & 255] ^ ws[k + 3])
            k += 4
        return ((B[a >> 24] << 24 | B[(d >> 16) & 255] << 16 | B[(c >> 8) & 255] << 8 | B[b & 255]) ^ ws[k],
                (B[b >> 24] << 24 | B[(a >> 16) & 255] << 16 | B[(d >> 8) & 255] << 8 | B[c & 255]) ^ ws[k + 1],
                (B[c >> 24] << 24 | B[(b >> 16) & 255] << 16 | B[(a >> 8) & 255] << 8 | B[d & 255]) ^ ws[k + 2],
                (B[d >> 24] << 24 | B[(c >> 16) & 255] << 16 | B[(b >> 8) & 255] << 8 | B[a & 255]) ^ ws[k + 3])
    for _ in range(nr - 1):
        a, b, c, d = (T0[a >> 24] ^ T1[(b >> 16) & 255] ^ T2[(c >> 8) & 255] ^ T3[d & 255] ^ ws[k],
                      T0[b >> 24] ^ T1[(c >> 16) & 255] ^ T2[(d >> 8) & 255] ^ T3[a & 255] ^ ws[k + 1],
                      T0[c >> 24] ^ T1[(d >> 16) & 255] ^ T2[(a >> 8) & 255] ^ T3[b & 255] ^ ws[k + 2],
                      T0[d >> 24] ^ T1[(a >> 16) & 255] ^ T2[(b >> 8) & 255] ^ T3[c & 255] ^ ws[k + 3])
        k += 4
    return ((B[a >> 24] << 24 | B[(b >> 16) & 255] << 16 | B[(c >> 8) & 255] << 8 | B[d & 255]) ^ ws[k],
            (B[b >> 24] << 24 | B[(c >> 16) & 255] << 16 | B[(d >> 8) & 255] << 8 | B[a & 255]) ^ ws[k + 1],
            (B[c >> 24] << 24 | B[(d >> 16) & 255] << 16 | B[(a >> 8) & 255] << 8 | B[b & 255]) ^ ws[k + 2],
            (B[d >> 24] << 24 | B[(a >> 16) & 255] << 16 | B[(b >> 8) & 255] << 8 | B[c & 255]) ^ ws[k + 3])


def ige(enc, key, iv, data):
    assert len(data) % 16 == 0
    w, nr = aes_expand(key)
    if not enc:
        w = aes_invkey(w, nr)
    cp = struct.unpack(">4I", iv[:16] if enc else iv[16:])          # le precedent de son cote
    pp = struct.unpack(">4I", iv[16:] if enc else iv[:16])
    out = bytearray()
    fmt = ">4I"
    for o in range(0, len(data), 16):
        b = struct.unpack_from(fmt, data, o)
        t = aes_block(w, nr, b[0] ^ cp[0], b[1] ^ cp[1], b[2] ^ cp[2], b[3] ^ cp[3], not enc)
        r = (t[0] ^ pp[0], t[1] ^ pp[1], t[2] ^ pp[2], t[3] ^ pp[3])
        out += struct.pack(fmt, *r)
        cp, pp = r, b
    return bytes(out)


# ---- SHA, RSA ---------------------------------------------------------------------------------------------------------

def sha1(*a):
    return hashlib.sha1(b"".join(a)).digest()


def sha256(*a):
    return hashlib.sha256(b"".join(a)).digest()


def is_prime(n):
    if n < 2:
        return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d, r = n - 1, 0
    while d % 2 == 0:
        d //= 2
        r += 1
    for _ in range(24):
        a = secrets.randbelow(n - 3) + 2
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(r - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True


def rand_prime(bits):
    while True:
        p = secrets.randbits(bits) | (3 << (bits - 2)) | 1
        if is_prime(p):
            return p


def rsa_key():
    if os.path.exists(RSA_FILE):
        n, d = open(RSA_FILE).read().split()
        return int(n, 16), int(d, 16)
    note("cle RSA de test : creation")
    e = 65537
    while True:
        p, q = rand_prime(1024), rand_prime(1024)
        n = p * q
        if n.bit_length() == 2048 and (p - 1) % e and (q - 1) % e:
            break
    d = pow(e, -1, (p - 1) * (q - 1))
    os.makedirs(TRY, exist_ok=True)
    with open(RSA_FILE, "w") as f:
        f.write("%x %x\n" % (n, d))
    return n, d


def rsa_fingerprint(n):
    ser = tl_bytes(n.to_bytes(256, "big")) + tl_bytes(b"\x01\x00\x01")
    return struct.unpack("<q", sha1(ser)[12:20])[0]


# ---- TL : le schema de l'app, un codec generique -------------------------------------------------------------------------

class Ctor:
    __slots__ = ("id", "name", "params", "type", "fn")


BY_ID = {}
BY_NAME = {}


def parse_type(s):
    if s in ("int", "long", "double", "string", "bytes", "Bool", "true", "int128", "int256", "#"):
        return s
    if s.startswith("Vector<"):
        return ("V", parse_type(s[7:-1]))
    if s.startswith("vector<"):
        return ("v", parse_type(s[7:-1]))
    base = s.split(".")[-1]
    if s[0] == "%" or base[:1].islower():
        return ("B", s.lstrip("%"))
    return "O"


def load_schema():
    txt = open(SCHEMA, encoding="utf-8").read()
    for m in re.finditer(r'^\s*"((?:\\.|[^"\\])*)\\n"', txt, re.M):
        line = m.group(1).replace('\\"', '"').replace("\\\\", "\\")
        c = Ctor()
        c.fn = line.startswith("F ")
        if c.fn:
            line = line[2:]
        head, _, res = line.partition(" = ")
        parts = head.split(" ")
        c.id = int(parts[0], 16)
        c.name = parts[1]
        c.type = res.strip()
        c.params = []
        for p in parts[2:]:
            nm, _, t = p.partition(":")
            q = None
            if "?" in t:
                cond, t = t.split("?", 1)
                fl, bit = cond.split(".")
                q = (fl, int(bit))
            c.params.append((nm, parse_type(t), q))
        BY_ID[c.id] = c
        BY_NAME[c.name] = c


def tl_bytes(b):
    n = len(b)
    h = bytes([n]) if n < 254 else bytes([254]) + n.to_bytes(3, "little")
    o = h + b
    return o + b"\0" * (-len(o) % 4)


TRUE_ID, FALSE_ID, VEC_ID = 0x997275b5, 0xbc799737, 0x1cb5c415


def enc(t, v):
    if t == "int":
        return struct.pack("<i", v if v is not None else 0)
    if t == "long":
        return struct.pack("<q", v if v is not None else 0)
    if t == "double":
        return struct.pack("<d", v or 0.0)
    if t in ("string", "bytes"):
        if v is None:
            v = b""
        return tl_bytes(v.encode("utf-8") if isinstance(v, str) else v)
    if t == "Bool":
        return struct.pack("<I", TRUE_ID if v else FALSE_ID)
    if t == "true":
        return b""
    if t in ("int128", "int256"):
        return v
    if isinstance(t, tuple) and t[0] == "V":
        v = v or []
        return struct.pack("<II", VEC_ID, len(v)) + b"".join(enc(t[1], x) for x in v)
    if isinstance(t, tuple) and t[0] == "v":
        v = v or []
        return struct.pack("<I", len(v)) + b"".join(enc(t[1], x) for x in v)
    if isinstance(t, tuple) and t[0] == "B":
        return enc_fields(BY_NAME[v["_"]], v)
    if isinstance(v, (bytes, bytearray)):         # (deja encode)
        return bytes(v)
    if isinstance(v, bool):
        return struct.pack("<I", TRUE_ID if v else FALSE_ID)
    if v is None:
        raise ValueError("objet manquant")
    return struct.pack("<I", BY_NAME[v["_"]].id) + enc_fields(BY_NAME[v["_"]], v)


def enc_fields(c, v):
    o = bytearray()
    for nm, t, q in c.params:
        if t == "#":
            fl = 0
            for nm2, t2, q2 in c.params:
                if q2 and q2[0] == nm:
                    x = v.get(nm2)
                    if (t2 == "true" and x) or (t2 != "true" and x is not None):
                        fl |= 1 << q2[1]
            o += struct.pack("<I", fl)
            continue
        if q:
            if t == "true" or v.get(nm) is None:
                continue
        o += enc(t, v.get(nm))
    return bytes(o)


def obj(_name, **kw):
    kw["_"] = _name
    return kw


def enc_obj(v):
    return enc("O", v)


def vector(items):
    return struct.pack("<II", VEC_ID, len(items)) + b"".join(enc_obj(x) for x in items)


class Rd:
    def __init__(self, b):
        self.b = b
        self.o = 0

    def take(self, n):
        if self.o + n > len(self.b):
            raise ValueError("court")
        r = self.b[self.o:self.o + n]
        self.o += n
        return r

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]

    def i64(self):
        return struct.unpack("<q", self.take(8))[0]

    def tlbytes(self):
        n = self.take(1)[0]
        h = 1
        if n == 254:
            n = int.from_bytes(self.take(3), "little")
            h = 4
        r = self.take(n)
        self.take(-(h + n) % 4)
        return r


def dec(r, t):
    if t == "int":
        return r.i32()
    if t == "#":
        return r.u32()
    if t == "long":
        return r.i64()
    if t == "double":
        return struct.unpack("<d", r.take(8))[0]
    if t in ("string", "bytes"):
        return r.tlbytes()
    if t == "Bool":
        return r.u32() == TRUE_ID
    if t == "true":
        return True
    if t == "int128":
        return r.take(16)
    if t == "int256":
        return r.take(32)
    if isinstance(t, tuple) and t[0] == "V":
        if r.u32() != VEC_ID:
            raise ValueError("vecteur attendu")
        return [dec(r, t[1]) for _ in range(r.u32())]
    if isinstance(t, tuple) and t[0] == "v":
        return [dec(r, t[1]) for _ in range(r.u32())]
    if isinstance(t, tuple) and t[0] == "B":
        return dec_fields(r, BY_NAME[t[1]])
    return dec_obj(r)


def dec_obj(r):
    i = r.u32()
    if i == TRUE_ID:
        return True
    if i == FALSE_ID:
        return False
    if i == VEC_ID:
        return [dec_obj(r) for _ in range(r.u32())]
    c = BY_ID.get(i)
    if not c:
        raise ValueError("constructeur inconnu %08x" % i)
    return dec_fields(r, c)


def dec_fields(r, c):
    v = {"_": c.name}
    for nm, t, q in c.params:
        if q:
            if not v.get(q[0], 0) & (1 << q[1]):
                continue
        v[nm] = dec(r, t)
    return v


# ---- les donnees fictives ------------------------------------------------------------------------------------------------

SELF = 5000
ALICE, BOB, IRIS = 5001, 5002, 5003
GROUP = 6001
HOME_DC = 2
PHOTO_ALICE = 7001
PHOTO_MSG = 7101
PASSWORD = b"hunter2"


def people():
    now = int(time.time())
    return {
        SELF: dict(first_name="Sam", last_name="Tester", username="samtester", phone="9996612345", contact=False, status=obj("userStatusOnline", expires=now + 300), self=True),
        ALICE: dict(first_name="Alice", last_name="Local", username="alice_local", phone="9996600001", contact=True, status=obj("userStatusOnline", expires=now + 3600), photo=obj("userProfilePhoto", photo_id=PHOTO_ALICE, dc_id=HOME_DC)),
        BOB: dict(first_name="Bob", last_name="Local", username=None, phone="9996600002", contact=True, status=obj("userStatusOffline", was_online=now - 900)),
        IRIS: dict(first_name="Iris", last_name="Inconnue", username=None, phone=None, contact=False, status=obj("userStatusRecently")),
    }


def tl_user(uid):
    p = people()[uid]
    kw = {k: v for k, v in p.items() if k not in ("self", "contact") and v is not None}
    if p.get("self"):
        kw["self"] = True
    if p.get("contact"):
        kw["contact"] = True
    return obj("user", id=uid, access_hash=uid * 7919, **kw)


def tl_users():
    return [tl_user(u) for u in people()]


def tl_chat():
    return obj("chat", id=GROUP, title="Groupe d'essai", photo=obj("chatPhotoEmpty"), participants_count=3, date=int(time.time()) - 86400, version=1)


def peer_user(u):
    return obj("peerUser", user_id=u)


class World:
    def __init__(self):
        self.pts = 100
        self.next_id = 100
        now = int(time.time())
        self.hist = {ALICE: [], BOB: [], IRIS: [], -GROUP: []}
        self.push(ALICE, ALICE, "Bonjour Sam ! Tu me recois ?", now - 7200)
        self.push(ALICE, SELF, "Oui, en clair je veux dire : chiffre et bien arrive :)", now - 7100)
        self.photo_msg()
        self.push(ALICE, ALICE, "Voila la photo du lac (Y)", now - 7000)
        self.push(BOB, BOB, "A demain", now - 90000)
        self.push(IRIS, IRIS, "Bonjour, on ne se connait pas encore.", now - 3000)
        self.push(-GROUP, ALICE, "Bienvenue dans le groupe", now - 5000)
        self.push(-GROUP, BOB, "Salut tout le monde", now - 4800)
        self.unread = {IRIS: 1}
        self.read_out = {ALICE: 0}
        self.contacts_extra = []

    def new_id(self):
        self.next_id += 1
        return self.next_id

    def push(self, conv, frm, text, date=None, media=None):
        m = dict(id=self.new_id(), frm=frm, conv=conv, text=text, date=date or int(time.time()), media=media)
        self.hist[conv].append(m)
        return m

    def photo_msg(self):
        ph = obj("photo", id=PHOTO_MSG, access_hash=11, file_reference=b"ref", date=int(time.time()) - 7050,
                 sizes=[obj("photoSize", type="x", w=640, h=400, size=os.path.getsize(PHOTO))], dc_id=HOME_DC)
        self.push(ALICE, ALICE, "", int(time.time()) - 7050, obj("messageMediaPhoto", photo=ph))

    def tl_msg(self, m):
        out = m["frm"] == SELF
        peer = obj("peerChat", chat_id=GROUP) if m["conv"] < 0 else peer_user(m["conv"])
        kw = dict(id=m["id"], peer_id=peer, date=m["date"], message=m["text"])
        if out:
            kw["out"] = True
        if m["conv"] < 0 or out:
            kw["from_id"] = peer_user(m["frm"])
        if m["media"]:
            kw["media"] = m["media"]
        return obj("message", **kw)

    def dialogs(self):
        out = []
        tops = []
        convs = sorted(self.hist, key=lambda c: -self.hist[c][-1]["date"])
        for c in convs:
            top = self.hist[c][-1]
            tops.append(self.tl_msg(top))
            peer = obj("peerChat", chat_id=GROUP) if c < 0 else peer_user(c)
            kw = {}
            if c == ALICE:
                kw["pinned"] = True
            out.append(obj("dialog", peer=peer, top_message=top["id"], read_inbox_max_id=0 if self.unread.get(c) else top["id"],
                           read_outbox_max_id=self.read_out.get(c, 0), unread_count=self.unread.get(c, 0),
                           notify_settings=obj("peerNotifySettings"), **kw))
        return obj("messages.dialogs", dialogs=out, messages=tops, chats=[tl_chat()], users=tl_users())


world = None


def srp_setup():
    salt1, salt2 = secrets.token_bytes(40), secrets.token_bytes(16)
    H = lambda *a: hashlib.sha256(b"".join(a)).digest()
    SH = lambda d, s: H(s, d, s)
    ph1 = SH(SH(PASSWORD, salt1), salt2)
    ph2 = SH(hashlib.pbkdf2_hmac("sha512", ph1, salt1, 100000, 64), salt2)
    x = int.from_bytes(ph2, "big")
    v = pow(DH_G, x, DH_PRIME)
    pb = DH_PRIME.to_bytes(256, "big")
    gb = DH_G.to_bytes(256, "big")
    k = int.from_bytes(H(pb, gb), "big")
    b = secrets.randbits(2040)
    B = (k * v + pow(DH_G, b, DH_PRIME)) % DH_PRIME
    return dict(salt1=salt1, salt2=salt2, v=v, b=b, B=B, id=secrets.randbits(62))


def srp_check(s, A_b, M1):
    H = lambda *a: hashlib.sha256(b"".join(a)).digest()
    A = int.from_bytes(A_b, "big")
    pb = DH_PRIME.to_bytes(256, "big")
    gb = DH_G.to_bytes(256, "big")
    Ab, Bb = A.to_bytes(256, "big"), s["B"].to_bytes(256, "big")
    u = int.from_bytes(H(Ab, Bb), "big")
    S = pow(A * pow(s["v"], u, DH_PRIME) % DH_PRIME, s["b"], DH_PRIME)
    K = H(S.to_bytes(256, "big"))
    ok = H(bytes(i ^ j for i, j in zip(H(pb), H(gb))), H(s["salt1"]), H(s["salt2"]), Ab, Bb, K)
    return ok == M1


# ---- une connexion -----------------------------------------------------------------------------------------------------------

class Conn:
    def __init__(self, sock, peer, rsa, bad_salt):
        self.s = sock
        self.peer = peer
        self.n, self.d = rsa
        self.fp = rsa_fingerprint(self.n)
        self.key = None
        self.key_id = None
        self.session = None
        self.salt = secrets.randbits(63)
        self.seq = 0
        self.last_id = 0
        self.tmp = None
        self.dh = None
        self.wlock = threading.Lock()
        self.bad_salt = bad_salt
        self.inited = False
        self.srp = None
        self.phone = ""
        self.signed = False
        self.first_seen = False

    # -- l'envoi

    def frame(self, p):
        with self.wlock:
            self.s.sendall(struct.pack("<I", len(p)) + p)

    def msg_id(self):
        t = time.time()
        i = (int(t) << 32) | (int((t % 1) * (1 << 32)) & 0xFFFFFFFC) | 1
        if i <= self.last_id:
            i = self.last_id + 4
        self.last_id = i
        return i

    def send_plain(self, body):
        self.frame(struct.pack("<qqI", 0, self.msg_id(), len(body)) + body)

    def kdf(self, msg_key, x):
        k = self.key
        a = sha256(msg_key, k[x:x + 36])
        b = sha256(k[40 + x:40 + x + 36], msg_key)
        return a[:8] + b[8:24] + a[24:32], b[:8] + a[8:24] + b[24:32]

    def send_enc(self, body, content=True, seq=None):
        mid = self.msg_id()
        if seq is None:
            seq = self.seq * 2 + (1 if content else 0)
            if content:
                self.seq += 1
        n = len(body)
        pl = struct.pack("<qqqiI", self.salt, self.session, mid, seq, n) + body
        pad = 16 - (len(pl) % 16)
        if pad < 12:
            pad += 16
        pl += secrets.token_bytes(pad)
        mk = sha256(self.key[96:128], pl)[8:24]
        ak, ai = self.kdf(mk, 8)
        self.frame(self.key_id + mk + ige(True, ak, ai, pl))
        return mid

    def container(self, items):
        o = bytearray(struct.pack("<II", 0x73f1f8dc, len(items)))
        for body, content in items:
            seq = self.seq * 2 + (1 if content else 0)
            if content:
                self.seq += 1
            o += struct.pack("<qiI", self.msg_id(), seq, len(body)) + body
        self.send_enc(bytes(o), content=False)

    def rpc_body(self, req, res):
        return struct.pack("<Iq", 0xf35c6d01, req) + res

    def result(self, req, res, zipped=False):
        if zipped and len(res) > 256:
            res = struct.pack("<I", 0x3072cfa1) + tl_bytes(gzip.compress(res))
        self.send_enc(self.rpc_body(req, res))

    def error(self, req, code, text):
        note("  rpc_error %d %s" % (code, text))
        self.send_enc(self.rpc_body(req, enc_obj(obj("rpc_error", error_code=code, error_message=text))))

    def push(self, up):
        if self.key and self.session is not None:
            self.send_enc(enc_obj(up))

    # -- la reception

    def run(self):
        try:
            tag = self.recvn(4)
            if tag != b"\xee\xee\xee\xee":
                note("%s : transport inconnu %s" % (self.peer, tag.hex()))
                return
            note("%s : transport intermediaire" % self.peer)
            while True:
                n = struct.unpack("<I", self.recvn(4))[0]
                self.packet(self.recvn(n))
        except (ConnectionError, OSError, EOFError) as e:
            note("%s : fin (%s)" % (self.peer, e))
        finally:
            with lock:
                if self in conns:
                    conns.remove(self)
            self.s.close()

    def recvn(self, n):
        b = bytearray()
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise EOFError("fermee")
            b += c
        return bytes(b)

    def packet(self, p):
        kid = p[:8]
        if kid == b"\0" * 8:
            n = struct.unpack("<I", p[16:20])[0]
            self.plain(dec_obj(Rd(p[20:20 + n])))
            return
        with lock:
            k = keys.get(kid)
        if not k:
            note("%s : cle inconnue : -404" % self.peer)
            self.frame(struct.pack("<i", -404))
            return
        if k is not self.key:
            self.key = k
            self.key_id = kid
        mk = p[8:24]
        ak, ai = self.kdf(mk, 0)
        pl = ige(False, ak, ai, p[24:])
        if sha256(k[88:120], pl)[8:24] != mk:
            note("%s : msg_key faux" % self.peer)
            return
        salt, sid, mid, seq, n = struct.unpack("<qqqiI", pl[:32])
        pad = len(pl) - 32 - n
        if pad < 12 or pad > 1024:
            note("%s : remplissage %d refuse" % (self.peer, pad))
            return
        note("%s : <- chiffre, msg %x seq %d, %d octets, msg_key verifie" % (self.peer, mid, seq, n))
        if sid != self.session:
            self.session = sid
            self.seq = 0
            self.first_seen = True
        self.message(mid, seq, pl[32:32 + n])

    # -- l'echange de cle

    def plain(self, q):
        n = q["_"]
        note("%s : <- %s" % (self.peer, n))
        if n == "req_pq_multi" or n == "req_pq":
            self.nonce = q["nonce"]
            self.snonce = secrets.token_bytes(16)
            p, r = rand_prime(31), rand_prime(31)
            self.pq = p * r
            pq = self.pq.to_bytes((self.pq.bit_length() + 7) // 8, "big")
            self.send_plain(enc_obj(obj("resPQ", nonce=self.nonce, server_nonce=self.snonce, pq=pq, server_public_key_fingerprints=[self.fp])))
            note("  -> resPQ, pq de %d bits" % self.pq.bit_length())
        elif n == "req_DH_params":
            self.dh_params(q)
        elif n == "set_client_DH_params":
            self.client_dh(q)
        else:
            note("  inattendu")

    def dh_params(self, q):
        p = int.from_bytes(q["p"], "big")
        r = int.from_bytes(q["q"], "big")
        if q["nonce"] != self.nonce or q["server_nonce"] != self.snonce or p * r != self.pq or p >= r:
            raise EOFError("pq faux")
        note("  pq factorise par le client : verifie")
        if q["public_key_fingerprint"] != self.fp:
            raise EOFError("empreinte RSA inconnue")
        c = int.from_bytes(q["encrypted_data"], "big")
        m = pow(c, self.d, self.n).to_bytes(256, "big")
        h = sha256(m[32:])
        temp = bytes(a ^ b for a, b in zip(m[:32], h))
        dwh = ige(False, temp, b"\0" * 32, m[32:])
        rev, hsh = dwh[:192], dwh[192:]
        data = rev[::-1]
        if sha256(temp, data) != hsh:
            raise EOFError("RSA_PAD faux")
        note("  RSA_PAD dechiffre et verifie")
        inner = dec_obj(Rd(data))
        if inner["_"] != "p_q_inner_data_dc" or inner["nonce"] != self.nonce or inner["server_nonce"] != self.snonce:
            raise EOFError("donnees internes fausses : %s" % inner["_"])
        note("  p_q_inner_data_dc, dc %d" % inner["dc"])
        self.new_nonce = inner["new_nonce"]
        nn, sn = self.new_nonce, self.snonce
        h1, h2, h3 = sha1(nn, sn), sha1(sn, nn), sha1(nn, nn)
        self.tkey, self.tiv = h1 + h2[:12], h2[12:] + h3 + nn[:4]
        self.a = secrets.randbits(2040)
        ga = pow(DH_G, self.a, DH_PRIME)
        body = enc_obj(obj("server_DH_inner_data", nonce=self.nonce, server_nonce=self.snonce, g=DH_G,
                           dh_prime=DH_PRIME.to_bytes(256, "big"), g_a=ga.to_bytes(256, "big"), server_time=int(time.time())))
        ans = sha1(body) + body
        ans += secrets.token_bytes(-len(ans) % 16)
        self.send_plain(enc_obj(obj("server_DH_params_ok", nonce=self.nonce, server_nonce=self.snonce,
                                    encrypted_answer=ige(True, self.tkey, self.tiv, ans))))
        note("  -> server_DH_params_ok")

    def client_dh(self, q):
        ans = ige(False, self.tkey, self.tiv, q["encrypted_data"])
        r = Rd(ans[20:])
        inner = dec_obj(r)
        if inner["_"] != "client_DH_inner_data" or sha1(ans[20:20 + r.o]) != ans[:20]:
            raise EOFError("client_DH_inner_data faux")
        gb = int.from_bytes(inner["g_b"], "big")
        if not (1 << 1984) <= gb <= DH_PRIME - (1 << 1984):
            raise EOFError("g_b hors bornes")
        k = pow(gb, self.a, DH_PRIME).to_bytes(256, "big")
        kh = sha1(k)
        self.key = k
        self.key_id = kh[12:20]
        with lock:
            keys[self.key_id] = k
            with open(KEYS_FILE, "a") as f:
                f.write(base64.b64encode(k).decode() + "\n")
        self.salt = int.from_bytes(bytes(a ^ b for a, b in zip(self.new_nonce[:8], self.snonce[:8])), "little", signed=True)
        self.send_plain(enc_obj(obj("dh_gen_ok", nonce=self.nonce, server_nonce=self.snonce,
                                    new_nonce_hash1=sha1(self.new_nonce, b"\x01", kh[:8])[4:20])))
        note("  -> dh_gen_ok : cle d'autorisation de 2048 bits etablie")

    # -- les messages

    def message(self, mid, seq, b):
        i = struct.unpack("<I", b[:4])[0]
        if i == 0x62d6b459:
            r = Rd(b[4:])
            note("  msgs_ack de %d message(s)" % len(dec(r, ("V", "long"))))
            return
        if i == 0x73f1f8dc:
            r = Rd(b[4:])
            for _ in range(r.u32()):
                m, s, n = struct.unpack("<qiI", r.take(16))
                self.message(m, s, r.take(n))
            return
        if self.first_seen:
            self.first_seen = False
            ns = struct.pack("<Iqqq", 0x9ec20908, mid, secrets.randbits(62), self.salt)
            if self.bad_salt:                                       # (un sel faux la premiere fois : le client refait)
                self.bad_salt = False
                self.send_enc(ns)
                self.salt ^= 0x1234
                self.send_enc(struct.pack("<IqiIq", 0xedab447b, mid, seq, 48, self.salt), content=False)
                note("  -> new_session_created, bad_server_salt")
                return
            self.pending_ns = ns
        else:
            self.pending_ns = None
        q = dec_obj(Rd(b))
        while isinstance(q, dict) and q["_"] in ("invokeWithLayer", "initConnection", "invokeWithoutUpdates"):
            if q["_"] == "invokeWithLayer":
                note("  invokeWithLayer, couche %d" % q["layer"])
            if q["_"] == "initConnection":
                note("  initConnection : api_id present, modele %s, systeme %s, app %s" % (q["device_model"].decode(), q["system_version"].decode(), q["app_version"].decode()))
                self.inited = True
            q = q["query"]
        name = q["_"] if isinstance(q, dict) else "?"
        note("%s : requete %s" % (self.peer, name))
        f = getattr(self, "f_" + name.replace(".", "_"), None)
        ns = self.pending_ns
        self.pending_ns = None
        if ns:                                                      # (new_session_created et la reponse, en conteneur)
            self.ns_prefix = ns
        else:
            self.ns_prefix = None
        if f:
            f(mid, q)
        else:
            note("  pas de reponse : BAD_REQUEST")
            self.error(mid, 400, "METHOD_NOT_HANDLED")
        if self.ns_prefix:
            self.send_enc(self.ns_prefix)
            self.ns_prefix = None

    def ok(self, mid, res, zipped=False):
        if self.ns_prefix:
            if zipped and len(res) > 256:
                res = struct.pack("<I", 0x3072cfa1) + tl_bytes(gzip.compress(res))
            self.container([(self.ns_prefix, True), (self.rpc_body(mid, res), True)])
            self.ns_prefix = None
            note("  -> new_session_created et la reponse, en conteneur")
            return
        self.result(mid, res, zipped)

    # -- les fonctions

    def f_ping_delay_disconnect(self, mid, q):
        self.send_enc(enc_obj(obj("pong", msg_id=mid, ping_id=q["ping_id"])), content=True)
        note("  -> pong")

    def f_help_getConfig(self, mid, q):
        now = int(time.time())
        dc = obj("dcOption", id=HOME_DC, ip_address="203.0.113.1", port=443)    # (adresse de documentation : le client doit l'ignorer)
        self.ok(mid, enc_obj(obj("config", date=now, expires=now + 3600, test_mode=True, this_dc=HOME_DC, dc_options=[dc],
                                 dc_txt_domain_name="", chat_size_max=200, megagroup_size_max=200000, forwarded_count_max=100,
                                 online_update_period_ms=120000, offline_blur_timeout_ms=5000, offline_idle_timeout_ms=30000,
                                 online_cloud_timeout_ms=300000, notify_cloud_delay_ms=30000, notify_default_delay_ms=1500,
                                 push_chat_period_ms=60000, push_chat_limit=2, edit_time_limit=172800, revoke_time_limit=172800,
                                 revoke_pm_time_limit=172800, rating_e_decay=2419200, stickers_recent_limit=200,
                                 channels_read_media_period=604800, call_receive_timeout_ms=20000, call_ring_timeout_ms=90000,
                                 call_connect_timeout_ms=30000, call_packet_timeout_ms=10000, me_url_prefix="", caption_length_max=1024,
                                 message_length_max=4096, webfile_dc_id=HOME_DC)))

    def f_auth_sendCode(self, mid, q):
        self.phone = q["phone_number"].decode()
        note("  numero fictif %s" % self.phone)
        if not self.phone.startswith("99966"):
            self.error(mid, 400, "PHONE_NUMBER_INVALID")
            return
        self.ok(mid, enc_obj(obj("auth.sentCode", type=obj("auth.sentCodeTypeApp", length=5), phone_code_hash="fictif", timeout=120)))

    def authorization(self, mid):
        self.signed = True
        self.ok(mid, enc_obj(obj("auth.authorization", user=tl_user(SELF))))
        note("  -> auth.authorization : Sam Tester")

    def f_auth_signIn(self, mid, q):
        if q.get("phone_code", b"") != b"12345":
            self.error(mid, 400, "PHONE_CODE_INVALID")
            return
        if self.phone == "9996620000":
            self.error(mid, 401, "SESSION_PASSWORD_NEEDED")
            return
        if self.phone == "9996630000":
            self.ok(mid, enc_obj(obj("auth.authorizationSignUpRequired")))
            return
        self.authorization(mid)

    def f_auth_signUp(self, mid, q):
        note("  nouveau compte : %s" % q["first_name"].decode())
        self.authorization(mid)

    def f_account_getPassword(self, mid, q):
        self.srp = srp_setup()
        algo = obj("passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow", salt1=self.srp["salt1"], salt2=self.srp["salt2"],
                   g=DH_G, p=DH_PRIME.to_bytes(256, "big"))
        self.ok(mid, enc_obj(obj("account.password", has_password=True, current_algo=algo, srp_B=self.srp["B"].to_bytes(256, "big"), srp_id=self.srp["id"],
                                 hint="le mot de passe des films", new_algo=obj("passwordKdfAlgoUnknown"), new_secure_algo=obj("securePasswordKdfAlgoUnknown"),
                                 secure_random=secrets.token_bytes(32))))

    def f_auth_checkPassword(self, mid, q):
        p = q["password"]
        if not self.srp or p["srp_id"] != self.srp["id"] or not srp_check(self.srp, p["A"], p["M1"]):
            note("  M1 SRP faux")
            self.error(mid, 400, "PASSWORD_HASH_INVALID")
            return
        note("  M1 SRP verifie")
        self.authorization(mid)

    def f_auth_logOut(self, mid, q):
        self.signed = False
        self.ok(mid, enc_obj(obj("auth.loggedOut")))

    def f_users_getUsers(self, mid, q):
        self.ok(mid, vector([tl_user(SELF)]))

    def f_users_getFullUser(self, mid, q):
        uid = q["id"].get("user_id", SELF)
        st = obj("peerSettings", add_contact=True, block_contact=True) if uid == IRIS else obj("peerSettings")
        full = obj("userFull", id=uid, settings=st, notify_settings=obj("peerNotifySettings"), common_chats_count=0)
        self.ok(mid, enc_obj(obj("users.userFull", full_user=full, chats=[], users=[tl_user(uid), tl_user(SELF)])))

    def f_updates_getState(self, mid, q):
        self.ok(mid, enc_obj(obj("updates.state", pts=world.pts, qts=0, date=int(time.time()), seq=1, unread_count=0)))

    def f_updates_getDifference(self, mid, q):
        self.ok(mid, enc_obj(obj("updates.differenceEmpty", date=int(time.time()), seq=1)))

    def f_messages_getDialogs(self, mid, q):
        with lock:
            self.ok(mid, enc_obj(world.dialogs()), zipped=True)

    def f_contacts_getContacts(self, mid, q):
        ids = [ALICE, BOB]
        self.ok(mid, enc_obj(obj("contacts.contacts", contacts=[obj("contact", user_id=u, mutual=True) for u in ids], saved_count=2, users=tl_users())))

    def f_account_updateStatus(self, mid, q):
        note("  hors ligne : %s" % q["offline"])
        self.ok(mid, enc_obj(True))

    def f_messages_getHistory(self, mid, q):
        p = q["peer"]
        conv = -GROUP if p["_"] == "inputPeerChat" else p.get("user_id", SELF)
        with lock:
            ms = [world.tl_msg(m) for m in reversed(world.hist.get(conv, []))]
        self.ok(mid, enc_obj(obj("messages.messages", messages=ms, chats=[tl_chat()], users=tl_users())), zipped=True)

    def f_messages_readHistory(self, mid, q):
        p = q["peer"]
        world.unread.pop(p.get("user_id"), None)
        world.pts += 1
        self.ok(mid, enc_obj(obj("messages.affectedMessages", pts=world.pts, pts_count=1)))

    def f_messages_setTyping(self, mid, q):
        note("  le client ecrit")
        self.ok(mid, enc_obj(True))

    def f_messages_sendMessage(self, mid, q):
        p = q["peer"]
        conv = -GROUP if p["_"] == "inputPeerChat" else p.get("user_id", SELF)
        text = q["message"].decode()
        note("  message recu du client, dechiffre : %r" % text)
        with lock:
            m = world.push(conv, SELF, text)
            world.pts += 1
        self.ok(mid, enc_obj(obj("updateShortSentMessage", out=True, id=m["id"], pts=world.pts, pts_count=1, date=m["date"])))
        if conv == ALICE:
            threading.Thread(target=self.alice_answers, args=(text, m["id"]), daemon=True).start()

    def alice_answers(self, text, sent):
        time.sleep(1.5)
        self.push(obj("updateShort", update=obj("updateUserTyping", user_id=ALICE, action=obj("sendMessageTypingAction")), date=int(time.time())))
        time.sleep(2)
        self.push(obj("updateShort", update=obj("updateReadHistoryOutbox", peer=peer_user(ALICE), max_id=sent, pts=world.pts, pts_count=0), date=int(time.time())))
        deliver(ALICE, "Bien recu : " + text)

    def f_upload_getFile(self, mid, q):
        data = open(PHOTO, "rb").read()
        o, n = q["offset"], q["limit"]
        note("  fichier, a partir de %d" % o)
        self.ok(mid, enc_obj(obj("upload.file", type=obj("storage.fileJpeg"), mtime=int(time.time()), bytes=data[o:o + n])))

    def f_upload_saveFilePart(self, mid, q):
        with lock:
            up = uploads.setdefault(q["file_id"], {})
            up[q["file_part"]] = q["bytes"]
        note("  partie %d, %d octets" % (q["file_part"], len(q["bytes"])))
        self.ok(mid, enc_obj(True))

    def f_messages_sendMedia(self, mid, q):
        f = q["media"]["file"]
        with lock:
            up = uploads.pop(f["id"], {})
        blob = b"".join(up[i] for i in sorted(up))
        path = os.path.join(TRY, "tgsrv_recu.jpg")
        with open(path, "wb") as o:
            o.write(blob)
        note("  photo recue : %d octets, %d parties, commence par %s" % (len(blob), f["parts"], blob[:3].hex()))
        p = q["peer"]
        conv = p.get("user_id", SELF)
        with lock:
            ph = obj("photo", id=7200 + world.next_id, access_hash=11, file_reference=b"ref", date=int(time.time()),
                     sizes=[obj("photoSize", type="x", w=640, h=400, size=len(blob))], dc_id=HOME_DC)
            m = world.push(conv, SELF, q["message"].decode(), None, obj("messageMediaPhoto", photo=ph))
            world.pts += 1
            up_m = obj("updateNewMessage", message=world.tl_msg(m), pts=world.pts, pts_count=1)
        ids = obj("updateMessageID", id=m["id"], random_id=q["random_id"])
        self.ok(mid, enc_obj(obj("updates", updates=[ids, up_m], users=tl_users(), chats=[], date=int(time.time()), seq=0)))

    def f_contacts_importContacts(self, mid, q):
        c = q["contacts"][0]
        note("  contact importe : %s" % c["phone"].decode())
        if c["phone"].startswith(b"99966"):
            self.ok(mid, enc_obj(obj("contacts.importedContacts", imported=[obj("importedContact", user_id=BOB, client_id=c["client_id"])],
                                     popular_invites=[], retry_contacts=[], users=tl_users())))
        else:
            self.ok(mid, enc_obj(obj("contacts.importedContacts", imported=[], popular_invites=[], retry_contacts=[], users=[])))

    def f_contacts_addContact(self, mid, q):
        people_extra.add(q["id"].get("user_id"))
        self.ok(mid, enc_obj(obj("updates", updates=[], users=tl_users(), chats=[], date=int(time.time()), seq=0)))

    def f_contacts_block(self, mid, q):
        self.ok(mid, enc_obj(True))

    def f_contacts_unblock(self, mid, q):
        self.ok(mid, enc_obj(True))

    def f_messages_hidePeerSettingsBar(self, mid, q):
        self.ok(mid, enc_obj(True))


uploads = {}
people_extra = set()


def deliver(uid, text):
    with lock:
        m = world.push(uid, uid, text)
        world.pts += 1
        world.unread[uid] = world.unread.get(uid, 0) + 1
        for c in conns:
            if c.key:
                try:
                    c.send_enc(enc_obj(obj("updateShortMessage", id=m["id"], user_id=uid, message=text, pts=world.pts, pts_count=1, date=m["date"])))
                except OSError:
                    pass
    note("-> message entrant de %d : %r" % (uid, text))


def control(port):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(5)
    while True:
        c, _ = s.accept()
        line = c.makefile("r", encoding="utf-8").readline().strip()
        c.close()
        a = line.split(" ", 2)
        note("controle : %s" % line)
        if a[0] == "msg":
            deliver(int(a[1]), a[2])
        elif a[0] == "typing":
            for k in list(conns):
                k.push(obj("updateShort", update=obj("updateUserTyping", user_id=int(a[1]), action=obj("sendMessageTypingAction")), date=int(time.time())))
        elif a[0] == "status":
            st = obj("userStatusOnline", expires=int(time.time()) + 300) if a[2] == "on" else obj("userStatusOffline", was_online=int(time.time()))
            for k in list(conns):
                k.push(obj("updateShort", update=obj("updateUserStatus", user_id=int(a[1]), status=st), date=int(time.time())))
        elif a[0] == "read":
            for k in list(conns):
                k.push(obj("updateShort", update=obj("updateReadHistoryOutbox", peer=peer_user(int(a[1])), max_id=int(a[2]), pts=world.pts, pts_count=0), date=int(time.time())))
        elif a[0] == "drop":
            for k in list(conns):
                k.s.close()
        elif a[0] == "rekey":
            with lock:
                keys.clear()


def ntp():
    """Le SNTP de l'invite (options.txt : ntp=10.0.2.2) : l'heure du PC, pour ne pas demander a pool.ntp.org."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 123))
    note("NTP local sur 127.0.0.1:123")
    while True:
        d, a = s.recvfrom(128)
        t = int(time.time()) + 2208988800
        r = bytes([0x24, 2, 6, 0xEC]) + b"\0" * 8 + b"LOCL" + struct.pack(">II", t, 0) + d[40:48] + struct.pack(">II", t, 0) + struct.pack(">II", t, 0)
        s.sendto(r, a)


def seed(path, port):
    n, _ = rsa_key()
    with open(path, "w") as f:
        f.write("[telegram]\ntest=0\npictures=1\nwindows=1\ntest_host=10.0.2.2\ntest_port=%d\ntest_rsa_n=%x\n" % (port, n))
    note("config.ini ecrit : %s (cle RSA de test %d bits)" % (path, n.bit_length()))


def main():
    global log_f, world
    a = sys.argv[1:]
    port = 14430
    if "--port" in a:
        port = int(a[a.index("--port") + 1])
    if a and a[0] == "seed":
        seed(a[1], port)
        return
    if a and a[0] == "ctl":
        s = socket.create_connection(("127.0.0.1", port + 1))
        s.sendall((" ".join(a[1:]) + "\n").encode("utf-8"))
        s.close()
        return
    if "--log" in a:
        log_f = open(a[a.index("--log") + 1], "w", encoding="utf-8")
    if "--ntp" in a:
        threading.Thread(target=ntp, daemon=True).start()
    if "--ntp-only" in a:
        ntp()
        return
    load_schema()
    note("schema : %d constructeurs" % len(BY_ID))
    rsa = rsa_key()
    if os.path.exists(KEYS_FILE):
        for l in open(KEYS_FILE):
            k = base64.b64decode(l.strip())
            keys[sha1(k)[12:20]] = k
    world = World()
    threading.Thread(target=control, args=(port + 1,), daemon=True).start()
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(8)
    note("tgsrv sur 127.0.0.1:%d" % port)
    bad = "--bad-salt" in a
    while True:
        c, addr = s.accept()
        c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        k = Conn(c, "%s:%d" % addr, rsa, bad)
        with lock:
            conns.append(k)
        threading.Thread(target=k.run, daemon=True).start()


if __name__ == "__main__":
    main()
