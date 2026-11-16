#!/bin/sh
# build-glkit-pc.sh -- GlKit du PC (Korin x86-64, docs/etude/PC-PILOTES.md « GPU Intel, etape 3 ») : Mesa (iris,
# softpipe) et armgl du PC en un kit partage, comme build-glkit.sh pour le Pi (meme table src/kits/glkit/glkit.abi,
# meme chargeur tools/mesa/glkit/glkit.c, ses relocations de x86-64 en plus).
#   build/pc/mesa/sdroot/lib/glkit.so   le kit (/lib/glkit.so du PC : tools/pc/sysroot.sh) ; pas dans l'initrd
#   build/pc/mesa/lib/libglkit.a        ce que les programmes du PC lient : relais, chargeur, imports
#   build/pc/mesa/sdroot/bin/gltest     gltest lie a GlKit (le gltest statique reste dans ~/armpi-mesa/out-x86_64/armgl)
# Appele par build-iris.sh a la fin, ou seul (dans WSL, depuis la racine du depot) :
#     sh tools/mesa/build-glkit-pc.sh
set -eu
. "$(dirname "$0")/env.sh"
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
POSIX=$ARMPI_ROOT/build/pc/posix
OUT=$A/out-x86_64
B=$OUT/build
G=$OUT/armgl
K=$ARMPI_ROOT/tools/mesa/glkit
W=$OUT/glkit
PM=$ARMPI_ROOT/build/pc/mesa
PCG=$ARMPI_ROOT/build/pc/gcc
[ -f "$G/libarmgl.a" ] && [ -f "$G/libs.txt" ] || { echo "build-glkit-pc.sh : d'abord sh tools/mesa/build-iris.sh" >&2; exit 1; }
LIBS=$(cat "$G/libs.txt")
mkdir -p "$W/gen" "$PM/lib" "$PM/sdroot/lib" "$PM/sdroot/bin"
CF="-fPIC -fvisibility=hidden -O2 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector"

# ce que le kit exporte : GL (gl*), armgl, armegl ; korin_i915_simu, korin_i915_describe (ce qui rend, en clair)
${TC}nm -g --defined-only "$G/libarmgl.a" $LIBS 2>/dev/null | awk '$2=="T"{print $3}' \
	| grep -E '^(gl[A-Z]|armgl_|armegl_|korin_i915_simu$|korin_i915_describe$)' | sort -u > "$W/exports"
{ echo "{ global:"; sed 's/$/;/' "$W/exports"; echo "kit_table;"; echo "local: *; };"; } > "$W/glkit.ver"
U="-u kit_table $(sed 's/^/-u /' "$W/exports" | tr '\n' ' ')"

# l'identite du kit pour le cache de shaders d'iris (korin-pc.patch, iris_disk_cache.c) : le sha1 des archives de
# Mesa et d'armgl ; un Mesa change donne un autre cache (l'ancien est ignore, puis evince par Mesa)
ID=$(cat "$G/libarmgl.a" $LIBS | sha1sum | cut -c1-40)
printf 'const char korin_glkit_id[] __attribute__((visibility("hidden"))) = "%s";\n' "$ID" > "$W/glkit_id.c"
${TC}gcc $CF -c "$W/glkit_id.c" -o "$W/glkit_id.o"

link () {	# $1 : taille, $2 : alignement de la zone par thread du kit
	${TC}gcc $CF -DGLKIT_TLS_SIZE=$1 -DGLKIT_TLS_ALIGN=$2 -c "$K/glkit.c" -o "$W/glkit.o"
	${TC}gcc -c "$K/glkit_x86_64.S" -o "$W/glkit_s.o"
	${TC}ld -shared --gc-sections -z now --hash-style=sysv -z max-page-size=0x1000 -z noexecstack \
		-T "$K/glkit_x86_64.ld" --version-script="$W/glkit.ver" $U -o "$W/glkit.elf" "$W/glkit_s.o" "$W/glkit.o" "$W/glkit_id.o" \
		--start-group "$G/libarmgl.a" $LIBS --end-group 2> "$W/link.log" \
		|| { grep -v warning "$W/link.log" | head -20; exit 1; }
	${TC}strip --strip-all -o "$W/glkit.strip" "$W/glkit.elf"
	python3 "$K/finish.py" "$W/glkit.strip" "$W/glkit.so" "$W/gen"
}
link 256 64			# (premier passage : les besoins par thread de Mesa ne sont connus qu'apres)
read TS TA < "$W/gen/tls.txt"
[ "$TS" -le 256 ] || { echo "build-glkit-pc.sh : Mesa veut $TS octets par thread, libglkit.a en a 256" >&2; exit 1; }
link "$TS" "$TA"
cp "$W/glkit.so" "$PM/sdroot/lib/glkit.so"

# libglkit.a : relais, chargeur, imports
H=$K/host
HF="-specs=$POSIX/lib/posix.specs -O2 -g0 -fno-builtin"
${TC}gcc $HF -c "$H/glkit_host.c" -o "$W/glkit_host.o"
${TC}gcc $HF -w -c "$W/gen/glkit_imports.c" -o "$W/glkit_imports.o"
${TC}gcc -c "$H/glkit_lazy_x86_64.S" -o "$W/glkit_lazy.o"
${TC}gcc -c "$W/gen/glkit_stubs.S" -o "$W/glkit_stubs.o"
rm -f "$PM/lib/libglkit.a"
${TC}ar rcsD "$PM/lib/libglkit.a" "$W/glkit_stubs.o" "$W/glkit_lazy.o" "$W/glkit_host.o" "$W/glkit_imports.o"
ls -l "$PM/sdroot/lib/glkit.so" "$PM/lib/libglkit.a"

# gltest lie a GlKit : ce que /bin/gltest du PC devient (le statique : ~/armpi-mesa/out-x86_64/armgl/gltest)
if [ -f "$G/gltest.o" ]; then
	${TC}g++ -specs="$POSIX/lib/posix.specs" -Wl,--gc-sections "$G/gltest.o" "$PM/lib/libglkit.a" \
		"$PCG/lib/libuikit.a" -o "$W/gltestk"
	${TC}strip -o "$PM/sdroot/bin/gltest" "$W/gltestk"
	ls -l "$PM/sdroot/bin/gltest"
fi
