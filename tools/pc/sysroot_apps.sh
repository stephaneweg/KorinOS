#!/bin/sh
# Programmes C et C++ de libposix dans l'arbre système du PC (docs/etude/PC-PILOTES.md, « Chaîne
# x86-64 et apps C/C++ »), dans WSL, après tools/pc/sysroot.sh (qui refait build/pc/sysroot) :
#     bin/                  les essais de libposix (build/pc/posixtest : posixtest, posixtest-cxx,
#                           ttytest, jobtest, et à côté d'eux les modules du groupe dl), comme SD0:/bin
#                           du Pi (build.ps1)
#     apps/<nom>.app/       les apps C et C++ (build/pc/apps/sdroot, tools/pc/apps.sh), comme sur le Pi, avec leur
#                           icône (tools/icons/rendered/<nom>.rgba)
#     bin/basic, lib/*.bi   le runtime de BASIC et ses interfaces (tools/pc/apps.sh basic)
#     home/…                les fichiers d'exemple que posent les apps (préréglages d'Arabesque…)
#
#   wsl sh tools/pc/sysroot.sh && wsl sh tools/pc/sysroot_apps.sh [arbre]
# Ce qui n'est pas construit est sauté (rien n'est exigé).
set -e
cd "$(dirname "$0")/../.."

OUT=${1:-build/pc/sysroot}		# (un autre arbre : son dossier en argument)
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : wsl sh tools/pc/sysroot.sh)"; exit 1; }

n0=$(find "$OUT" -type f | wc -l)
P=build/pc/posixtest
if [ -d "$P" ]; then
	for f in "$P"/* "$P"/dltest/*; do
		[ -f "$f" ] && cp "$f" "$OUT/bin/"
	done
fi
# les essais des bibliothèques tierces (tools/pc/thirdparty : openssltest, openssltest-cxx)
for f in build/pc/thirdparty/*/sdroot/bin/*; do
	[ -f "$f" ] && cp "$f" "$OUT/bin/"
done
A=build/pc/apps/sdroot
if [ -d "$A" ]; then
	cp -r "$A"/. "$OUT/"
fi
# l'icône de chaque app (tools/icons/rendered, comme build.ps1 sur le Pi : elle part avec l'app)
for d in "$OUT"/apps/*.app; do
	f=tools/icons/rendered/$(basename "$d" .app).rgba
	[ ! -f "$f" ] || cp "$f" "$d/icon.rgba"
done
n=$(find "$OUT" -type f | wc -l)
k=$(du -sk "$OUT" | cut -f1)
echo "$OUT : $((n - n0)) fichiers de plus (libposix, apps), $n en tout, $k Ko"
