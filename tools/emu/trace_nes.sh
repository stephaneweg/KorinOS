#!/bin/sh
# trace_nes.sh rom images : premiere instruction ou les deux coeurs divergent (WSL, racine du depot)
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
sed 's|^int Machine::step ()$|}\nextern void tr_print (unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned long long, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);\nnamespace nes {\nint Machine::step ()|; s|^	u8 opc = read (pc++);$|	tr_print (pc, a, x, y, s, p, cycles, status, line, dot, mask, v, t, (unsigned) fineX + ({ unsigned k = 0; for (int i = 0; i < 4096; i++) k = k * 3 + ciram[i]; k; })); u8 opc = read (pc++);|' $ONYX/nes/nes.cpp > $W/nes_tr.cpp
cat > $W/tr_p.cpp <<'X'
#include <stdio.h>
void tr_print (unsigned pc, unsigned a, unsigned x, unsigned y, unsigned s, unsigned p, unsigned long long c, unsigned st, unsigned l, unsigned d, unsigned m, unsigned v, unsigned t, unsigned z)
{ printf ("%08x%08x%08x%08x%08x%08x%llu%08x%08x%08x%08x%08x%08x%08x\n", pc, a, x, y, s, p, c, st, l, d, m, v, t, z); }
X
g++ -O1 -w -I$ONYX tools/emu/ref_nes.cpp $W/nes_tr.cpp $W/tr_p.cpp $ONYX/nes/nes_ppu.cpp $ONYX/nes/nes_apu.cpp -o $W/ref_tr || exit 1
./$W/ref_tr "$1" $2 | grep -v ' ' > $W/tr_ref2.txt
qemu-aarch64 $W/nes_harness_tr "$1" $2 | grep -v ' ' > $W/tr_asm2.txt
wc -l $W/tr_ref2.txt $W/tr_asm2.txt
python3 -I tools/emu/trdiff.py $W/tr_ref2.txt $W/tr_asm2.txt
