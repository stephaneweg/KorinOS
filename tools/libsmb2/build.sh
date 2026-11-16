#!/bin/sh
# libsmb2 : la bibliothèque SMB2/3 (client et moteur de serveur) de smbfs et smbd (docs/SMB.md), sur
# newlib + libposix, par la chaîne d'Onyx. À lancer dans WSL depuis la racine du dépôt, après
# tools/posix/build.sh :
#     ARCH=x86_64 sh tools/libsmb2/build.sh     le PC  -> build/pc/thirdparty/libsmb2
#     sh tools/libsmb2/build.sh                 le Pi  -> build/libsmb2
#     NATIVE=1 sh tools/libsmb2/build.sh        Linux  -> build/native/libsmb2 (essais de smbd et smbfs dans WSL)
#
# Produit : <sortie>/include/smb2/*.h, <sortie>/lib/libsmb2.a.
# Sources : ~/.cache/korin-libsmb2/src (git, étiquette $LIBSMB2_TAG), hors du dépôt.
# Licence : LGPL 2.1 (la bibliothèque) ; Korin est libre, ses sources et ce script permettent de
# relier smbd et smbfs à une autre libsmb2.
# Sans Kerberos (NTLMv2 seulement) ; sa cryptographie (MD4, MD5, HMAC, SHA, AES) est la sienne.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TAG=${LIBSMB2_TAG:-libsmb2-6.3.2}
CACHE=${CACHE:-$HOME/.cache/korin-libsmb2}
SRC=$CACHE/src

if [ "${NATIVE:-0}" = 1 ]; then
	CC=${CC:-gcc}
	AR=${AR:-ar}
	OUT=${OUT:-$ROOT/build/native/libsmb2}
	CFLAGS=${LIBSMB2_CFLAGS:-"-O1 -g -fsanitize=address -fno-omit-frame-pointer"}
	DEFS=""
elif [ "${ARCH:-aarch64}" = x86_64 ]; then
	TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2}
	CC="$TOOL/bin/x86_64-onyx-elf-gcc -specs=$ROOT/build/pc/posix/lib/posix.specs"
	AR=$TOOL/bin/x86_64-onyx-elf-ar
	OUT=${OUT:-$ROOT/build/pc/thirdparty/libsmb2}
	CFLAGS="-O2 -g0 -ffunction-sections -fdata-sections"
	DEFS="-DKORIN"
else
	TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2}
	CC="$TOOL/bin/aarch64-onyx-elf-gcc -specs=$ROOT/build/posix/lib/posix.specs -mcpu=cortex-a72"
	AR=$TOOL/bin/aarch64-onyx-elf-ar
	OUT=${OUT:-$ROOT/build/libsmb2}
	CFLAGS="-O2 -g0 -ffunction-sections -fdata-sections"
	DEFS="-DKORIN"
fi

# ---- les sources (une étiquette fixe)
mkdir -p "$CACHE"
if [ ! -d "$SRC/.git" ]; then
	git clone -q https://github.com/sahlberg/libsmb2 "$SRC"
fi
# korin.patch : en passthrough, le serveur donne les réponses QUERY_INFO en octets bruts (smbd les
# encode toutes lui-même, docs/SMB.md)
( cd "$SRC" && git fetch -q --tags 2>/dev/null || true
  git checkout -q -- . && git -c advice.detachedHead=false checkout -q "$TAG" && git apply "$ROOT/tools/libsmb2/korin.patch" )

# ---- config.h (ce que CMake trouve sous Linux ; libposix a les mêmes en-têtes)
BLD=$CACHE/build-$(basename "$OUT")-$( [ "${NATIVE:-0}" = 1 ] && echo native || echo "${ARCH:-aarch64}")
rm -rf "$BLD"
mkdir -p "$BLD/obj" "$OUT/lib" "$OUT/include/smb2"
cat >"$BLD/config.h" <<'EOF'
/* config.h de libsmb2 pour Korin (tools/libsmb2/build.sh) */
#define HAVE_ARPA_INET_H 1
#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_POLL_H 1
#define HAVE_SOCKADDR_STORAGE 1
#define HAVE_LINGER 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UIO_H 1
#define HAVE_TIME_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_ERRNO_H 1
#define STDC_HEADERS 1
#define _U_ __attribute__((unused))
#define PACKAGE "libsmb2"
#define PACKAGE_VERSION "6.3.2"
#define VERSION "6.3.2"
EOF

SOURCES="aes.c aes128ccm.c alloc.c asn1-ber.c compat.c dcerpc.c dcerpc-lsa.c dcerpc-srvsvc.c errors.c hmac.c
hmac-md5.c init.c libsmb2.c md4c.c md5.c ntlmssp.c pdu.c sha1.c sha224-256.c sha384-512.c smb2-cmd-close.c
smb2-cmd-create.c smb2-cmd-echo.c smb2-cmd-error.c smb2-cmd-flush.c smb2-cmd-ioctl.c smb2-cmd-lock.c
smb2-cmd-logoff.c smb2-cmd-negotiate.c smb2-cmd-notify-change.c smb2-cmd-oplock-break.c
smb2-cmd-query-directory.c smb2-cmd-query-info.c smb2-cmd-read.c smb2-cmd-session-setup.c smb2-cmd-set-info.c
smb2-cmd-tree-connect.c smb2-cmd-tree-disconnect.c smb2-cmd-write.c smb2-data-file-info.c
smb2-data-filesystem-info.c smb2-data-security-descriptor.c smb2-data-reparse-point.c smb2-share-enum.c
smb3-seal.c smb2-signing.c socket.c spnego-wrapper.c sync.c timestamps.c unicode.c usha.c"

for f in $SOURCES; do
	$CC $CFLAGS $DEFS -std=gnu99 -DHAVE_CONFIG_H -D_GNU_SOURCE -w -I"$BLD" -I"$SRC/include" -I"$SRC/include/smb2" \
		-I"$SRC/lib" -c "$SRC/lib/$f" -o "$BLD/obj/${f%.c}.o"
done
rm -f "$OUT/lib/libsmb2.a"
$AR rcs "$OUT/lib/libsmb2.a" "$BLD"/obj/*.o
cp "$SRC"/include/smb2/*.h "$OUT/include/smb2/"
echo "libsmb2 ($TAG) : $OUT/lib/libsmb2.a ($(stat -c %s "$OUT/lib/libsmb2.a") octets)"
