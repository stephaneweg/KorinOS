# rebuild-sdk.py -- recompile tous les objets du SDK de Jet (commands.txt) avec des drapeaux en plus, en parallèle,
# depuis /devtools/jet-sdk/webcore ; reprend là où il s'est arrêté (fait.txt). Usage :
#   python3 -I rebuild-sdk.py JOBS "DRAPEAUX" [FILTRE]
# Le ZBook (i7-11800H, 16 cœurs, 32 Go) : python3 -I rebuild-sdk.py 10 "-march=tigerlake -mno-avx512f -mtune=tigerlake"
# (≈ 1 h ; le noyau de Korin n'autorise pas l'AVX-512 : XCR0 = 0x7). L'état (fait.txt, rebuild.log) est à côté du script :
# copier le script dans un dossier de travail (~/jet-build) avant de le lancer.
import os, re, subprocess, sys, time, threading
SDK = os.environ.get("JETSDK", "/devtools/jet-sdk")
HERE = os.path.dirname(os.path.abspath(__file__))
JOBS = int(sys.argv[1]); FLAGS = sys.argv[2]; FILT = sys.argv[3] if len(sys.argv) > 3 else ""
DONE = os.path.join(HERE, "fait.txt"); LOG = os.path.join(HERE, "rebuild.log")
done = set(open(DONE).read().split("\n")) if os.path.exists(DONE) else set()
todo = []
for line in open(os.path.join(SDK, "commands.txt"), encoding="utf-8"):
    line = line.rstrip("\n")
    if "\t" not in line: continue
    obj, cmd = line.split("\t", 1)
    if obj in done or FILT not in obj: continue
    # (sans les fichiers de dépendances ; les en-têtes de renvoi du SDK ; les drapeaux du processeur)
    cmd = re.sub(r" -MD -MT \S+ -MF \S+", "", cmd)
    cmd += " -idirafter %s/webcore/WebCore/PrivateHeaders/WebCore %s" % (SDK, FLAGS)
    todo.append((obj, cmd))
lock = threading.Lock(); t0 = time.time(); n = [0, 0]
log = open(LOG, "a", encoding="utf-8"); fd = open(DONE, "a", encoding="utf-8")
log.write("== %s : %d objets, %d jobs, %s\n" % (time.strftime("%H:%M:%S"), len(todo), JOBS, FLAGS)); log.flush()
def work():
    while True:
        with lock:
            if not todo: return
            obj, cmd = todo.pop(0)
        a = time.time()
        r = subprocess.run(cmd, shell=True, cwd=os.path.join(SDK, "webcore"), capture_output=True, text=True, errors="replace")
        with lock:
            if r.returncode == 0:
                n[0] += 1; fd.write(obj + "\n"); fd.flush()
            else:
                n[1] += 1; log.write("ÉCHEC %s\n%s\n" % (obj, (r.stderr or "")[-3000:]))
            if (n[0] + n[1]) % 20 == 0 or r.returncode:
                log.write("%s : %d faits, %d échecs, %d restants, %.0f s (dernier %.0f s : %s)\n" % (time.strftime("%H:%M:%S"), n[0], n[1], len(todo), time.time() - t0, time.time() - a, os.path.basename(obj)))
            log.flush()
ths = [threading.Thread(target=work) for _ in range(JOBS)]
for t in ths: t.start()
for t in ths: t.join()
log.write("== fini %s : %d faits, %d échecs en %.0f s\n" % (time.strftime("%H:%M:%S"), n[0], n[1], time.time() - t0)); log.flush()
print("fini : %d faits, %d échecs en %.0f s" % (n[0], n[1], time.time() - t0))
