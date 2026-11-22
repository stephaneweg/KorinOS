#!/bin/bash
# jet-sdk-get.sh [VERSION] : installe le SDK de Jet (tools/webkit/jet-sdk/README.md) dans /devtools/jet-sdk, depuis la
# Release « packages » du dépôt (parts jet-sdk-<version>-<n>.zip). Dans Korin : sudo bash jet-sdk-get.sh
# Besoins : le paquet gcc (pkg add gcc), ≈ 1,5 Go libres.
set -e
VER=${1:-1.4.6}
URL=https://github.com/stephaneweg/KorinOS/releases/download/packages
DL=${TMPDIR:-$HOME}/jet-sdk-dl
mkdir -p "$DL" /devtools/jet-sdk
n=1
while :; do
	f=jet-sdk-$VER-$n.zip
	if [ ! -s "$DL/$f" ]; then
		echo "jet-sdk : $f"
		wget -q -O "$DL/$f" "$URL/$f" || { rm -f "$DL/$f"; break; }
	fi
	unzip -o -q "$DL/$f" -d /devtools/jet-sdk
	n=$((n + 1))
done
[ $n -gt 1 ] || { echo "jet-sdk : rien téléchargé ($URL/jet-sdk-$VER-1.zip)" >&2; exit 1; }
chmod +x /devtools/jet-sdk/jetbuild /devtools/jet-sdk/jetlink 2>/dev/null || true
echo "jet-sdk $(cat /devtools/jet-sdk/VERSION) dans /devtools/jet-sdk ($((n - 1)) parts) ; voir /devtools/jet-sdk/README.md"
echo "les archives téléchargées restent dans $DL (à effacer)"
