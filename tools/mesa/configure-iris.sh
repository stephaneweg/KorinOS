#!/bin/sh
# configure-iris : Mesa pour Korin PC (x86-64), pilote iris du GPU Intel (Tiger Lake, Gen12), en construction
# croisee par x86_64-onyx-elf (tools/pc/toolchain.sh) sur la libposix du PC (build/pc/posix). Meme montage que
# le Pi (configure.sh) : pas de cible finale de Mesa, les .a seulement (build-iris.sh), reliees en statique.
# Le Pi ne change pas : autre dossier de sortie (~/armpi-mesa/out-x86_64), autre faux libdrm
# (tools/mesa/libdrm-pc), et korin-pc.patch ne touche que des sources d'Intel (que le Pi ne compile pas).
# Avant : sh tools/mesa/fetch.sh (source, meson), sh tools/mesa/build-clc.sh (mesa_clc, vtn_bindgen2).
#     sh tools/mesa/configure-iris.sh
set -eu
. "$(dirname "$0")/env.sh"
TC=/opt/toolchains/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
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
	-Dllvm=disabled -Dshader-cache=disabled -Dzstd=disabled -Dzlib=disabled -Dexpat=disabled -Dxmlconfig=disabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
	-Dglvnd=disabled -Dtools= -Dperfetto=false \
	-Dmesa-clc=system -Dintel-elk=false -Dintel-rt=disabled -Dspirv-tools=disabled "$@"
