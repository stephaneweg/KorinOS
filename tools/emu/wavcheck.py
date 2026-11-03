"""wavcheck.py fichier.wav : le son enregistre par QEMU (usb-audio) n'est-il pas muet ? Niveau par tranche de 2 s."""
import struct
import sys
d = open(sys.argv[1], "rb").read()
p = d.find(b"data") + 8
s = struct.unpack("<%dh" % ((len(d) - p) // 2), d[p:p + (len(d) - p) // 2 * 2])
n = 48000 * 2 * 2
out = []
for i in range(0, len(s), n):
    t = s[i:i + n]
    out.append(max(abs(min(t)), abs(max(t))) if t else 0)
print(len(s) // 96000, "s, crete par 2 s :", out)
