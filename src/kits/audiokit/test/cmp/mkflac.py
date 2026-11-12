"""mkflac.py - fichiers FLAC du banc différentiel (cmp_flac.c), écrits par
tools/audiotest/flacenc.py : 8/12/16/20/24 bits, 1 à 8 canaux, tailles de
bloc variées (codes 1 à 15, fixes et variables), CONSTANT, VERBATIM, FIXED,
LPC (ordres 2 à 32), échappement, paramètres de 5 bits, bits perdus, les
quatre modes stéréo, fréquences dans l'en-tête (codes 12, 13, 14) ; et des
variantes des métadonnées (sans SEEKTABLE, total inconnu, taille de trame
maximale nulle, SEEKTABLE fausse).

    python -I src/kits/audiokit/test/cmp/mkflac.py     (depuis la racine du dépôt)
    -> build/audiokit_cmp/flac/*.flac
"""
import os
import struct
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'audiotest'))
import flacenc  # noqa: E402
import mkfiles  # noqa: E402

OUT = os.path.join(ROOT, 'build', 'audiokit_cmp', 'flac')


def blocks(data):
    """'fLaC' + blocs -> [(type, octets)], trames"""
    assert data[:4] == b'fLaC'
    p = 4
    out = []
    while True:
        h = data[p]
        n = int.from_bytes(data[p + 1:p + 4], 'big')
        out.append((h & 0x7F, bytearray(data[p + 4:p + 4 + n])))
        p += 4 + n
        if h & 0x80:
            break
    return out, data[p:]


def join(bl, frames):
    out = bytearray(b'fLaC')
    for i, (t, d) in enumerate(bl):
        out.append((0x80 if i == len(bl) - 1 else 0) | t)
        out += len(d).to_bytes(3, 'big') + d
    return bytes(out + frames)


def write(name, data):
    with open(os.path.join(OUT, name), 'wb') as f:
        f.write(data)


def enc(name, x, rate, bits, block, **kw):
    path = os.path.join(OUT, name)
    flacenc.encode(path, x, rate, bits, block, **kw)
    return open(path, 'rb').read()


def main():
    os.makedirs(OUT, exist_ok=True)
    sig = mkfiles.signal
    ti = mkfiles.to_int
    tags = {'TITLE': 'Titre FLAC', 'ARTIST': 'Artiste', 'ALBUM': 'Album', 'COMMENT': 'x'}
    enc('f24v.flac', ti(sig(48000, 0.6, 1, 5), 24), 48000, 24, 4608, variable=True)
    w = ti(sig(16000, 0.8, 2, 6), 16) & ~3  # bits perdus
    enc('f16w.flac', w, 16000, 16, 1152, modes=['ms', 'ls', 'rs'])
    enc('f8m.flac', ti(sig(8000, 0.5, 1, 7), 8), 8000, 8, 576)
    enc('f12c3.flac', ti(sig(22050, 0.6, 3, 8), 12), 22050, 12, 1000, tags=tags)
    enc('f20c6.flac', ti(sig(96000, 0.35, 6, 9), 20), 96000, 20, 256)
    enc('f16c8.flac', ti(sig(32000, 0.5, 8, 10), 16), 32000, 16, 2048)
    rng = np.random.default_rng(3)
    noise = rng.integers(-(1 << 23), 1 << 23, size=(9000, 2))
    enc('n24.flac', noise, 44100, 24, 2304)  # paramètres de Rice de 5 bits
    enc('f8r14.flac', ti(sig(88200, 0.5, 1, 12), 8), 88200, 8, 192)  # fréquence code 14
    enc('f16b16.flac', ti(sig(11025, 0.3, 2, 13), 16), 11025, 16, 16)  # bloc code 6
    enc('f16b8k.flac', ti(sig(44100, 0.8, 2, 14), 16), 44100, 16, 8192)
    enc('f16b32k.flac', ti(sig(44100, 1.6, 1, 15), 16), 44100, 16, 32768)
    big = enc('f16l.flac', ti(sig(44100, 4.0, 2, 16), 16), 44100, 16, 4096,
              tags={'TITLE': 'Long'})
    # variantes des métadonnées
    bl, fr = blocks(big)
    write('f16l_noseek.flac', join([b for b in bl if b[0] != 3], fr))
    bl2 = [(t, bytearray(d)) for t, d in bl]
    si = bl2[0][1]
    v = int.from_bytes(si[10:18], 'big') & ~((1 << 36) - 1)  # total inconnu
    si[10:18] = v.to_bytes(8, 'big')
    si[7:10] = bytes(3)  # taille de trame maximale inconnue
    write('f16l_nototal.flac', join([b for b in bl2 if b[0] != 3], fr))
    bl3 = []
    for t, d in bl:
        if t == 3:  # points faux : au-delà du fichier, désordre
            d = bytearray()
            for s, o in ((0, 0), (90000, 999999), (44100, 1234), (20000, 5), (2 ** 64 - 1, 0)):
                d += struct.pack('>QQH', s, o, 4096)
        bl3.append((t, d))
    write('f16l_badseek.flac', join(bl3, fr))
    # blocs inconnus, PADDING, STREAMINFO tardif, deux SEEKTABLE
    bl4 = [(2, bytearray(b'APPLxyz')), (1, bytearray(5))] + [b for b in bl if b[0] != 1]
    write('f16l_meta.flac', join(bl4, fr))
    print('fichiers dans', OUT)


if __name__ == '__main__':
    main()
