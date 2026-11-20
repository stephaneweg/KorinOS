#!/bin/sh
# avcenc_test.sh -- le chiffrement commun (CENC) de la bibliothèque av de Jet, essayé sous Linux (WSL) sur
# le flux Clear Key public « Angel One » de Shaka (début de 1 Mo par fichier, dans build/pc/linux/media).
# Il faut libavcodec de développement (WSL). Voir avcenc_test.c.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/build/pc/linux
MEDIA=$OUT/media
AV=$ROOT/src/extra/onyx-libs/av
mkdir -p "$MEDIA" "$OUT/obj"
BASE=https://storage.googleapis.com/shaka-demo-assets/angel-one-clearkey
[ -s "$MEDIA/ck-aac.mp4" ] || curl -sS -m 120 -r 0-999999 "$BASE/a-eng-0128k-aac-2c.mp4" -o "$MEDIA/ck-aac.mp4"
[ -s "$MEDIA/ck-144p.mp4" ] || curl -sS -m 120 -r 0-999999 "$BASE/v-0144p-0100k-libx264.mp4" -o "$MEDIA/ck-144p.mp4"
# av : les conteneurs, le chiffrement ; les décodeurs d'av ne servent pas (libavcodec décode dans l'essai)
gcc -O1 -g -std=gnu99 -w -DAV_POSIX -I"$AV" -I"$ROOT/src/extra" -I"$ROOT/src/extra/onyx-libs/i/a/b" \
	"$ROOT/tools/linux/avcenc_test.c" "$AV/av_demux.c" "$AV/av_mp4.c" "$AV/av_mkv.c" "$AV/av_riff.c" \
	"$AV/av_flac.c" "$AV/av_mp3.c" "$AV/av_crypt.c" "$ROOT/tools/linux/avcenc_stubs.c" \
	$(pkg-config --cflags --libs libavcodec libavutil) -o "$OUT/avcenc_test"
"$OUT/avcenc_test" audio "$MEDIA/ck-aac.mp4" vidéo "$MEDIA/ck-144p.mp4"
