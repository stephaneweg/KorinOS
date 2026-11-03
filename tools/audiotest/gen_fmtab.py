"""gen_fmtab.py -- les tables de la synthese FM d'AudioKit (src/kits/audiokit/ak_fmtab.inc) :
ak_sin1024 : une periode de sinus en 1024 pas, amplitude 32767 ;
ak_exp256 : 65535 * 2^(-i/256) (une attenuation de a unites : exp[a & 255] >> (a >> 8)) ;
ak_cents1200 : 2^(i/1200) en 2.30, pour les hauteurs (MIDI, SoundFont).

  python -I tools/audiotest/gen_fmtab.py src/kits/audiokit/ak_fmtab.inc
"""
import math
import sys


def table(out, name, kw, vals, per=16):
    out.append(name + ":")
    for i in range(0, len(vals), per):
        out.append("\t" + kw + " " + ",".join(str(v) for v in vals[i:i + per]))


out = ["; genere par tools/audiotest/gen_fmtab.py : ne pas modifier"]
table(out, "ak_sin1024", "dh", [round(32767 * math.sin(2 * math.pi * i / 1024)) & 0xFFFF for i in range(1024)])
table(out, "ak_exp256", "dh", [round(65535 * 2 ** (-i / 256)) for i in range(256)])
out.append("\talign 4")
table(out, "ak_cents1200", "dw", [round((1 << 30) * 2 ** (i / 1200)) for i in range(1200)], 8)
with open(sys.argv[1], "w", newline="") as f:
    f.write("\r\n".join(out) + "\r\n")
