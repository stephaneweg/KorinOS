"""testimage : la carte d'essai de Rust pour QEMU, build/rust/sd.img (docs/RUST.md) : build/sdroot
(fait par build.ps1), un mot de passe d'essai pour telnetd et ftpd (« essai », que
tools/armpi_remote.py lit dans ARMPI_REMOTE_PW), et les essais construits par tools/rust/build.sh
(build/rust/tests : korintest, rdlhost, librdl.so, les essais de std) dans SD0:/home/rust.
Ni build/sdroot ni build/sd.img ne changent.
    python -I tools/rust/testimage.py          (depuis la racine du worktree, après build.ps1)

Le PC (x86_64-unknown-korin) : un disque d'essai de QEMU q35, build/pc/rust/disk.img
(tools/pc/mkkorindisk.py --layout zbook) : une copie de build/pc/sysroot sans apps/ ni gcc/
(tools/pc/sysroot.sh), la console texte à la place du bureau, telnetd lancé par init2, le même mot
de passe d'essai, et build/pc/rust/tests (ARCH=x86_64 bash tools/rust/build.sh) dans
SD0:/home/rust (aussi SD1:/rust : SD1: est /home de la partition KORIN OS). L'arbre et le volume de
démarrage (build/pc/esp) sont copiés sous le verrou build/pc.lock ; ni l'un ni l'autre ne change.
    python -I tools/rust/testimage.py --pc
puis, pour lancer des commandes (QEMU q35, NVMe en snapshot : l'image n'est jamais écrite ; usb-net
et réseau user de QEMU, telnet du PC sur 127.0.0.1:PORT, QMP sur PORT + 1000 ; journal du noyau
build/pc/rust/debug.log ; « @600 commande » : 600 s au plus, 60 sinon ; QEMU arrêté à la fin) :
    python -I tools/rust/testimage.py --pc-run [--port 47323] "cd SD0:/home/rust && ./korintest" ...
"""
import os
import shutil
import subprocess
import sys
import time

root = os.getcwd()


def pi():
    t = os.path.join(root, "build", "rust", "testroot")
    shutil.rmtree(t, ignore_errors=True)
    shutil.copytree(os.path.join(root, "build", "sdroot"), t)
    open(os.path.join(t, "etc", "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    src = os.path.join(root, "build", "rust", "tests")
    if not os.path.isdir(src):
        sys.exit("build/rust/tests absent : lancer d'abord sh tools/rust/build.sh dans WSL")
    shutil.copytree(src, os.path.join(t, "home", "rust"), dirs_exist_ok=True)
    subprocess.check_call([sys.executable, "-I", "tools/mkdisk.py", "build/rust/sd.img", "--fat32", t,
                           "--exfat", "build/exfat_root"], stdout=subprocess.DEVNULL)
    print("build/rust/sd.img", os.path.getsize("build/rust/sd.img"))


def du(d):
    return sum(os.path.getsize(os.path.join(b, f)) for b, _, fs in os.walk(d) for f in fs)


def pc():
    out = os.path.join(root, "build", "pc", "rust")
    src = os.path.join(out, "tests")
    if not os.path.isdir(src):
        sys.exit("build/pc/rust/tests absent : lancer d'abord ARCH=x86_64 bash tools/rust/build.sh dans WSL")
    t = os.path.join(out, "testroot")
    esp = os.path.join(out, "esp")
    shutil.rmtree(t, ignore_errors=True)
    shutil.rmtree(esp, ignore_errors=True)
    lock = os.path.join(root, "build", "pc.lock")
    for _ in range(600):			# (un autre chantier écrit peut-être l'arbre : on attend)
        try:
            os.mkdir(lock)
            break
        except FileExistsError:
            time.sleep(1)
    else:
        sys.exit("build/pc.lock pris depuis 10 min")
    try:
        sysroot = os.path.join(root, "build", "pc", "sysroot")
        top = lambda d, names: {"apps", "gcc"} & set(names) if os.path.samefile(d, sysroot) else set()
        shutil.copytree(sysroot, t, ignore=top)
        shutil.copytree(os.path.join(root, "build", "pc", "esp"), esp)
    finally:
        os.rmdir(lock)
    e = os.path.join(t, "etc")
    ini = open(os.path.join(e, "system.ini"), encoding="utf-8").read()
    open(os.path.join(e, "system.ini"), "w", encoding="utf-8", newline="\n").write(
        ini.replace("\nshell=desktop", "\nshell=console"))
    with open(os.path.join(e, "services.txt"), "a", encoding="utf-8", newline="\n") as f:
        f.write("# essais de Rust (tools/rust/testimage.py --pc) : telnet\nSD0:/sys/svc/telnetd\n")
    open(os.path.join(e, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    shutil.copytree(src, os.path.join(t, "home", "rust"), dirs_exist_ok=True)
    mb = max(65, du(t) // (1 << 20) + 48)		# (de la place pour les fichiers des essais)
    img = os.path.join(out, "disk.img")
    subprocess.check_call([sys.executable, "-I", "tools/pc/mkkorindisk.py", img, "--layout", "zbook",
                           "--sysroot", t, "--esp", esp, "--korin-mb", str(mb),
                           # (Korin tout de suite : sans boot.cfg, le menu du chargeur lance le faux Windows)
                           "--cfg", "default = korin\ntimeout = 0"], stdout=subprocess.DEVNULL)
    print("build/pc/rust/disk.img", os.path.getsize(img), "(KORIN OS : %d Mo)" % mb)


def load(name, path):
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def pc_run(args):
    port = 47323				# (pas 4461 ni 4471, pris par d'autres essais)
    if args[:1] == ["--port"]:
        port, args = int(args[1]), args[2:]
    out = os.path.join(root, "build", "pc", "rust")
    img = os.path.join(out, "disk.img")
    if not os.path.isfile(img):
        sys.exit("faire d'abord : python -I tools/rust/testimage.py --pc")
    log = os.path.join(out, "debug.log")
    os.environ["KORIN_PC_DEBUGLOG"] = log
    qp = load("qemu_pc", os.path.join(root, "tools", "pc", "qemu_pc.py"))
    rem = load("armpi_remote", os.path.join(root, "tools", "armpi_remote.py"))
    qp.DEBUGLOG = log
    qa = qp.qemu_args(True, port + 1000, 4, vars_fd=os.path.join(out, "ovmf_vars.fd"), boot_drive=False)
    qa += ["-drive", "if=none,id=nv,format=raw,snapshot=on,file=" + img.replace("\\", "/"),
           "-device", "nvme,serial=KORINTEST,drive=nv", "-device", "qemu-xhci,id=rx",
           "-netdev", "user,id=rn,hostfwd=tcp:127.0.0.1:%d-:23" % port, "-device", "usb-net,bus=rx.0,netdev=rn"]
    p = subprocess.Popen(qa)
    t0, rc = time.time(), 0
    try:
        q = qp.Qmp(port + 1000)
        t = None
        while t is None and time.time() - t0 < 120:
            try:
                t = rem.Telnet("127.0.0.1", port, timeout=10)
                if b"Mot de passe" in t.read_until(b" : ", 15):
                    t.s.sendall(b"essai\r\n")
                t.s.sendall(b'echo @PR""ET\r\n')
                t.read_until(b"@PRET", 20)
                t.read_until(b"\n", 20)
            except (OSError, EOFError, TimeoutError):
                t = None
                time.sleep(3)
        if t is None:
            sys.exit("telnet : pas de réponse après 120 s (journal : %s)" % log)
        print("telnet prêt après %.0f s" % (time.time() - t0))
        for c in args:
            limit = 60
            if c.startswith("@") and " " in c:
                limit, c = int(c[1:c.index(" ")]), c[c.index(" ") + 1:]
            t1 = time.time()
            o, code = t.run(c, limit)
            print("$ %s\n%s[code %d, %.1f s]" % (c, o, code, time.time() - t1), flush=True)
            rc = rc or code
        t.close()
        q.cmd("quit")
    finally:
        try:
            p.wait(15)
        except subprocess.TimeoutExpired:
            p.kill()
    return rc


if __name__ == "__main__":
    if sys.argv[1:2] == ["--pc-run"]:
        sys.exit(pc_run(sys.argv[2:]))
    pc() if "--pc" in sys.argv[1:] else pi()
