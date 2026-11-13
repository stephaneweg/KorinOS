#!/bin/sh
# Le recompilateur du Gekko pour x86-64 (src/apps/gcemu/gc/gc_jit_x64.cpp, d'Onyx) contre celui d'AArch64 (gc_jit.cpp,
# sous qemu-aarch64) et contre l'interprete, sur des DOL d'essai (tools/emu/romgen.py gc-stress), sans fenetre ni GL :
# les deux recompilateurs donnent la meme machine a chaque trame (image, memoire, cycles, pc, registres :
# tools/emu/ref_gc.cpp, GC_REGS=1) ; l'interprete, la meme memoire (il s'arrete a l'instruction, eux au bloc).
# Dans WSL, depuis la racine du depot :  sh tools/emu/check_gc_jit.sh [trames] [graines...]
set -u
W=build/emu/gcjit
N=${1:-60}
[ $# -gt 0 ] && shift
SEEDS=${*:-1 2 3}
G=src/apps/gcemu
mkdir -p $W
F="-O2 -w -std=gnu++17 -ffp-contract=off -Dprivate=public -I$G"
SRC="tools/emu/ref_gc.cpp $G/gxes_rec.cpp $(ls $G/gc/*.cpp)"
g++ -static $F $SRC -o $W/ref_x64 || exit 1
aarch64-linux-gnu-g++ -static -mcpu=cortex-a72 $F $SRC -o $W/ref_a64 || exit 1
fail=0
for s in $SEEDS; do
	for v in "" fp ps; do
		R=$W/s$s$v.dol
		python3 -I tools/emu/romgen.py gc-stress $s $R $v
		GC_REGS=1 GC_JIT=0 ./$W/ref_x64 $R $N > $W/xi.txt 2> $W/xi.err
		GC_REGS=1 ./$W/ref_x64 $R $N > $W/xj.txt 2> $W/xj.err
		GC_REGS=1 GC_JIT=0 qemu-aarch64 $W/ref_a64 $R $N > $W/ai.txt 2> $W/ai.err
		GC_REGS=1 qemu-aarch64 $W/ref_a64 $R $N > $W/aj.txt 2> $W/aj.err
		r="graine $s ${v:-entiers} :"
		# le recompilateur s'arrete a la fin d'un bloc, l'interprete a l'instruction : cycles, pc et registres d'une
		# trame different d'un bloc ; la memoire, elle, doit etre la meme. Les deux generateurs : tout pareil.
		if cmp -s $W/xj.txt $W/aj.txt; then r="$r JIT x86-64 = JIT AArch64 ($(grep -c regs $W/xj.txt) trames, registres compris)"
		else fail=1; r="$r JIT x86-64 DIFF AArch64 ($(diff $W/xj.txt $W/aj.txt | sed -n 2p | cut -c1-90))"; fi
		mem () { grep -o 'mem [0-9a-f]*' $1; }
		if [ "$(mem $W/xi.txt)" = "$(mem $W/xj.txt)" ]; then r="$r, memoire = interprete"
		else r="$r, (memoire differente de l'interprete a une trame : un bloc de plus)"; fi
		if cmp -s $W/xi.txt $W/ai.txt; then r="$r, interpretes x86-64 = AArch64"; else r="$r, (interpretes x86-64 et AArch64 differents)"; fi
		echo "$r"
	done
done
exit $fail
