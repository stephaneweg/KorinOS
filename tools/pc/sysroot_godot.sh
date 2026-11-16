#!/bin/sh
# Godot dans l'arbre système du PC (docs/GODOT.md), dans WSL, après tools/pc/sysroot.sh (qui refait build/pc/sysroot) :
#     apps/godot.app/main, app.txt     l'éditeur (tools/godot/build.sh)
#     lib/glkit.so                     GlKit (Mesa du PC), si l'arbre ne l'a pas encore (tools/mesa/build-iris.sh)
#   (pas home/ : SD1, que tools/pc/korin.ps1 ne touche jamais)
#     apps/godot.app/templates/        les modèles d'export de Korin PC (template_release.x86_64, template_debug.x86_64) :
#                                      à copier aussi sous Windows pour exporter un jeu (docs/GODOT.md, « Exporter »)
# Construire d'abord : sh tools/godot/fetch.sh ; sh tools/godot/build.sh [template_release ; template_debug]
#     sh tools/pc/sysroot.sh && sh tools/pc/sysroot_godot.sh [arbre]
# Ce qui n'est pas construit est sauté. Rien ne change pour le Pi.
set -e
cd "$(dirname "$0")/../.."

OUT=${1:-build/pc/sysroot}
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : wsl sh tools/pc/sysroot.sh)"; exit 1; }
G=build/pc/godot

x86() { od -An -tx1 -j18 -N2 "$1" | grep -q '3e 00' || { echo "$1 n'est pas un programme x86-64"; exit 1; }; }

if [ -f "$G/sdroot/apps/godot.app/main" ]; then
	x86 "$G/sdroot/apps/godot.app/main"
	mkdir -p "$OUT/apps/godot.app"
	cp "$G/sdroot/apps/godot.app/main" "$G/sdroot/apps/godot.app/app.txt" "$OUT/apps/godot.app/"
	# le greffon d'export (un jeu en app de Korin) : à copier dans res://addons/ d'un projet
	rm -rf "$OUT/apps/godot.app/addons"
	cp -r tools/godot/addons "$OUT/apps/godot.app/addons"
else
	echo "Godot pas construit ($G : sh tools/godot/build.sh)"
fi
if [ ! -f "$OUT/lib/glkit.so" ] && [ -f build/pc/mesa/sdroot/lib/glkit.so ]; then
	cp build/pc/mesa/sdroot/lib/glkit.so "$OUT/lib/"
fi
# (Vulkan : Forward+ et Mobile ; sans lui, Godot passe en Compatibility)
if [ ! -f "$OUT/lib/vkkit.so" ] && [ -f build/pc/mesa/sdroot/lib/vkkit.so ]; then
	cp build/pc/mesa/sdroot/lib/vkkit.so "$OUT/lib/"
fi
for t in template_release template_debug; do
	if [ -f "$G/templates/$t.x86_64" ]; then
		mkdir -p "$OUT/apps/godot.app/templates"
		cp "$G/templates/$t.x86_64" "$OUT/apps/godot.app/templates/"
	fi
done
du -sh "$OUT/apps/godot.app" 2>/dev/null || true

# les outils de développement sous /devtools (arbre unique)
sh tools/devtools_move.sh "$OUT"
