#!/bin/sh
# trace_gba.sh rom images : premiere instruction ou les deux coeurs divergent (WSL, racine du depot),
# apres avoir assemble tools/emu/gba_harness.asm avec -d GBA_TRACE=1 en build/emu/gba_harness_tr.
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
sed 's|^	u32 pc = r\[15\];$|	tr_print (r, cpsr, now, io16 (0x202), vcount); u32 pc = r[15];|; s|^namespace gba {$|extern void tr_print (const unsigned *, unsigned, unsigned long long, unsigned, unsigned);\nnamespace gba {|' $ONYX/gba/gba_cpu.cpp > $W/gba_cpu_tr.cpp
cat > $W/tr_a.cpp <<'X'
#include <stdio.h>
void tr_print (const unsigned *r, unsigned p, unsigned long long c, unsigned f, unsigned v)
{ for (int i = 0; i < 16; i++) printf ("%08x", r[i]); printf ("%08x%llu%08x%08x\n", p, c, f, v); }
X
g++ -O1 -w -Dprivate=public -I$ONYX tools/emu/ref_gba.cpp $W/gba_cpu_tr.cpp $W/tr_a.cpp $ONYX/gba/gba.cpp $ONYX/gba/gba_ppu.cpp $ONYX/gba/gba_apu.cpp $ONYX/gba/gba_bios.cpp -o $W/ref_atr || exit 1
chmod +x $W/gba_harness_tr
./$W/ref_atr "$1" $2 | grep -v ' ' > $W/tr_ref4.txt
qemu-aarch64 $W/gba_harness_tr "$1" $2 | grep -v ' ' > $W/tr_asm4.txt
wc -l $W/tr_ref4.txt $W/tr_asm4.txt
python3 -I tools/emu/trdiff.py $W/tr_ref4.txt $W/tr_asm4.txt
