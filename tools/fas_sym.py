"""Symboles d'un fichier .fas (fasmarm -s) : retrouve l'etiquette d'une adresse.

Usage : python -I tools/fas_sym.py fichier.fas [adresse_hexa ...]
Sans adresse : liste les etiquettes (valeur, nom) triees.
Format : FAS.TXT de fasm (en-tete, table des chaines, table des symboles de
32 octets ; nom dans la table des chaines si le bit 31 est mis, sinon chaine
pascal dans le source pretraite).
"""
import struct
import sys


def load(path):
    b = open(path, 'rb').read()
    sig, maj, mino, hlen = struct.unpack_from('<IBBH', b, 0)
    if sig != 0x1A736166:
        raise SystemExit('pas un fichier .fas')
    (in_name, out_name, str_off, str_len, sym_off, sym_len,
     src_off, src_len) = struct.unpack_from('<8I', b, 8)
    syms = []
    for i in range(sym_off, sym_off + sym_len, 32):
        value, flags, size, vtype, sib, pdef, pused, sect, name, line = \
            struct.unpack_from('<QHBBIHHIII', b, i)
        if not (flags & 1) or name == 0:
            continue
        if name & 0x80000000:
            o = str_off + (name & 0x7FFFFFFF)
            n = b[o:b.index(b'\0', o)].decode('latin-1')
        else:
            o = src_off + name
            n = b[o + 1:o + 1 + b[o]].decode('latin-1')
        syms.append((value, n))
    syms.sort()
    return syms


def main():
    syms = load(sys.argv[1])
    if len(sys.argv) == 2:
        for v, n in syms:
            print('%016X %s' % (v, n))
        return
    for a in sys.argv[2:]:
        x = int(a, 16)
        best = None
        for v, n in syms:
            if v <= x and (best is None or v >= best[0]) and not n.startswith('..'):
                best = (v, n)
        print('%s -> %s + 0x%X' % (a, best[1], x - best[0]) if best else '%s -> ?' % a)


if __name__ == '__main__':
    main()
