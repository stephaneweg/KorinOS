#!/bin/sh
# test_eme_jet.sh -- EME Clear Key dans Jet, sous QEMU PC (docs/etude/WIDEVINE.md, étape 4), dans WSL après
# « . ~/korin-cloud/env.sh » :
#
#   JET=…/jet.app sh tools/linux/test_eme_jet.sh
#
# JET : le dossier jet.app construit avec EME (build-jet.sh avec BUILD et JET_OUT d'une construction à part).
# 1. l'arbre build/pc/linux/emeroot : une copie de build/pc/sysroot sans apps/ (sauf ce Jet), devtools/, python/ ;
#    /etc/web-console (la console des pages dans le journal du noyau), telnetd (mot de passe d'essai « essai ») ;
# 2. le disque d'essai build/pc/linux/emedisk.img (mkkorindisk.py, layout zbook) ;
# 3. la page tools/webkit/tests/eme-clearkey.html et le premier Mo du flux Clear Key de Shaka (avcenc_test.sh
#    les télécharge), servis par python3 -m http.server sur le port 8642 de WSL : http://10.0.2.2:8642/ pour QEMU ;
# 4. QEMU (tools/python/pctest.py run : NVMe en snapshot, usb-net, telnet) : Jet lancé sur la page, puis le
#    journal du noyau (« EME-CLEARKEY : » de la console de la page) et une capture.
# L'arbre, le disque et le serveur sont effacés ou arrêtés à la fin (KEEP=1 : garder l'arbre et le disque).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
OUT=build/pc/linux
JET=${JET:?JET=…/jet.app (Jet construit avec EME)}
[ -f "$JET/main" ] || { echo "pas de $JET/main" >&2; exit 1; }
[ -s "$OUT/media/ck-144p.mp4" ] && [ -s "$OUT/media/ck-aac.mp4" ] || { echo "d'abord : sh tools/linux/avcenc_test.sh" >&2; exit 1; }

T=$OUT/emeroot
rm -rf "$T"
mkdir -p "$T"
for d in build/pc/sysroot/*; do
	case $(basename "$d") in apps|devtools|python) continue ;; esac
	cp -r "$d" "$T/"
done
mkdir -p "$T/apps"
cp -r "$JET" "$T/apps/jet.app"
touch "$T/etc/web-console"
printf 'password = essai\n' >"$T/etc/remote.txt"
grep -q "/sys/svc/telnetd" "$T/etc/services.txt" || printf '# essai EME : telnet\n/sys/svc/telnetd\n' >>"$T/etc/services.txt"
MB=$(( $(du -sm "$T" | cut -f1) + 200 ))
IMG=$OUT/emedisk.img
rm -f "$IMG"
python3 -I tools/pc/mkkorindisk.py "$IMG" --layout zbook --korin-mb "$MB" --sysroot "$T" --cfg 'default = korin
timeout = 0' >/dev/null
echo "disque : $IMG ($MB Mo pour KORIN OS)"

WWW=$OUT/eme-www
rm -rf "$WWW"
mkdir -p "$WWW"
cp tools/webkit/tests/eme-clearkey.html "$WWW/index.html"
cp "$OUT/media/ck-144p.mp4" "$WWW/video.mp4"
cp "$OUT/media/ck-aac.mp4" "$WWW/audio.mp4"
python3 -m http.server 8642 --directory "$WWW" >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT

W=$OUT/emetest
mkdir -p "$W"
python3 -I - "$ROOT" "$W" "$IMG" <<'PY'
import importlib.util, os, sys, types
root, w, img = sys.argv[1:4]
spec = importlib.util.spec_from_file_location("pctest", os.path.join(root, "tools", "python", "pctest.py"))
pt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pt)
pt.W, pt.IMG = os.path.abspath(w), os.path.abspath(img)
a = types.SimpleNamespace(port=2395, hostfwd=[], smp=4, mem="4G", boot=150, shot=os.path.join(w, "fin.png"), qemu="",
    cmd=["sleep:20", "( /apps/jet.app/main http://10.0.2.2:8642/index.html > /dev/null 2>&1 & ) ; echo Jet lancé",
         "sleep:120", "shot:eme"])
pt.run(a)
PY
grep -a "EME-CLEARKEY\|MediaPlayer\|encrypted\|source buffer" "$W/debug.log" | tail -40 || true
if [ -z "${KEEP:-}" ]; then
	rm -rf "$T" "$IMG" "$WWW"
fi
grep -aq "EME-CLEARKEY : RÉUSSI" "$W/debug.log"
