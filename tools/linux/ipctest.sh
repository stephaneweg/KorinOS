#!/bin/sh
# ipctest.sh -- cdmhost et son client (cdmclient) essayés sous Linux (WSL), hors de Korin
# (docs/etude/WIDEVINE.md § 3.3) : le même flux et le même serveur de licences que tools/linux/cdmtest.sh, à
# travers cdmhost. Les empreintes attendues sont celles de cdmtest.
#
#   sh tools/linux/ipctest.sh MODULE.so
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SO=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
OUT=$ROOT/build/pc/linux
INC=$OUT/cdm
MEDIA=$OUT/media
S=$ROOT/src/sys/cdmhost
[ -s "$MEDIA/angel-one-144p.mp4" ] && [ -s "$MEDIA/angel-one-aac.mp4" ] && [ -s "$INC/content_decryption_module.h" ] ||
	{ echo "d'abord : sh tools/linux/cdmtest.sh MODULE.so" >&2; exit 1; }
CF="-O1 -g -Wall -std=gnu++17 -I$INC -I$S"
g++ $CF "$S/cdmhost.cpp" "$S/host.cpp" "$S/module.cpp" "$S/proto.cpp" -o "$OUT/cdmhost-linux" -ldl
g++ $CF "$S/ipctest.cpp" "$S/cdmclient.cpp" "$S/proto.cpp" "$S/cenc.cpp" "$S/net.cpp" -o "$OUT/ipctest-linux" -lssl -lcrypto
"$OUT/ipctest-linux" "$OUT/cdmhost-linux" "$SO" "$MEDIA/angel-one-144p.mp4" https://cwip-shaka-proxy.appspot.com/no_auth \
	"$MEDIA/angel-one-aac.mp4" --storage "$OUT/ipctest-storage"
