#!/bin/sh
# run.sh -- sqltest sous QEMU PC (après build.sh) : un disque d'essai (build/pc/sysroot sans apps, avec /bin/sqltest,
# telnetd), puis :
#   1. sqltest /home/sqltest.db         écrire, fermer, rouvrir, relire (doit montrer « yes » deux fois) ;
#   2. sqltest /home/hold.db hold &      écrire puis garder le fichier ouvert 25 s, et sortir sans le fermer ;
#   3. pendant et après : grep -a -c holdperm, sqltest … scan (le fichier lu par 1 Mo : FS_READ_DIRECT, par 4 Ko et par
#      100 octets, sans grep) et sqltest … read (lecture par SQLite).
# Le 20/11 : SQLite voit « holdperm », l'ancien grep non (0) : il s'arrêtait au premier nul d'une ligne (corrigé) ;
# scan, pendant (lancé par sqltest … hold) et après : 2 à chaque taille de lecture, fatfs est cohérent.
# OLDGREP=fichier : un autre grep à comparer (/bin/oldgrep). PORT : telnet (QMP = PORT + 1000), 2391 par défaut. Sortie : build/sqltest/run/run.txt.
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
OUT=build/pc/linux
T=$OUT/sqlroot
rm -rf "$T"; mkdir -p "$T"
for d in build/pc/sysroot/*; do case $(basename "$d") in apps|devtools|python) continue ;; esac; cp -r "$d" "$T/"; done
cp build/sqltest/sqltest "$T/bin/sqltest"
[ -z "${OLDGREP:-}" ] || cp "$OLDGREP" "$T/bin/oldgrep"	# (un autre grep à comparer : OLDGREP=fichier)
printf 'password = essai\n' >"$T/etc/remote.txt"
grep -q "/sys/svc/telnetd" "$T/etc/services.txt" || printf '/sys/svc/telnetd\n' >>"$T/etc/services.txt"
ESP=$OUT/kbuild/esp; [ -f "$ESP/korin/kernel.elf" ] || ESP=build/pc/esp
IMG=$OUT/sqldisk.img; rm -f "$IMG"
python3 -I tools/pc/mkkorindisk.py "$IMG" --layout zbook --korin-mb $(( $(du -sm "$T" | cut -f1) + 100 )) --sysroot "$T" --esp "$ESP" --cfg 'default = korin
timeout = 0' >/dev/null
W=build/sqltest/run; mkdir -p "$W"
python3 -I - "$W" "$IMG" "${PORT:-2391}" "${OLDGREP:+1}" > "$W/run.txt" 2>&1 <<'PY' || true
import importlib.util, os, sys, types
w, img, port, old = (sys.argv[1:] + [""])[:4]
g = ["grep -a -c holdperm /home/hold.db"] + (["oldgrep -a -c holdperm /home/hold.db"] if old else [])
spec = importlib.util.spec_from_file_location("pctest", "tools/python/pctest.py")
pt = importlib.util.module_from_spec(spec); spec.loader.exec_module(pt)
pt.W, pt.IMG = os.path.abspath(w), os.path.abspath(img)
a = types.SimpleNamespace(port=int(port), hostfwd=[], smp=2, mem="2G", boot=150, shot=os.path.join(w, "fin.png"), qemu="",
    cmd=["sleep:10", "/bin/sqltest /home/sqltest.db", "( /bin/sqltest /home/hold.db hold > /tmp/hold.log 2>&1 & ) ; echo ok", "sleep:8",
         "cat /tmp/hold.log", "ls -l /home"] + g + ["/bin/sqltest /home/hold.db scan", "/bin/sqltest /home/hold.db read",
         "sleep:30", "ls -l /home"] + g + ["/bin/sqltest /home/hold.db scan",
         "/bin/sqltest /home/hold.db read"])
pt.run(a)
PY
rm -rf "$T" "$IMG"
grep -a -v "^korin:\|@FI" "$W/run.txt" | head -80
