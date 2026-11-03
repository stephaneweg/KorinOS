"""pisync : met le systeme a jour sur le Pi par FTP, depuis la racine du depot, sans argument.
Avant : refaire ce qui est perime dans build/sdroot (docs/MESA.md, piege du redeploiement). Fichiers au CRC different seulement, aucun effacement, jamais etc ni
les donnees, le noyau en dernier. Une seconde passe verifie tout. Journal dans build/mesa/sync.log.
"""
import ftplib, glob, os, sys, time, zlib

HOST = os.environ.get("ARMPI_PI", "192.168.1.20")
DIRS = ("sys", "lib", "res", "fonts", "bin", "apps", "basic", "dotnet", "gcc")
log = open("build/mesa/sync.log", "a", encoding="utf-8", buffering=1)


def say(*a):
    t = " ".join(str(x) for x in a)
    log.write(t + "\n")
    print(t, flush=True)


f = None


def conn():
    global f
    f = ftplib.FTP()
    f.connect(HOST, 21, timeout=120)
    f.login()


def crc_local(p):
    c = 0
    with open(p, "rb") as i:
        while True:
            b = i.read(1 << 20)
            if not b:
                return "%08X" % c
            c = zlib.crc32(b, c)


def crc_remote(r):
    try:
        return f.sendcmd("XCRC " + r).split()[-1]
    except ftplib.error_perm:
        return "ABSENT"


def mkdirs(r):
    parts = r.split("/")
    for i in range(2, len(parts)):
        try:
            f.mkd("/".join(parts[:i]))
        except ftplib.error_perm:
            pass


def put(p, r):
    mkdirs(r)
    with open(p, "rb") as i:
        f.storbinary("STOR " + r, i, 1 << 16)


plan = []
for d in DIRS:
    for base, dirs, files in os.walk(os.path.join("build", "sdroot", d)):
        dirs.sort()
        for n in sorted(files):
            p = os.path.join(base, n)
            r = os.path.relpath(p, os.path.join("build", "sdroot")).replace("\\", "/")
            assert r.split("/")[0] not in ("etc", "home", "var")
            plan.append((p, "SD0:/" + r))
for p in sorted(glob.glob("build/modules/*.mod")):
    plan.append((p, "SD0:/" + os.path.basename(p)))
plan.append(("build/kernel_2712_sd.img", "SD0:/kernel_2712.img"))
plan.append(("build/kernel8_sd.img", "SD0:/kernel8.img"))
say("fichiers", len(plan), "octets", sum(os.path.getsize(p) for p, r in plan))

conn()
sent = bad = 0
for p, r in plan:
    for essai in range(3):
        try:
            l = crc_local(p)
            if crc_remote(r) == l:
                break
            put(p, r)
            sent += 1
            say("envoye", r, os.path.getsize(p))
            if crc_remote(r) != l:
                say("CRC FAUX", r)
                continue
            break
        except (OSError, EOFError, ftplib.Error) as e:
            say("reprise", r, repr(e)[:80])
            time.sleep(5)
            try:
                conn()
            except (OSError, ftplib.Error) as e2:
                say("connexion perdue", repr(e2)[:80])
                time.sleep(20)
                try:
                    conn()
                except (OSError, ftplib.Error):
                    say("ARRET : le Pi ne repond plus")
                    sys.exit(1)
say("passe 1 finie, envoyes", sent)
for p, r in plan:
    if crc_remote(r) != crc_local(p):
        bad += 1
        say("DIFFERENT", r)
say("verification :", len(plan) - bad, "bons,", bad, "differents")
f.quit()
sys.exit(1 if bad else 0)
