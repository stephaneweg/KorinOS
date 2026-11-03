#!/bin/sh
# mediakit : compile la bibliotheque av d'Onyx (tools/mediakit/av), libvpx, dav1d
# (sources copiees d'Onyx dans src/extra du depot principal : EXTRA, src/extra/README.md) et
# la glue en code independant de la position, lie un kit d'ARMPI (mediakit.ld), le met en
# ET_EXEC (finish.py) : prebuilt/mediakit.so.
# A lancer dans WSL depuis la racine du depot : sh tools/mediakit/build.sh
# (chaine : /opt/toolchains/aarch64-onyx-elf-14.2, tools/toolchain/fetch.sh d'Onyx)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/armpi-mediakit}
EXTRA=${EXTRA:-$(r=$(cd "$(dirname "$0")/../.." && pwd); echo "${r%%/.claude/worktrees/*}")/src/extra}
TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
for d in libvpx-1.15.2 dav1d-1.5.1; do
	[ -d "$EXTRA/$d" ] || { echo "mediakit : $EXTRA/$d absent (src/extra/README.md)"; exit 1; }
done
mkdir -p "$OUT"
make -s -f "$HERE/Makefile" -j"$(nproc)" OUT="$OUT" EXTRA="$EXTRA" TC="$TC"
${TC}readelf -lrSW "$OUT/mediakit.elf" > "$OUT/mediakit.txt"
if grep -q "\.tbss\|\.tdata" "$OUT/mediakit.txt"; then
	echo "mediakit : variables par thread (TLS) dans le kit : refuse"; exit 1
fi
python3 "$HERE/finish.py" "$OUT/mediakit.elf" "$HERE/../../prebuilt/mediakit.so"
