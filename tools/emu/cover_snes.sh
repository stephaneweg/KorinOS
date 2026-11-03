#!/bin/sh
# cover_snes.sh images rom... : ce qu'une ROM met a l'epreuve (les codes du 65C816 et du SPC700 jamais
# executes, les modes du PPU, les chemins du DSP), sur le coeur C++ d'Onyx (WSL, racine du depot).
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
N=$1
shift
sed 's|^	u8 op = fetch ();$|	u8 op = fetch (); cov_cpu[op]++;|; s|^namespace snes {$|extern unsigned cov_cpu[256];\nnamespace snes {|' $ONYX/snes/snes_cpu.cpp > $W/cov_cpu.cpp
sed 's|^	u8 op = spcRd (spcPC++);$|	u8 op = spcRd (spcPC++); cov_spc[op]++;|; s|^namespace snes {$|extern unsigned cov_spc[256], cov_voice;\nnamespace snes {|; s|^		int pitch = (r\[2\]|		if (vo.env) cov_voice++;\n		int pitch = (r[2]|' $ONYX/snes/snes_apu.cpp > $W/cov_apu.cpp
sed 's|^	int mode = bgmode \& 7;\n	bool hires|&|; s|^	if (inidisp \& 0x80) { for (int x = 0; x < W; x++) out\[x\] = 0; return; }|	cov_line[(inidisp >> 7) \& 1]++; if (inidisp \& 0x80) { for (int x = 0; x < W; x++) out[x] = 0; return; } cov_mode[bgmode \& 7]++;|; s|^namespace snes {$|extern unsigned cov_mode[8], cov_line[2];\nnamespace snes {|' $ONYX/snes/snes_ppu.cpp > $W/cov_ppu.cpp
cat > $W/cov_m.cpp <<'X'
#include <stdio.h>
unsigned cov_cpu[256], cov_spc[256], cov_mode[8], cov_line[2], cov_voice;
struct R { ~R () {
	fprintf (stderr, "65C816 jamais :"); for (int i = 0; i < 256; i++) if (!cov_cpu[i]) fprintf (stderr, " %02X", i);
	fprintf (stderr, "\nSPC700 jamais :"); for (int i = 0; i < 256; i++) if (!cov_spc[i]) fprintf (stderr, " %02X", i);
	fprintf (stderr, "\nmodes :"); for (int i = 0; i < 8; i++) fprintf (stderr, " %u", cov_mode[i]);
	fprintf (stderr, "\nlignes montrees %u, noires %u, voix actives %u\n", cov_line[0], cov_line[1], cov_voice);
} } r;
X
g++ -O1 -w -Dprivate=public -I$ONYX tools/emu/ref_snes.cpp $W/cov_cpu.cpp $W/cov_apu.cpp $W/cov_ppu.cpp $W/cov_m.cpp $ONYX/snes/snes.cpp -o $W/ref_cov || exit 1
for r in "$@"; do echo "== $r"; ./$W/ref_cov "$r" $N time; done
