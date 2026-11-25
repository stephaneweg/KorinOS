#!/bin/sh
# test.sh : les programmes d'essai de tools/qt/test, liés au Qt de Korin (build.sh) -> $OUT/test/bin.
#   HOST=1 sh tools/qt/test.sh : les mêmes avec le Qt de l'hôte (host.sh), pour comparer
set -e
. "$(dirname "$0")/env.sh"
if [ -n "$HOST" ]; then
	o=$K/host-test
	cmake -S "$QTT/test" -B "$o" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$HOSTQT"
else
	o=$OUT/test
	KORIN_POSIX=$POSIX KORIN_UIKIT_LIBDIR=$UIKIT_LIBDIR KORIN_QT_DEPS=$DEVSYS/devtools/gcc cmake -S "$QTT/test" -B "$o" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$QTT/korin.cmake" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" -DQT_HOST_PATH="$HOSTQT" \
		-DQt6_DIR="$PREFIX/lib/cmake/Qt6"
fi
ninja -C "$o" -j "$JOBS"
ls -la "$o"/qt*_test 2>/dev/null
