#!/bin/sh
# fetch.sh : télécharge et dépaquette les modules de Qt (env.sh : QT_MODULES), sommes SHA-256 de download.qt.io.
set -e
. "$(dirname "$0")/env.sh"
for m in $QT_MODULES ${QT_HOSTONLY:-}; do
	a=$m-everywhere-src-$QT_VER.tar.xz
	[ -f "$DL/$a" ] || curl -sSfL -o "$DL/$a" "$QT_URL/$a"
	[ -f "$DL/$a.sha256" ] || curl -sSfL -o "$DL/$a.sha256" "$QT_URL/$a.sha256"
	(cd "$DL" && sha256sum -c "$a.sha256")
	[ -d "$(qt_src $m)" ] || tar xf "$DL/$a" -C "$SRC" --no-same-owner
done
