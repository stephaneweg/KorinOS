#!/bin/sh
# dotnet : CoreCLR de .NET 10 pour ARMPI (docs/FEUILLE-DE-ROUTE.md, section .NET). Construit dans WSL
# avec la chaine d'Onyx, hors du selfhost. Sources et resultats hors du depot :
#     ~/armpi-dotnet/runtime      dotnet/runtime, etiquette v10.0.14
#     ~/armpi-dotnet/obj/coreclr  l'arbre de construction (le PC : obj/coreclr-x64)
# Le runtime vient de v10.0.14, avec tools/dotnet/armpi.patch ; ICU vient de tools/webkit/build-deps.sh.
# A lancer depuis la racine du depot, apres sh tools/posix/build.sh et sh tools/openssl/build.sh :
#     sh tools/dotnet/build.sh               configure, puis compile
#     CONFIG_ONLY=1 sh tools/dotnet/build.sh configure seulement
#     ARCH=x86_64 sh tools/dotnet/build.sh   Korin PC (x86-64) : chaine x86_64-onyx-elf, libposix de
#                                            build/pc/posix, OpenSSL de build/pc/thirdparty, ICU du sysroot
#                                            de Jet (~/korin-webkit-x86_64) ; docs/DOTNET.md, « Le PC »
set -eu
ARMPI_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARMPI_ARCH=${ARCH:-aarch64}
export ARMPI_ROOT ARMPI_ARCH
W=$HOME/armpi-dotnet
R=$W/runtime
case $ARMPI_ARCH in
aarch64)
	O=$W/obj/coreclr
	L=$W/lib
	ICU=$W/icu		# (seulement unicode/ d'ICU : le reste du sysroot de WebKit cacherait libposix)
	WKS=$HOME/armpi-webkit-build/sysroot
	CLRARCH=arm64
	TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
	SSL=$ARMPI_ROOT/build/openssl
	AOPT=
	;;
x86_64)
	O=$W/obj/coreclr-x64
	L=$W/lib-x64
	ICU=$W/icu-x64
	WKS=$HOME/korin-webkit-x86_64/sysroot
	CLRARCH=x64
	TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
	SSL=$ARMPI_ROOT/build/pc/thirdparty/openssl
	AOBJ=$W/obj/artifacts-x64	# (libunwind et ses en-tetes generes : pas ceux du Pi, runtime/artifacts/obj)
	AOPT=-DCLR_ARTIFACTS_OBJ_DIR=$AOBJ
	;;
*) echo "build.sh : ARCH=aarch64 ou x86_64"; exit 1 ;;
esac
mkdir -p "$ICU/include" "$ICU/lib"
ln -sf "$WKS/include/unicode" "$ICU/include/unicode"
ln -sf "$WKS"/lib/libicu*.a "$ICU/lib/"
export TARGET_BUILD_ARCH=$CLRARCH ROOTFS_DIR=/nonexistent
export PATH=$(dirname "$TC"):$PATH
if [ ! -d "$R/.git" ]; then
	git clone -q --depth 1 --branch v10.0.14 https://github.com/dotnet/runtime "$R"
	(cd "$R" && git apply "$ARMPI_ROOT/tools/dotnet/armpi.patch")
fi
mkdir -p "$O" "$L"
# (artifacts/obj : un clone neuf ne l'a pas, copy_version_files.sh y copie)
[ -f "$R/artifacts/obj/_version.c" ] || { mkdir -p "$R/artifacts/obj" && (cd "$R" && bash eng/native/version/copy_version_files.sh); }
if [ -n "$AOPT" ]; then
	mkdir -p "$AOBJ"
	for f in "$R"/artifacts/obj/*.c "$R"/artifacts/obj/*.h; do [ -f "$AOBJ/${f##*/}" ] || cp "$f" "$AOBJ/"; done
fi
for l in pthread dl rt gcc_s; do
	[ -f "$L/lib$l.a" ] || ${TC}ar rcs "$L/lib$l.a"
done
cmake --no-warn-unused-cli -G Ninja -DCMAKE_BUILD_TYPE=RELEASE \
	-C "$R/eng/native/tryrun.cmake" \
	-DCMAKE_TOOLCHAIN_FILE="$ARMPI_ROOT/tools/dotnet/toolchain.cmake" \
	-DCLR_CMAKE_TARGET_ARCH=$CLRARCH -DCLR_CMAKE_TARGET_OS=linux -DCLR_CMAKE_RUNTIME_CORECLR=1 \
	-DCLI_CMAKE_FALLBACK_OS=linux -DCLR_CMAKE_TARGET_ARMPI=1 $AOPT \
	-DCMAKE_ICU_DIR="$ICU" -DCLR_CMAKE_ICU_DIR="$ICU" \
	-DFEATURE_EVENTSOURCE_XPLAT=0 -DFEATURE_DBGIPC=0 \
	-DOPENSSL_INCLUDE_DIR="$SSL/include" \
	-DOPENSSL_CRYPTO_LIBRARY="$SSL/lib/libcrypto.a" \
	-DOPENSSL_SSL_LIBRARY="$SSL/lib/libssl.a" \
	-S "$R/src/coreclr" -B "$O"
[ "${CONFIG_ONLY:-0}" = 1 ] && exit 0
rm -f "$O/Corehost.Static/singlefilehost"	# (libposix passe par posix.specs : ninja ne la voit pas changer)
ninja -C "$O" Corehost.Static/singlefilehost "$@"
