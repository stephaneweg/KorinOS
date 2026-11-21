#!/bin/bash
# rustc-mkpatch : refait tools/rust/rustc.patch depuis l'arbre de rustc-build.sh (~/korin-rustc/src, KORIN_RUSTC), après
# une retouche faite dans l'arbre (docs/RUST.md, « rustc dans Korin ») ; vérifie qu'il s'applique sur des sources neuves.
#     bash tools/rust/rustc-mkpatch.sh [fichier de plus...]
# Les fichiers : ceux que rustc.patch touche déjà (relus dans le patch), plus ceux donnés en argument ; les fichiers
# nouveaux (absents de l'archive des sources) sont pris tels quels. Après : mettre à jour le tampon de l'arbre
# (rustc-build.sh refait l'arbre quand un patch change ; sinon ce qui a été construit est gardé) :
#     cat tools/rust/{rustc,std,libc,llvm}.patch tools/rust/rustc-vendor/*.patch | sha256sum | cut -c1-16 > ~/korin-rustc/src/.korin
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${KORIN_RUSTC:-$HOME/korin-rustc}
V=1.97.1
TAR=$W/dl/rustc-$V-src.tar.xz
P=$ROOT/tools/rust/rustc.patch
FILES=$( { grep '^+++ b/' "$P" | sed 's#^+++ b/##'; for f in "$@"; do echo "$f"; done; } | sort -u)
T=$(mktemp -d)
# (une seule décompression de l'archive : ses fichiers parmi ceux du patch ; les autres sont nouveaux)
IN=$(tar tJf "$TAR" | sed "s#^rustc-$V-src/##" | grep -Fx -f <(echo "$FILES") || true)
mkdir -p "$T/src" "$T/a" "$T/b"
[ -n "$IN" ] && tar xJf "$TAR" -C "$T/src" --strip-components=1 $(for f in $IN; do echo "rustc-$V-src/$f"; done)
for f in $FILES; do
	mkdir -p "$T/a/$(dirname "$f")" "$T/b/$(dirname "$f")"
	[ -f "$T/src/$f" ] && cp "$T/src/$f" "$T/a/$f"
	cp "$W/src/$f" "$T/b/$f"
done
(cd "$T" && diff -ruN a b | sed -E 's#^--- a/([^\t]*).*#--- a/\1#; s#^\+\+\+ b/([^\t]*).*#+++ b/\1#') > "$P" || true
(cd "$T/src" && patch -p1 --dry-run -s < "$P") && echo "rustc-mkpatch : $P ($(grep -c '^+++ ' "$P") fichiers), s'applique sur des sources neuves"
rm -rf "$T"
