#!/bin/sh
# La Suite ArtCraft dans l'arbre système du PC (docs/CRAFT.md), après tools/pc/sysroot.sh (qui refait build/pc/sysroot) :
#     apps/craft.app/          le lanceur « Suite ArtCraft » (tools/craft/launcher.sh), paquet craft
#     apps/<app>.app/          chaque app Craft construite (tools/craft/all.sh), un paquet par app qui dépend de craft
#                              (tools/pkg/packages-pc.ini) ; la démo d'egui (eguidemo) n'y va pas
# Le panel et le menubar qui cachent la catégorie « Craft » sont ceux de sysroot.sh (paquet pc).
#     sh tools/pc/sysroot.sh && sh tools/pc/sysroot_craft.sh [arbre]
# Ce qui n'est pas construit est sauté.
set -e
cd "$(dirname "$0")/../.."

OUT=${1:-build/pc/sysroot}
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : sh tools/pc/sysroot.sh)"; exit 1; }
A=build/craft/x86_64/apps
[ -d "$A/craft.app" ] || { echo "Suite ArtCraft pas construite ($A : bash tools/craft/all.sh)"; exit 0; }
mkdir -p "$OUT/apps"
n=0
for d in "$A"/*.app; do
	a=$(basename "$d" .app)
	[ "$a" = eguidemo ] && continue
	[ -f "$d/main" ] && [ -f "$d/app.txt" ] || continue
	od -An -tx1 -j18 -N2 "$d/main" | grep -q '3e 00' || { echo "$d/main n'est pas un programme x86-64"; exit 1; }
	rm -rf "$OUT/apps/$a.app"
	cp -r "$d" "$OUT/apps/$a.app"
	n=$((n + 1))
done
echo "sysroot_craft : $n apps ($(du -sh "$OUT/apps/craft.app" | cut -f1) pour le lanceur)"
