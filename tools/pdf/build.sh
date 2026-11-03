#!/bin/sh
# build.sh -- MuPDF pour le Lecteur PDF (src/apps/pdf) : build/pdf/libmupdf.a et ses en-tetes dans build/pdf/include. Hors du
# depot comme les autres dependances (src/extra/README.md) : les sources tierces sont copiees d'Onyx dans src/extra la
# premiere fois. Licence : AGPL-3.0, celle de MuPDF, donc celle de l'application (src/apps/pdf/README.md).
#
# A lancer dans WSL depuis la racine du depot, apres tools/gcc/kits.sh :  sh tools/pdf/build.sh
# Puis :  sh tools/office/build.sh pdf
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
EXTRA=${EXTRA:-$ROOT/src/extra}
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx}
OUT=$ROOT/build/pdf
JOBS=${JOBS:-$(nproc)}
for d in mupdf-1.28.5 jpeg-9f zlib-1.3.1 freetype-2.14.3; do
	[ -d "$EXTRA/$d" ] || {
		[ -d "$ONYX/third_party/$d" ] || { echo "pdf : $ONYX/third_party/$d absent"; exit 1; }
		echo "pdf : copie de $d depuis Onyx"
		mkdir -p "$EXTRA"
		(cd "$ONYX/third_party" && tar cf - --exclude='*.o' --exclude='*.a' --exclude='*.d' "$d") | (cd "$EXTRA" && tar xf -)
	}
done
mkdir -p "$OUT"
sed -E "s#/[^ ]*/build/posix#$ROOT/build/posix#g" "$ROOT/build/posix/lib/posix.specs" > "$OUT/posix.specs"
make -s -j"$JOBS" -f "$ROOT/tools/pdf/mupdf.mk" EXTRA="$EXTRA" CONF="$ROOT/tools/pdf/mu" OUT="$OUT" \
	CC="${TC}gcc" AR="${TC}ar" CFLAGS="-mcpu=cortex-a72 -specs=$OUT/posix.specs -O2 -g0 -ffunction-sections -fdata-sections"
rm -rf "$OUT/include"
mkdir -p "$OUT/include"
cp -r "$EXTRA/mupdf-1.28.5/include/mupdf" "$OUT/include/"
ls -l "$OUT/libmupdf.a"
