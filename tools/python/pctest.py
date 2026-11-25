"""pctest : les essais de Python et de Vibe sur Korin PC x86-64 sous QEMU q35 (docs/PYTHON-PORT.md,
« Korin PC »), l'équivalent de testimage.py + qemu_drive.py du Pi.

  python -I tools/python/pctest.py prep [--mb 1600] [--no-tests]
      l'arbre d'essai build/pc/pytest/root : une COPIE de build/pc/sysroot (jamais modifié), puis
      tools/pc/sysroot_python.sh avec TESTS=1 (Python, Vibe, agent, vibe.app, Lib/test, les scripts d'essai
      dans HOME:/pytest), les essais de libposix de build/pc/posixtest ; telnetd lancé par services.txt, mot
      de passe d'essai « essai » (etc/remote.txt) ; puis le disque build/pc/pytest/disk.img
      (tools/pc/mkkorindisk.py --layout zbook --sysroot)
  python -I tools/python/pctest.py run [--port 2391] [--smp 4] [--mem 4G] [--boot 60] "commande"...
      démarre QEMU (NVMe, l'image en snapshot : jamais écrite ; usb-net, réseau user de QEMU, telnet
      du PC sur 127.0.0.1:PORT), attend telnetd, lance chaque commande (bash) et l'affiche avec son code ;
      « @600 commande » : jusqu'à 600 s (60 sinon) ; « rips:N » : N relevés du RIP de chaque processeur, avec son CR3 (profil) ; puis arrête QEMU. --hostfwd tcp::P-:Q en plus
      (plusieurs permis). « shot:NOM » : une capture de l'écran (build/pc/pytest/NOM.png) ; « sleep:S » : attendre ; « spawn:CMD » : lancer une app sans attendre sa fin (le shell reste pris) ;
      « click:X,Y » : un clic gauche (écran de 1280×800, avec --qemu "-device usb-tablet,bus=xhci.0") ;
      « dblclick:X,Y » : un double-clic ; « rclick:X,Y » : un clic droit ; « drag:X1,Y1,X2,Y2 » : glisser ;
      « wheel:X,Y,N » : N crans de molette (négatif : vers le haut) ; « key:ctrl-c tab ret » : des touches (noms de sendkey de QEMU) ;
      « type:TEXTE » : un texte tapé (sans Entrée ; le noyau décode en AZERTY : qemu_pc.azerty_keys).
      Le journal du noyau : build/pc/pytest/debug.log.
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
    if not os.path.isdir(os.path.join(PC, "sysroot")):
        raise SystemExit("absent : build/pc/sysroot (wsl sh tools/pc/sysroot.sh)")
    shutil.rmtree(TROOT, ignore_errors=True)
    os.makedirs(W, exist_ok=True)
    shutil.copytree(os.path.join(PC, "sysroot"), TROOT)
    # Python, Vibe, agent, vibe.app, .vibe et (sans --no-tests) les essais : tools/pc/sysroot_python.sh, dans WSL
    env = "" if a.no_tests else "TESTS=1 "
    subprocess.check_call(["wsl", "sh", "-c", env + "sh tools/pc/sysroot_python.sh build/pc/pytest/root"], cwd=ROOT)
    # les essais de libposix (build/pc/posixtest), plus récents que l'arbre
    px = os.path.join(PC, "posixtest")
    if os.path.isdir(px):
        for f in os.listdir(px):
            if os.path.isfile(os.path.join(px, f)):
                shutil.copy(os.path.join(px, f), os.path.join(TROOT, "bin", f))
    # les services du moment (build/svc/x86_64 : netd...), plus récents que l'arbre
    sv = os.path.join(ROOT, "build", "svc", "x86_64")
    if os.path.isdir(sv):
        for f in os.listdir(sv):
            if os.path.isfile(os.path.join(sv, f)):
                shutil.copy(os.path.join(sv, f), os.path.join(TROOT, "sys", "svc", f))
    # bash relié à la libposix du moment (ARCH=x86_64 sh tools/bash/build.sh : HOME=/ de l'arbre POSIX)
    if os.path.isfile(os.path.join(PC, "bash", "bash")):
        shutil.copy(os.path.join(PC, "bash", "bash"), os.path.join(TROOT, "bin", "bash"))
    etc =os.path.join(TROOT, "etc")
    # la table des chemins du dépôt (l'arbre POSIX : /, /sd1, /ram0), si l'arbre est plus ancien
    shutil.copy(os.path.join(ROOT, "skel", "sd0", "etc", "paths.txt"), os.path.join(etc, "paths.txt"))
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "/sys/svc/telnetd" not in s:
        s += "# essais de tools/python/pctest.py : telnet\n/sys/svc/telnetd\n"
        open(sv, "w", encoding="utf-8", newline="\n").write(s)
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
             "-device", "usb-net,bus=xhci.0,netdev=un0"] + a.qemu.split()
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
        kept = []  # (spawn2 : les autres sessions telnet, gardées ouvertes)
        for c in a.cmd:
            if c.startswith("shot:"):  # une capture de l'écran : build/pc/pytest/NOM.png
                f = os.path.join(W, c[5:] + ".png").replace("\\", "/")
                q.cmd("screendump", filename=f, format="png")
                print("capture :", f, flush=True)
                continue
            if c.startswith("sleep:"):
                time.sleep(float(c[6:]))
                continue
            if c.startswith("click:"):  # un clic gauche en X,Y d'un écran de 1280×800 (--qemu "-device usb-tablet,bus=xhci.0")
                x, y = (int(v) for v in c[6:].split(","))
                q.cmd("input-send-event", events=[{"type": "abs", "data": {"axis": "x", "value": x * 32767 // 1279}},
                                                  {"type": "abs", "data": {"axis": "y", "value": y * 32767 // 799}}])
                time.sleep(0.3)
                for down in (True, False):
                    q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": "left"}}])
                    time.sleep(0.15)
                print("clic :", x, y, flush=True)
                continue
            if c.startswith("rclick:"):  # un clic droit en X,Y (menus contextuels)
                x, y = (int(v) for v in c[7:].split(","))
                q.cmd("input-send-event", events=[{"type": "abs", "data": {"axis": "x", "value": x * 32767 // 1279}},
                                                  {"type": "abs", "data": {"axis": "y", "value": y * 32767 // 799}}])
                time.sleep(0.3)
                for down in (True, False):
                    q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": "right"}}])
                    time.sleep(0.15)
                print("clic droit :", x, y, flush=True)
                continue
            if c.startswith("drag:"):  # glisser de X1,Y1 à X2,Y2, bouton gauche tenu, en dix pas
                x1, y1, x2, y2 = (int(v) for v in c[5:].split(","))
                def at(x, y):
                    q.cmd("input-send-event", events=[{"type": "abs", "data": {"axis": "x", "value": x * 32767 // 1279}},
                                                      {"type": "abs", "data": {"axis": "y", "value": y * 32767 // 799}}])
                at(x1, y1)
                time.sleep(0.3)
                q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}])
                time.sleep(0.2)
                for k in range(1, 11):
                    at(x1 + (x2 - x1) * k // 10, y1 + (y2 - y1) * k // 10)
                    time.sleep(0.08)
                time.sleep(0.3)
                q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": "left"}}])
                print("glissé :", x1, y1, "->", x2, y2, flush=True)
                continue
            if c.startswith("wheel:"):  # la molette en X,Y : N crans (positif : vers le bas)
                x, y, n = (int(v) for v in c[6:].split(","))
                q.cmd("input-send-event", events=[{"type": "abs", "data": {"axis": "x", "value": x * 32767 // 1279}},
                                                  {"type": "abs", "data": {"axis": "y", "value": y * 32767 // 799}}])
                time.sleep(0.2)
                for _ in range(abs(n)):
                    for down in (True, False):
                        q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": "wheel-down" if n > 0 else "wheel-up"}}])
                    time.sleep(0.05)
                print("molette :", x, y, n, flush=True)
                continue
            if c.startswith("dblclick:"):  # un double-clic gauche en X,Y (comme click:, appuis rapprochés)
                x, y = (int(v) for v in c[9:].split(","))
                q.cmd("input-send-event", events=[{"type": "abs", "data": {"axis": "x", "value": x * 32767 // 1279}},
                                                  {"type": "abs", "data": {"axis": "y", "value": y * 32767 // 799}}])
                time.sleep(0.3)
                for down in (True, False, True, False):
                    q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": "left"}}])
                    time.sleep(0.05)
                print("double-clic :", x, y, flush=True)
                continue
            if c.startswith("rips:"):  # un profil par échantillons : N fois le RIP de chaque processeur (info registers -a)
                import re
                for _ in range(int(c[5:])):
                    r = q.hmp("info registers -a")
                    r = r.get("return", "") if isinstance(r, dict) else str(r)
                    rips, cr3s = re.findall(r"RIP=([0-9a-f]+)", r), re.findall(r"CR3=([0-9a-f]+)", r)
                    print("rips: " + " ".join("%s/%s" % rc for rc in zip(rips, cr3s)), flush=True)
                    time.sleep(0.02)
                continue
            if c.startswith("spawn:"):  # une commande qui ne rend pas la main (une app) : envoyée sans attendre sa fin
                t.s.sendall(c[6:].encode("utf-8") + b"\r\n")
                print("lancé :", c[6:], flush=True)
                continue
            if c.startswith("watch:"):  # watch:SECONDES:CAPTURE:COMMANDE : tout ce qu'elle écrit pendant ce temps,
                #                             puis une capture (CAPTURE : un nom, ou vide) et Ctrl+C (une app qui ne
                #                             rend pas la main : on voit ses messages au fil de l'eau)
                secs, shot, cmd = c[6:].split(":", 2)
                t.s.sendall(cmd.encode("utf-8") + b"\r\n")
                end = time.time() + float(secs)
                while time.time() < end:
                    t.s.settimeout(max(0.1, end - time.time()))
                    try:
                        d = t.s.recv(65536)
                    except OSError:
                        continue
                    if not d:
                        break
                    t.buf += d.replace(b"\xff\xf1", b"")
                print("$ (%s s) %s\n%s" % (secs, cmd, rem.nettoyer(t.buf)), flush=True)
                t.buf = b""
                if shot:
                    f = os.path.join(W, shot + ".png").replace("\\", "/")
                    q.cmd("screendump", filename=f, format="png")
                    print("capture :", f, flush=True)
                t.s.sendall(b"\x03")
                time.sleep(2)
                continue
            if c.startswith("spawn2:"):  # une app lancée dans une autre session telnet, sans attendre : deux apps à
                #                              l'écran (la session reste ouverte jusqu'à la fin de l'essai)
                t2 = rem.Telnet("127.0.0.1", a.port, timeout=30)
                if b"Mot de passe" in t2.read_until(b" : ", 30):
                    t2.s.sendall(b"essai\r\n")
                t2.s.sendall(b'echo @PR""ET\r\n')
                t2.read_until(b"@PRET", 30)
                t2.read_until(b"\n", 30)
                t2.s.sendall(c[7:].encode("utf-8") + b"\r\n")
                kept.append(t2)
                print("lancé (autre session) :", c[7:], flush=True)
                continue
            if c.startswith("fresh:"):  # une commande dans une autre session telnet (la première tient une app lancée)
                t2 = None
                for _ in range(5):  # (une machine très chargée, llvmpipe par exemple, tarde à ouvrir la session)
                    try:
                        t2 = rem.Telnet("127.0.0.1", a.port, timeout=30)
                        if b"Mot de passe" in t2.read_until(b" : ", 30):
                            t2.s.sendall(b"essai\r\n")
                        t2.s.sendall(b'echo @PR""ET\r\n')
                        t2.read_until(b"@PRET", 30)
                        t2.read_until(b"\n", 30)
                        break
                    except (OSError, EOFError, TimeoutError):
                        t2 = None
                        time.sleep(5)
                if t2 is None:
                    print("(autre session : pas de réponse)", flush=True)
                    continue
                out, code = t2.run(c[6:], 120)
                print("$ (autre session) %s\n%s[code %d]" % (c[6:], out, code), flush=True)
                t2.close()
                continue
            if c.startswith("key:") or c.startswith("type:"):  # des touches (sendkey), ou un texte en AZERTY
                ks = c[4:].split() if c.startswith("key:") else qp.azerty_keys(c[5:])[:-1]
                for k in ks:
                    q.hmp("sendkey " + k)
                    time.sleep(0.08)
                print("touches :", " ".join(ks), flush=True)
                continue
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
    ap.add_argument("--qemu", default="", help="arguments de plus pour QEMU (-rtc base=... : une horloge CMOS décalée)")
    a = ap.parse_args()
    if a.mode == "prep":
        prep(a)
    else:
        sys.exit(run(a))


if __name__ == "__main__":
    main()
