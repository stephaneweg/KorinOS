#!/bin/sh
# run.sh -- polltest sous QEMU PC : un disque d'essai (build/pc/sysroot sans apps, avec /bin/polltest, telnetd) ; un
# serveur HTTP doit écouter sur le port 18765 de la machine hôte (10.0.2.2 pour QEMU), par exemple :
#   python3 -m http.server 18765
# Construire : x86_64-onyx-elf-gcc -specs=build/pc/posix/lib/posix.specs -O2 tools/linux/polltest/polltest.c
#   -o build/polltest/polltest. Sortie : build/polltest/run/run.txt. PORT : telnet (2393 par défaut).
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
OUT=build/pc/linux
T=$OUT/pollroot
rm -rf "$T"; mkdir -p "$T"
for d in build/pc/sysroot/*; do case $(basename "$d") in apps|devtools|python) continue ;; esac; cp -r "$d" "$T/"; done
cp build/polltest/polltest "$T/bin/polltest"
[ ! -f build/polltest/curlpoll ] || cp build/polltest/curlpoll "$T/bin/curlpoll"
printf 'password = essai\n' >"$T/etc/remote.txt"
grep -q "^/sys/svc/telnetd" "$T/etc/services.txt" || printf '/sys/svc/telnetd\n' >>"$T/etc/services.txt"
IMG=$OUT/polldisk.img; rm -f "$IMG"
python3 -I tools/pc/mkkorindisk.py "$IMG" --layout zbook --korin-mb $(( $(du -sm "$T" | cut -f1) + 100 )) --sysroot "$T" --esp build/pc/esp --cfg 'default = korin
timeout = 0' >/dev/null
W=build/polltest/run; mkdir -p "$W"
python3 -I - "$W" "$IMG" "${PORT:-2393}" > "$W/run.txt" 2>&1 <<'PY' || true
import importlib.util, os, sys, types
w, img, port = sys.argv[1:4]
spec = importlib.util.spec_from_file_location("pctest", "tools/python/pctest.py")
pt = importlib.util.module_from_spec(spec); spec.loader.exec_module(pt)
pt.W, pt.IMG = os.path.abspath(w), os.path.abspath(img)
a = types.SimpleNamespace(port=int(port), hostfwd=[], smp=2, mem="2G", boot=150, shot=os.path.join(w, "fin.png"), qemu="",
    cmd=["sleep:10", "@120 /bin/polltest", "@60 /bin/curlpoll"])
pt.run(a)
PY
rm -rf "$T" "$IMG"
grep -a -E "^(poll|select|boucle|réponse|connect|  |libcurl|sans|après|requête)" "$W/run.txt"
