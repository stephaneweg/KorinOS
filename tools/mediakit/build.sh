#!/bin/sh
# mediakit : compile la bibliotheque av d'Onyx (tools/mediakit/av), libvpx, dav1d
# (sources copiees d'Onyx dans src/extra du depot principal : EXTRA, src/extra/README.md) et
# la glue en code independant de la position, lie un kit d'ARMPI (mediakit.ld), le met en
# ET_EXEC (finish.py) : prebuilt/mediakit.so.
# A lancer dans WSL depuis la racine du depot : sh tools/mediakit/build.sh
# (chaine : /opt/toolchains/aarch64-onyx-elf-14.2, tools/toolchain/fetch.sh d'Onyx)
# Korin PC : ARCH=x86_64 sh tools/mediakit/build.sh -> build/pc/mediakit/mediakit.so (pas dans prebuilt/ :
# tools/pc/sysroot.sh le met dans /lib) ; JOBS=n (WSL : peu de memoire).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
ARCH=${ARCH:-aarch64}
case $ARCH in
aarch64) OUT=${OUT:-/tmp/armpi-mediakit}; DEST=$HERE/../../prebuilt/mediakit.so ;;
x86_64) OUT=${OUT:-/tmp/korin-mediakit-x86_64}; DEST=$ROOT/build/pc/mediakit/mediakit.so; mkdir -p "$ROOT/build/pc/mediakit" ;;
*) echo "mediakit : ARCH=aarch64 ou x86_64"; exit 1 ;;
esac
EXTRA=${EXTRA:-$(r=$(cd "$(dirname "$0")/../.." && pwd); echo "${r%%/.claude/worktrees/*}")/src/extra}
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/$ARCH-onyx-elf-14.2/bin/$ARCH-onyx-elf-
for d in libvpx-1.15.2 dav1d-1.5.1; do
	[ -d "$EXTRA/$d" ] || { echo "mediakit : $EXTRA/$d absent (src/extra/README.md)"; exit 1; }
done
mkdir -p "$OUT"
make -s -f "$HERE/Makefile" -j"${JOBS:-$(nproc)}" ARCH="$ARCH" OUT="$OUT" EXTRA="$EXTRA" TC="$TC"
${TC}readelf -lrSW "$OUT/mediakit.elf" > "$OUT/mediakit.txt"
if grep -q "\.tbss\|\.tdata" "$OUT/mediakit.txt"; then
	echo "mediakit : variables par thread (TLS) dans le kit : refuse"; exit 1
fi
python3 "$HERE/finish.py" "$OUT/mediakit.elf" "$DEST"
