#!/bin/sh
# dotnet : CoreCLR de .NET 10 pour ARMPI (docs/FEUILLE-DE-ROUTE.md, section .NET). Construit dans WSL
# avec la chaine d'Onyx, hors du selfhost. Sources et resultats hors du depot :
#     ~/armpi-dotnet/runtime      dotnet/runtime, etiquette v10.0.14
#     ~/armpi-dotnet/obj/coreclr  l'arbre de construction
# Le runtime vient de v10.0.14, avec tools/dotnet/armpi.patch ; ICU vient de tools/webkit/build-deps.sh.
# A lancer depuis la racine du depot, apres sh tools/posix/build.sh et sh tools/openssl/build.sh :
#     sh tools/dotnet/build.sh               configure, puis compile
#     CONFIG_ONLY=1 sh tools/dotnet/build.sh configure seulement
set -eu
ARMPI_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export ARMPI_ROOT
W=$HOME/armpi-dotnet
R=$W/runtime
O=$W/obj/coreclr
ICU=$W/icu			# (seulement unicode/ d'ICU : le reste du sysroot de WebKit cacherait libposix)
mkdir -p "$ICU/include" "$ICU/lib"
ln -sf "$HOME/armpi-webkit-build/sysroot/include/unicode" "$ICU/include/unicode"
ln -sf "$HOME"/armpi-webkit-build/sysroot/lib/libicu*.a "$ICU/lib/"
export TARGET_BUILD_ARCH=arm64 ROOTFS_DIR=/nonexistent
export PATH=/opt/toolchains/aarch64-onyx-elf-14.2/bin:$PATH
if [ ! -d "$R/.git" ]; then
	git clone -q --depth 1 --branch v10.0.14 https://github.com/dotnet/runtime "$R"
	(cd "$R" && git apply "$ARMPI_ROOT/tools/dotnet/armpi.patch")
fi
mkdir -p "$O" "$W/lib"
[ -f "$R/artifacts/obj/_version.c" ] || (cd "$R" && bash eng/native/version/copy_version_files.sh)
for l in pthread dl rt gcc_s; do
	[ -f "$W/lib/lib$l.a" ] || aarch64-onyx-elf-ar rcs "$W/lib/lib$l.a"
done
cmake --no-warn-unused-cli -G Ninja -DCMAKE_BUILD_TYPE=RELEASE \
	-C "$R/eng/native/tryrun.cmake" \
	-DCMAKE_TOOLCHAIN_FILE="$ARMPI_ROOT/tools/dotnet/toolchain.cmake" \
	-DCLR_CMAKE_TARGET_ARCH=arm64 -DCLR_CMAKE_TARGET_OS=linux -DCLR_CMAKE_RUNTIME_CORECLR=1 \
	-DCLI_CMAKE_FALLBACK_OS=linux -DCLR_CMAKE_TARGET_ARMPI=1 \
	-DCMAKE_ICU_DIR="$ICU" -DCLR_CMAKE_ICU_DIR="$ICU" \
	-DFEATURE_EVENTSOURCE_XPLAT=0 -DFEATURE_DBGIPC=0 \
	-DOPENSSL_INCLUDE_DIR="$ARMPI_ROOT/build/openssl/include" \
	-DOPENSSL_CRYPTO_LIBRARY="$ARMPI_ROOT/build/openssl/lib/libcrypto.a" \
	-DOPENSSL_SSL_LIBRARY="$ARMPI_ROOT/build/openssl/lib/libssl.a" \
	-S "$R/src/coreclr" -B "$O"
[ "${CONFIG_ONLY:-0}" = 1 ] && exit 0
rm -f "$O/Corehost.Static/singlefilehost"	# (libposix passe par posix.specs : ninja ne la voit pas changer)
ninja -C "$O" Corehost.Static/singlefilehost "$@"
