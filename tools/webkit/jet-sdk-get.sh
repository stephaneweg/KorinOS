#!/bin/bash
# jet-sdk-get.sh [VERSION] [DOSSIER] : installe le SDK de Jet (tools/webkit/jet-sdk/README.md) depuis la Release
# « packages » du dépôt (parts jet-sdk-<version>-<n>.zip). Dans Korin : bash jet-sdk-get.sh 1.4.8
# DOSSIER : /devtools/jet-sdk par défaut ; ailleurs (conseillé : /home/<vous>/jet-sdk, en ext4 : le SDK a des
# dossiers de milliers de fichiers, le FAT32 du volume système les sert mal), ses chemins sont réécrits
# (tools/webkit/local/relocate-sdk.py, ou la copie à côté de ce script) et jetbuild, jetlink le trouvent par
# JETSDK=DOSSIER.
# Besoins : le paquet gcc (pkg add gcc), python (pkg add python : le téléchargement en HTTPS, la relocalisation),
# ≈ 1,5 Go libres.
set -e
VER=${1:-1.4.8}
DEST=${2:-/devtools/jet-sdk}
URL=https://github.com/stephaneweg/KorinOS/releases/download/packages
DL=${TMPDIR:-$HOME}/jet-sdk-dl
mkdir -p "$DL" "$DEST"
# (le wget de /bin n'a pas HTTPS : Python, sinon wget, sinon curl)
get () {
	if command -v python3 >/dev/null; then
		python3 -c 'import sys, urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])' "$1" "$2" 2>/dev/null
	elif command -v curl >/dev/null; then
		curl -fsSL -o "$2" "$1"
	else
		wget -q -O "$2" "$1"
	fi
}
n=1
while :; do
	f=jet-sdk-$VER-$n.zip
	if [ ! -s "$DL/$f" ]; then
		echo "jet-sdk : $f"
		get "$URL/$f" "$DL/$f" || { rm -f "$DL/$f"; break; }
	fi
	unzip -o -q "$DL/$f" -d "$DEST"
	n=$((n + 1))
done
[ $n -gt 1 ] || { echo "jet-sdk : rien téléchargé ($URL/jet-sdk-$VER-1.zip)" >&2; exit 1; }
chmod +x "$DEST/jetbuild" "$DEST/jetlink" 2>/dev/null || true
if [ "$DEST" != /devtools/jet-sdk ]; then
	R=$(dirname "$0")/local/relocate-sdk.py
	[ -f "$R" ] || R=$(dirname "$0")/relocate-sdk.py
	[ -f "$R" ] || R=$DEST/relocate-sdk.py
	python3 -I "$R" /devtools/jet-sdk "$DEST"
	echo "jet-sdk : dans $DEST : export JETSDK=$DEST avant jetbuild et jetlink"
fi
echo "jet-sdk $(cat "$DEST/VERSION") dans $DEST ($((n - 1)) parts) ; voir $DEST/README.md"
echo "les archives téléchargées restent dans $DL (à effacer)"
