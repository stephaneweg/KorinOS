#!/bin/sh
# test_webapp.sh -- une app web (Spotify, Netflix : un site sur le moteur de Jet, src/extra/onyxports/jet/webapp.cpp)
# sous QEMU PC, dans WSL après « . ~/korin-cloud/env.sh » :
#
#   JET=…/jet.app [APP=spotify|netflix] [APPDIR=build/pc/apps/sdroot/apps/<app>.app] sh tools/linux/test_webapp.sh
#
# Un disque d'essai (build/pc/linux/webappdisk.img : build/pc/sysroot sans apps/, sauf ce Jet et cette app ; telnetd,
# mot de passe d'essai « essai ») ; dans Korin, cdmupdate télécharge le module Widevine chez Google, puis
# /apps/<app>.app/main s'ouvre sur son site ; captures dans build/pc/linux/webapptest (<app>.png, puis
# celles de CLICKS="click:X,Y shot:nom sleep:S key:… …" : la tablette USB de QEMU, écran de 1280×800). Sans compte :
# l'identification au service est faite par l'utilisateur, jamais par un essai. Le disque et l'arbre sont effacés à la
# fin (KEEP=1 : gardés).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
OUT=build/pc/linux
JET=${JET:?JET=…/jet.app}
APP=${APP:-spotify}
APPDIR=${APPDIR:-build/pc/apps/sdroot/apps/$APP.app}
[ -f "$JET/main" ] && [ -f "$JET/libexec/cdmupdate" ] || { echo "pas de $JET/main ou de son libexec/cdmupdate" >&2; exit 1; }
[ -f "$APPDIR/main" ] || { echo "pas de $APPDIR/main (sh tools/pc/apps.sh $APP)" >&2; exit 1; }
ESP=$OUT/kbuild/esp
[ -f "$ESP/korin/kernel.elf" ] || ESP=build/pc/esp

T=$OUT/webapproot
rm -rf "$T"
mkdir -p "$T"
for d in build/pc/sysroot/*; do
	case $(basename "$d") in apps|devtools|python) continue ;; esac
	cp -r "$d" "$T/"
done
mkdir -p "$T/apps"
cp -r "$JET" "$T/apps/jet.app"
cp -r "$APPDIR" "$T/apps/$APP.app"
[ ! -f "tools/icons/rendered/$APP.rgba" ] || cp "tools/icons/rendered/$APP.rgba" "$T/apps/$APP.app/icon.rgba"
grep -qx /apps/jet.app/libexec "$T/etc/exec.txt" || echo /apps/jet.app/libexec >>"$T/etc/exec.txt"
touch "$T/etc/web-console"
# (PROBE=1 : chaque page écrit ses cookies lisibles dans la console, 5 s après son début)
[ -z "${PROBE:-}" ] || printf "setTimeout(function(){console.log('PROBE-COOKIES ['+location.host+'] '+document.cookie)},5000);
" >"$T/etc/web-probe.js"
printf 'password = essai\n' >"$T/etc/remote.txt"
grep -q "/sys/svc/telnetd" "$T/etc/services.txt" || printf '# essai : telnet\n/sys/svc/telnetd\n' >>"$T/etc/services.txt"
MB=$(( $(du -sm "$T" | cut -f1) + 200 ))
IMG=$OUT/webappdisk.img
rm -f "$IMG"
python3 -I tools/pc/mkkorindisk.py "$IMG" --layout zbook --korin-mb "$MB" --sysroot "$T" --esp "$ESP" --cfg 'default = korin
timeout = 0' >/dev/null
echo "disque : $IMG ($MB Mo pour KORIN OS)"

W=$OUT/webapptest
mkdir -p "$W"
python3 -I - "$ROOT" "$W" "$IMG" "${WAIT:-150}" "${CLICKS:-}" "$APP" > "$W/run.txt" 2>&1 <<'PY' || true
import importlib.util, os, sys, types
root, w, img, wait, clicks, app = sys.argv[1:7]
url = os.environ.get ("URL", "")	# (une adresse donnée à l'app : APP=jet URL=… pour comparer avec Jet)
token = os.environ.get ("TOKEN", app.replace ("plus", "")[:5])	# (un mot des cookies attendus)
spec = importlib.util.spec_from_file_location("pctest", os.path.join(root, "tools", "python", "pctest.py"))
pt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pt)
pt.W, pt.IMG = os.path.abspath(w), os.path.abspath(img)
a = types.SimpleNamespace(port=int (os.environ.get ("PORT", "2399")),	# (PORT : telnet ; QMP = PORT + 1000)
    hostfwd=[], smp=4, mem="4G", boot=150, shot=os.path.join(w, "fin.png"),
    qemu="-device usb-tablet,bus=xhci.0",
    cmd=["sleep:20", "@600 /apps/jet.app/libexec/cdmupdate ; cat /home/.jet/widevine/version.txt",
         "( /apps/%s.app/main '%s' > /tmp/app.log 2>&1 & ) ; echo lancé" % (app, url), "sleep:" + wait, "shot:" + app]
    + clicks.split ()
    # SESSION=1 : quitter l'app (Ctrl+Q), voir ce qui reste et la base des cookies, la relancer : la session est-elle gardée ?
    + (["key:ctrl-q", "sleep:20", "sysstat", "ls -l /home/%s/webkit" % app,
        "grep -a -o -E '[a-z0-9-]+[.](com|net|fr|org|co)' /home/%s/webkit/cookies.db | sort -u | head -n 30" % app, "grep -a -c %s /home/%s/webkit/cookies.db" % (token, app),
        "( /apps/%s.app/main '%s' > /tmp/app2.log 2>&1 & ) ; echo relancé" % (app, url), "sleep:" + wait, "shot:%s-2" % app,
        "sysstat", "ls -l /home/%s/webkit" % app, "grep -a -i -E 'cookie|sqlite|database' /tmp/app2.log | head -n 20"]
       if os.environ.get ("SESSION") else [])
    + ["grep -a -E 'webapp|agent|sites. data|widevine|cdmhost' /tmp/app.log | head -n 40", "ls /home/%s /home/%s/webkit" % (app, app),
       "sysstat", "grep -a -E 'CONSOLE|FAILED|tab 1|player|PROBE' /tmp/app.log | grep -v woff2 | head -n 100"])
pt.run(a)
PY

[ -n "${KEEP:-}" ] || rm -rf "$T" "$IMG"
echo "captures : $W ; journal : $W/run.txt"
