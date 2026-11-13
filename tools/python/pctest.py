"""pctest : les essais de Python et de Vibe sur Korin PC x86-64 sous QEMU q35 (docs/PYTHON-PORT.md,
« Korin PC »), l'équivalent de testimage.py + qemu_drive.py du Pi.

  python -I tools/python/pctest.py prep [--mb 1600] [--no-tests]
      l'arbre d'essai build/pc/pytest/root : une COPIE de build/pc/sysroot (jamais modifié), puis
      build/pc/python/sdroot (python3, /python, Vibe), Lib/test et les modules d'essai (build/pc/python/tests),
      les scripts de tools/python/tests, tools/vibe (acp_client.py, mcp_test_server.py, fakellm.py) et
      tools/agent (ptytest.py, skilltest.py) dans SD0:/home/pytest ; telnetd lancé par services.txt, mot de
      passe d'essai « essai » (etc/remote.txt) ; puis le disque build/pc/pytest/disk.img
      (tools/pc/mkkorindisk.py --layout zbook --sysroot)
  python -I tools/python/pctest.py run [--port 2391] [--smp 4] [--mem 4G] [--boot 60] "commande"...
      démarre QEMU (NVMe, l'image en snapshot : jamais écrite ; usb-net, réseau user de QEMU, telnet
      du PC sur 127.0.0.1:PORT), attend telnetd, lance chaque commande (bash) et l'affiche avec son code ;
      « @600 commande » : jusqu'à 600 s (60 sinon) ; puis arrête QEMU. --hostfwd tcp::P-:Q en plus
      (plusieurs permis). Le journal du noyau : build/pc/pytest/debug.log.
Les ports par défaut évitent ceux des autres essais (4461, 4471) ; QMP sur PORT + 1000.
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = os.path.join(ROOT, "build", "pc")
W = os.path.join(PC, "pytest")
TROOT = os.path.join(W, "root")
IMG = os.path.join(W, "disk.img")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def prep(a):
    py = os.path.join(PC, "python")
    if not os.path.isdir(os.path.join(PC, "sysroot")):
        raise SystemExit("absent : build/pc/sysroot (wsl sh tools/pc/sysroot.sh)")
    shutil.rmtree(TROOT, ignore_errors=True)
    os.makedirs(W, exist_ok=True)
    shutil.copytree(os.path.join(PC, "sysroot"), TROOT)
    # ce que construisent les scripts de Python et de Vibe (ce qui manque est sauté) ; les essais de libposix
    for d in (os.path.join(py, "sdroot"), os.path.join(PC, "agent", "sdroot"), os.path.join(PC, "vibe", "sdroot")):
        if os.path.isdir(d):
            shutil.copytree(d, TROOT, dirs_exist_ok=True)
    va = os.path.join(PC, "apps", "sdroot", "apps", "vibe.app")
    if os.path.isdir(va):
        shutil.copytree(va, os.path.join(TROOT, "apps", "vibe.app"), dirs_exist_ok=True)
    px = os.path.join(PC, "posixtest")
    if os.path.isdir(px):
        for f in os.listdir(px):
            if os.path.isfile(os.path.join(px, f)):
                shutil.copy(os.path.join(px, f), os.path.join(TROOT, "bin", f))
    etc = os.path.join(TROOT, "etc")
    # la table des chemins du dépôt (l'arbre POSIX : /sd0, /sd1, /ram0), si l'arbre est plus ancien
    shutil.copy(os.path.join(ROOT, "skel", "sd0", "etc", "paths.txt"), os.path.join(etc, "paths.txt"))
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "SD0:/sys/svc/telnetd" not in s:
        s += "# essais de tools/python/pctest.py : telnet\nSD0:/sys/svc/telnetd\n"
        open(sv, "w", encoding="utf-8", newline="\n").write(s)
    bt = os.path.join(py, "tests")
    lib = os.path.join(TROOT, "python", "lib", "python3.14")
    if os.path.isdir(bt) and not a.no_tests:
        shutil.copytree(os.path.join(bt, "test"), os.path.join(lib, "test"), dirs_exist_ok=True)
        shutil.copytree(os.path.join(bt, "lib-dynload"), os.path.join(lib, "lib-dynload"), dirs_exist_ok=True)
        if os.path.isdir(os.path.join(bt, "site-packages")):
            shutil.copytree(os.path.join(bt, "site-packages"), os.path.join(lib, "site-packages"), dirs_exist_ok=True)
        if os.path.isdir(os.path.join(bt, "rust-ext")):
            shutil.copytree(os.path.join(bt, "rust-ext"), os.path.join(TROOT, "home", "pytest", "rust-ext"))
    home = os.path.join(TROOT, "home", "pytest")
    shutil.copytree(os.path.join(ROOT, "tools", "python", "tests"), home, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("panic"))
    for d, fs in (("vibe", ("acp_client.py", "mcp_test_server.py", "fakellm.py")),
                  ("agent", ("ptytest.py", "skilltest.py"))):
        for f in fs:
            p = os.path.join(ROOT, "tools", d, f)
            if os.path.isfile(p):
                shutil.copy(p, os.path.join(home, f))
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "mkkorindisk.py"), IMG,
                           "--layout", "zbook", "--korin-mb", str(a.mb), "--sysroot", TROOT,
                           "--cfg", "default = korin\ntimeout = 0"])
    print(IMG, os.path.getsize(IMG))


def run(a):
    qp = load("qemu_pc", os.path.join(ROOT, "tools", "pc", "qemu_pc.py"))
    rem = load("armpi_remote", os.path.join(ROOT, "tools", "armpi_remote.py"))
    if not os.path.isfile(IMG):
        raise SystemExit("faire d'abord : python -I tools/python/pctest.py prep")
    log = os.path.join(W, "debug.log")
    os.environ["KORIN_PC_DEBUGLOG"] = log
    qp.DEBUGLOG = log
    vars_fd = os.path.join(W, "ovmf_vars.fd")
    qmp = a.port + 1000
    fwd = ",".join(["hostfwd=tcp:127.0.0.1:%d-:23" % a.port] + ["hostfwd=" + h for h in a.hostfwd])
    args = qp.qemu_args(True, qmp, a.smp, vars_fd=vars_fd, boot_drive=False)
    args = [x for x in args if x != "2G"]
    i = args.index("-m")
    args[i:i + 1] = ["-m", a.mem]
    args += ["-drive", "if=none,id=nv,format=raw,snapshot=on,file=" + IMG.replace("\\", "/"),
             "-device", "nvme,serial=KORINTEST,drive=nv",
             "-device", "qemu-xhci,id=xhci", "-netdev", "user,id=un0," + fwd,
             "-device", "usb-net,bus=xhci.0,netdev=un0"]
    p = subprocess.Popen(args)
    t0 = time.time()
    rc = 0
    try:
        q = qp.Qmp(qmp)
        t = None
        while time.time() - t0 < a.boot:
            try:
                t = rem.Telnet("127.0.0.1", a.port, timeout=10)
                if b"Mot de passe" in t.read_until(b" : ", 15):
                    t.s.sendall(b"essai\r\n")
                t.s.sendall(b'echo @PR""ET\r\n')
                t.read_until(b"@PRET", 20)
                t.read_until(b"\n", 20)
                break
            except (OSError, EOFError, TimeoutError):
                t = None
                time.sleep(3)
        if t is None:
            raise SystemExit("telnet : pas de réponse après %d s (journal : %s)" % (a.boot, log))
        print("telnet prêt après %.0f s" % (time.time() - t0))
        for c in a.cmd:
            limit = 60
            if c.startswith("@") and " " in c:
                limit, c = int(c[1:c.index(" ")]), c[c.index(" ") + 1:]
            t1 = time.time()
            out, code = t.run(c, limit)
            print("$ %s\n%s[code %d, %.1f s]" % (c, out, code, time.time() - t1), flush=True)
            rc = rc or code
        t.close()
        if a.shot:
            q.cmd("screendump", filename=os.path.abspath(a.shot).replace("\\", "/"), format="png")
        q.cmd("quit")
    finally:
        try:
            p.wait(15)
        except subprocess.TimeoutExpired:
            p.kill()
    return rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["prep", "run"])
    ap.add_argument("cmd", nargs="*")
    ap.add_argument("--mb", type=int, default=1600)
    ap.add_argument("--no-tests", action="store_true")
    ap.add_argument("--port", type=int, default=2391)
    ap.add_argument("--hostfwd", action="append", default=[])
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot", type=float, default=90)
    ap.add_argument("--shot", default="")
    a = ap.parse_args()
    if a.mode == "prep":
        prep(a)
    else:
        sys.exit(run(a))


if __name__ == "__main__":
    main()
