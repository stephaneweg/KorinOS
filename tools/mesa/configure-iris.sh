#!/bin/sh
# configure-iris : Mesa pour Korin PC (x86-64), pilote iris du GPU Intel (Tiger Lake, Gen12), en construction
# croisee par x86_64-onyx-elf (tools/pc/toolchain.sh) sur la libposix du PC (build/pc/posix). Meme montage que
# le Pi (configure.sh) : pas de cible finale de Mesa, les .a seulement (build-iris.sh), reliees en statique.
# Le Pi ne change pas : autre dossier de sortie (~/armpi-mesa/out-x86_64), autre faux libdrm
# (tools/mesa/libdrm-pc), et korin-pc.patch ne touche que des sources d'Intel (que le Pi ne compile pas) et, sous
# __armpi__, le type de cache par defaut de util/disk_cache.c (sans effet sur le Pi, construit sans cache).
# Cache de shaders : mode base de donnees, identite du kit par build-glkit-pc.sh (korin-pc.patch) ; dans
# HOME/.cache (MESA_SHADER_CACHE_DIR pour un autre dossier, MESA_SHADER_CACHE_DISABLE=1 pour s'en passer).
# Mesa le veut avec une compression : zlib 1.3.1 (l'archive de tools/git/build.sh, DEPS), compilee ici ; sans
# elle, Mesa sans cache.
# Avant : sh tools/mesa/fetch.sh (source, meson), sh tools/mesa/build-clc.sh (mesa_clc, vtn_bindgen2).
#     sh tools/mesa/configure-iris.sh
set -eu
. "$(dirname "$0")/env.sh"
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
POSIX=$ARMPI_ROOT/build/pc/posix
OUT=$A/out-x86_64
PATH=$A/clc/bin:$PATH
[ -x "$A/clc/bin/mesa_clc" ] || { echo "manque mesa_clc : sh tools/mesa/build-clc.sh"; exit 1; }
[ -f "$POSIX/lib/posix.specs" ] || { echo "manque la libposix du PC : ARCH=x86_64 sh tools/posix/build.sh"; exit 1; }
# les retouches du PC, par-dessus armpi.patch (fetch.sh les retire : on les remet)
PATCH=$ARMPI_ROOT/tools/mesa/korin-pc.patch
git -C "$SRC" apply --check -R "$PATCH" 2>/dev/null || git -C "$SRC" apply "$PATCH"
P=$OUT/pc
mkdir -p "$P"
sed -e "s|@DIR@|$ARMPI_ROOT/tools/mesa/libdrm-pc|" -e "s|@SRC@|$SRC|" \
	"$ARMPI_ROOT/tools/mesa/libdrm-pc/libdrm.pc.in" > "$P/libdrm.pc"
# la zlib (cache de shaders) : compilee ici en -fPIC (glkit.so est un objet partage ; les libz.a de Jet et de git
# ne le sont pas), de l'archive de tools/git/build.sh ; zlib.pc ecrit ici, l'archive notee pour build-iris.sh
DEPS=${DEPS:-/opt/src/git-deps}
[ -d "$DEPS" ] || [ ! -d "$HOME/.cache/korin-src/git-deps" ] || DEPS=$HOME/.cache/korin-src/git-deps
ZT=$DEPS/zlib_1.3.dfsg+really1.3.1.orig.tar.gz
Z=$OUT/zlib
rm -f "$P/zlib.pc" "$OUT/zlib.txt"
if [ -f "$ZT" ]; then
	rm -rf "$Z"
	mkdir -p "$Z/src" "$Z/obj" "$Z/include"
	tar xzf "$ZT" -C "$Z/src"
	for c in "$Z"/src/zlib-1.3.1/*.c; do
		${TC}gcc -specs="$POSIX/lib/posix.specs" -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -w \
			-DHAVE_UNISTD_H -DZ_HAVE_UNISTD_H -DHAVE_STDARG_H -c "$c" -o "$Z/obj/$(basename "$c" .c).o"
	done
	${TC}ar rcs "$Z/libz.a" "$Z"/obj/*.o
	cp "$Z/src/zlib-1.3.1/zlib.h" "$Z/src/zlib-1.3.1/zconf.h" "$Z/include/"
	printf 'Name: zlib\nDescription: zlib\nVersion: 1.3.1\nLibs: %s/libz.a\nCflags: -I%s/include\n' "$Z" "$Z" > "$P/zlib.pc"
	echo "$Z/libz.a" > "$OUT/zlib.txt"
	CACHE="-Dshader-cache=enabled -Dzlib=enabled"
else
	echo "configure-iris : pas d'archive zlib ($ZT) : Mesa sans cache de shaders"
	CACHE="-Dshader-cache=disabled -Dzlib=disabled"
fi
ARGS="'-specs=$POSIX/lib/posix.specs', '-D_GNU_SOURCE', '-ffunction-sections', '-fdata-sections', '-include', '$ARMPI_ROOT/tools/mesa/libdrm-pc/korin_pre.h'"
cat > "$OUT/cross.ini" <<CROSS
[binaries]
c = '${TC}gcc'
cpp = '${TC}g++'
ar = '${TC}ar'
strip = '${TC}strip'
pkg-config = 'pkg-config'

[built-in options]
c_args = [$ARGS]
cpp_args = [$ARGS, '-DLLONG_MAX=__LONG_LONG_MAX__']
c_link_args = ['-specs=$POSIX/lib/posix.specs']
cpp_link_args = ['-specs=$POSIX/lib/posix.specs']

[properties]
pkg_config_libdir = '$P'
needs_exe_wrapper = true

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
CROSS
rm -rf "$OUT/build"
cd "$SRC"
PKG_CONFIG_LIBDIR="$P" meson setup "$OUT/build" --cross-file "$OUT/cross.ini" \
	--buildtype=release -Db_ndebug=true --default-library=static \
	-Dplatforms= -Dgallium-drivers=iris,softpipe -Dvulkan-drivers= -Dgallium-va=disabled \
	-Dopengl=false -Dgles1=disabled -Dgles2=enabled -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dllvm=disabled $CACHE -Dzstd=disabled -Dexpat=disabled -Dxmlconfig=disabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
	-Dglvnd=disabled -Dtools= -Dperfetto=false \
	-Dmesa-clc=system -Dintel-elk=false -Dintel-rt=disabled -Dspirv-tools=disabled "$@"
