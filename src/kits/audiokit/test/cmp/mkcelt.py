"""mkcelt.py - flux CELT du banc différentiel (cmp_celt.c), faits par le libopus
de WSL (ctypes) : mode CELT seul (OPUS_SET_FORCE_MODE, ou RESTRICTED_LOWDELAY),
NB à FB, mono et stéréo (et stéréo codée en mono), 2,5 / 5 / 10 / 20 ms (et 40,
60 ms : paquets de plusieurs trames), débits de 6 à 510 kbit/s, VBR et CBR,
complexités 0 à 10 ; signaux : musique, transitoires (blocs courts,
anti-effondrement), impulsions périodiques (post-filtre), bruit, silence
(DTX), voies en opposition de phase (inversion).

    wsl python3 -I src/kits/audiokit/test/cmp/mkcelt.py      (depuis la racine du dépôt)
    -> build/audiokit_cmp/celt/lib_*.pk

Format d'un .pk : « CELTPK1 », canaux (1 octet), paquets (u32), puis pour
chaque paquet sa taille (u16) et ses octets.
"""
import ctypes
import math
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
OUT = os.path.join(ROOT, 'build', 'audiokit_cmp', 'celt')

APP_VOIP, APP_AUDIO, APP_LOWDELAY = 2048, 2049, 2051
SET_BITRATE, SET_BANDWIDTH, SET_VBR, SET_COMPLEXITY = 4002, 4008, 4006, 4010
SET_DTX, SET_VBR_CONSTRAINT, SET_FORCE_CHANNELS = 4016, 4020, 4022
SET_PHASE_INV_DISABLED, SET_FORCE_MODE, MODE_CELT_ONLY = 4046, 11002, 1002
BW = {'nb': 1101, 'mb': 1102, 'wb': 1103, 'swb': 1104, 'fb': 1105}

# (nom, application, canaux, échantillons par paquet, débit, largeur, vbr,
#  complexité, options, signal, durée en ms)
CASES = [
    ('fb20s', APP_AUDIO, 2, 960, 128000, 'fb', 1, 10, '', 'music', 800),
    ('fb20m', APP_AUDIO, 1, 960, 64000, 'fb', 1, 10, '', 'music', 800),
    ('fb10s', APP_AUDIO, 2, 480, 96000, 'fb', 1, 10, '', 'music', 600),
    ('fb5s', APP_AUDIO, 2, 240, 64000, 'fb', 1, 8, '', 'music', 400),
    ('fb2m', APP_AUDIO, 1, 120, 48000, 'fb', 1, 10, '', 'music', 300),
    ('fb2s', APP_AUDIO, 2, 120, 256000, 'fb', 0, 10, '', 'noise', 300),
    ('swb20s', APP_AUDIO, 2, 960, 32000, 'swb', 1, 10, '', 'music', 800),
    ('wb20m', APP_AUDIO, 1, 960, 24000, 'wb', 1, 10, '', 'music', 800),
    ('mb20m', APP_AUDIO, 1, 960, 16000, 'mb', 1, 5, '', 'music', 600),
    ('nb20m', APP_AUDIO, 1, 960, 12000, 'nb', 1, 10, '', 'music', 800),
    ('nb10s', APP_AUDIO, 2, 480, 16000, 'nb', 1, 10, '', 'music', 600),
    ('lo20s', APP_AUDIO, 2, 960, 12000, 'fb', 1, 10, '', 'music', 800),
    ('lo10m', APP_AUDIO, 1, 480, 6000, 'fb', 1, 10, '', 'music', 600),
    ('hi20s', APP_AUDIO, 2, 960, 510000, 'fb', 1, 10, '', 'noise', 400),
    ('hi20m', APP_AUDIO, 1, 960, 320000, 'fb', 0, 10, '', 'music', 400),
    ('tr20s', APP_AUDIO, 2, 960, 64000, 'fb', 1, 10, '', 'clicks', 1000),
    ('tr20m', APP_AUDIO, 1, 960, 32000, 'fb', 1, 10, '', 'clicks', 1000),
    ('tr10s', APP_AUDIO, 2, 480, 48000, 'fb', 1, 10, '', 'clicks', 600),
    ('tr5m', APP_AUDIO, 1, 240, 40000, 'wb', 1, 10, '', 'clicks', 400),
    ('pf20m', APP_AUDIO, 1, 960, 48000, 'fb', 1, 10, '', 'pitch', 1000),
    ('pf20s', APP_AUDIO, 2, 960, 64000, 'fb', 1, 10, '', 'pitch', 1000),
    ('pf10m', APP_AUDIO, 1, 480, 32000, 'swb', 1, 10, '', 'pitch', 600),
    ('pf5s', APP_AUDIO, 2, 240, 96000, 'fb', 1, 10, '', 'pitch', 400),
    ('fm20s', APP_AUDIO, 2, 960, 48000, 'fb', 1, 10, 'mono', 'music', 600),
    ('fm10s', APP_AUDIO, 2, 480, 32000, 'wb', 1, 10, 'mono', 'clicks', 400),
    ('cbr20s', APP_AUDIO, 2, 960, 40000, 'fb', 0, 10, '', 'music', 600),
    ('cvbr20m', APP_AUDIO, 1, 960, 40000, 'fb', 2, 10, '', 'clicks', 600),
    ('cx0s', APP_AUDIO, 2, 960, 64000, 'fb', 1, 0, '', 'music', 600),
    ('cx3m', APP_AUDIO, 1, 480, 48000, 'fb', 1, 3, '', 'pitch', 400),
    ('sil20s', APP_AUDIO, 2, 960, 48000, 'fb', 1, 10, 'dtx', 'silence', 1000),
    ('sil10m', APP_AUDIO, 1, 480, 24000, 'fb', 0, 10, '', 'silence', 600),
    ('anti20s', APP_AUDIO, 2, 960, 64000, 'fb', 1, 10, '', 'anti', 600),
    ('anti20n', APP_AUDIO, 2, 960, 64000, 'fb', 1, 10, 'noinv', 'anti', 400),
    ('anti10s', APP_AUDIO, 2, 480, 24000, 'swb', 1, 10, '', 'anti', 400),
    ('ld20s', APP_LOWDELAY, 2, 960, 96000, 'fb', 1, 10, 'nomode', 'music', 600),
    ('ld5m', APP_LOWDELAY, 1, 240, 32000, 'fb', 1, 10, 'nomode', 'clicks', 400),
    ('m40s', APP_AUDIO, 2, 1920, 64000, 'fb', 1, 10, '', 'music', 800),
    ('m60m', APP_AUDIO, 1, 2880, 32000, 'wb', 1, 10, '', 'clicks', 960),
    ('m40m', APP_LOWDELAY, 1, 1920, 24000, 'fb', 0, 10, 'nomode', 'pitch', 800),
    ('sweep20s', APP_AUDIO, 2, 960, 80000, 'fb', 1, 10, '', 'sweep', 800),
    ('sweep2s', APP_AUDIO, 2, 120, 128000, 'swb', 1, 10, '', 'sweep', 300),
]


class Rng:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self):
        self.s = (self.s * 1103515245 + 12345) & 0xFFFFFFFF
        return self.s >> 8

    def uni(self):
        return self.next() / float(1 << 24) * 2 - 1


def signal(kind, ch, n, seed):
    r = Rng(seed)
    out = []
    for i in range(n):
        t = i / 48000.0
        for c in range(ch):
            v = 0.0
            if kind == 'music':
                f0 = 220 * (1 + 0.5 * c) * (1 + 0.01 * math.sin(2 * math.pi * 5 * t))
                for h in range(1, 8):
                    v += 0.25 / h * math.sin(2 * math.pi * f0 * h * t + h)
                v += 0.03 * r.uni()
                if int(t * 5) % 4 == 3:
                    v *= 0.05
            elif kind == 'noise':
                v = 0.4 * r.uni()
            elif kind == 'clicks':
                ph = (i + c * 1700) % 4800
                v = 0.01 * r.uni()
                if ph < 40:
                    v += 0.9 * r.uni()
                if (i // 9000) % 2:
                    v += 0.2 * math.sin(2 * math.pi * 900 * t)
            elif kind == 'pitch':
                per = 48000 // (150 + 40 * c + int(30 * math.sin(2 * math.pi * 0.7 * t)))
                v = 0.7 if i % per < 3 else 0.0
                v += 0.002 * r.uni()
            elif kind == 'silence':
                v = 0.0
                if 0.4 < t < 0.55:
                    v = 0.3 * math.sin(2 * math.pi * 440 * t)
                elif t > 0.8:
                    v = 1e-5 * r.uni()
            elif kind == 'anti':
                v = 0.5 * math.sin(2 * math.pi * 330 * t) + 0.05 * r.uni()
                if c:
                    v = -v * (1.0 if t < 0.3 else 0.8)
            elif kind == 'sweep':
                v = 0.6 * math.sin(2 * math.pi * (50 + 12000 * t) * t)
                if c:
                    v = 0.6 * math.sin(2 * math.pi * (20000 - 12000 * t) * t)
            out.append(max(-1.0, min(1.0, v)))
    return out


def main():
    lib = ctypes.CDLL('libopus.so.0')
    lib.opus_encoder_create.restype = ctypes.c_void_p
    lib.opus_encoder_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
    lib.opus_encode_float.restype = ctypes.c_int
    lib.opus_encode_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                      ctypes.c_char_p, ctypes.c_int]
    lib.opus_encoder_destroy.argtypes = [ctypes.c_void_p]
    os.makedirs(OUT, exist_ok=True)

    def ctl(st, req, v):
        r = lib.opus_encoder_ctl(ctypes.c_void_p(st), ctypes.c_int(req), ctypes.c_int(v))
        if r != 0:
            raise SystemExit('ctl %d = %d : %d' % (req, v, r))

    for k, (name, app, ch, fs, br, bw, vbr, cx, opt, kind, ms) in enumerate(CASES):
        err = ctypes.c_int()
        st = lib.opus_encoder_create(48000, ch, app, ctypes.byref(err))
        if not st:
            raise SystemExit('opus_encoder_create : %d' % err.value)
        if 'nomode' not in opt:
            ctl(st, SET_FORCE_MODE, MODE_CELT_ONLY)
        ctl(st, SET_BITRATE, br)
        ctl(st, SET_BANDWIDTH, BW[bw])
        ctl(st, SET_VBR, 1 if vbr else 0)
        if vbr == 2:
            ctl(st, SET_VBR_CONSTRAINT, 1)
        ctl(st, SET_COMPLEXITY, cx)
        if 'mono' in opt:
            ctl(st, SET_FORCE_CHANNELS, 1)
        if 'dtx' in opt:
            ctl(st, SET_DTX, 1)
        if 'noinv' in opt:
            ctl(st, SET_PHASE_INV_DISABLED, 1)
        n = 48000 * ms // 1000
        n -= n % fs
        pcm = signal(kind, ch, n, 1000 + k)
        buf = (ctypes.c_float * len(pcm))(*pcm)
        pkts = []
        data = ctypes.create_string_buffer(1500)
        for o in range(0, n, fs):
            p = ctypes.cast(ctypes.byref(buf, o * ch * 4), ctypes.POINTER(ctypes.c_float))
            r = lib.opus_encode_float(ctypes.c_void_p(st), p, fs, data, 1500)
            if r < 0:
                raise SystemExit('%s : opus_encode_float %d' % (name, r))
            pkts.append(data.raw[:r])
        lib.opus_encoder_destroy(ctypes.c_void_p(st))
        out = [b'CELTPK1', bytes([ch]), struct.pack('<I', len(pkts))]
        for p in pkts:
            out.append(struct.pack('<H', len(p)) + p)
        with open(os.path.join(OUT, 'lib_' + name + '.pk'), 'wb') as f:
            f.write(b''.join(out))
        tocs = sorted(set(p[0] >> 3 for p in pkts if p))
        print('%-10s %3d paquets, configurations %s' % (name, len(pkts), tocs))


if __name__ == '__main__':
    main()
