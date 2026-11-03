#!/bin/sh
# trace_n64.sh rom images cycle : premiere instruction de l'interprete ou les deux coeurs divergent, a partir de ce
# cycle (WSL, racine du depot), apres avoir assemble n64_harness.asm avec -d N64_TRACE=1 -d TR_FROM=cycle en
# build/emu/n64_harness_tr.
W=build/emu
S=tools/emu/n64ref
sed 's|^void Machine::step ()$|extern "C" void tr_print (const void *, const void *, u64, u32, u64, u64);\nvoid Machine::step ()|; s|^	if (excPending)	*// an interrupt, before this instruction$|	tr_print (r, fpr, cycles, pc, hi, lo);\n&|' $S/n64/n64_cpu.cpp > $W/n64_cpu_tr.cpp
cat > $W/tr_n.cpp <<X
#include <stdio.h>
extern "C" void tr_print (const void *r, const void *f, unsigned long long c, unsigned pc, unsigned long long hi, unsigned long long lo)
{
	if (c < $3ull) return;
	unsigned h = 0x811C9DC5, g = 0x811C9DC5;
	for (int i = 0; i < 64; i++) h = (h ^ ((const unsigned *) r)[i]) * 16777619u;
	for (int i = 0; i < 64; i++) g = (g ^ ((const unsigned *) f)[i]) * 16777619u;
	printf ("T %08x %llu %08x %08x %08x %08x\n", pc, c, h, g, (unsigned) hi, (unsigned) lo);
}
X
aarch64-linux-gnu-g++ -static -O1 -w -std=gnu++17 -ffp-contract=off -Dprivate=public -I$S -Itools/emu/n64ref/n64 -Itools/emu/n64ref tools/emu/ref_n64.cpp $W/n64_cpu_tr.cpp $W/tr_n.cpp $(ls $S/n64/*.cpp | grep -v n64_cpu.cpp) -o $W/ref_ntr || exit 1
chmod +x $W/n64_harness_tr
qemu-aarch64 $W/ref_ntr "$1" $2 2> /dev/null | grep '^T' > $W/tr_ref.txt
qemu-aarch64 $W/n64_harness_tr "$1" $2 | grep '^T' > $W/tr_asm.txt
wc -l $W/tr_ref.txt $W/tr_asm.txt
python3 -I tools/emu/trdiff.py $W/tr_ref.txt $W/tr_asm.txt
