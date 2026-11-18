#!/bin/sh
# tools/mediakit/ffmpeg.sh -- FFmpeg 7.1.2 (src/extra/ffmpeg-7.1.2, tools/extra/fetch.sh) pour MediaKit :
# LGPL-2.1-or-later (pas de --enable-gpl), décodeurs seulement, sans encodeur, sans protocole, sans
# programme, sans fil d'exécution (MediaKit décode sur un seul fil). Ce que le reste de MediaKit ne
# décode pas : H.264, H.265, AAC, MPEG-4 Part 2, MPEG-1/2, MJPEG, AC-3/E-AC-3, MP2 ; et les conteneurs
# AVI, MPEG-TS/PS, FLV (libavformat, nourri par la bibliothèque av : av_lavf.c). libswscale ramène les
# images qui ne sont pas en 4:2:0 8 bits (10 bits, 4:2:2...).
# Le code est indépendant de la position (le kit est lié à 0 et chargé n'importe où), tout symbole est
# caché (hidden.h : accès sans GOT), comme le reste du kit.
#
#   ARCH=x86_64 sh tools/mediakit/ffmpeg.sh    Korin PC : le C seul (l'assembleur x86 de FFmpeg est celui
#                                               de nasm, absent)
#   sh tools/mediakit/ffmpeg.sh                 le Pi : avec l'assembleur NEON (GAS)
# -> $OUT/lib*.a et $OUT/include (OUT : /tmp/korin-ffmpeg-<arch>) ; tools/mediakit/Makefile les lie.
# JOBS=n : WSL a peu de mémoire.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
ARCH=${ARCH:-aarch64}
EXTRA=${EXTRA:-${ROOT%%/.claude/worktrees/*}/src/extra}
SRC=$EXTRA/ffmpeg-7.1.2
OUT=${FF_OUT:-/tmp/korin-ffmpeg-$ARCH}
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/$ARCH-onyx-elf-14.2/bin/$ARCH-onyx-elf-
[ -f "$SRC/configure" ] || { echo "ffmpeg : $SRC absent (sh tools/extra/fetch.sh)"; exit 1; }
[ -f "$OUT/.done" ] && [ "$OUT/.done" -nt "$0" ] && { echo "ffmpeg : déjà construit dans $OUT"; exit 0; }
case $ARCH in
x86_64) CPU="--arch=x86_64 --disable-asm"; CF="-mno-red-zone"; SPECS=$ROOT/build/pc/posix/lib/posix.specs ;;
aarch64) CPU="--arch=aarch64 --cpu=cortex-a72"; CF="-mcpu=cortex-a72 -mno-outline-atomics"; SPECS=$ROOT/build/posix/lib/posix.specs ;;
*) echo "ffmpeg : ARCH=aarch64 ou x86_64"; exit 1 ;;
esac
# (libposix : seulement pour les essais de configure, qui lient des programmes ; le kit, lui, n'a pas de
# libc : mk_libc.c et mk_math.c donnent ce que FFmpeg appelle)
[ -f "$SPECS" ] || { echo "ffmpeg : $SPECS absent (tools/posix/build.sh)"; exit 1; }
# les correctifs de Korin (tools/mediakit/ffmpeg-patches) sur une copie dans WSL : src/extra reste la copie d'Onyx
W=${FF_SRC:-/tmp/korin-ffmpeg-src}
if [ ! -f "$W/.korin-patched" ] || [ -n "$(find "$HERE/ffmpeg-patches" -newer "$W/.korin-patched" -name '*.patch')" ]; then
	rm -rf "$W"; mkdir -p "$W"
	(cd "$SRC" && tar cf - --exclude='*.o' --exclude='*.a' .) | (cd "$W" && tar xf -)
	for p in "$HERE"/ffmpeg-patches/*.patch; do patch -s -d "$W" -p1 < "$p"; done
	touch "$W/.korin-patched"
fi
SRC=$W
DEC=h264,hevc,aac,aac_latm,mpeg4,mpeg1video,mpeg2video,mjpeg,ac3,eac3,mp2
DMX=avi,mpegts,mpegps,flv,aac,ac3,h264,hevc
PAR=h264,hevc,aac,aac_latm,mpeg4video,mpegvideo,mjpeg,ac3,mpegaudio,vorbis
mkdir -p "$OUT"
cd "$OUT"
"$SRC/configure" --enable-cross-compile --cross-prefix="$TC" --target-os=none $CPU \
	--disable-everything --disable-programs --disable-doc --disable-avdevice --disable-avfilter \
	--disable-postproc --disable-swresample --disable-network --disable-pthreads --disable-debug \
	--disable-autodetect --disable-iconv --disable-runtime-cpudetect --enable-pic \
	--enable-static --disable-shared --pkg-config=false \
	--enable-avcodec --enable-avformat --enable-avutil --enable-swscale \
	--enable-decoder=$DEC --enable-demuxer=$DMX --enable-parser=$PAR \
	--extra-cflags="$CF -fPIE -fvisibility=hidden -include $HERE/hidden.h -I$HERE/inc -ffunction-sections -fdata-sections \
-fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -D__ONYX__ -w" \
	--extra-ldflags="-specs=$SPECS" >configure.log
grep -q "^License: LGPL" configure.log || { echo "ffmpeg : la licence n'est pas la LGPL :"; grep "^License" configure.log; exit 1; }
# ce que configure a trouvé dans libposix et que le kit n'a pas (pas de libc : FFmpeg prend alors ses replis)
for h in DIRENT_H SYS_RESOURCE_H SYS_SELECT_H ACCESS CLOCK_GETTIME FCNTL GETOPT GETRUSAGE GETTIMEOFDAY GMTIME_R \
	ISATTY MKSTEMP MMAP MPROTECT NANOSLEEP POSIX_MEMALIGN SCHED_GETAFFINITY SETRLIMIT STRERROR_R SYSCONF TEMPNAM \
	USLEEP STRUCT_STAT_ST_MTIM_TV_NSEC; do
	sed -i "s/^#define HAVE_$h 1$/#define HAVE_$h 0/" config.h
	sed -i "s/^HAVE_$h=yes$/!HAVE_$h=yes/" ffbuild/config.mak
done
make -j"${JOBS:-4}" >make.log 2>&1 || { tail -30 make.log; exit 1; }
rm -rf "$OUT/inst"; make install-headers prefix="$OUT/inst" >/dev/null
rm -rf "$OUT/include"; cp -r "$OUT/inst/include" "$OUT/include"
for l in avformat avcodec swscale avutil; do cp "lib$l/lib$l.a" "$OUT/"; done
touch "$OUT/.done"
ls -l "$OUT"/lib*.a
