"""gen_upcase.py : la table des majuscules d'exFAT (compressée), pour mkexfat.c -> upcase.inc

    python -I src/lib/exfat/gen_upcase.py

Tirée des données Unicode de Python (str.upper : seulement les correspondances d'un caractère du plan de base).
Forme compressée de la spécification exFAT (§7.2.5.1) : une suite de caractères identité s'écrit 0xFFFF, n."""
import os

t = []
for cp in range(0x10000):
    u = chr(cp).upper() if not 0xD800 <= cp <= 0xDFFF else chr(cp)
    t.append(ord(u) if len(u) == 1 and ord(u) < 0x10000 else cp)

out = []
cp = 0
while cp < 0x10000:
    if t[cp] == cp:
        n = 1
        while cp + n < 0x10000 and t[cp + n] == cp + n:
            n += 1
        if n >= 3 or cp + n >= 0x10000:
            out += [0xFFFF, n]
        else:
            out += list(range(cp, cp + n))
        cp += n
    else:
        out.append(t[cp])
        cp += 1

here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "upcase.inc"), "w", encoding="ascii", newline="\n") as f:
    f.write("/* upcase.inc : table des majuscules exFAT compressee (src/lib/exfat/gen_upcase.py), %d mots */\n" % len(out))
    for i in range(0, len(out), 12):
        f.write("\t" + " ".join("0x%04X," % w for w in out[i:i + 12]) + "\n")
print(len(out), "mots")
