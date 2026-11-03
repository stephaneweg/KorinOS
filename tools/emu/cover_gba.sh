#!/bin/sh
# cover_gba.sh images rom... : ce qu'une ROM met a l'epreuve, sur le coeur C++ d'Onyx (WSL, racine du
# depot) : classes d'instructions ARM (bits 27-20 + 7-4) et Thumb (8 bits hauts) jamais executees, appels
# du BIOS, modes du PPU, sauvegarde.
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
N=$1
shift
sed 's|^		switch ((op >> 25) \& 7)$|		cov_arm[(op >> 25) \& 7]++; switch ((op >> 25) \& 7)|; s|^	switch (op >> 13)$|	cov_th[op >> 8]++; switch (op >> 13)|; s|^namespace gba {$|extern unsigned cov_arm[8], cov_th[256];\nnamespace gba {|' $ONYX/gba/gba_cpu.cpp > $W/covg_cpu.cpp
sed 's|^	int mode = disp \& 7;$|	int mode = disp \& 7; cov_mode[mode]++;|; s|^namespace gba {$|extern unsigned cov_mode[8];\nnamespace gba {|' $ONYX/gba/gba_ppu.cpp > $W/covg_ppu.cpp
sed 's|^	cyc += 3;$|	cyc += 3; cov_swi[n \& 255]++;|; s|^namespace gba {$|extern unsigned cov_swi[256];\nnamespace gba {|' $ONYX/gba/gba_bios.cpp > $W/covg_bios.cpp
cat > $W/covg_m.cpp <<'X'
#include <stdio.h>
unsigned cov_arm[8], cov_th[256], cov_mode[8], cov_swi[256];
struct R { ~R () {
	fprintf (stderr, "ARM :"); for (int i = 0; i < 8; i++) fprintf (stderr, " %u", cov_arm[i]);
	fprintf (stderr, "\nThumb jamais :"); for (int i = 0; i < 256; i++) if (!cov_th[i]) fprintf (stderr, " %02X", i);
	fprintf (stderr, "\nBIOS :"); for (int i = 0; i < 256; i++) if (cov_swi[i]) fprintf (stderr, " %02X:%u", i, cov_swi[i]);
	fprintf (stderr, "\nmodes :"); for (int i = 0; i < 8; i++) fprintf (stderr, " %u", cov_mode[i]);
	fprintf (stderr, "\n");
} } r;
X
g++ -O1 -w -Dprivate=public -I$ONYX tools/emu/ref_gba.cpp $W/covg_cpu.cpp $W/covg_ppu.cpp $W/covg_bios.cpp $W/covg_m.cpp $ONYX/gba/gba.cpp $ONYX/gba/gba_apu.cpp -o $W/ref_gcov || exit 1
for r in "$@"; do echo "== $r"; ./$W/ref_gcov "$r" $N time; done
