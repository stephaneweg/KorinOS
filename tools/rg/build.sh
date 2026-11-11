#!/bin/sh
# rg : ripgrep reecrit en C sur PCRE2 pour Korin OS (docs/RG.md), construit sur le PC (WSL, Linux), hors du
# selfhost, sur libposix et newlib comme bash (tools/bash/build.sh). A lancer depuis la racine du depot,
# apres tools/posix/build.sh :
#     sh tools/rg/build.sh [archive]
# L'archive : PCRE2 10.46 telle quelle (PCRE2_TAR, par defaut /opt/src/rg-deps/pcre2_10.46.orig.tar.gz, celle
# d'Ubuntu : la meme source que github.com/PCRE2Project). Les sources de rg : tools/rg/src (le notre).
# Produit :
#   prebuilt/rg               pour la carte (copie sur SD0:/bin/rg par build.ps1 et tools/ci/carte_linux.py)
#   build/rg/host/rg          le meme rg pour le PC, pour les essais differentiels (tools/rg/difftest.py)
# PCRE2 : libpcre2-8 statique, Unicode et JIT (sljit, ARM64 sur la carte) ; le JIT demande de la memoire
# executable (mmap PROT_EXEC), sinon rg se rabat sur l'interpreteur.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
WORK=${WORK:-$HOME/.cache/korin-rg}
TAR=${1:-${PCRE2_TAR:-/opt/src/rg-deps/pcre2_10.46.orig.tar.gz}}
V=pcre2-10.46
SPECS=$ROOT/build/posix/lib/posix.specs
HOSTCC=${HOSTCC:-cc}
export PATH="$TOOL/bin:$PATH"
[ -f "$TAR" ] || { echo "rg : archive absente : $TAR" >&2; exit 1; }
[ -f "$SPECS" ] || [ "${HOST_ONLY:-0}" = 1 ] || { echo "rg : lancer d'abord tools/posix/build.sh" >&2; exit 1; }

# PCRE2 : extrait une fois (REBUILD=1 : de zero), compile sans configure (les sources de CMakeLists.txt)
if [ "${REBUILD:-0}" = 1 ] || [ ! -f "$WORK/src/$V/src/pcre2.h.generic" ]; then
	rm -rf "${WORK:?}/src" "$WORK/pcre2-host" "$WORK/pcre2-korin"
	mkdir -p "$WORK/src"
	tar -C "$WORK/src" -xzf "$TAR"
fi
P=$WORK/src/$V/src
mkdir -p "$WORK/gen"
cp "$P/config.h.generic" "$WORK/gen/config.h"
cp "$P/pcre2.h.generic" "$WORK/gen/pcre2.h"
cp "$P/pcre2_chartables.c.dist" "$WORK/gen/pcre2_chartables.c"
PSRC="pcre2_auto_possess pcre2_chkdint pcre2_compile pcre2_compile_class pcre2_config pcre2_context pcre2_convert
pcre2_dfa_match pcre2_error pcre2_extuni pcre2_find_bracket pcre2_jit_compile pcre2_maketables pcre2_match
pcre2_match_data pcre2_newline pcre2_ord2utf pcre2_pattern_info pcre2_script_run pcre2_serialize
pcre2_string_utils pcre2_study pcre2_substitute pcre2_substring pcre2_tables pcre2_ucd pcre2_valid_utf
pcre2_xclass"
PDEF="-DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8 -DPCRE2_STATIC -DSUPPORT_UNICODE -DSUPPORT_JIT -DSUPPORT_PCRE2_8
-DHAVE_STDINT_H -DHAVE_INTTYPES_H -DHAVE_LIMITS_H -DHAVE_STRING_H -DHAVE_STDLIB_H -DHAVE_MEMMOVE -DHAVE_STRERROR
-DHAVE_UNISTD_H -DHAVE_SYS_TYPES_H -DHAVE_SYS_STAT_H -DHAVE_ASSERT_H"

# pcre_lib DOSSIER CC AR CFLAGS : libpcre2-8.a dans DOSSIER
pcre_lib () {
	d=$1; cc=$2; ar=$3; shift 3
	if [ -f "$d/libpcre2-8.a" ] && [ "${REBUILD:-0}" != 1 ]; then return 0; fi
	rm -rf "$d"
	mkdir -p "$d"
	objs=""
	for f in $PSRC; do
		$cc "$@" $PDEF -I"$WORK/gen" -I"$P" -c "$P/$f.c" -o "$d/$f.o"
		objs="$objs $d/$f.o"
	done
	$cc "$@" $PDEF -I"$WORK/gen" -I"$P" -c "$WORK/gen/pcre2_chartables.c" -o "$d/pcre2_chartables.o"
	$ar rcs "$d/libpcre2-8.a" $objs "$d/pcre2_chartables.o"
}

RGSRC="$ROOT/tools/rg/src/main.c $ROOT/tools/rg/src/search.c $ROOT/tools/rg/src/walk.c $ROOT/tools/rg/src/glob.c
$ROOT/tools/rg/src/util.c $ROOT/tools/rg/src/types.c"
WARN="-Wall -Wno-unused-function"

# pour le PC (essais)
pcre_lib "$WORK/pcre2-host" "$HOSTCC" ar -O2
mkdir -p "$ROOT/build/rg/host"
$HOSTCC -std=gnu11 -O2 $WARN -I"$WORK/gen" $RGSRC "$WORK/pcre2-host/libpcre2-8.a" -o "$ROOT/build/rg/host/rg"
ls -l "$ROOT/build/rg/host/rg"
[ "${HOST_ONLY:-0}" = 1 ] && exit 0

# pour la carte : la chaine d'Onyx et les specs de libposix
cat > "$WORK/korin-cc" <<CC
#!/bin/sh
exec ${TC}gcc -specs=$SPECS "\$@"
CC
chmod +x "$WORK/korin-cc"
pcre_lib "$WORK/pcre2-korin" "$WORK/korin-cc" ${TC}ar -O2 -g0 -mcpu=cortex-a72
mkdir -p "$ROOT/build/rg/korin"
"$WORK/korin-cc" -std=gnu11 -O2 -g0 -mcpu=cortex-a72 $WARN -I"$WORK/gen" $RGSRC "$WORK/pcre2-korin/libpcre2-8.a" \
	-o "$ROOT/build/rg/korin/rg.elf"
${TC}strip "$ROOT/build/rg/korin/rg.elf" -o "$ROOT/prebuilt/rg"
ls -l "$ROOT/prebuilt/rg"
