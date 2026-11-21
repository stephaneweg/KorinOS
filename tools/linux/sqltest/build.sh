#!/bin/sh
# build.sh -- sqltest (essai de SQLite et de la cohérence des fichiers sous Korin PC) -> build/sqltest/sqltest
#
# Avec la chaîne et le sysroot de Jet (tools/webkit/env.sh, ARCH=x86_64) s'ils sont là : le même libsqlite3.a que
# WebKit. Sinon (le cloud, sans WebKit) : libposix du PC (ARCH=x86_64 sh tools/posix/build.sh) et SQLite 3.50.4
# amalgamé (la version de tools/webkit/ports/sqlite, mêmes options), téléchargé dans ~/.cache/korin-sqlite.
#
#   sh tools/linux/sqltest/build.sh      (dans WSL ou le cloud, après . ~/korin-cloud/env.sh)
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
mkdir -p build/sqltest
if [ -f "$HOME/korin-webkit-x86_64/sysroot/lib/libsqlite3.a" ] && [ -z "${SQLTEST_AMALG:-}" ]; then
	ARCH=x86_64; export ARCH
	WK_ENV_SELF=tools/webkit/build-jet.sh . tools/webkit/env.sh
	S=$ONYX_SYSROOT
	${ONYX_TOOLCHAIN_PREFIX}gcc -specs="$S/lib/onyx.specs" -O2 -I"$S/include" tools/linux/sqltest/sqltest.c \
		"$S/lib/libsqlite3.a" -o build/sqltest/sqltest
else
	TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
	SPECS=$ROOT/build/pc/posix/lib/posix.specs
	[ -f "$SPECS" ] || { echo "sqltest : d'abord ARCH=x86_64 sh tools/posix/build.sh" >&2; exit 1; }
	C=$HOME/.cache/korin-sqlite
	Z=sqlite-amalgamation-3500400
	if [ ! -f "$C/$Z/sqlite3.c" ]; then
		mkdir -p "$C"
		curl -fsSL -o "$C/$Z.zip" "https://www.sqlite.org/2025/$Z.zip"
		echo "1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032  $C/$Z.zip" | sha256sum -c -
		(cd "$C" && unzip -oq "$Z.zip")
	fi
	DEFS="-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_MAX_MMAP_SIZE=0 -DHAVE_USLEEP=1
	      -DHAVE_LOCALTIME_R=1 -DHAVE_GMTIME_R=1 -DHAVE_FDATASYNC=1 -DHAVE_STRCHRNUL=0 -DSQLITE_OS_UNIX=1
	      -DSQLITE_DEFAULT_FILE_PERMISSIONS=0644 -DSQLITE_TEMP_STORE=2"
	B=build/sqltest/obj; mkdir -p "$B"
	[ "$B/sqlite3.o" -nt "$C/$Z/sqlite3.c" ] || ${TC}gcc -specs="$SPECS" -O2 $DEFS -c "$C/$Z/sqlite3.c" -o "$B/sqlite3.o"
	${TC}gcc -specs="$SPECS" -O2 -I"$C/$Z" tools/linux/sqltest/sqltest.c "$B/sqlite3.o" -o build/sqltest/sqltest
fi
ls -l build/sqltest/sqltest
