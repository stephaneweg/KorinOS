#!/bin/sh
# trace_snes.sh rom images : premiere instruction ou les deux coeurs divergent (WSL, racine du depot),
# apres avoir assemble tools/emu/snes_harness.asm avec -d SNES_TRACE=1 en build/emu/snes_harness_tr.
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
sed 's|^	u8 op = fetch ();$|	tr_print ((unsigned) PB << 16 \| PC, A, X, Y, S, P, (unsigned) DB << 16 \| D, E, now, mdr, line); u8 op = fetch ();|; s|^namespace snes {$|extern void tr_print (unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned long long, unsigned, unsigned);\nnamespace snes {|' $ONYX/snes/snes_cpu.cpp > $W/snes_cpu_tr.cpp
cat > $W/tr_s.cpp <<'X'
#include <stdio.h>
void tr_print (unsigned pc, unsigned a, unsigned x, unsigned y, unsigned s, unsigned p, unsigned d, unsigned e, unsigned long long c, unsigned m, unsigned l)
{ printf ("%08x%08x%08x%08x%08x%08x%08x%08x%llu%08x%08x\n", pc, a, x, y, s, p, d, e, c, m, l); }
X
g++ -O1 -w -Dprivate=public -I$ONYX tools/emu/ref_snes.cpp $W/snes_cpu_tr.cpp $W/tr_s.cpp $ONYX/snes/snes.cpp $ONYX/snes/snes_ppu.cpp $ONYX/snes/snes_apu.cpp -o $W/ref_str || exit 1
chmod +x $W/snes_harness_tr
./$W/ref_str "$1" $2 | grep -v ' ' > $W/tr_ref3.txt
qemu-aarch64 $W/snes_harness_tr "$1" $2 | grep -v ' ' > $W/tr_asm3.txt
wc -l $W/tr_ref3.txt $W/tr_asm3.txt
python3 -I tools/emu/trdiff.py $W/tr_ref3.txt $W/tr_asm3.txt
