"""Essais de l'etape 2 de Python sous Korin (docs/PYTHON.md §9, docs/PYTHON-PORT.md), hors de regrtest.

    python3 /home/pytest/step2.py [--net]

Chaque essai affiche OK, ECHEC ou SAUTE avec la raison ; le code de sortie est le nombre d'echecs.
--net : aussi urllib.request.urlopen("https://api.mistral.ai") (une requete, rien d'envoye).
"""
import os
import sys
import tempfile
import traceback

results = []


def check(name):
    def deco(fn):
        try:
            r = fn()
            results.append(("OK", name, "" if r is None else str(r)))
        except Skip as e:
            results.append(("SAUTE", name, str(e)))
        except Exception as e:
            results.append(("ECHEC", name, "%s: %s" % (type(e).__name__, e)))
            traceback.print_exc()
        return fn
    return deco


class Skip(Exception):
    pass


@check("plateforme")
def _():
    import platform
    return "%s %s %s, %s" % (sys.version.split()[0], sys.platform, platform.machine(), platform.system())


@check("modules natifs dans l'interpreteur")
def _():
    import _ssl, _hashlib, _sqlite3, zlib, _ctypes, _decimal, pyexpat, _elementtree, _json, _asyncio, select
    absent = [m for m in ("_ssl", "zlib", "_ctypes", "_sqlite3") if m not in sys.builtin_module_names]
    assert not absent, absent
    return "%d modules integres" % len(sys.builtin_module_names)


@check("_testcapi en .so (dlopen)")
def _():
    import _testcapi
    f = _testcapi.__file__
    assert f.endswith(".so"), f
    import _testinternalcapi, _testmultiphase, _testsinglephase, _ctypes_test
    return f


@check("sysconfig : modules en -fPIC -shared")
def _():
    import sysconfig
    v = sysconfig.get_config_vars()
    assert v["CCSHARED"] == "-fPIC", v["CCSHARED"]
    assert v["LDSHARED"].split()[:2] == ["gcc", "-shared"], v["LDSHARED"]
    assert v["EXT_SUFFIX"].endswith(".so"), v["EXT_SUFFIX"]
    return "CC=%s LDSHARED=%s EXT_SUFFIX=%s" % (v["CC"], v["LDSHARED"], v["EXT_SUFFIX"])


@check("hashlib (OpenSSL)")
def _():
    import hashlib
    assert hashlib.sha256(b"abc").hexdigest() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    assert hashlib.new("sha3_256", b"abc").hexdigest().startswith("3a985da7")
    h = hashlib.sha256(b"abc")
    return "sha256 et sha3_256 justes, fournis par %s" % type(h).__module__


@check("ssl : contexte par defaut et magasin de certificats")
def _():
    import ssl
    c = ssl.create_default_context()
    n = c.cert_store_stats()
    return "%s, %s" % (ssl.OPENSSL_VERSION, n)


@check("zlib, zipfile, gzip")
def _():
    import zlib, gzip, io
    d = b"korin " * 1000
    assert zlib.decompress(zlib.compress(d)) == d
    assert gzip.decompress(gzip.compress(d)) == d
    return "zlib %s" % zlib.ZLIB_VERSION


@check("sqlite3")
def _():
    import sqlite3
    p = os.path.join(tempfile.gettempdir(), "pytest-korin.db")
    if os.path.exists(p):
        os.remove(p)
    c = sqlite3.connect(p)
    c.execute("create table t (a integer, b text)")
    c.executemany("insert into t values (?, ?)", [(i, str(i)) for i in range(100)])
    c.commit()
    s = c.execute("select sum(a), count(*) from t").fetchone()
    c.close()
    c = sqlite3.connect(p)
    assert c.execute("select count(*) from t").fetchone()[0] == 100
    c.close()
    os.remove(p)
    return "SQLite %s, %s" % (sqlite3.sqlite_version, s)


@check("ctypes : CDLL(None) voit la libc de l'hote")
def _():
    import ctypes
    libc = ctypes.CDLL(None)
    assert libc.getpid() == os.getpid()
    buf = ctypes.create_string_buffer(32)
    libc.snprintf(buf, 32, b"%d-%s", 42, b"ok")
    assert buf.value == b"42-ok", buf.value
    # une fermeture (libffi : page PROT_EXEC)
    CMP = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int))
    arr = (ctypes.c_int * 5)(5, 1, 4, 2, 3)
    libc.qsort(arr, 5, ctypes.sizeof(ctypes.c_int), CMP(lambda a, b: a[0] - b[0]))
    assert list(arr) == [1, 2, 3, 4, 5], list(arr)
    return "getpid, snprintf, qsort avec rappel"


@check("decimal, json, xml")
def _():
    import decimal, json, xml.etree.ElementTree as ET
    assert str(decimal.Decimal(1) / decimal.Decimal(7))[:8] == "0.142857"
    assert json.loads(json.dumps({"a": [1, 2]})) == {"a": [1, 2]}
    assert ET.fromstring("<a><b>x</b></a>").find("b").text == "x"


@check("subprocess.run(['bash', '-c', 'ls | cat'])")
def _():
    import subprocess
    if not os.path.exists("/bin/bash"):
        raise Skip("pas de /bin/bash")
    r = subprocess.run(["bash", "-c", "ls / | cat"], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r
    assert "bin" in r.stdout.split(), r.stdout
    r = subprocess.run([sys.executable, "-c", "import sys; print(sys.argv[1]); sys.exit(3)", "fils"],
                       capture_output=True, text=True, timeout=120)
    assert r.returncode == 3 and r.stdout.strip() == "fils", r
    return "bash : ls / | cat, puis un python fils : code 3"


@check("os.replace sur une cible existante")
def _():
    d = tempfile.mkdtemp()
    a, b = os.path.join(d, "a.txt"), os.path.join(d, "b.txt")
    open(a, "w").write("nouveau")
    open(b, "w").write("ancien")
    os.replace(a, b)
    assert not os.path.exists(a)
    assert open(b).read() == "nouveau"
    os.remove(b)
    os.rmdir(d)
    return d


@check("os.replace sur la carte (fatfs)")
def _():
    d = "/home/pytest/tmp-replace"
    os.makedirs(d, exist_ok=True)
    a, b = os.path.join(d, "a.txt"), os.path.join(d, "b.txt")
    open(a, "w").write("nouveau")
    open(b, "w").write("ancien")
    os.replace(a, b)
    assert not os.path.exists(a)
    assert open(b).read() == "nouveau"
    os.remove(b)
    os.rmdir(d)


@check("threads")
def _():
    import threading
    n = []
    ts = [threading.Thread(target=lambda i=i: n.append(sum(range(i * 1000)))) for i in range(8)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert len(n) == 8


@check("asyncio : sockets locaux et sous-process")
def _():
    import asyncio

    async def main():
        async def handle(r, w):
            w.write((await r.readline()).upper())
            await w.drain()
            w.close()
        try:
            srv = await asyncio.start_server(handle, "127.0.0.1", 0)
            port = srv.sockets[0].getsockname()[1]
            note.append("port 0 : %d" % port)
        except OSError as e:                 # netd : pas de port choisi pour le port 0
            note.append("port 0 refuse (%s), port 47011" % e.strerror)
            srv = await asyncio.start_server(handle, "127.0.0.1", 47011)
            port = 47011
        r, w = await asyncio.open_connection("127.0.0.1", port)
        w.write(b"korin\n")
        line = await r.readline()
        w.close()
        srv.close()
        p = await asyncio.create_subprocess_exec(sys.executable, "-c", "print(7*6)", stdout=asyncio.subprocess.PIPE)
        out, _ = await p.communicate()
        return line, out
    note = []
    line, out = asyncio.run(main())
    assert line == b"KORIN\n", line
    assert out.strip() == b"42", out
    return "selecteur %s, %s" % (__import__("selectors").DefaultSelector.__name__, note[0])


@check("pty.fork")
def _():
    import pty
    pid, fd = pty.fork()
    if pid == 0:
        os.write(1, b"dans le pty\n")
        os._exit(0)
    out = b""
    while True:
        try:
            d = os.read(fd, 1024)
        except OSError:
            break
        if not d:
            break
        out += d
    os.waitpid(pid, 0)
    assert b"dans le pty" in out, out


@check("urllib.request.urlopen('https://api.mistral.ai')")
def _():
    if "--net" not in sys.argv:
        raise Skip("sans --net")
    import urllib.request, urllib.error
    try:
        r = urllib.request.urlopen("https://api.mistral.ai", timeout=60)
        return "HTTP %d" % r.status
    except urllib.error.HTTPError as e:      # une reponse HTTP, meme 401 / 404, suffit
        return "HTTP %d (%s)" % (e.code, e.reason)


for st, name, info in results:
    print("%-6s %s%s" % (st, name, (" : " + info) if info else ""))
fails = sum(1 for r in results if r[0] == "ECHEC")
print("%d OK, %d ECHEC, %d SAUTE" % (sum(1 for r in results if r[0] == "OK"), fails,
                                     sum(1 for r in results if r[0] == "SAUTE")))
sys.exit(fails)
