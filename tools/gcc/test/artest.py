"""artest.py -- l'essai d'ArchiveKit de bout en bout, a lancer depuis la racine du depot (PC, Python, WSL) :

  python -I tools/gcc/test/artest.py build     le kit, arc, artest
  python -I tools/gcc/test/artest.py prep      archives de reference dans build/try/ar/in ; carte build/try/ar.img
  python -I tools/gcc/test/artest.py run       QEMU, artest au demarrage ; attend la fin dans le rapport
  python -I tools/gcc/test/artest.py check     relit /atest sur la carte, verifie avec zipfile, tarfile, gzip, zlib
  python -I tools/gcc/test/artest.py arc       arc en vrai sous sh, et FASMARM natif sur le kit et sur arc : memes octets

Lit aussi l'archive que tools/gcc/test/artest.c a ecrite. Port 4520, ecran VNC 70. Le kit et artest sont assembles
et compiles par la commande build (arc compris).
"""
import gzip
import hashlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tarfile
import time
import zipfile
import zlib

TAG = "ar"
W = "build/try/ar"
IN = W + "/in"
OUT = W + "/out"
DOCS = os.path.join(os.environ.get("ONYX", "build/onyx"), "sdcard/docs")
DOCX = os.path.join(os.environ.get("ONYX", "build/onyx"), "docs/assets/reference.docx")
TC = "/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-"


def wslp(p):
    p = os.path.abspath(p).replace("\\", "/")
    return "/mnt/%s%s" % (p[0].lower(), p[2:])


def text(n, seed=1):
    r = seed
    words = [b"lorem ", b"ipsum ", b"dolor ", b"sit ", b"amet ", b"<xml>", b"</xml>", b"\n", b"12345 "]
    o = io.BytesIO()
    while o.tell() < n:
        r = (r * 1664525 + 1013904223) & 0xFFFFFFFF
        o.write(words[(r >> 8) % len(words)])
    return o.getvalue()[:n]


def noise(n, seed=2):
    return hashlib.shake_128(b"%d" % seed).digest(n)


class NonSeek(io.RawIOBase):
    """un fichier sans seek : zipfile ecrit alors des descripteurs de donnees (bit 3)"""

    def __init__(self, f):
        self.f = f

    def writable(self):
        return True

    def write(self, b):
        return self.f.write(b)

    def seekable(self):
        return False


def prep():
    shutil.rmtree(W, ignore_errors=True)
    os.makedirs(IN)
    t = text(400000)
    n = noise(200000)
    D = (2026, 3, 14, 15, 9, 26)

    def zinfo(name, method):
        z = zipfile.ZipInfo(name, D)
        z.compress_type = method
        return z

    with zipfile.ZipFile(IN + "/python.zip", "w") as z:
        z.writestr(zinfo("deflate.txt", zipfile.ZIP_DEFLATED), t)
        z.writestr(zinfo("stored.bin", zipfile.ZIP_STORED), n)
        z.writestr(zinfo("rep/vide.txt", zipfile.ZIP_DEFLATED), b"")
        z.writestr(zinfo("rep/sous/", zipfile.ZIP_STORED), b"")
        z.writestr(zinfo("\u00e9t\u00e9/caf\u00e9.txt", zipfile.ZIP_DEFLATED), b"cafe")
        z.comment = b"un commentaire d'archive"
    with zipfile.ZipFile(IN + "/level1.zip", "w", compresslevel=1) as z:
        z.writestr(zinfo("t1.txt", zipfile.ZIP_DEFLATED), t)
    with zipfile.ZipFile(IN + "/level9.zip", "w", compresslevel=9) as z:
        z.writestr(zinfo("t9.txt", zipfile.ZIP_DEFLATED), t)
    with open(IN + "/stream.zip", "wb") as f:              # bit 3, tailles apres les donnees
        with zipfile.ZipFile(NonSeek(f), "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(zinfo("s.txt", zipfile.ZIP_DEFLATED), t)
    with zipfile.ZipFile(IN + "/z64.zip", "w") as z:       # extra ZIP64
        with z.open(zinfo("big.txt", zipfile.ZIP_DEFLATED), "w", force_zip64=True) as f:
            f.write(t)
        with z.open(zinfo("big2.bin", zipfile.ZIP_STORED), "w", force_zip64=True) as f:
            f.write(n)
    # ZIP64 complet : fin ZIP64 et localisateur (la fin ordinaire pleine de 0xFFFF)
    b = open(IN + "/z64.zip", "rb").read()
    e = b.rfind(b"PK\x05\x06")
    cnt, cds, cdo = struct.unpack_from("<HII", b, e + 10)[0], struct.unpack_from("<I", b, e + 12)[0], struct.unpack_from("<I", b, e + 16)[0]
    pos = e
    z64 = b"PK\x06\x06" + struct.pack("<QHHIIQQQQ", 44, 45, 45, 0, 0, cnt, cnt, cds, cdo)
    loc = b"PK\x06\x07" + struct.pack("<IQI", 0, pos, 1)
    end = b"PK\x05\x06" + struct.pack("<HHHHIIH", 0, 0, 0xFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)
    open(IN + "/z64full.zip", "wb").write(b[:e] + z64 + loc + end)
    # donnees devant l'archive (auto-extractible)
    open(IN + "/sfx.zip", "wb").write(b"#!/bin/sh\necho junk\n" * 50 + open(IN + "/python.zip", "rb").read())
    # un nom CP437 (sans le bit UTF-8) : 0x82 = e accent
    b = bytearray(open(IN + "/level1.zip", "rb").read())
    i = b.find(b"t1.txt")
    b[i] = 0x82
    i = b.rfind(b"t1.txt")
    b[i] = 0x82
    open(IN + "/cp437.zip", "wb").write(b)
    # une entree marquee chiffree
    b = bytearray(open(IN + "/level1.zip", "rb").read())
    c = b.find(b"PK\x01\x02")
    b[c + 8] |= 1
    open(IN + "/crypt.zip", "wb").write(b)
    # une methode inconnue
    b = bytearray(open(IN + "/level1.zip", "rb").read())
    c = b.find(b"PK\x01\x02")
    b[c + 10] = 12
    open(IN + "/bzip.zip", "wb").write(b)
    with zipfile.ZipFile(IN + "/empty.zip", "w"):
        pass

    def tarset(path, fmt, mode="w"):
        with tarfile.open(path, mode, format=fmt) as a:
            for nm, data in (("a.txt", b"bonjour\n"), ("rep/t.txt", t), ("x" * 90 + "/" + "y" * 60 + ".txt", b"prefixe"),
                             ("z" * 150 + ".txt", b"nom long")):
                ti = tarfile.TarInfo(nm)
                ti.size = len(data)
                ti.mtime = 1790000000
                a.addfile(ti, io.BytesIO(data))
            ti = tarfile.TarInfo("dossier")
            ti.type = tarfile.DIRTYPE
            ti.mtime = 1790000000
            a.addfile(ti)
            ti = tarfile.TarInfo("lien")
            ti.type = tarfile.SYMTYPE
            ti.linkname = "a.txt"
            a.addfile(ti)
    tarset(IN + "/gnu.tar", tarfile.GNU_FORMAT)
    tarset(IN + "/pax.tar", tarfile.PAX_FORMAT)
    tarset(IN + "/ustar.tar", tarfile.GNU_FORMAT)
    tarset(IN + "/python.tgz", tarfile.GNU_FORMAT, "w:gz")
    with gzip.GzipFile(filename=IN + "/python.gz", mode="wb", mtime=1790000000) as f:
        f.write(t)
    with gzip.GzipFile(filename="", fileobj=open(IN + "/noname.gz", "wb"), mode="wb", mtime=0) as f:
        f.write(b"sans nom")
    open(IN + "/multi.gz", "wb").write(gzip.compress(b"un ") + gzip.compress(b"deux ") + gzip.compress(b"trois"))
    # des fichiers qui ne sont pas des archives
    open(IN + "/rien.txt", "wb").write(b"pas une archive")
    open(IN + "/vide.bin", "wb").write(b"")
    for n_ in os.listdir(DOCS):
        if n_.rsplit(".", 1)[-1].lower() in ("docx", "xlsx", "pptx", "odt", "ods", "odp"):
            shutil.copy(os.path.join(DOCS, n_), IN + "/doc-" + n_.replace(" ", "_"))
    if os.path.exists(DOCX):
        shutil.copy(DOCX, IN + "/doc-reference.docx")
    subprocess.check_call([sys.executable, "-I", "tools/office/tryimg.py", "new", TAG])
    put = lambda a, b: subprocess.check_call([sys.executable, "-I", "tools/office/tryimg.py", "put", TAG, a, b])
    tree = W + "/tree"
    os.makedirs(tree + "/d1/d2")
    open(tree + "/a.txt", "wb").write(text(5000, 3))
    open(tree + "/d1/b.txt", "wb").write(text(90000, 4))
    open(tree + "/d1/d2/c.bin", "wb").write(noise(70000, 5))
    subprocess.run(["wsl", "mmd", "-i", "%s@@%d" % (wslp("build/try/%s.img" % TAG), offset()), "::/atest"], stdin=subprocess.DEVNULL)
    put(IN, "/atest/in")
    put(tree, "/atest/tree")
    put("build/try/ar-archivekit.so", "/lib/archivekit.so")
    put("build/try/ar-artest", "/bin/artest")
    put("build/try/ar-arc", "/bin/arc")
    open(W + "/startup.txt", "w", newline="\n").write("SD0:/bin/artest\n")
    put(W + "/startup.txt", "/etc/startup.txt")


def offset():
    with open("build/try/%s.img" % TAG, "rb") as f:
        mbr = f.read(512)
    return struct.unpack("<I", mbr[446 + 8:446 + 12])[0] * 512


def get(card, local):
    return subprocess.run([sys.executable, "-I", "tools/office/tryimg.py", "get", TAG, card, local], stdin=subprocess.DEVNULL).returncode


def check():
    os.makedirs(OUT, exist_ok=True)
    for n in ("report.txt",):
        get("/atest/" + n, W + "/" + n)
    for n in ("text.bin", "text.zlib", "text.gzip", "text.raw", "data.bin", "mem.zip", "card.zip", "card.tar", "card.tgz", "card.gz"):
        get("/atest/out/" + n, OUT + "/" + n)
    bad = 0

    def ok(c, what):
        nonlocal bad
        if not c:
            bad += 1
        print(("ok   " if c else "FAUX ") + what)

    rep = open(W + "/report.txt", encoding="utf-8", errors="replace", newline="\n").read()
    t = open(OUT + "/text.bin", "rb").read()
    d = open(OUT + "/data.bin", "rb").read()
    ok(zlib.decompress(open(OUT + "/text.zlib", "rb").read()) == t, "zlib lit ar_deflate(AR_ZLIB, 9)")
    ok(gzip.decompress(open(OUT + "/text.gzip", "rb").read()) == t, "gzip lit ar_deflate(AR_GZIP, 6)")
    ok(zlib.decompress(open(OUT + "/text.raw", "rb").read(), -15) == t, "zlib lit ar_deflate(AR_RAW, 1)")
    with zipfile.ZipFile(OUT + "/mem.zip") as z:
        ok(z.testzip() is None, "zipfile : mem.zip testzip")
        ok(z.namelist()[0] == "mimetype" and z.getinfo("mimetype").compress_type == 0, "mem.zip : mimetype stocke, en premier")
        ok(z.read("[Content_Types].xml") == b"<Types/>", "mem.zip : [Content_Types].xml")
        ok("caf\u00e9/\u00e9t\u00e9.txt" in z.namelist(), "mem.zip : nom UTF-8")
        ok(z.getinfo("dossier/").is_dir(), "mem.zip : dossier")
    b = open(OUT + "/mem.zip", "rb").read()
    ok(b[30:38] == b"mimetype" and b[26:30] == b"\x08\0\0\0", "mem.zip : mimetype sans extra, a l'octet 30")
    with zipfile.ZipFile(OUT + "/card.zip") as z:
        ok(z.testzip() is None, "zipfile : card.zip testzip")
        ok(z.read("sub/data.bin") == d, "card.zip : donnees")
        ok(z.read("hello.txt") == b"hello\n", "card.zip : hello")
        print("     ", len(z.namelist()), "entrees")
        ok(z.read("arbre/d1/b.txt") == open(W + "/tree/d1/b.txt", "rb").read(), "card.zip : arbre")
        ok(z.read("arbre/d1/d2/c.bin") == open(W + "/tree/d1/d2/c.bin", "rb").read(), "card.zip : arbre, hasard")
        ok(z.getinfo("hello.txt").date_time[0] == 2026, "card.zip : date %r" % (z.getinfo("hello.txt").date_time,))
    for n, mode in (("card.tar", "r:"), ("card.tgz", "r:gz")):
        with tarfile.open(OUT + "/" + n, mode) as a:
            names = a.getnames()
            ok(a.extractfile("sub/data.bin").read() == d, n + " : donnees")
            ok(a.extractfile("hello.txt").read() == b"hello\n", n + " : hello")
            ok(a.extractfile("n" * 150 + "/" + "m" * 80 + ".txt").read() == b"prefixe", n + " : prefixe ustar")
            ok(a.extractfile("q" * 200 + ".txt").read() == b"long", n + " : nom long GNU")
            ok(a.getmember("vide").isdir() and a.getmember("sub/data.bin").mtime == 1790000000, n + " : dossier, date")
            ok(a.extractfile("arbre/d1/d2/c.bin").read() == open(W + "/tree/d1/d2/c.bin", "rb").read(), n + " : arbre")
    ok(gzip.open(OUT + "/card.gz").read() == open(W + "/tree/a.txt", "rb").read(), "gzip lit card.gz")
    # l'autre sens : le rapport contre zipfile et tarfile
    lines = rep.split("\n")
    ent = {}
    arcs = {}
    mem = {}
    for ln in lines:
        p = ln.split(" ")
        if ln.startswith("A "):
            arcs[p[1]] = p[2:]
        elif ln.startswith("M "):
            mem[p[1]] = p[2:]
        elif ln.startswith("E "):
            # le nom peut contenir tout, mais pas de blanc dans ces essais
            ent.setdefault(p[1], {})[p[2]] = p[3:]
    for n in sorted(os.listdir(IN)):
        path = IN + "/" + n
        a = arcs.get(n)
        if n in ("rien.txt", "vide.bin"):
            ok(a and a[0] == "erreur", n + " : refuse (%s)" % (a,))
            continue
        if n == "crypt.zip" or n == "bzip.zip":
            e = list(ent.get(n, {}).values())
            ok(e and e[0][0] == "erreur", n + " : " + repr(e))
            continue
        if not a or a[0] == "erreur":
            ok(False, n + " : " + repr(a))
            continue
        if n == "cp437.zip":
            ok(list(ent[n].keys()) == ["\u00e91.txt"], n + " : nom " + repr(list(ent[n].keys())))
            continue
        want = {}
        if tarfile.is_tarfile(path):
            with tarfile.open(path) as t_:
                for m in t_.getmembers():
                    if m.isfile():
                        want[m.name] = t_.extractfile(m).read()
                    elif m.isdir():
                        want[m.name] = None
        elif zipfile.is_zipfile(path) and n != "sfx.zip" or n == "sfx.zip":
            with zipfile.ZipFile(path) as z:
                for i in z.infolist():
                    want[i.filename.rstrip("/")] = None if i.is_dir() else z.read(i)
        else:
            want[None] = gzip.open(path).read()
        got = ent.get(n, {})
        if None in want:
            good = len(got) == 1 and int(list(got.values())[0][0]) == len(want[None]) and \
                int(list(got.values())[0][1], 16) == zlib.crc32(want[None])
            ok(good, n + " : gz %s %r" % (list(got.keys()), list(got.values())))
        else:
            good = set(got.keys()) == set(want.keys())
            for k, v in want.items():
                if not good:
                    break
                g = got[k]
                if g[0] == "erreur":
                    good = False
                elif v is None:
                    good = g[3] == "1"
                else:
                    good = int(g[0]) == len(v) and int(g[1], 16) == zlib.crc32(v)
            ok(good, n + " : %d entrees %s" % (len(want), "" if good else repr(sorted(set(got) ^ set(want)))[:300]))
        m = mem.get(n)
        ok(m and int(m[1]) == 0 and int(m[0]) == len(got), n + " : par la memoire %r" % (m,))
    fin = [ln for ln in lines if ln.startswith("artest : termine") or ln.startswith("FAIL")]
    print("\n".join(fin[:40]))
    ok(any("0 echecs" in x for x in fin), "artest au bout, sans echec")
    print("--- faux :", bad)
    return bad


PHASE2 = """cd SD0:/atest
O=SD0:/atest/arc.out
A=SD0:/bin/arc
echo debut > $O
$A f >> $O 2>&1
SD0:/bin/mkdir -p SD0:/atest/o2
$A c SD0:/atest/o2/tree.zip -9 SD0:/atest/tree >> $O 2>&1
$A c SD0:/atest/o2/tree.tar SD0:/atest/tree >> $O 2>&1
$A c SD0:/atest/o2/tree.tar.gz -1 SD0:/atest/tree SD0:/atest/in/python.gz >> $O 2>&1
$A c SD0:/atest/o2/a.txt.gz SD0:/atest/tree/a.txt >> $O 2>&1
$A l SD0:/atest/o2/tree.zip >> $O 2>&1
$A l SD0:/atest/in/doc-cafe-2026.pptx >> $O 2>&1
$A l SD0:/atest/o2/tree.tar.gz >> $O 2>&1
$A t SD0:/atest/in/doc-reference.docx >> $O 2>&1
echo code $? >> $O
$A t SD0:/atest/in/crypt.zip >> $O 2>&1
echo code $? >> $O
$A l SD0:/atest/in/rien.txt >> $O 2>&1
echo code $? >> $O
$A x SD0:/atest/o2/tree.tar.gz SD0:/atest/o2/x >> $O 2>&1
echo code $? >> $O
SD0:/bin/ls -l SD0:/atest/o2/x/tree >> $O 2>&1
cd SD0:/src/kits/archivekit
SD0:/bin/fasmarm archivekit.asm archivekit.nat >> $O 2>&1
echo code $? >> $O
cd SD0:/src/bin/arc
SD0:/bin/fasmarm arc.asm arc.nat >> $O 2>&1
echo code $? >> $O
echo fin >> $O
"""


def phase2():
    T = [sys.executable, "-I", "tools/office/tryimg.py"]
    open(W + "/phase2.sh", "w", newline="\n").write(PHASE2)
    open(W + "/startup2.txt", "w", newline="\n").write("SD0:/bin/sh SD0:/atest/phase2.sh\n")
    open(W + "/run2.txt", "w").write("wait 200\n")
    for a, b in ((W + "/phase2.sh", "/atest/phase2.sh"), (W + "/startup2.txt", "/etc/startup.txt"),
                 ("build/try/ar-arc", "/bin/arc"), ("build/try/ar-archivekit.so", "/lib/archivekit.so"),
                 ("src/kits/archivekit", "/src/kits/archivekit"), ("src/bin/arc", "/src/bin/arc")):
        subprocess.check_call(T + ["put", TAG, a, b])
    subprocess.call([sys.executable, "-I", "tools/qemu_drive.py", W + "/run2.txt", "--sd", "build/try/ar.img", "--port", "4520", "--vnc", "70",
                     "--uart", "build/try/ar.log"])
    get("/atest/arc.out", W + "/arc.out")
    print(open(W + "/arc.out", encoding="utf-8", errors="replace").read()[-1500:])
    for c, l in (("/src/kits/archivekit/archivekit.nat", "build/try/ar-archivekit.so"), ("/src/bin/arc/arc.nat", "build/try/ar-arc")):
        get(c, W + "/nat")
        print(c, "identique" if open(W + "/nat", "rb").read() == open(l, "rb").read() else "DIFFERENT")
    for n in ("tree.zip", "tree.tar", "tree.tar.gz", "a.txt.gz"):
        get("/atest/o2/" + n, OUT + "/" + n)
    with zipfile.ZipFile(OUT + "/tree.zip") as z:
        print(z.testzip(), z.namelist())
    print(tarfile.open(OUT + "/tree.tar").getnames())
    print(tarfile.open(OUT + "/tree.tar.gz").getnames())
    print(len(gzip.open(OUT + "/a.txt.gz").read()))


def build():
    tag = TAG
    env = "WORK=$HOME/.cache/armpi-gcc/%s sh tools/gcc/kits.sh" % tag
    fas = os.path.abspath("tools/fasmarm/fasmarm.exe")
    out = os.path.abspath("build/try/ar-archivekit.so")
    subprocess.check_call([fas, "archivekit.asm", out], cwd="src/kits/archivekit")
    subprocess.check_call([fas, "arc.asm", os.path.abspath("build/try/ar-arc")], cwd="src/bin/arc")
    r = "build/gcc/sdroot/gcc"
    cmd = "%sgcc -mcpu=cortex-a72 -specs=build/office/posix.specs -O2 -g0 -I%s/include/kits tools/gcc/test/artest.c -L%s/lib -larchivekit -lm -o build/try/ar-artest && %sstrip build/try/ar-artest" % (TC, r, r, TC)
    subprocess.check_call(["wsl", "sh", "-c", cmd])


def run(minutes):
    subprocess.call(["powershell", "-NoProfile", "-Command",
                     "Get-CimInstance Win32_Process -Filter \"name='qemu-system-aarch64.exe'\" | Where-Object { $_.CommandLine -match 'build/try/ar.img' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"])
    wait = int(minutes) * 60
    open(W + "/run.txt", "w").write("wait %d\nshot %s/end.png\n" % (wait, W))
    print(subprocess.call([sys.executable, "-I", "tools/qemu_drive.py", W + "/run.txt", "--sd", "build/try/ar.img", "--port", "4520", "--vnc", "70",
                           "--uart", "build/try/ar.log"]))


if __name__ == "__main__":
    c = sys.argv[1] if len(sys.argv) > 1 else ""
    if c == "build":
        build()
    elif c == "prep":
        prep()
    elif c == "run":
        run(sys.argv[2] if len(sys.argv) > 2 else "10")
    elif c == "arc":
        phase2()
    elif c == "check":
        sys.exit(1 if check() else 0)
    else:
        sys.exit(__doc__)
