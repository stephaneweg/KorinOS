#!/bin/sh
# test_pc.sh -- win16 sous QEMU PC (docs/etude/WIN16.md), dans WSL après « . ~/korin-cloud/env.sh » :
#
#   sh tools/win16/test_pc.sh [hello]          (le programme d'essai build/win16/tests/<nom>.exe)
#   CLICKS="click:404,62 sleep:3 shot:ferme" sh tools/win16/test_pc.sh hello   (clics après la capture : tablette
#                                               USB, écran de 1280×800 ; ici le ✕ de la fenêtre du Hello)
#
# Il faut : sh tools/pc/apps.sh win16 (build/pc/apps/sdroot/apps/win16.app), sh tools/win16/build-tests.sh, et
# build/pc/sysroot. Fait l'arbre build/win16/pcroot (une copie de build/pc/sysroot sans apps/, devtools/, python/,
# plus win16.app et les .exe d'essai dans /home/win16), le disque build/win16/pcdisk.img (mkkorindisk.py), démarre
# QEMU (tools/python/pctest.py run : NVMe en snapshot, telnet), lance le programme et fait une capture :
# build/win16/pctest/<nom>.png ; la sortie de win16 (W16_TRACE=1) suit. Arbre et disque effacés à la fin (KEEP=1 :
# gardés).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
NAME=${1:-hello}
OUT=build/win16
APP=build/pc/apps/sdroot/apps/win16.app
[ -f "$APP/main" ] || { echo "d'abord : sh tools/pc/apps.sh win16" >&2; exit 1; }
[ -f "$OUT/tests/$NAME.exe" ] || { echo "d'abord : sh tools/win16/build-tests.sh" >&2; exit 1; }
T=$OUT/pcroot
rm -rf "$T"
mkdir -p "$T"
for d in build/pc/sysroot/*; do
	case $(basename "$d") in apps|devtools|python) continue ;; esac
	cp -r "$d" "$T/"
done
mkdir -p "$T/apps" "$T/home/win16"
for a in files terminal; do [ ! -d "build/pc/sysroot/apps/$a.app" ] || cp -r "build/pc/sysroot/apps/$a.app" "$T/apps/"; done
cp -r "$APP" "$T/apps/win16.app"
cp "$OUT"/tests/*.exe "$T/home/win16/"
printf 'password = essai\n' >"$T/etc/remote.txt"
grep -q "/sys/svc/telnetd" "$T/etc/services.txt" || printf '# essai win16 : telnet\n/sys/svc/telnetd\n' >>"$T/etc/services.txt"
MB=$(( $(du -sm "$T" | cut -f1) + 100 ))
IMG=$OUT/pcdisk.img
rm -f "$IMG"
python3 -I tools/pc/mkkorindisk.py "$IMG" --layout zbook --korin-mb "$MB" --sysroot "$T" --esp build/pc/esp --cfg 'default = korin
timeout = 0' >/dev/null
echo "disque : $IMG ($MB Mo pour KORIN OS)"
W=$OUT/pctest
mkdir -p "$W"
python3 -I - "$ROOT" "$W" "$IMG" "$NAME" > "$W/run.txt" 2>&1 <<'PY' || true
import importlib.util, os, sys, types
root, w, img, name = sys.argv[1:5]
spec = importlib.util.spec_from_file_location("pctest", os.path.join(root, "tools", "python", "pctest.py"))
pt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pt)
pt.W, pt.IMG = os.path.abspath(w), os.path.abspath(img)
clicks = os.environ.get("CLICKS", "").split()
a = types.SimpleNamespace(port=2397, hostfwd=[], smp=4, mem="2G", boot=150, shot=os.path.join(w, "fin.png"),
    qemu="-device usb-tablet,bus=xhci.0" if clicks else "",
    cmd=["sleep:20",
         "( W16_TRACE=1 /apps/win16.app/main /home/win16/%s.exe > /tmp/w16.log 2>&1 & ) ; echo lancé" % name,
         "sleep:8", "shot:" + name] + clicks + ["cat /tmp/w16.log | tail -n 40"])
pt.run(a)
PY
cat "$W/run.txt" | tail -n 100
if [ -z "${KEEP:-}" ]; then
	rm -rf "$T" "$IMG"
fi
echo "capture : $W/$NAME.png"
