#!/bin/sh
# Construit le banc différentiel d'AudioKit (voir cmp.h), dans WSL, depuis la racine du dépôt :
#   sh src/kits/audiokit/test/cmp/build.sh NOM...    -> build/audiokit_cmp/{a64,x86}/cmp_NOM
# (la référence en asm vient de mkref.py, lancé sous Windows : build/audiokit_cmp/akref.bin)
# Les codecs en C (src/kits/audiokit/ak_*.c, sauf ak_play.c) sont compilés avec les options du
# kit ; un fichier qui ne compile pas encore (port en cours) est sauté avec un avertissement, et
# les symboles absents valent 0 à l'édition des liens (un essai ne doit pas les appeler).
set -e
cd "$(dirname "$0")/../../../../.."
OUT=build/audiokit_cmp
T=src/kits/audiokit/test/cmp
mkdir -p "$OUT/a64/obj" "$OUT/x86/obj"
[ -f "$OUT/akref_syms.h" ] || { echo "lancer d'abord : python -I $T/mkref.py (Windows)"; exit 1; }

KFLAGS="-std=gnu11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fvisibility=hidden -fwrapv
-fno-strict-aliasing -ffp-contract=off -fno-math-errno -Wall -Wextra -Werror -DAK_HOST -Isrc/kits/audiokit"
HFLAGS="-std=gnu11 -O1 -g -Wall -Wextra -DAK_HOST -I$OUT -I$T -Isrc/kits/audiokit"

for arch in a64 x86; do
	if [ $arch = a64 ]; then CC=aarch64-linux-gnu-gcc; KCC=${KCC_A64:-$CC}; AF="-ffixed-x18"; else CC=gcc; KCC=$CC; AF=""; fi
	OBJS=""
	for f in src/kits/audiokit/ak_*.c; do
		[ -f "$f" ] || continue
		b=$(basename "$f" .c)
		[ "$b" = ak_play ] && continue
		o=$OUT/$arch/obj/$b.o
		if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ src/kits/audiokit/ak.h -nt "$o" ] ||
			[ src/kits/audiokit/ak_fp.h -nt "$o" ] || [ -n "$(find src/kits/audiokit -maxdepth 1 -name '*.h' -newer "$o")" ]; then
			if ! $KCC $KFLAGS $AF -c "$f" -o "$o" 2>"$o.err"; then
				echo "($arch) $f ne compile pas : sauté" >&2
				head -20 "$o.err" >&2
				rm -f "$o"
				continue
			fi
		fi
		OBJS="$OBJS $o"
	done
	$CC $HFLAGS $AF -c $T/cmp_host.c -o $OUT/$arch/obj/cmp_host.o
	# symboles absents (codecs pas encore portés) : bouchons faibles qui arrêtent le banc
	nm -u $OBJS | awk 'NF==2{print $2}' | sort -u > $OUT/$arch/undef.txt
	nm --defined-only $OBJS $OUT/$arch/obj/cmp_host.o | awk 'NF==3{print $3}' | sort -u > $OUT/$arch/def.txt
	{
		echo '#include <stdio.h>'
		echo '#include <stdlib.h>'
		comm -23 $OUT/$arch/undef.txt $OUT/$arch/def.txt | grep -v '^\(mem\|str\)' | while read s; do
			echo "__attribute__((weak)) long $s(void) { fprintf(stderr, \"absent : $s\\\\n\"); abort(); }"
		done
	} > $OUT/$arch/stubs.c
	$CC $HFLAGS $AF -c $OUT/$arch/stubs.c -o $OUT/$arch/obj/stubs.o
	for n in "$@"; do
		$CC $HFLAGS $AF -static -o $OUT/$arch/cmp_$n $T/cmp_$n.c $OUT/$arch/obj/cmp_host.o $OBJS \
			$OUT/$arch/obj/stubs.o
	done
done
echo "construit : $*"
