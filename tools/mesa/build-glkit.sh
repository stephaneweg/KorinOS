#!/bin/sh
# build-glkit.sh -- GlKit (docs/MESA.md « GlKit ») : Mesa et armgl en un kit partage par tous les
# programmes, au lieu d'une copie liee dans chacun.
#   prebuilt/glkit.so            le kit (SD0:/lib/glkit.so, copie par build.ps1)
#   prebuilt/mesa/lib/libglkit.a ce que les programmes lient : un relais par fonction (gl*, armgl_*),
#                                le chargeur, et ce que le kit prend a leur libc
# Depuis prebuilt/mesa (tools/mesa/prebuilt.sh) : appele par lui, ou seul :
#     sh tools/mesa/build-glkit.sh        (dans WSL, depuis la racine du depot)
set -eu
. "$(dirname "$0")/env.sh"
K=$ARMPI_ROOT/tools/mesa/glkit
P=$ARMPI_ROOT/prebuilt/mesa
W=$OUT/glkit
mkdir -p "$W/gen"
CF="-fPIC -fvisibility=hidden -O2 -mcpu=cortex-a72 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector"

# ce que le kit exporte : GL (gl*), armgl, armegl
${TC}nm -g --defined-only "$P/lib/libmesa_v3d.a" "$P/lib/libarmgl.a" 2>/dev/null | awk '$2=="T"{print $3}' \
	| grep -E '^(gl[A-Z]|armgl_|armegl_)' | sort -u > "$W/exports"
{ echo "{ global:"; sed 's/$/;/' "$W/exports"; echo "kit_table;"; echo "local: *; };"; } > "$W/glkit.ver"
U="-u kit_table $(sed 's/^/-u /' "$W/exports" | tr '\n' ' ')"

link () {	# $1 : taille, $2 : alignement de la zone par thread du kit
	${TC}gcc $CF -DGLKIT_TLS_SIZE=$1 -DGLKIT_TLS_ALIGN=$2 -c "$K/glkit.c" -o "$W/glkit.o"
	${TC}gcc -c "$K/glkit.S" -o "$W/glkit_s.o"
	${TC}ld -shared --gc-sections -z now --hash-style=sysv -T "$K/glkit.ld" --version-script="$W/glkit.ver" $U \
		-o "$W/glkit.elf" "$W/glkit_s.o" "$W/glkit.o" "$P/lib/libarmgl.a" "$P/lib/libmesa_v3d.a" "$P/lib/libz.a" \
		2> "$W/link.log" || { grep -v warning "$W/link.log" | head -20; exit 1; }
	${TC}strip --strip-all -o "$W/glkit.strip" "$W/glkit.elf"
	python3 "$K/finish.py" "$W/glkit.strip" "$W/glkit.so" "$W/gen"
}
link 256 64			# (premier passage : les besoins par thread de Mesa ne sont connus qu'apres)
read TS TA < "$W/gen/tls.txt"
[ "$TS" -le 256 ] || { echo "build-glkit.sh : Mesa veut $TS octets par thread, libglkit.a en a 256" >&2; exit 1; }
link "$TS" "$TA"
cp "$W/glkit.so" "$ARMPI_ROOT/prebuilt/glkit.so"

# libglkit.a : relais, chargeur, imports
H=$K/host
HF="-specs=$POSIX/lib/posix.specs -mcpu=cortex-a72 -O2 -g0 -fno-builtin"
${TC}gcc $HF -c "$H/glkit_host.c" -o "$W/glkit_host.o"
${TC}gcc $HF -w -c "$W/gen/glkit_imports.c" -o "$W/glkit_imports.o"
${TC}gcc -c "$H/glkit_lazy.S" -o "$W/glkit_lazy.o"
${TC}gcc -c "$W/gen/glkit_stubs.S" -o "$W/glkit_stubs.o"
rm -f "$P/lib/libglkit.a"
${TC}ar rcsD "$P/lib/libglkit.a" "$W/glkit_stubs.o" "$W/glkit_lazy.o" "$W/glkit_host.o" "$W/glkit_imports.o"
ls -l "$ARMPI_ROOT/prebuilt/glkit.so" "$P/lib/libglkit.a"

# l'essai : gltest lie a GlKit (gltestk), a comparer a gltest lie en statique (build-armgl.sh)
G=$OUT/armgl
if [ -f "$G/gltest.o" ]; then
	${TC}g++ -specs="$POSIX/lib/posix.specs" -Wl,--gc-sections "$G/gltest.o" "$P/lib/libglkit.a" -o "$W/gltestk"
	${TC}strip -o "$W/gltestk.s" "$W/gltestk"
	mkdir -p "$ARMPI_ROOT/build/mesa/sdroot/bin"
	cp "$W/gltestk.s" "$ARMPI_ROOT/build/mesa/sdroot/bin/gltestk"
	ls -l "$W/gltestk.s"
fi
