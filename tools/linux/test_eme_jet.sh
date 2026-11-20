#!/bin/sh
# test_eme_jet.sh -- EME dans Jet, sous QEMU PC (docs/etude/WIDEVINE.md, étapes 4 et 5), dans WSL après
# « . ~/korin-cloud/env.sh » :
#
#   JET=…/jet.app sh tools/linux/test_eme_jet.sh                                   (Clear Key)
#   JET=…/jet.app KS=widevine WIDEVINE_SO=…/libwidevinecdm.so sh tools/linux/test_eme_jet.sh
#
# Widevine : Jet avec cdmhost (build-jet.sh le met dans jet.app), le module dans /home/.jet/widevine (comme
# l'utilisateur l'y mettra), le noyau avec la capture des syscall et l'arène haute (construit à part dans
# build/pc/linux/kbuild, avec son initrd) ; la page eme-widevine.html, le début du flux Widevine de Shaka
# (cdmtest.sh le télécharge) ; la licence vient du serveur de test de Shaka, par le réseau de QEMU.
#
# JET : le dossier jet.app construit avec EME (build-jet.sh avec BUILD et JET_OUT d'une construction à part).
# 1. l'arbre build/pc/linux/emeroot : une copie de build/pc/sysroot sans apps/ (sauf ce Jet), devtools/, python/ ;
#    /etc/web-console (la console des pages dans le journal du noyau), telnetd (mot de passe d'essai « essai ») ;
# 2. le disque d'essai build/pc/linux/emedisk.img (mkkorindisk.py, layout zbook) ;
# 3. la page tools/webkit/tests/eme-clearkey.html (ou eme-widevine.html) et le premier Mo du flux Clear Key de Shaka (avcenc_test.sh
#    les télécharge), servis par python3 -m http.server sur le port 8642 de WSL : http://10.0.2.2:8642/ pour QEMU ;
# 4. QEMU (tools/python/pctest.py run : NVMe en snapshot, usb-net, telnet) : Jet lancé sur la page, une capture
#    (build/pc/linux/emetest/eme.png) ; la page envoie chaque ligne de son journal au serveur de l'essai
#    (GET /result?…) : « EME-CLEARKEY : RÉUSSI » attendu.
# L'arbre, le disque et le serveur sont effacés ou arrêtés à la fin (KEEP=1 : garder l'arbre et le disque).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
OUT=build/pc/linux
JET=${JET:?JET=…/jet.app (Jet construit avec EME)}
[ -f "$JET/main" ] || { echo "pas de $JET/main" >&2; exit 1; }
KS=${KS:-clearkey}
ESP=build/pc/esp
if [ "$KS" = widevine ]; then
	SO=${WIDEVINE_SO:?WIDEVINE_SO=…/libwidevinecdm.so}
	[ -f "$JET/cdmhost" ] || { echo "pas de $JET/cdmhost (sh tools/linux/build.sh, puis build-jet.sh)" >&2; exit 1; }
	PAGE=eme-widevine.html VIDEO=angel-one-144p.mp4 AUDIO=angel-one-aac.mp4 WORD=EME-WIDEVINE
	ESP=$OUT/kbuild/esp
	[ -f "$ESP/korin/kernel.elf" ] || PC_OUT=$OUT/kbuild PC_QUICK=1 sh tools/pc/build.sh >/dev/null
else
	PAGE=eme-clearkey.html VIDEO=ck-144p.mp4 AUDIO=ck-aac.mp4 WORD=EME-CLEARKEY
fi
[ -s "$OUT/media/$VIDEO" ] && [ -s "$OUT/media/$AUDIO" ] || { echo "d'abord : sh tools/linux/avcenc_test.sh (ou cdmtest.sh)" >&2; exit 1; }

T=$OUT/emeroot
rm -rf "$T"
mkdir -p "$T"
for d in build/pc/sysroot/*; do
	case $(basename "$d") in apps|devtools|python) continue ;; esac
	cp -r "$d" "$T/"
done
mkdir -p "$T/apps"
cp -r "$JET" "$T/apps/jet.app"
if [ "$KS" = widevine ]; then
	mkdir -p "$T/home/.jet/widevine"
	cp "$SO" "$T/home/.jet/widevine/libwidevinecdm.so"
fi
touch "$T/etc/web-console"
printf 'password = essai\n' >"$T/etc/remote.txt"
grep -q "/sys/svc/telnetd" "$T/etc/services.txt" || printf '# essai EME : telnet\n/sys/svc/telnetd\n' >>"$T/etc/services.txt"
MB=$(( $(du -sm "$T" | cut -f1) + 200 ))
IMG=$OUT/emedisk.img
rm -f "$IMG"
python3 -I tools/pc/mkkorindisk.py "$IMG" --layout zbook --korin-mb "$MB" --sysroot "$T" --esp "$ESP" --cfg 'default = korin
timeout = 0' >/dev/null
echo "disque : $IMG ($MB Mo pour KORIN OS)"

WWW=$OUT/eme-www
rm -rf "$WWW"
mkdir -p "$WWW"
cp "tools/webkit/tests/$PAGE" "$WWW/index.html"
cp "$OUT/media/$VIDEO" "$WWW/video.mp4"
cp "$OUT/media/$AUDIO" "$WWW/audio.mp4"
python3 -m http.server 8642 --directory "$WWW" >"$OUT/emetest-www.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT

W=$OUT/emetest
mkdir -p "$W"
[ "$KS" = widevine ] && : "${WAIT:=200}"
python3 -I - "$ROOT" "$W" "$IMG" "${WAIT:-120}" > "$W/run.txt" 2>&1 <<'PY' || true	# (QEMU coupe QMP en quittant : sans effet)
import importlib.util, os, sys, types
root, w, img, wait = sys.argv[1:5]
spec = importlib.util.spec_from_file_location("pctest", os.path.join(root, "tools", "python", "pctest.py"))
pt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pt)
pt.W, pt.IMG = os.path.abspath(w), os.path.abspath(img)
a = types.SimpleNamespace(port=2395, hostfwd=[], smp=4, mem="4G", boot=150, shot=os.path.join(w, "fin.png"), qemu="",
    cmd=["sleep:20", "( /apps/jet.app/main http://10.0.2.2:8642/index.html > /dev/null 2>&1 & ) ; echo Jet lancé",
         "sleep:" + wait, "shot:eme"])
pt.run(a)
PY

if [ -z "${KEEP:-}" ]; then
	rm -rf "$T" "$IMG" "$WWW"
fi
# (la page envoie chaque ligne de son journal au serveur de l'essai : GET /result?…)
python3 -I -c 'import sys, urllib.parse
for l in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if "/result?" in l:
        print(urllib.parse.unquote(l.split("/result?", 1)[1].split(" HTTP/", 1)[0]))' "$OUT/emetest-www.log" | tee "$W/page.txt"
echo "capture : $W/eme.png"
grep -aq "$WORD : RÉUSSI" "$W/page.txt"
