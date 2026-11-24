# env.sh : versions et dossiers du port de Blender (docs/etude/BLENDER.md). Tout se construit dans WSL (ou une
# machine Linux), hors du dépôt, sous ~/korin-blender. Inclus par les autres scripts de tools/blender.
#
#   KORIN_BLENDER (défaut ~/korin-blender)
#   JOBS (défaut : nproc, au plus 4 : WSL n'a que 9 Go, et un -j12 a déjà fait tomber un WSL de 7,8 Go ; une
#   longue construction : setsid nohup … & dans WSL)
#
# Les versions et les sommes des dépendances ne sont pas recopiées ici : versions.py les lit dans
# build_files/build_environment/cmake/versions.cmake de la source de Blender, qui fait foi.
BLENDER_VER=5.2.4
BLENDER_URL=https://download.blender.org/source/blender-$BLENDER_VER.tar.xz

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BL=$ROOT/tools/blender
ARCH=x86_64			# Korin PC seulement : le Pi n'a ni Vulkan ni OpenGL de bureau
K=${KORIN_BLENDER:-$HOME/korin-blender}
DL=$K/dl			# archives téléchargées
SRC=$K/src			# sources dépaquetées (blender-$BLENDER_VER, une par dépendance)
BSRC=$SRC/blender-$BLENDER_VER
OUT=$K/$ARCH			# deps/ (préfixe commun des dépendances), obj/<nom>/, blender/
# HOST=1 : pour la machine de construction (Linux de WSL), dans $K/host : les outils que Blender lance pendant sa
# construction (datatoc, shader_tool, makesdna, makesrna : hosttools.sh) ; le compilateur et les bibliothèques
# de la distribution (zlib, png, jpeg, freetype, zstd, TBB), le reste comme pour Korin.
if [ -n "$HOST" ]; then
	OUT=$K/host
	export CXXFLAGS="${CXXFLAGS:-} -include cstdint"	# GCC 15 de l'hôte : yaml-cpp 0.8 oublie <cstdint>
fi
DEPS=$OUT/deps
OBJ=$OUT/obj
TOOL=${KORIN_TOOLCHAINS:-/opt/toolchains}/$ARCH-onyx-elf-14.2
TC=$TOOL/bin/$ARCH-onyx-elf-
POSIX=${POSIX:-$ROOT/build/pc/posix}
VKINC=${VKINC:-$ROOT/build/pc/mesa/include}	# en-têtes Vulkan de VkKit (vulkan_korin.h)
JET=${KORIN_JET_SYSROOT:-$HOME/korin-webkit-x86_64/sysroot}	# zlib, png, jpeg, webp, freetype… de Jet
n=$(nproc 2>/dev/null || echo 4)
[ "$n" -le 4 ] || n=4
JOBS=${JOBS:-$n}
mkdir -p "$DL" "$SRC" "$DEPS" "$OBJ"

# Les dépendances de classe A (étude § 3), dans l'ordre de construction : noms des variables de versions.cmake.
DEPS_A="IMATH OPENEXR DEFLATE TIFF FMT ROBINMAP EXPAT YAMLCPP PYSTRING MINIZIPNG OPENCOLORIO OPENIMAGEIO TBB
SHADERC SHADERC_GLSLANG SPIRV_HEADERS SPIRV_TOOLS SPIRV_REFLECT VULKAN_HEADERS VULKAN_MEMORY_ALLOCATOR PYTHON NUMPY
JPEG ZSTD EPOXY OPENJPH EIGEN"
CPU="-march=tigerlake -mno-avx512f -mtune=tigerlake"	# (comme korin.cmake et Jet)

# meson_korin <source> <obj> [options…] : la même chose pour une dépendance meson (fichier croisé fait ici).
meson_korin() {
	_s=$1; _o=$2; shift 2
	_x=$OUT/meson-korin.ini
	cat > "$_x" <<EOF
[binaries]
c = ['${TC}gcc', '-specs=$POSIX/lib/posix.specs']
cpp = ['${TC}g++', '-specs=$POSIX/lib/posix.specs']
ar = '${TC}ar'
strip = '${TC}strip'
pkg-config = 'pkg-config'

[built-in options]
c_args = ['-march=tigerlake', '-mno-avx512f', '-mtune=tigerlake', '-D_GNU_SOURCE', '-g0']
cpp_args = ['-march=tigerlake', '-mno-avx512f', '-mtune=tigerlake', '-D_GNU_SOURCE', '-g0']
pkg_config_path = '$DEPS/lib/pkgconfig'

[properties]
pkg_config_libdir = '$DEPS/lib/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
	rm -rf "$_o"
	if [ -n "$HOST" ]; then _xf=; else _xf="--cross-file $_x"; fi
	meson setup "$_o" "$_s" $_xf --prefix "$DEPS" --libdir lib --buildtype release \
		--default-library static "$@" &&
	ninja -C "$_o" -j "$JOBS" && ninja -C "$_o" install
}

# cmake_korin <source> <obj> [options…] : configure, construit et installe dans $DEPS une dépendance CMake.
cmake_korin() {
	_s=$1; _o=$2; shift 2
	# (l'hôte : exécutables PIE d'Ubuntu, donc des .a en -fPIC)
	if [ -n "$HOST" ]; then _tcf=-DCMAKE_POSITION_INDEPENDENT_CODE=ON; else _tcf="-DCMAKE_TOOLCHAIN_FILE=$BL/korin.cmake"; fi
	KORIN_POSIX=$POSIX KORIN_BLENDER_DEPS=$DEPS cmake -S "$_s" -B "$_o" -G Ninja \
		$_tcf -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
		-DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$DEPS" \
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev "$@" &&
	if [ -n "$TARGETS" ]; then	# seulement ces cibles, sans « install » (l'appelant installe)
		ninja -C "$_o" -j "$JOBS" $TARGETS
	else
		ninja -C "$_o" -j "$JOBS" && ninja -C "$_o" install
	fi
}
