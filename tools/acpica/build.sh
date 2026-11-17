#!/bin/sh
# ACPICA (l'interprète d'AML d'Intel) pour le service acpid du PC (src/svc/acpid, docs/etude/BATTERIE-PC.md).
# À lancer dans WSL depuis la racine du dépôt (tools/svc/build_c.sh le lance : prebuild de svc.conf) :
#     sh tools/acpica/build.sh
#
# Produit : build/svc/x86_64/obj/libacpica.a et build/svc/x86_64/acpica/include (les en-têtes, avec
#           platform/ackorin.h).
# Sources : ~/.cache/korin-acpica (archive de la version $ACPICA_VER, empreinte vérifiée), hors du dépôt.
# Licence : ACPICA est sous double licence BSD-3-Clause ou GPL-2.0 ; on la prend sous BSD-3-Clause
#           (LICENSE.BSD-3-Clause de l'archive).
# Seulement l'interprète (dispatcher, events, executer, hardware, namespace, parser, resources, tables,
# utilities) : ni iasl, ni débogueur, ni désassembleur. Compilé comme les services (gcc de WSL,
# autonome, sans zone rouge, adresses fixes : pas de relocations), sans -Werror (code d'Intel).
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VER=${ACPICA_VER:-20260930}
SHA=aa18901b92e30749be0edc3081c8d550c61fce4fa37546fc6a65d367a4ae71a5
CACHE=${CACHE:-$HOME/.cache/korin-acpica}
OUT=${SVC_OUT:-$ROOT/build/svc}/x86_64
LIB=$OUT/obj/libacpica.a
INC=$OUT/acpica/include
TGZ=$CACHE/acpica-unix-$VER.tar.gz
SRC=$CACHE/acpica-unix-$VER/source

mkdir -p "$CACHE" "$OUT/obj"
if [ ! -f "$TGZ" ]; then
	curl -sSL -m 300 -o "$TGZ.part" \
		"https://github.com/open-acpica/acpica/releases/download/$VER/acpica-unix-$VER.tar.gz"
	mv "$TGZ.part" "$TGZ"
fi
if [ "$VER" = 20260930 ] && ! echo "$SHA  $TGZ" | sha256sum -c - >/dev/null 2>&1; then
	echo "acpica : empreinte fausse pour $TGZ" >&2
	exit 1
fi
[ -d "$SRC" ] || tar xzf "$TGZ" -C "$CACHE"

# (acenv.patch : acenv.h et acgccex.h)
# déjà à jour : la bibliothèque est plus récente que ce script, l'en-tête et le correctif
if [ -f "$LIB" ] && [ -f "$INC/platform/ackorin.h" ] && [ "$LIB" -nt "$0" ] &&
	[ "$LIB" -nt "$ROOT/tools/acpica/ackorin.h" ] && [ "$LIB" -nt "$ROOT/tools/acpica/acenv.patch" ]; then
	exit 0
fi

rm -rf "$INC"
mkdir -p "$INC"
cp -r "$SRC/include/." "$INC/"
cp "$ROOT/tools/acpica/ackorin.h" "$INC/platform/"
( cd "$INC/platform" && patch -s -p0 <"$ROOT/tools/acpica/acenv.patch" )

BLD=$CACHE/build-x86_64
rm -rf "$BLD"
mkdir -p "$BLD"
CFLAGS="-std=gnu11 -O2 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector
-fno-pie -fno-strict-aliasing -fwrapv -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone
-ffunction-sections -fdata-sections -w -D__KORIN__ -nostdinc -isystem $(gcc -print-file-name=include) -I$INC"
for d in dispatcher events executer hardware namespace parser resources tables utilities; do
	ls "$SRC/components/$d"/*.c | grep -v -e /rsdump.c
done >"$BLD/files"
# (rsdump.c : seulement pour le débogueur ; en parallèle, un échec arrête tout)
export CFLAGS BLD
xargs -P "$(nproc)" -I{} sh -c 'gcc $CFLAGS -c "$1" -o "$BLD/$(basename "$1" .c).o"' _ {} <"$BLD/files"
[ "$(ls "$BLD"/*.o | wc -l)" -eq "$(wc -l <"$BLD/files")" ] || { echo "acpica : compilation incomplète" >&2; exit 1; }
rm -f "$LIB"
ar rcs "$LIB" "$BLD"/*.o
echo "acpica $VER : $(ls "$BLD"/*.o | wc -l) fichiers -> $LIB"
