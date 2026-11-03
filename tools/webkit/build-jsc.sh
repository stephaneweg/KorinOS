#!/bin/sh
# build-jsc.sh -- WTF + JavaScriptCore + le shell jsc pour ARMPI (PORT=Onyx de la serie de patchs
# d'Onyx, sur libposix ; docs/WEBKIT.md). Repris de tools/webkit/build-jsc.sh d'Onyx.
#
#   sh tools/webkit/build-jsc.sh                  # C_LOOP (interpreteur C++, sans WebAssembly)
#   INTERP=llint sh tools/webkit/build-jsc.sh     # le LLInt (offlineasm ARM64) + WebAssembly (IPInt)
#   INTERP=jit sh tools/webkit/build-jsc.sh       # + le JIT de base et le DFG (pas le FTL), comme Onyx
#   sh tools/webkit/build-jsc.sh install          # + jsc depouille dans $WK_OUT/out/jsc
#
# Variables : celles d'env.sh (WK_OUT, WEBKIT_DIR, ONYX_SYSROOT, JOBS), BUILD (defaut
# $WK_OUT/jsc-<interp>), CMAKE_EXTRA. La copie de travail de WebKit est faite d'abord si absente
# (src/extra/webkit -> $WEBKIT_DIR). Environ 8 minutes sur 16 coeurs.
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
: "${INTERP:=cloop}"
: "${BUILD:=$WK_OUT/jsc-$INTERP}"
: "${CMAKE_EXTRA:=}"
unset CFLAGS CXXFLAGS LDFLAGS CC CXX	# (les drapeaux sont ceux du fichier de chaine)

sh "$ARMPI/tools/webkit/fetch.sh"
[ -f "$ONYX_SYSROOT/lib/libicuuc.a" ] || { echo "build-jsc.sh : pas d'ICU dans $ONYX_SYSROOT : sh tools/webkit/build-deps.sh" >&2; exit 1; }
for t in cmake ninja perl python3 ruby gperf; do
	command -v $t >/dev/null 2>&1 || { echo "build-jsc.sh : outil $t absent" >&2; exit 1; }
done

case $INTERP in
cloop) INTERP_FLAGS="-DENABLE_JIT=OFF -DENABLE_C_LOOP=ON -DENABLE_WEBASSEMBLY=OFF";;
llint) INTERP_FLAGS="-DENABLE_JIT=OFF -DENABLE_C_LOOP=OFF -DENABLE_WEBASSEMBLY=ON -DENABLE_WEBASSEMBLY_BBQJIT=OFF -DENABLE_WEBASSEMBLY_OMGJIT=OFF";;
jit) INTERP_FLAGS="-DENABLE_JIT=ON -DENABLE_DFG_JIT=ON -DENABLE_FTL_JIT=OFF -DENABLE_C_LOOP=OFF -DENABLE_WEBASSEMBLY=ON -DENABLE_WEBASSEMBLY_BBQJIT=OFF -DENABLE_WEBASSEMBLY_OMGJIT=OFF";;
*) echo "build-jsc.sh : INTERP vaut cloop, llint ou jit" >&2; exit 1;;
esac

if [ ! -f "$BUILD/build.ninja" ]; then
	mkdir -p "$BUILD"
	cmake -S "$WEBKIT_DIR" -B "$BUILD" -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DONYX_SYSROOT="$ONYX_SYSROOT" \
		-DPORT=Onyx -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -g0" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -g0" \
		-DENABLE_SAMPLING_PROFILER=OFF \
		-DUSE_SYSTEM_MALLOC=ON -DENABLE_REMOTE_INSPECTOR=OFF -DDEVELOPER_MODE=OFF \
		$INTERP_FLAGS $CMAKE_EXTRA
fi
start=$(date +%s)
cmake --build "$BUILD" --target jsc -- -j"$JOBS"
echo "build-jsc.sh : jsc construit en $(( $(date +%s) - start )) s : $BUILD/bin/jsc"

if [ "${1:-}" = install ]; then
	mkdir -p "$WK_OUT/out"
	${STRIP} -o "$WK_OUT/out/jsc" "$BUILD/bin/jsc"
	ls -l "$WK_OUT/out/jsc"
fi
