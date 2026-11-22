#!/bin/bash
# build-jpeg.sh -- libjpeg-turbo 3.1.4 avec son SIMD x86-64 (SSE2 et AVX2, choisis au lancement par jsimd.c), dans Korin,
# sans CMake : la liste des sources de CMakeLists.txt (JPEG_SOURCES, arithmétique en codage et décodage, comme
# tools/webkit/ports/libjpeg-turbo/build.sh), les .asm par NASM (~/jet-build/bin/nasm, construit ici),
# -> ~/jet-build/build/jpeg/libjpeg.a, à mettre à la place de /devtools/jet-sdk/sysroot/lib/libjpeg.a.
set -e
S=${SRC:-$HOME/jet-build/src/libjpeg-turbo-3.1.4}
B=${OUT:-$HOME/jet-build/build/jpeg}
NASM=${NASM:-$HOME/jet-build/bin/nasm}
CFLAGS=${CFLAGS:--O2 -march=tigerlake -mno-avx512f -mtune=tigerlake -ffunction-sections -fdata-sections}
rm -rf "$B"; mkdir -p "$B/obj"
cd "$S"
# les fichiers de configuration (les valeurs que CMake donne pour un x86-64 Unix)
sed -e 's/@JPEG_LIB_VERSION@/62/; s/@VERSION@/3.1.4/; s/@LIBJPEG_TURBO_VERSION_NUMBER@/3001004/' \
    -e 's/^#cmakedefine C_ARITH_CODING_SUPPORTED 1/#define C_ARITH_CODING_SUPPORTED 1/' \
    -e 's/^#cmakedefine D_ARITH_CODING_SUPPORTED 1/#define D_ARITH_CODING_SUPPORTED 1/' \
    -e 's/^#cmakedefine WITH_SIMD 1/#define WITH_SIMD 1/' \
    -e 's/^#cmakedefine RIGHT_SHIFT_IS_UNSIGNED 1/\/* #undef RIGHT_SHIFT_IS_UNSIGNED *\//' \
    -e 's/^#cmakedefine \(.*\)$/\/* #undef \1 *\//' src/jconfig.h.in > "$B/jconfig.h"
sed -e 's/@BUILD@/korin/; s/@HIDDEN@/__attribute__((visibility("hidden")))/; s/@INLINE@/__inline__ __attribute__((always_inline))/' \
    -e 's/@THREAD_LOCAL@/__thread/; s/@CMAKE_PROJECT_NAME@/libjpeg-turbo/; s/@VERSION@/3.1.4/; s/@SIZE_T@/8/' \
    -e 's/^#cmakedefine HAVE_BUILTIN_CTZL/#define HAVE_BUILTIN_CTZL/' \
    -e 's/^#cmakedefine C_ARITH_CODING_SUPPORTED 1/#define C_ARITH_CODING_SUPPORTED 1/' \
    -e 's/^#cmakedefine D_ARITH_CODING_SUPPORTED 1/#define D_ARITH_CODING_SUPPORTED 1/' \
    -e 's/^#cmakedefine WITH_SIMD 1/#define WITH_SIMD 1/' -e 's/^#cmakedefine \(.*\)$/\/* #undef \1 *\//' \
    src/jconfigint.h.in > "$B/jconfigint.h"
sed -e 's/@COPYRIGHT_YEAR@/2026/' src/jversion.h.in > "$B/jversion.h"
grep -n "@" "$B/jconfig.h" "$B/jconfigint.h" "$B/jversion.h" && { echo "build-jpeg : une variable non remplacée" >&2; exit 1; } || true
# jsimdcfg.inc : les constantes de jsimdcfg.inc.h pour NASM (comme simd/CMakeLists.txt)
gcc -E -I"$B" -I"$B/obj" -I"$S/simd" -I"$S/src" simd/nasm/jsimdcfg.inc.h | grep -E '^[;%]|^ %' | sed 's%_cpp_protection_%%' |
	sed 's@% define@%define@g' > "$B/jsimdcfg.inc"
# les .asm du x86-64
asm="jsimdcpu jfdctflt-sse jccolor-sse2 jcgray-sse2 jchuff-sse2 jcphuff-sse2 jcsample-sse2 jdcolor-sse2 jdmerge-sse2
jdsample-sse2 jfdctfst-sse2 jfdctint-sse2 jidctflt-sse2 jidctfst-sse2 jidctint-sse2 jidctred-sse2 jquantf-sse2 jquanti-sse2
jccolor-avx2 jcgray-avx2 jcsample-avx2 jdcolor-avx2 jdmerge-avx2 jdsample-avx2 jfdctint-avx2 jidctint-avx2 jquanti-avx2"
for a in $asm; do
	"$NASM" -f elf64 -DELF -D__x86_64__ -I"$B/" -I"$S/simd/nasm/" -I"$S/simd/x86_64/" "simd/x86_64/$a.asm" -o "$B/obj/simd-$a.o"
done
# les sources C (JPEG_SOURCES de CMakeLists.txt, avec jaricom, jcarith, jdarith) et le choix du SIMD (jsimd.c)
srcs=$(sed -n '/^set(JPEG_SOURCES src\/jcapimin.c/,/)$/p' CMakeLists.txt | tr -s ' \n' '\n' | grep '\.c' | sed 's/)$//; s/^set(JPEG_SOURCES//')
srcs="$srcs src/jaricom.c src/jcarith.c src/jdarith.c simd/x86_64/jsimd.c"
n=0
for c in $srcs; do
	o="$B/obj/$(echo "$c" | tr / _ | sed 's/\.c$/.o/')"
	gcc $CFLAGS -I"$B" -I"$S/src" -I"$S/simd" -c "$c" -o "$o" &
	n=$((n + 1)); [ $((n % 8)) = 0 ] && wait
done
wait
ls "$B"/obj/*.o | wc -l
ar rcs "$B/libjpeg.a" "$B"/obj/*.o
echo "build-jpeg : $B/libjpeg.a ($(ls -l "$B/libjpeg.a" | awk '{print $5}') octets, $(nm "$B/libjpeg.a" 2>/dev/null | grep -c ' T jsimd_') fonctions jsimd_)"
