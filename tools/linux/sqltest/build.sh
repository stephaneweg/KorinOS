#!/bin/sh
# build.sh -- sqltest (essai de SQLite et de la cohérence des fichiers sous Korin PC), avec la chaîne et le sysroot de
# Jet (tools/webkit/env.sh, ARCH=x86_64) : le même libsqlite3.a que WebKit. -> build/sqltest/sqltest
#
#   sh tools/linux/sqltest/build.sh      (dans WSL ou le cloud, après . ~/korin-cloud/env.sh)
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
ARCH=x86_64; export ARCH
WK_ENV_SELF=tools/webkit/build-jet.sh . tools/webkit/env.sh
S=$ONYX_SYSROOT
mkdir -p build/sqltest
${ONYX_TOOLCHAIN_PREFIX}gcc -specs="$S/lib/onyx.specs" -O2 -I"$S/include" tools/linux/sqltest/sqltest.c "$S/lib/libsqlite3.a" \
	-o build/sqltest/sqltest
ls -l build/sqltest/sqltest
