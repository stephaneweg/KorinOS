#!/bin/sh
# stage.sh : l'arbre installé de Blender (build.sh) rangé en app de Korin, pour le paquet `blender` (packages-pc.ini) :
#   build/pc/blender-pkg/apps/blender.app/main      le binaire, dépouillé (strip)
#   build/pc/blender-pkg/apps/blender.app/5.2/      datafiles, scripts, python (cherchés à côté de l'exécutable)
#   build/pc/blender-pkg/apps/blender.app/app.txt   (tools/blender/app.txt) ; license/
# puis (Windows) : python tools/pkg/pub.py blender --tree build/pc/blender-pkg [--new]
#
#   CONFIG=gui sh tools/blender/stage.sh       (dans WSL, depuis la racine du dépôt)
set -e
. "$(dirname "$0")/env.sh"
CONFIG=${CONFIG:-gui}
I=$OUT/install-$CONFIG
[ -x "$I/blender" ] || { echo "stage.sh : pas de $I/blender (CONFIG=$CONFIG sh tools/blender/build.sh)" >&2; exit 1; }
T=$ROOT/build/pc/blender-pkg
A=$T/apps/blender.app
rm -rf "$T"
mkdir -p "$A"
${TC}strip --strip-all -o "$A/main" "$I/blender"
chmod +x "$A/main"
cp -r "$I/${BLENDER_VER%.*}" "$A/"
cp -r "$I/license" "$A/" 2>/dev/null || true
cp "$BL/app.txt" "$A/app.txt"
cp "$BL/SKILL.md" "$A/SKILL.md"	# (la compétence de l'app : clé skill de app.txt, lue par Claude Code et Vibe)
# l'icône : celle de Blender pour Windows (release/windows/icons/winblender.ico, 256 x 256), au format de Korin
# (tools/craft/icon_rgba.py ; PIL)
python3 -c "
import sys
from PIL import Image
im = Image.open(sys.argv[1]); im.size = (256, 256); im.load(); im.convert('RGBA').save(sys.argv[2])
" "$BSRC/release/windows/icons/winblender.ico" "$OUT/blender-icon.png"
python3 -E -P "$ROOT/tools/craft/icon_rgba.py" "$OUT/blender-icon.png" "$A/icon.rgba"
# (pas de __pycache__ : Blender les refait au besoin)
find "$A" -name __pycache__ -type d -prune -exec rm -rf {} +
echo "stage.sh : $(find "$A" -type f | wc -l) fichiers, $(du -sh "$A" | cut -f1) dans $A"
