#!/bin/sh
# stage.sh -- les deux jeux en apps d'ARMPI (docs/SDL.md), dans build/sdl/sdroot/apps : pour la carte d'essai de QEMU
# (tools/sdl/testimage.py) et pour le Pi (tools/sdl/deploy.py). Les donnees de SuperTuxKart viennent de son archive
# officielle : 726 Mo. Les deux sont lies avec Mesa, SuperTux reste sur le processeur sans GPU.
#     sh tools/sdl/stage.sh       (avant : GPU=1 sh tools/sdl/build-supertux.sh)
set -eu
. "$(dirname "$0")/env.sh"
R=$A/sysroot
S=$ARMPI_ROOT/build/sdl/sdroot/apps
D=$A/data
T=$ARMPI_ROOT/tools/sdl
ST=$S/supertux.app
STK=$S/supertuxkart.app
mkdir -p "$ST" "$STK" "$D/x"

${TC}strip -o "$ST/supertux2" "$OUT/supertux/supertux2"
${TC}strip -o "$STK/supertuxkart" "$OUT/stk/bin/supertuxkart"
for g in SUPERTUX:$ST SUPERTUXKART:$STK; do
	${TC}gcc -specs="$POSIX/lib/posix.specs" -mcpu=cortex-a72 -O2 -g0 -D${g%%:*} "$T/launch.c" -o "${g#*:}/main"
	${TC}strip "${g#*:}/main"
done
printf '# Manifeste de SuperTux (docs/SDL.md)\nname     = SuperTux\ncategory = Jeux\n' > "$ST/app.txt"
printf '# Manifeste de SuperTuxKart (docs/SDL.md)\nname     = SuperTuxKart\ncategory = Jeux\n' > "$STK/app.txt"
cp "$SRC/supertux/data/images/engine/icons/supertux-256x256.png" "$ARMPI_ROOT/build/sdl/icon-st.png"
cp "$SRC/stk-code/data/supertuxkart_256.png" "$ARMPI_ROOT/build/sdl/icon-stk.png"

[ -d "$ST/data" ] || tar cf - -C "$SRC/supertux" data | tar xf - -C "$ST"
if [ ! -d "$STK/data" ]; then
	[ -f "$D/stk-src.tar.gz" ] || curl -fsL -o "$D/stk-src.tar.gz" https://github.com/supertuxkart/stk-code/releases/download/1.5/SuperTuxKart-1.5-src.tar.gz
	echo "33cf8841e4ff4082d80b9248014295bbbea61d14683e86dff100e3ab8f7b27cb  $D/stk-src.tar.gz" | sha256sum -c --quiet -
	[ -d "$D/x/SuperTuxKart-1.5-src" ] || tar xzf "$D/stk-src.tar.gz" -C "$D/x"
	tar cf - -C "$D/x/SuperTuxKart-1.5-src" data | tar xf - -C "$STK"
fi
du -sh "$ST" "$STK"
