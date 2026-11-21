#!/bin/sh
# run.sh -- jalon 1 de l'hôte Linux (docs/etude/LINUX-HOST.md) sous QEMU PC : des exécutables de Linux (gcc
# d'Ubuntu, glibc ; PIE et ET_EXEC) démarrés par « linuxhost run ». Un disque d'essai : build/pc/sysroot sans
# apps, avec /bin/linuxhost (sh tools/linux/build.sh), les essais dans /home/lx, telnetd ; le noyau de
# build/pc/esp (la zone basse de VM_FIXED : PC_QUICK=1 sh tools/pc/build.sh).
#   sh tools/linux/exectest/run.sh            sortie : build/pc/linux/exectest/run/run.txt
#   EXTRA="/chemin/prog …"                     d'autres exécutables de Linux copiés dans /home/lx
#   MEM=4G : la mémoire de QEMU (2G par défaut) ; APPS="terminal" : ces apps de build/pc/apps (l'écran : shot:, type:, key:)
#   EXTRA_BIN=… (dans /bin), CA_EXTRA=certificat (ajouté à /etc/ca-bundle), EXTRA_MB=N (place libre en plus),
#   TREE=dossier (recopié à la racine)
#   ONLY="sleep:10|@60 /bin/linuxhost run /home/lx/hello"   seulement ces commandes ; PORT : telnet (2394)
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
OUT=build/pc/linux
B=$OUT/exectest
mkdir -p "$B"
S=tools/linux/exectest
gcc -O2 "$S/hello.c" -o "$B/hello"
gcc -O2 -no-pie "$S/hello.c" -o "$B/hello-nopie"
gcc -O2 "$S/tlstest.c" -o "$B/tlstest"
gcc -O2 -no-pie "$S/tlstest.c" -o "$B/tlstest-nopie"
gcc -O2 "$S/spawntest.c" -o "$B/spawntest"
gcc -O2 "$S/epolltest.c" -o "$B/epolltest"
gcc -O2 "$S/sigchldtest.c" -o "$B/sigchldtest"
gcc -O2 "$S/bashtooltest.c" -o "$B/bashtooltest"
T=$OUT/execroot
rm -rf "$T"; mkdir -p "$T"
for d in build/pc/sysroot/*; do case $(basename "$d") in apps|devtools|python) continue ;; esac; cp -r "$d" "$T/"; done
cp "$OUT/linuxhost" "$T/bin/linuxhost"
mkdir -p "$T/home/lx"
cp "$B/hello" "$B/hello-nopie" "$B/tlstest" "$B/tlstest-nopie" "$B/spawntest" "$B/epolltest" "$B/sigchldtest" "$B/bashtooltest" "$T/home/lx/"
for f in ${EXTRA:-}; do cp "$f" "$T/home/lx/"; done
mkdir -p "$T/apps"
for a in ${APPS:-}; do	# (APPS="terminal jet" : sh tools/pc/apps.sh terminal ; jet : build/pc/sysroot)
	if [ -d "build/pc/apps/sdroot/apps/$a.app" ]; then cp -r "build/pc/apps/sdroot/apps/$a.app" "$T/apps/"; else cp -r "build/pc/sysroot/apps/$a.app" "$T/apps/"; fi
done
cp src/sys/linuxhost/xdg-open "$T/bin/xdg-open"
for f in ${EXTRA_BIN:-}; do cp "$f" "$T/bin/"; done
[ -z "${TREE:-}" ] || cp -r "$TREE"/. "$T/"	# (TREE=dossier : son contenu recopié à la racine du disque d'essai)	# (EXTRA_BIN="build/pc/apps/sdroot/bin/pkg" : un autre programme de /bin)
[ -z "${CA_EXTRA:-}" ] || cat "$CA_EXTRA" >>"$T/etc/ca-bundle"	# (le proxy TLS du cloud : /root/.ccr/ca-bundle.crt ; disque d'essai seulement)
printf 'password = essai\n' >"$T/etc/remote.txt"
grep -q "^/sys/svc/telnetd" "$T/etc/services.txt" || printf '/sys/svc/telnetd\n' >>"$T/etc/services.txt"
IMG=$OUT/execdisk.img; rm -f "$IMG"
python3 -I tools/pc/mkkorindisk.py "$IMG" --layout zbook --korin-mb $(( $(du -sm "$T" | cut -f1) + 100 + ${EXTRA_MB:-0} )) --sysroot "$T" --esp build/pc/esp --cfg 'default = korin
timeout = 0' >/dev/null
W=$B/run; mkdir -p "$W"
python3 -I - "$W" "$IMG" "${PORT:-2394}" > "$W/run.txt" 2>&1 <<'PY' || true
import importlib.util, os, sys, types
w, img, port = sys.argv[1:4]
spec = importlib.util.spec_from_file_location("pctest", "tools/python/pctest.py")
pt = importlib.util.module_from_spec(spec); spec.loader.exec_module(pt)
pt.W, pt.IMG = os.path.abspath(w), os.path.abspath(img)
L = "/bin/linuxhost run /home/lx/"
cmd = os.environ.get("ONLY", "").split("|") if os.environ.get("ONLY") else [
    "sleep:10",
    "@60 " + L + "hello-nopie un deux; echo rc=$?",
    "@60 " + L + "hello trois; echo rc=$?",
    "@60 " + L + "tlstest-nopie; echo rc=$?",
    "@60 " + L + "tlstest; echo rc=$?",
    "@60 " + L + "spawntest; echo rc=$?",
]
a = types.SimpleNamespace(port=int(port), hostfwd=[], smp=2, mem=os.environ.get("MEM", "2G"), boot=150, shot=os.path.join(w, "fin.png"), qemu=os.environ.get("QEMU_EXTRA", ""), cmd=cmd)
pt.run(a)
PY
rm -rf "$T" "$IMG"
grep -a -E "^(hello|tlstest|spawntest|bonjour|fils|linuxhost|liblinux|rc=)" "$W/run.txt" || tail -40 "$W/run.txt"
