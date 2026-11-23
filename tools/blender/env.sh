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
JPEG"

# cmake_korin <source> <obj> [options…] : configure, construit et installe dans $DEPS une dépendance CMake.
cmake_korin() {
	_s=$1; _o=$2; shift 2
	KORIN_POSIX=$POSIX KORIN_BLENDER_DEPS=$DEPS cmake -S "$_s" -B "$_o" -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$BL/korin.cmake" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
		-DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$DEPS" \
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev "$@" &&
	if [ -n "$TARGETS" ]; then	# seulement ces cibles, sans « install » (l'appelant installe)
		ninja -C "$_o" -j "$JOBS" $TARGETS
	else
		ninja -C "$_o" -j "$JOBS" && ninja -C "$_o" install
	fi
}
