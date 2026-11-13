"""Essais de l'etape 6 de Python sous Korin (docs/PYTHON.md §9, docs/PYTHON-PORT.md « Étape 6 ») :
l'arbre POSIX des chemins vu de Python, le harness de Vibe en .so (cdylib Rust), le site-packages de
Vibe, et vibe-acp / vibe-app-server qui repondent a initialize.

    cd /home/pytest && python3 step6.py [--no-agents]

Chaque essai affiche OK ou ECHEC ; le code de sortie est le nombre d'echecs.
"""
import os
import subprocess
import sys
import time
import traceback

results = []


def check(name):
    def deco(fn):
        t = time.monotonic()
        try:
            r = fn()
            results.append(("OK", name, ("" if r is None else str(r)) + " [%.1f s]" % (time.monotonic() - t)))
        except Exception as e:
            results.append(("ECHEC", name, "%s: %s" % (type(e).__name__, e)))
            traceback.print_exc()
        sys.stdout.flush()
        return fn
    return deco


# ---- l'arbre POSIX des chemins (libposix et vfs) ----

@check("os.getcwd() -> /sd0/...")
def _():
    d = os.getcwd()
    assert d.startswith("/sd0/") or d == "/sd0", d
    return d


@check("pathlib : cwd absolu, resolve, relative_to")
def _():
    from pathlib import Path
    c = Path.cwd()
    assert c.is_absolute(), c
    r = Path("x/../step6.py").resolve()
    assert r == c / "step6.py" and r.exists(), r
    assert Path(__file__).resolve().relative_to(c) == Path("step6.py")
    return str(r)


@check("les formes d'un meme fichier : samefile, realpath, la forme de volume en entree")
def _():
    # os.path.realpath est en Python (posixpath) : il garde « /sd0/bin » tel quel (pas de liens) ;
    # c'est le realpath de libposix qui rend la forme unique (posixtest tree). Ici : une identite
    # par fichier (st_ino) quelle que soit la forme, et la forme de volume acceptee par os.*
    same = [("/sd0/etc/paths.txt", "SD0:/etc/paths.txt"), ("/etc/paths.txt", "/sd0/etc/paths.txt"),
            ("/home", "/sd0/home"), ("/bin/python3", "SD0:/bin/python3"), ("/tmp", "/ram0/tmp")]
    for a, b in same:
        assert os.path.samefile(a, b), (a, b)
    assert os.path.realpath("/sd0/x/../home") == "/sd0/home"
    assert os.path.isabs(os.path.realpath("."))
    return ", ".join("%s = %s" % ab for ab in same)


@check("« /x » ne depend plus du volume courant (chdir /tmp, puis /home)")
def _():
    old = os.getcwd()
    try:
        os.chdir("/tmp")
        assert os.getcwd() == "/tmp", os.getcwd()
        assert os.path.isdir("/home/pytest"), "/home/pytest vu depuis /tmp"
        assert os.path.samefile("/home/pytest", old)
        os.chdir("/ram0")
        assert os.getcwd() == "/ram0", os.getcwd()
        os.chdir("/home")
        assert os.getcwd() == "/sd0/home", os.getcwd()
    finally:
        os.chdir(old)
    return "ok"


@check("HOME, expanduser, sys.executable, tempfile")
def _():
    import tempfile
    h = os.path.expanduser("~")
    assert os.path.isabs(h) and os.path.isdir(h), h
    assert os.path.isabs(sys.executable), sys.executable
    t = tempfile.gettempdir()
    assert t == "/tmp", t
    return "HOME %s, sys.executable %s, tmp %s" % (h, sys.executable, t)


@check("pip : son cache est absolu, sans cache-dir dans pip.conf")
def _():
    r = subprocess.run([sys.executable, "-m", "pip", "cache", "dir"], capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, r.stderr[-400:]
    d = r.stdout.strip()
    assert d.startswith("/sd0/"), d
    return d


# ---- Vibe ----

@check("import mistralai_vibe_local_harness (_native, cdylib Rust)")
def _():
    t = time.monotonic()
    import mistralai_vibe_local_harness
    from mistralai_vibe_local_harness import _native
    assert _native.__file__.endswith(".abi3.so"), _native.__file__
    assert _native.CODE_MODE is False, "code mode"
    return "%s, import %.1f s" % (_native.__file__, time.monotonic() - t)


@check("importlib.metadata : mistral-vibe 3.1.2 (dist-info minimal)")
def _():
    import importlib.metadata
    import vibe
    v = importlib.metadata.version("mistral-vibe")
    assert v == vibe.__version__ == "3.1.2", (v, vibe.__version__)
    return v


@check("remplacants : watchfiles par sondage, uuid.uuid7, sans uuid_utils")
def _():
    import importlib.util
    import uuid
    import watchfiles
    assert watchfiles.VERSION.endswith("korin.poll"), watchfiles.VERSION
    for absent in ("uuid_utils", "setproctitle", "miniaudio", "secretstorage"):
        assert importlib.util.find_spec(absent) is None, absent
    return "%s, %s" % (watchfiles.VERSION, uuid.uuid7())


@check("import vibe.acp.entrypoint, vibe.app_server.stdio")
def _():
    t = time.monotonic()
    import vibe.acp.entrypoint  # noqa: F401
    import vibe.app_server.stdio  # noqa: F401
    return "%.1f s" % (time.monotonic() - t)


def agent(args, name):
    env = dict(os.environ, VIBE_HOME=os.environ.get("VIBE_HOME", "/sd0/home/pytest/vibe-home"))
    os.makedirs(env["VIBE_HOME"], exist_ok=True)
    t = time.monotonic()
    r = subprocess.run([sys.executable, "acp_client.py"] + args, capture_output=True, text=True,
                       timeout=900, env=env)
    out = (r.stdout + r.stderr).strip().splitlines()
    assert r.returncode == 0, "\n".join(out[-15:])
    line = [x for x in out if x.startswith("initialize")]
    return "%s [%.0f s]" % (line[-1] if line else out[-1:], time.monotonic() - t)


if "--no-agents" not in sys.argv:
    @check("vibe-acp repond a initialize")
    def _():
        return agent(["--agent", "/bin/vibe-acp", "--stop-after", "initialize"], "vibe-acp")

    @check("vibe-app-server repond a initialize")
    def _():
        return agent(["--app-server", "--agent", "/bin/vibe-app-server"], "vibe-app-server")


for st, name, info in results:
    print("%-6s %s : %s" % (st, name, info))
fails = sum(1 for r in results if r[0] == "ECHEC")
print("%d reussis, %d echecs" % (sum(1 for r in results if r[0] == "OK"), fails))
sys.exit(fails)
