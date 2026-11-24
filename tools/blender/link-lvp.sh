#!/bin/sh
# link-lvp.sh : un Blender avec interface pour les essais sous QEMU, qui n'a pas de GPU Intel : la même édition de liens
# que la construction (CONFIG=gui), avec lavapipe en statique (tools/mesa/build-lvp.sh : lvp-static.txt) à la place
# de VkKit (libvkkit.a, anv). Rien n'est recompilé. Sortie : ~/korin-blender/x86_64/install-gui-lvp (l'arbre de
# install-gui, avec ce binaire), pour `pctest.py prep --tree … --lvp`.
#
#   sh tools/blender/link-lvp.sh        (dans WSL, depuis la racine du dépôt, après CONFIG=gui build.sh et build-lvp.sh)
set -e
. "$(dirname "$0")/env.sh"
B=$OUT/blender-gui
I=$OUT/install-gui
L=$OUT/install-gui-lvp
LVP=$ROOT/build/pc/mesa-lvp/lib/lvp-static.txt
[ -f "$LVP" ] || { echo "link-lvp.sh : pas de $LVP (sh tools/mesa/build-lvp.sh)" >&2; exit 1; }
[ -x "$I/blender" ] || { echo "link-lvp.sh : pas de $I/blender (CONFIG=gui sh tools/blender/build.sh)" >&2; exit 1; }
cmd=$(cd "$B" && ninja -t commands bin/blender | tail -1)
case "$cmd" in *libvkkit.a*) ;; *) echo "link-lvp.sh : libvkkit.a absente de la liaison de Blender" >&2; exit 1 ;; esac
# la sortie et VkKit remplacés (python : les arguments de lvp-static.txt contiennent des « / »)
cmd=$(python3 -c '
import sys
c, lvp, out = sys.argv[1], open(sys.argv[2]).read().strip(), sys.argv[3]
c = " ".join(lvp if a.endswith("/libvkkit.a") else a for a in c.split(" "))
c = c.replace(" -o bin/blender ", " -o " + out + " ")
print(c)' "$cmd" "$LVP" "$B/bin/blender-lvp")
(cd "$B" && sh -c "$cmd")
rm -rf "$L"
mkdir -p "$L"
cp -r "$I/${BLENDER_VER%.*}" "$L/"
cp "$B/bin/blender-lvp" "$L/blender"
ls -l "$L/blender"
