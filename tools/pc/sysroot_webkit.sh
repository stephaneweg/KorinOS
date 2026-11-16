#!/bin/sh
# Jet, le navigateur WebKit, dans l'arbre système du PC (docs/etude/PC-PILOTES.md, « Jet pour le PC »), dans WSL,
# après tools/pc/sysroot.sh et tools/pc/sysroot_apps.sh :
#     apps/jet.app/         main (WebKit2 : les process UI, web et réseau d'un même programme, JIT de JSC,
#                           WebGL par ANGLE et GlKit), app.txt, start.html, aides (help_*.md, help/), icône
#     etc/ca-bundle         les autorités de https (curl sur OpenSSL), si l'arbre ne l'a pas
#     etc/preload.txt       /apps/jet.app/main (image épinglée au démarrage, partagée par les trois process)
#     webkit/               avec TESTS=1 : jsc (bin/jsc), avplay (bin/avplay), pages et scripts d'essai (tools/webkit/tests,
#                           tools/mesa/webkit/webgl.html), clips/ (les clips de tools/webkit/mkclips.py, dans $K/clips)
# Construire d'abord (dans WSL, depuis la racine du dépôt) :
#     ARCH=x86_64 sh tools/webkit/build-deps.sh       les dépendances (sysroot ~/korin-webkit-x86_64)
#     ARCH=x86_64 sh tools/webkit/build-ui.sh && ARCH=x86_64 sh tools/webkit/build-media.sh
#     ARCH=x86_64 sh tools/mesa/build-webkit.sh       WebKit avec WebGL et le JIT (≈ 1 h sur 16 cœurs)
#     ARCH=x86_64 sh tools/webkit/build-jet.sh        -> ~/korin-webkit-x86_64/out/jet.app
#     ARCH=x86_64 INTERP=ftl sh tools/webkit/build-jsc.sh install   (jsc, pour TESTS=1)
# puis :
#     sh tools/pc/sysroot.sh && sh tools/pc/sysroot_apps.sh && sh tools/pc/sysroot_webkit.sh [arbre]
# Rien ne change pour le Pi.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

OUT=${1:-build/pc/sysroot}		# (un autre arbre : son dossier en argument)
K=${WK_OUT:-$HOME/korin-webkit-x86_64}
J=$K/out/jet.app
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : wsl sh tools/pc/sysroot.sh)"; exit 1; }
[ -x "$J/main" ] || { echo "absent : $J/main (d'abord : ARCH=x86_64 sh tools/webkit/build-jet.sh)"; exit 1; }
# un Jet du PC, pas celui du Pi
od -An -tx1 -j18 -N2 "$J/main" | grep -q '3e 00' || { echo "$J/main n'est pas un programme x86-64"; exit 1; }

k0=$(du -sk "$OUT" | cut -f1)
rm -rf "$OUT/apps/jet.app"
mkdir -p "$OUT/apps/jet.app"
cp -r "$J"/. "$OUT/apps/jet.app/"
S=src/extra/onyxports/jet
for f in "$S"/help_*.md; do [ -f "$f" ] && cp "$f" "$OUT/apps/jet.app/"; done
[ -d "$S/help" ] && cp -r "$S/help" "$OUT/apps/jet.app/"
[ -f tools/icons/rendered/jet.rgba ] && cp tools/icons/rendered/jet.rgba "$OUT/apps/jet.app/icon.rgba"
rm -f "$OUT/res/icons/jet.rgba"		# (sysroot.sh l'y met : Jet n'est pas dans src/apps)
[ -f "$OUT/etc/ca-bundle" ] || cp skel/sd0/etc/ca-bundle "$OUT/etc/"
P=$OUT/etc/preload.txt
grep -qx '/apps/jet.app/main' "$P" 2>/dev/null || echo '/apps/jet.app/main' >> "$P"

if [ "${TESTS:-0}" = 1 ]; then
	mkdir -p "$OUT/webkit"
	cp tools/webkit/tests/* "$OUT/webkit/"
	cp tools/mesa/webkit/webgl.html "$OUT/webkit/"
	[ -f "$K/out/jsc" ] && cp "$K/out/jsc" "$OUT/bin/jsc"
	[ -f "$K/out/avplay" ] && cp "$K/out/avplay" "$OUT/bin/avplay"	# (le banc des codecs : avplay --decode)
	if [ -d "$K/clips" ]; then					# (tools/webkit/mkclips.py : video.html)
		mkdir -p "$OUT/webkit/clips"
		cp "$K/clips"/* "$OUT/webkit/clips/"
	fi
fi

k=$(du -sk "$OUT" | cut -f1)
echo "$OUT : apps/jet.app ($(du -sk "$OUT/apps/jet.app" | cut -f1) Ko) ; $((k - k0)) Ko de plus, $k Ko en tout"

# les outils de développement sous /devtools (arbre unique)
sh tools/devtools_move.sh "$OUT"
