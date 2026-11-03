#!/bin/sh
# build-compat.sh -- libSDL2.a : sdl2-compat tel quel sur libSDL3.a, en statique (docs/SDL.md). Les deux exportent
# les memes noms : ceux de SDL 3 deviennent SDL3R_* dans libSDL3r.a, et dlopen, dlsym, dlclose de sdl2_compat.c
# deviennent une table (compat/sdl3table.c). Puis les essais, avec et sans Mesa.
#     sh tools/sdl/build-compat.sh
set -eu
. "$(dirname "$0")/env.sh"
C=$SRC/sdl2-compat
M=$ARMPI_ROOT/tools/sdl/armpi
T=$ARMPI_ROOT/tools/sdl/test
O=$OUT/compat
mkdir -p "$O"

# les noms de SDL 3 qui entrent en conflit, en table de renommage
${TC}nm --defined-only -g "$OUT/libSDL3.a" 2>/dev/null | awk '$3 ~ /^SDL_/ { n = $3; sub(/^SDL_/, "SDL3R_", n); print $3, n }' | sort -u > "$O/rename.txt"
${TC}objcopy --redefine-syms="$O/rename.txt" "$OUT/libSDL3.a" "$OUT/libSDL3r.a"

# les noms de sdl3_syms.h, sans leurs types
grep -E '^SDL3_SYM' "$C/src/sdl3_syms.h" | sed -E 's/^SDL3_SYM_VARARGS\(([^,]*),([A-Za-z0-9_]+),.*/SDL3_SYM(x,\2,x,x,x)/; s/^SDL3_SYM_RENAMED[A-Z_]*\([^,]*,[A-Za-z0-9_]+,([A-Za-z0-9_]+),.*/SDL3_SYM(x,\1,x,x,x)/; s/^SDL3_SYM[A-Z_]*\([^,]*\**[^,]*,([A-Za-z0-9_]+),.*/SDL3_SYM(x,\1,x,x,x)/' | \
	awk 'NR == FNR { have[$1] = 1; next } { n = $0; sub(/^SDL3_SYM\(x,/, "", n); sub(/,.*/, "", n); if (have["SDL_" n]) print }' "$O/rename.txt" - > "$O/sdl3_names.h"

FLAGS="$CFLAGS_ARMPI -I$ARMPI_ROOT/tools/sdl/compat -I$C/include -I$C/src -I$SRC/SDL3/include -D__native_client__=1 -D_REENTRANT"
# (__native_client__ : seul moyen sans retouche de couper SDL_DYNAMIC_API, que sdl2_compat.c ne regarde pas ailleurs)
FLAGS="$FLAGS -DSDL2COMPAT_REVISION=\"armpi\""
${TC}gcc $CFLAGS_ARMPI -I"$O" -c "$ARMPI_ROOT/tools/sdl/compat/sdl3table.c" -o "$O/sdl3table.o"
${TC}gcc $FLAGS -Ddlopen=armpi_sdl3_open -Ddlsym=armpi_sdl3_sym -Ddlclose=armpi_sdl3_close -c "$C/src/sdl2_compat.c" -o "$O/sdl2_compat.o" 2> "$O/cc.log" || { grep -E 'error' "$O/cc.log" | head -30; exit 1; }
rm -f "$OUT/libSDL2.a"
${TC}ar rcs "$OUT/libSDL2.a" "$O/sdl2_compat.o" "$O/sdl3table.o"

# essais : sdltest sans Mesa, et les deux avec
L="-Wl,--gc-sections"
UI="$U/libuikit_c.a"
GL="-Wl,-u,armgl_init -Wl,--start-group $MESA/armgl/libarmgl.a $(cat "$MESA/armgl/libs.txt") $WK/lib/libz.a -Wl,--end-group"
${TC}gcc $CFLAGS_ARMPI -I"$SRC/SDL3/include" -c "$T/sdltest.c" -o "$O/sdltest.o"
${TC}gcc $CFLAGS_ARMPI -I"$ARMPI_ROOT/tools/sdl/compat" -I"$C/include/SDL2" -c "$T/sdl2test.c" -o "$O/sdl2test.o"
${TC}gcc -specs="$POSIX/lib/posix.specs" $L "$O/sdltest.o" "$OUT/libSDL3.a" $UI -lm -o "$O/sdltest-sw"
${TC}g++ -specs="$POSIX/lib/posix.specs" $L "$O/sdltest.o" "$OUT/libSDL3.a" $UI $GL -o "$O/sdltest"
${TC}g++ -specs="$POSIX/lib/posix.specs" $L "$O/sdl2test.o" "$OUT/libSDL2.a" "$OUT/libSDL3r.a" $UI $GL -o "$O/sdl2test"
${TC}gcc -specs="$POSIX/lib/posix.specs" $L "$O/sdl2test.o" "$OUT/libSDL2.a" "$OUT/libSDL3r.a" $UI -lm -o "$O/sdl2test-sw"
for p in sdltest sdltest-sw sdl2test sdl2test-sw; do ${TC}strip -o "$O/$p.s" "$O/$p"; done
ls -l "$O"/*.s
mkdir -p "$ARMPI_ROOT/build/sdl/sdroot/bin"
for p in sdltest sdltest-sw sdl2test sdl2test-sw; do cp "$O/$p.s" "$ARMPI_ROOT/build/sdl/sdroot/bin/$p"; done
