#!/bin/sh
# fetch.sh -- refait src/extra et ce qui vient d'Onyx, depuis les depots publics, aux revisions exactes
# (docs/CONSTRUIRE.md, src/extra/README.md). git verifie chaque fichier par son empreinte : la revision
# fixee ici vaut somme de controle pour tout ce qui en sort. Depuis la racine du depot, dans WSL :
#     sh tools/extra/fetch.sh              le depot d'Onyx dans build/onyx, puis src/extra
#     sh tools/extra/fetch.sh toolchain    la chaine aarch64-onyx-elf dans /opt/toolchains
#     SANS_WEBKIT=1 sh tools/extra/fetch.sh   sans WebKit (500 Mo)
# A relancer sans crainte : ce qui est deja la est garde.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXTRA=${EXTRA:-$ROOT/src/extra}
ONYX_REPO=${ONYX_REPO:-https://github.com/stephaneweg/Onyx}
ONYX_COMMIT=7114ea1ce5c3c7521ba3144ffc72b84f3ed4607d
O=$ROOT/build/onyx

# les bancs de comparaison (tools/emu/check_*.sh, tools/audiotest) lisent aussi user/Emulators, user/Apps/koton,
# tools/qpu et third_party/ffmpeg-7.1.2 : ONYX_BANCS=1 les ajoute
LIBS="brotli-1.1.0 curl-8.16.0 dav1d-1.5.1 freetype-2.14.3 harfbuzz-14.5.1 icu-78.3 jpeg-9f
libjpeg-turbo-3.1.4 libpng-1.6.44 libvpx-1.15.2 libwebp-1.4.0 libxml2-2.13.8 mupdf-1.28.5 nghttp2-1.70.0
opus-1.5.2 skia-m154 sqlite-3.50.4 zlib-1.3.1"
PATHS="tools/toolchain tools/webkit tools/ports user/Libs/av user/Libs/gpucomp sdcard/res/soundfonts third_party/minimp3"
for l in $LIBS; do PATHS="$PATHS third_party/$l"; done
[ "${ONYX_BANCS:-0}" = 1 ] && PATHS="$PATHS user/Emulators user/Apps/koton user/Kits/audiokit kernel/include tools/qpu third_party/ffmpeg-7.1.2 docs/assets"

if [ ! -e "$O/.git" ]; then
	mkdir -p "$ROOT/build"
	echo "fetch.sh : Onyx $ONYX_COMMIT -> build/onyx"
	git clone -q --filter=blob:none --no-checkout "$ONYX_REPO" "$O"
fi
git -C "$O" sparse-checkout set --cone $PATHS
git -C "$O" -c advice.detachedHead=false checkout -q "$ONYX_COMMIT"
[ "$(git -C "$O" rev-parse HEAD)" = "$ONYX_COMMIT" ] || { echo "fetch.sh : build/onyx n'est pas a la bonne revision" >&2; exit 1; }

if [ "${1:-}" = toolchain ]; then
	exec sh "$O/tools/toolchain/fetch.sh"
fi

# une copie sans les objets de construction
copie () {	# source, destination
	[ -e "$2" ] && return 0
	mkdir -p "$2"
	(cd "$1" && tar cf - --exclude='*.o' --exclude='*.a' --exclude='*.d' .) | (cd "$2" && tar xf -)
}
for l in $LIBS; do copie "$O/third_party/$l" "$EXTRA/$l"; done
mkdir -p "$EXTRA/onyx-tools" "$EXTRA/onyx-libs/third_party" "$EXTRA/onyx-libs/i/a/b"
copie "$O/tools/ports" "$EXTRA/onyx-tools/ports"
copie "$O/tools/webkit" "$EXTRA/onyx-tools/webkit"
copie "$O/user/Libs/av" "$EXTRA/onyx-libs/av"
copie "$O/user/Libs/gpucomp" "$EXTRA/onyx-libs/gpucomp"
copie "$O/third_party/minimp3" "$EXTRA/onyx-libs/third_party/minimp3"

# WebKit : la revision d'Onyx et sa serie de patchs sur le disque de WSL (git am sur /mnt/c est tres lent), puis
# copie sans .git, avec les retouches d'ARMPI (patches/)
if [ "${SANS_WEBKIT:-0}" != 1 ] && [ ! -f "$EXTRA/webkit/Source/cmake/OptionsOnyx.cmake" ]; then
	W=${WK_SRC:-$HOME/armpi-fetch/webkit}
	WEBKIT_DIR=$W TESTS=0 SPARSE_EXTRA="Source/WebKit" sh "$O/tools/webkit/fetch.sh"
	mkdir -p "$EXTRA/webkit"
	(cd "$W" && tar cf - --exclude=.git .) | (cd "$EXTRA/webkit" && tar xf -)
	git -C "$W" log -1 --format='%H%n%h %s' > "$EXTRA/webkit/ONYX-REVISION.txt"
	for p in "$ROOT"/tools/extra/patches/webkit-*.patch; do
		patch -d "$EXTRA/webkit" -p1 --forward < "$p"
	done
fi
echo "fetch.sh : src/extra pret (FreeBASIC : tools/gcc/fbc.sh le telecharge lui-meme)"
