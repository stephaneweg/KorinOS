#!/bin/sh
# build.sh -- liblinux et linuxhost pour Korin PC (docs/etude/WIDEVINE.md, jalon 1), dans WSL :
#   build/pc/linux/lxtest.so      le module d'essai, construit POUR LINUX (gcc de WSL, glibc)
#   build/pc/linux/liblinux.a     le chargeur, la glibc de compatibilité, les appels de Linux
#   build/pc/linux/linuxhost      l'hôte (libposix du PC), lxtest.so embarqué (--self-test)
#
#   sh tools/linux/build.sh
#   sh tools/linux/test_pc.sh     l'essai sous QEMU (une copie de build/pc/esp, rien n'est écrit ailleurs)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2}
TC=$TOOL/bin/x86_64-onyx-elf-
POSIX=$ROOT/build/pc/posix
OUT=$ROOT/build/pc/linux
SRC=$ROOT/src/lib/linux
HOST=$ROOT/src/sys/linuxhost
[ -f "$POSIX/lib/posix.specs" ] || { echo "linux : d'abord ARCH=x86_64 sh tools/posix/build.sh" >&2; exit 1; }
mkdir -p "$OUT/obj"

# 1. le module d'essai, pour Linux : les options par défaut de la gcc d'Ubuntu (PIC, canari de pile,
#    _FORTIFY_SOURCE, endbr64), comme un vrai module
gcc -O2 -g0 -shared -fPIC -fstack-protector-all -D_FORTIFY_SOURCE=2 -Wall -Wextra \
	"$HOST/test/lxtest.c" -o "$OUT/lxtest.so"

# 2. liblinux
CFLAGS="-specs=$POSIX/lib/posix.specs -O2 -g0 -std=gnu11 -Wall -Wextra -Wno-unused-parameter"
for f in elf glibc syscall; do
	${TC}gcc $CFLAGS -c "$SRC/$f.c" -o "$OUT/obj/$f.o"
done
rm -f "$OUT/liblinux.a"
${TC}ar rcs "$OUT/liblinux.a" "$OUT/obj/elf.o" "$OUT/obj/glibc.o" "$OUT/obj/syscall.o"

# 3. linuxhost
${TC}gcc $CFLAGS -c "$HOST/main.c" -o "$OUT/obj/main.o"
${TC}gcc -c -DLXTEST_SO="\"$OUT/lxtest.so\"" "$HOST/blob.S" -o "$OUT/obj/blob.o"
${TC}gcc $CFLAGS "$OUT/obj/main.o" "$OUT/obj/blob.o" "$OUT/liblinux.a" -o "$OUT/linuxhost"
ls -l "$OUT/lxtest.so" "$OUT/liblinux.a" "$OUT/linuxhost"
