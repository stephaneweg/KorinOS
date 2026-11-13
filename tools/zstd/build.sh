#!/bin/sh
# zstd : la bibliothèque Zstandard (BSD, Meta), décompression seulement, pour les programmes C/C++
# de Korin (gcemu : images RVZ de Dolphin). Dans WSL, depuis la racine du dépôt :
#     sh tools/zstd/build.sh            les deux machines
#     ARCHS=x86_64 sh tools/zstd/build.sh
# Source : l'archive officielle (vérifiée par sa somme) dans ~/.cache/korin-src/zstd-dl, téléchargée
# si elle manque ; dépliée dans ~/.cache/korin-src/zstd-$V.
# Produit build/zstd/<arch>/include/{zstd.h,zstd_errors.h} et build/zstd/<arch>/lib/libzstd.a
# (lib/common + lib/decompress, sans asm, sans les anciens formats, d'un seul fil).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
V=1.5.7
SUM=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
DL=$HOME/.cache/korin-src/zstd-dl
SRC=$HOME/.cache/korin-src/zstd-$V
ARCHS=${ARCHS:-"aarch64 x86_64"}
if [ ! -f "$SRC/lib/zstd.h" ]; then
	mkdir -p "$DL"
	A=$DL/zstd-$V.tar.gz
	[ -f "$A" ] || curl -sSLf -o "$A" "https://github.com/facebook/zstd/releases/download/v$V/zstd-$V.tar.gz"
	echo "$SUM  $A" | sha256sum -c - >/dev/null || { echo "zstd : somme de l'archive fausse"; exit 1; }
	tar -xzf "$A" -C "$HOME/.cache/korin-src"
fi
for arch in $ARCHS; do
	if [ "$arch" = aarch64 ]; then
		TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
		CPU=-mcpu=cortex-a72
	else
		TC=/opt/toolchains/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
		CPU=
	fi
	O=$ROOT/build/zstd/$arch
	rm -rf "$O"
	mkdir -p "$O/obj" "$O/lib" "$O/include"
	for f in "$SRC"/lib/common/*.c "$SRC"/lib/decompress/*.c; do
		${TC}gcc $CPU -O2 -g0 -ffunction-sections -fdata-sections -DZSTD_DISABLE_ASM -DZSTD_LEGACY_SUPPORT=0 \
			-DZSTD_NO_TRACE -DZSTD_STRIP_ERROR_STRINGS=0 -I"$SRC/lib" -I"$SRC/lib/common" \
			-c "$f" -o "$O/obj/$(basename "$f" .c).o"
	done
	${TC}ar rcs "$O/lib/libzstd.a" "$O"/obj/*.o
	cp "$SRC/lib/zstd.h" "$SRC/lib/zstd_errors.h" "$O/include/"
	cp "$SRC/LICENSE" "$O/LICENSE"
	echo "zstd $V $arch : $(stat -c %s "$O/lib/libzstd.a") octets"
done
