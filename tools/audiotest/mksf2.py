"""mksf2.py -- une toute petite SoundFont 2 d'essai pour synthtest (src/bin/synthtest/test.sf2) : une
periode de 100 echantillons de sinus en boucle (441 Hz a 44,1 kHz, racine 69), preset 0 de la banque
0 (enveloppe : attaque 10 ms, relachement 100 ms).

  python -I tools/audiotest/mksf2.py src/bin/synthtest/test.sf2
"""
import math
import struct
import sys


def ck(t, d):
    return t + struct.pack("<I", len(d)) + d + (b"\0" if len(d) & 1 else b"")


def lst(t, d):
    return ck(b"LIST", t + d)


def name(s, n):
    return s.encode() + b"\0" * (n - len(s))


def tc(s):
    return int(round(1200 * math.log2(s)))


N = 100
LOOPS = 20
smp = b"".join(struct.pack("<h", int(round(20000 * math.sin(2 * math.pi * i / N)))) for i in range(N * LOOPS))
smp += b"\0\0" * 46
info = ck(b"ifil", struct.pack("<HH", 2, 4)) + ck(b"isng", b"EMU8000\0") + ck(b"INAM", b"ARMPI test\0\0")
sdta = ck(b"smpl", smp)
phdr = name("Sine", 20) + struct.pack("<HHHIII", 0, 0, 0, 0, 0, 0) + name("EOP", 20) + struct.pack("<HHHIII", 0, 0, 1, 0, 0, 0)
pbag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 1, 0)
pmod = b"\0" * 10
pgen = struct.pack("<Hh", 41, 0) + struct.pack("<Hh", 0, 0)
inst = name("Sine", 20) + struct.pack("<H", 0) + name("EOI", 20) + struct.pack("<H", 1)
ibag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 4, 0)
imod = b"\0" * 10
igen = (struct.pack("<Hh", 34, tc(0.01)) + struct.pack("<Hh", 38, tc(0.1)) + struct.pack("<Hh", 54, 1)
        + struct.pack("<Hh", 53, 0) + struct.pack("<Hh", 0, 0))
shdr = (name("Sine", 20) + struct.pack("<IIIIIBbHH", 0, N * LOOPS, N * 2, N * 3, 44100, 69, 0, 0, 1)
        + name("EOS", 20) + struct.pack("<IIIIIBbHH", 0, 0, 0, 0, 0, 0, 0, 0, 0))
pdta = (ck(b"phdr", phdr) + ck(b"pbag", pbag) + ck(b"pmod", pmod) + ck(b"pgen", pgen) + ck(b"inst", inst)
        + ck(b"ibag", ibag) + ck(b"imod", imod) + ck(b"igen", igen) + ck(b"shdr", shdr))
body = b"sfbk" + lst(b"INFO", info) + lst(b"sdta", sdta) + lst(b"pdta", pdta)
open(sys.argv[1], "wb").write(ck(b"RIFF", body))
