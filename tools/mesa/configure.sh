#!/bin/sh
# configure : meson en construction croisee pour ARMPI. Pas de cible finale de Mesa : les .a seulement
# (build-libs.sh), reliees ensuite statiquement, sans dlopen.
#     sh tools/mesa/configure.sh
set -eu
. "$(dirname "$0")/env.sh"
P=$OUT/pc
Z=$OUT/zlib
mkdir -p "$P" "$Z"
ln -sf "$WK/include/zlib.h" "$WK/include/zconf.h" "$Z/"	# v3d_decoder.c, le decodeur des listes
sed -e "s|@DIR@|$ARMPI_ROOT/tools/mesa/libdrm|" -e "s|@SRC@|$SRC|" \
	"$ARMPI_ROOT/tools/mesa/libdrm/libdrm.pc.in" > "$P/libdrm.pc"
ARGS="'-specs=$POSIX/lib/posix.specs', '-mcpu=cortex-a72', '-D_GNU_SOURCE', '-ffunction-sections', '-fdata-sections', '-include', 'alloca.h', '-include', 'limits.h', '-I$Z'"
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
cpu_family = 'aarch64'
cpu = 'cortex-a72'
endian = 'little'
CROSS
rm -rf "$OUT/build"
cd "$SRC"
PKG_CONFIG_LIBDIR="$P" meson setup "$OUT/build" --cross-file "$OUT/cross.ini" \
	--buildtype=release -Db_ndebug=true --default-library=static \
	-Dplatforms= -Dgallium-drivers=v3d -Dvulkan-drivers= -Dgallium-va=disabled \
	-Dopengl=false -Dgles1=disabled -Dgles2=enabled -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dllvm=disabled -Dshader-cache=disabled -Dzstd=disabled -Dzlib=disabled -Dexpat=disabled -Dxmlconfig=disabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
	-Dglvnd=disabled -Dtools= -Dperfetto=false "$@"
