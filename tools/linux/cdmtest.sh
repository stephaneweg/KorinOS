#!/bin/sh
# cdmtest.sh -- l'essai de bout en bout de l'hôte du module Widevine sous Linux (WSL), hors de Korin
# (docs/etude/WIDEVINE.md, étape 3) : licence d'un serveur de test public, déchiffrement, décodage.
#
#   sh tools/linux/cdmtest.sh MODULE.so [-v]
#
# Le flux d'essai est la démonstration publique « Angel One » du lecteur Shaka (Google) : vidéo H.264 144p
# et audio AAC, chiffrés en « cenc » ; la licence vient de son serveur de test
# (cwip-shaka-proxy.appspot.com/no_auth). Seul le début de chaque fichier est téléchargé (1 Mo : init,
# index, le début en clair, des fragments chiffrés) dans build/pc/linux/media. L'en-tête de l'interface
# CDM (Chromium, BSD) va dans build/pc/linux/cdm. Il faut libavcodec et libssl de développement (WSL).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SO=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
OUT=$ROOT/build/pc/linux
INC=$OUT/cdm
MEDIA=$OUT/media
mkdir -p "$INC" "$MEDIA"
for f in content_decryption_module.h content_decryption_module_export.h; do
	[ -s "$INC/$f" ] || curl -sS -m 60 "https://chromium.googlesource.com/chromium/cdm/+/refs/heads/main/$f?format=TEXT" | base64 -d >"$INC/$f"
done
BASE=https://storage.googleapis.com/shaka-demo-assets/angel-one-widevine
[ -s "$MEDIA/angel-one-144p.mp4" ] || curl -sS -m 120 -r 0-999999 "$BASE/v-0144p-0100k-libx264.mp4" -o "$MEDIA/angel-one-144p.mp4"
[ -s "$MEDIA/angel-one-aac.mp4" ] || curl -sS -m 120 -r 0-999999 "$BASE/a-eng-0128k-aac-2c.mp4" -o "$MEDIA/angel-one-aac.mp4"
S=$ROOT/src/sys/cdmhost
g++ -O1 -g -Wall -std=gnu++17 -DCDMTEST_AVCODEC -I"$INC" -I"$S" $(pkg-config --cflags libavcodec) \
	"$S/cdmtest.cpp" "$S/host.cpp" "$S/cenc.cpp" -o "$OUT/cdmtest-linux" \
	-ldl -lssl -lcrypto $(pkg-config --libs libavcodec libavutil)
"$OUT/cdmtest-linux" "$SO" "$MEDIA/angel-one-144p.mp4" https://cwip-shaka-proxy.appspot.com/no_auth \
	"$MEDIA/angel-one-aac.mp4" --storage "$OUT/cdmtest-storage" "$@"
