"""Essais de l'etape 3 de Python sous Korin (docs/PYTHON.md §9, docs/PYTHON-PORT.md « Étape 3 ») :
modules C en .so (tree-sitter, cffi), pip.

    python3 /home/pytest/step3.py [--net] [--gcc]

Chaque essai affiche OK, ECHEC ou SAUTE avec la raison ; le code de sortie est le nombre d'echecs.
--net : pip install --only-binary=:all: d'un paquet pur depuis PyPI (six), puis le retire.
--gcc : pip install d'un paquet en C (mmh3) depuis ses sources, compile sur la carte par /gcc.
"""
import os
import subprocess
import sys
import time
import traceback

results = []


class Skip(Exception):
    pass


def check(name):
    def deco(fn):
        t = time.monotonic()
        try:
            r = fn()
            results.append(("OK", name, ("" if r is None else str(r)) + " [%.1f s]" % (time.monotonic() - t)))
        except Skip as e:
            results.append(("SAUTE", name, str(e)))
        except Exception as e:
            results.append(("ECHEC", name, "%s: %s" % (type(e).__name__, e)))
            traceback.print_exc()
        sys.stdout.flush()
        return fn
    return deco


def pip(*args, timeout=3600):
    r = subprocess.run([sys.executable, "-m", "pip", "--disable-pip-version-check", *args],
                       capture_output=True, text=True, timeout=timeout)
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-3000:], sep="\n")
        raise AssertionError("pip %s : code %d" % (" ".join(args), r.returncode))
    return r.stdout


@check("sysconfig : plateforme des roues")
def _():
    import sysconfig
    p = sysconfig.get_platform()
    assert p == "linux-aarch64", p
    return "%s, EXT_SUFFIX %s" % (p, sysconfig.get_config_var("EXT_SUFFIX"))


@check("import tree_sitter (.so)")
def _():
    import tree_sitter
    from tree_sitter import _binding
    assert _binding.__file__.endswith(".so"), _binding.__file__
    return _binding.__file__


@check("tree-sitter analyse « ls -la | grep x » (grammaire bash)")
def _():
    import tree_sitter_bash
    from tree_sitter import Language, Parser
    lang = Language(tree_sitter_bash.language())
    parser = Parser(lang)
    tree = parser.parse(b"ls -la | grep x")
    root = tree.root_node
    assert not root.has_error, str(root)
    pipe = root.children[0]
    assert pipe.type == "pipeline", pipe.type
    cmds = [c for c in pipe.children if c.type == "command"]
    names = [c.child_by_field_name("name").text.decode() for c in cmds]
    assert names == ["ls", "grep"], names
    args = [a.text.decode() for c in cmds for a in c.children_by_field_name("argument")]
    assert args == ["-la", "x"], args
    return str(root)


@check("tree-sitter : une requete sur l'arbre")
def _():
    import tree_sitter_bash
    from tree_sitter import Language, Parser, Query, QueryCursor
    lang = Language(tree_sitter_bash.language())
    tree = Parser(lang).parse(b"cd /tmp && rm -rf x; echo $(whoami)")
    q = Query(lang, "(command name: (command_name) @n)")
    caps = QueryCursor(q).captures(tree.root_node)
    names = sorted(n.text.decode() for n in caps["n"])
    assert names == ["cd", "echo", "rm", "whoami"], names
    return names


@check("import _cffi_backend (.so)")
def _():
    import _cffi_backend
    assert _cffi_backend.__file__.endswith(".so"), _cffi_backend.__file__
    return "%s %s" % (_cffi_backend.__version__, _cffi_backend.__file__)


@check("cffi en mode ABI : fonctions de la libc de python3")
def _():
    from cffi import FFI
    ffi = FFI()
    ffi.cdef("""
        int abs(int);
        size_t strlen(const char *);
        int snprintf(char *, size_t, const char *, ...);
        double strtod(const char *, char **);
        typedef int (*cmp_t)(const void *, const void *);
        void qsort(void *, size_t, size_t, cmp_t);
        struct pt { int x; double y; };
    """)
    lib = ffi.dlopen(None)          # l'hote : python3 exporte toute la libc
    assert lib.abs(-5) == 5
    assert lib.strlen(b"korin") == 5
    buf = ffi.new("char[64]")
    n = lib.snprintf(buf, 64, b"%s-%d-%.2f", ffi.new("char[]", b"x"), ffi.cast("int", 42), ffi.cast("double", 2.5))
    assert ffi.string(buf) == b"x-42-2.50", ffi.string(buf)
    assert lib.strtod(b"3.25", ffi.NULL) == 3.25

    @ffi.callback("int(const void *, const void *)")
    def cmp(a, b):                  # fermeture libffi : une page PROT_EXEC
        x, y = ffi.cast("int *", a)[0], ffi.cast("int *", b)[0]
        return (x > y) - (x < y)
    arr = ffi.new("int[]", [5, 3, 9, 1, 7])
    lib.qsort(arr, 5, ffi.sizeof("int"), cmp)
    assert list(arr) == [1, 3, 5, 7, 9], list(arr)
    p = ffi.new("struct pt *", {"x": 3, "y": 1.5})
    assert (p.x, p.y, ffi.sizeof("struct pt")) == (3, 1.5, 16)
    return "abs, strlen, snprintf (variadique), strtod, qsort avec rappel, struct"


@check("pip --version")
def _():
    out = pip("--version").strip()
    assert out.startswith("pip "), out
    return out


@check("pip list")
def _():
    out = pip("list")
    for n in ("cffi", "tree-sitter", "tree-sitter-bash", "pycparser", "setuptools"):
        assert n in out, (n, out)
    return "%d paquets" % (len(out.splitlines()) - 2)


@check("pip install --only-binary=:all: d'un paquet pur (six)")
def _():
    if "--net" not in sys.argv:
        raise Skip("sans --net")
    pip("install", "--only-binary=:all:", "six==1.17.0")
    r = subprocess.run([sys.executable, "-c", "import six; print(six.__version__, six.__file__)"],
                       capture_output=True, text=True)
    assert r.returncode == 0 and r.stdout.startswith("1.17.0"), (r.stdout, r.stderr)
    pip("uninstall", "-y", "six")
    return r.stdout.strip()


@check("pip install d'un paquet en C compile sur la carte (mmh3, /gcc)")
def _():
    if "--gcc" not in sys.argv:
        raise Skip("sans --gcc")
    if not os.path.exists("/gcc/bin/gcc"):
        raise Skip("pas de /gcc")
    # (--no-build-isolation : le setuptools de site-packages, pas un second telecharge dans /tmp)
    out = pip("install", "-v", "--no-binary=:all:", "--no-build-isolation", "mmh3==5.2.0")
    for line in out.splitlines():
        if "gcc" in line:
            print(line[:300])
    r = subprocess.run([sys.executable, "-c", "import mmh3; print(mmh3.hash('korin'), mmh3.__file__)"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert r.stdout.split()[1].endswith(".so"), r.stdout
    # la valeur de reference : mmh3.hash("korin") sur le PC (MurmurHash3 x86 32 bits, graine 0)
    want = os.environ.get("MMH3_KORIN")
    if want:
        assert r.stdout.split()[0] == want, (r.stdout, want)
    return r.stdout.strip()


for st, name, info in results:
    print("%-6s %s%s" % (st, name, (" : " + info) if info else ""))
fails = sum(1 for r in results if r[0] == "ECHEC")
print("%d OK, %d ECHEC, %d SAUTE" % (sum(1 for r in results if r[0] == "OK"), fails,
                                     sum(1 for r in results if r[0] == "SAUTE")))
sys.exit(fails)
