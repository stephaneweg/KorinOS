#!/bin/sh
# fetch_data.sh -- les donnees libres de Quake (LibreQuake), hors du depot -> build/quake/sdroot/quake/id1
# build.ps1 les met ensuite sur la carte, dans SD0:/quake/id1 (docs/QUAKE.md).
#
#   sh tools/quake/fetch_data.sh [lite|full]        lite par defaut : 56 Mo, full : 116 Mo
#
# LibreQuake : BSD a trois clauses pour les modeles, les textures et les sons, GPL 2 pour progs.dat et pop.lmp ;
# ses documents de licence suivent les donnees (docs/). L'original se lit de la meme facon : copier son pak0.pak (et
# pak1.pak) dans SD0:/quake/id1 a la place de ceux-ci. Depuis la racine du depot, avec curl et unzip.
set -eu
KIND=${1:-lite}
VER=v0.09-beta
URL=https://github.com/lavenderdotpet/LibreQuake/releases/download/$VER/$KIND.zip
OUT=build/quake/sdroot/quake/id1
TMP=build/quake/lq-$KIND
mkdir -p "$TMP" "$OUT"
[ -f "$TMP/$KIND.zip" ] || curl -fL --retry 3 -o "$TMP/$KIND.zip" "$URL"
case $KIND in
	lite) echo "428e736b2f01d953e09a08c60bee975bdc4a0ac2219e97fa095c8af41754da83  $TMP/$KIND.zip" | sha256sum -c - ;;
esac
unzip -q -o "$TMP/$KIND.zip" -d "$TMP"
cp "$TMP/$KIND/id1/"*.pak "$OUT/"
cp -r "$TMP/$KIND/id1/docs" "$OUT/"
ls -la "$OUT"
