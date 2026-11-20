#!/bin/sh
# test_spotify_app.sh -- l'app Spotify (le lecteur web de Spotify sur le moteur de Jet, src/extra/onyxports/jet/webapp.cpp)
# sous QEMU PC, dans WSL après « . ~/korin-cloud/env.sh » :
#
#   JET=…/jet.app SPOTIFY=build/pc/apps/sdroot/apps/spotify.app sh tools/linux/test_spotify_app.sh
#
# Un disque d'essai (build/pc/linux/spotifydisk.img : build/pc/sysroot sans apps/, sauf ce Jet et cette app ; telnetd,
# mot de passe d'essai « essai ») ; dans Korin, cdmupdate télécharge le module Widevine chez Google, puis
# /apps/spotify.app/main s'ouvre sur open.spotify.com ; captures dans build/pc/linux/spotifytest (spotify.png, puis
# celles de CLICKS="click:X,Y shot:nom sleep:S key:… …" : la tablette USB de QEMU, écran de 1280×800). Sans compte :
# l'identification à Spotify est faite par l'utilisateur, jamais par un essai. Le disque et l'arbre sont effacés à la
# fin (KEEP=1 : gardés).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
OUT=build/pc/linux
JET=${JET:?JET=…/jet.app}
SPOTIFY=${SPOTIFY:-build/pc/apps/sdroot/apps/spotify.app}
[ -f "$JET/main" ] && [ -f "$JET/libexec/cdmupdate" ] || { echo "pas de $JET/main ou de son libexec/cdmupdate" >&2; exit 1; }
[ -f "$SPOTIFY/main" ] || { echo "pas de $SPOTIFY/main (sh tools/pc/apps.sh spotify)" >&2; exit 1; }
ESP=$OUT/kbuild/esp
[ -f "$ESP/korin/kernel.elf" ] || ESP=build/pc/esp

T=$OUT/spotifyroot
rm -rf "$T"
mkdir -p "$T"
for d in build/pc/sysroot/*; do
	case $(basename "$d") in apps|devtools|python) continue ;; esac
	cp -r "$d" "$T/"
done
mkdir -p "$T/apps"
cp -r "$JET" "$T/apps/jet.app"
cp -r "$SPOTIFY" "$T/apps/spotify.app"
grep -qx /apps/jet.app/libexec "$T/etc/exec.txt" || echo /apps/jet.app/libexec >>"$T/etc/exec.txt"
touch "$T/etc/web-console"
printf 'password = essai\n' >"$T/etc/remote.txt"
grep -q "/sys/svc/telnetd" "$T/etc/services.txt" || printf '# essai : telnet\n/sys/svc/telnetd\n' >>"$T/etc/services.txt"
MB=$(( $(du -sm "$T" | cut -f1) + 200 ))
IMG=$OUT/spotifydisk.img
rm -f "$IMG"
python3 -I tools/pc/mkkorindisk.py "$IMG" --layout zbook --korin-mb "$MB" --sysroot "$T" --esp "$ESP" --cfg 'default = korin
timeout = 0' >/dev/null
echo "disque : $IMG ($MB Mo pour KORIN OS)"

W=$OUT/spotifytest
mkdir -p "$W"
python3 -I - "$ROOT" "$W" "$IMG" "${WAIT:-150}" "${CLICKS:-}" > "$W/run.txt" 2>&1 <<'PY' || true
import importlib.util, os, sys, types
root, w, img, wait, clicks = sys.argv[1:6]
spec = importlib.util.spec_from_file_location("pctest", os.path.join(root, "tools", "python", "pctest.py"))
pt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pt)
pt.W, pt.IMG = os.path.abspath(w), os.path.abspath(img)
a = types.SimpleNamespace(port=2399, hostfwd=[], smp=4, mem="4G", boot=150, shot=os.path.join(w, "fin.png"),
    qemu="-device usb-tablet,bus=xhci.0",
    cmd=["sleep:20", "@600 /apps/jet.app/libexec/cdmupdate ; cat /home/.jet/widevine/version.txt",
         "( /apps/spotify.app/main > /tmp/spotify.log 2>&1 & ) ; echo Spotify lancé", "sleep:" + wait, "shot:spotify"]
    + clicks.split ()
    + ["grep -a -E 'webapp|agent|sites. data|widevine|cdmhost' /tmp/spotify.log | head -n 40", "ls /home/spotify /home/spotify/webkit",
       "sysstat", "grep -a -E 'CONSOLE|FAILED|tab 1|player' /tmp/spotify.log | grep -v woff2 | head -n 100"])
pt.run(a)
PY

[ -n "${KEEP:-}" ] || rm -rf "$T" "$IMG"
echo "captures : $W ; journal : $W/run.txt"
