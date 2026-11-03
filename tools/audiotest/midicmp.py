"""midicmp.py -- compare deux rendus d'un meme .mid (s16 stereo 44,1 kHz brut) : celui d'AudioKit et la
reference de MeltySynth (midiref.cpp). Deux synthes ne donnent pas le meme PCM : on compare ce qui
s'entend : l'enveloppe de niveau (par 50 ms), le niveau global, et le spectre moyen (par bandes
d'un tiers d'octave). Python standard : FFT ecrite ici, tranches espacees.

  python -I tools/audiotest/midicmp.py kit.raw ref.raw
"""
import array
import cmath
import math
import sys


def load(p):
    a = array.array("h")
    a.frombytes(open(p, "rb").read())
    return a


def fft(x):
    n = len(x)
    if n == 1:
        return x
    e, o = fft(x[0::2]), fft(x[1::2])
    t = [cmath.exp(-2j * cmath.pi * k / n) * o[k] for k in range(n // 2)]
    return [e[k] + t[k] for k in range(n // 2)] + [e[k] - t[k] for k in range(n // 2)]


def rms_env(a, hop):
    out = []
    n = len(a) // 2
    for i in range(0, n - hop, hop):
        s = 0
        for k in range(i, i + hop, 4):
            s += a[2 * k] ** 2 + a[2 * k + 1] ** 2
        out.append(math.sqrt(s / (hop / 2)))
    return out


def bands(a, slices=60, size=4096):
    n = len(a) // 2
    edges = [31.25 * 2 ** (i / 3) for i in range(25)]
    tot = [0.0] * (len(edges) - 1)
    win = [0.5 - 0.5 * math.cos(2 * math.pi * i / size) for i in range(size)]
    for s in range(slices):
        p = (n - size) * s // slices
        x = [(a[2 * (p + i)] + a[2 * (p + i) + 1]) * win[i] for i in range(size)]
        X = fft([complex(v) for v in x])
        for b in range(len(tot)):
            lo = int(edges[b] * size / 44100)
            hi = max(lo + 1, int(edges[b + 1] * size / 44100))
            tot[b] += sum(abs(X[k]) ** 2 for k in range(lo, min(hi, size // 2)))
    return edges, tot


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    print("trames : %d / %d" % (len(a) // 2, len(b) // 2))
    ea, eb = rms_env(a, 2205), rms_env(b, 2205)
    n = min(len(ea), len(eb))
    ea, eb = ea[:n], eb[:n]
    ga, gb = math.sqrt(sum(v * v for v in ea) / n), math.sqrt(sum(v * v for v in eb) / n)
    print("niveau RMS : %.0f / %.0f, ecart %.1f dB" % (ga, gb, 20 * math.log10(ga / gb)))
    ma, mb = sum(ea) / n, sum(eb) / n
    c = sum((x - ma) * (y - mb) for x, y in zip(ea, eb))
    c /= math.sqrt(sum((x - ma) ** 2 for x in ea) * sum((y - mb) ** 2 for y in eb)) or 1
    print("correlation des enveloppes (50 ms) : %.3f" % c)
    pa, pb = max(abs(v) for v in a), max(abs(v) for v in b)
    print("crete : %d / %d" % (pa, pb))
    e, ta = bands(a)
    _, tb = bands(b)
    sa, sb = sum(ta), sum(tb)
    print("bandes (Hz : dB relatifs kit / ref)")
    worst = 0
    for i in range(len(ta)):
        da, db = 10 * math.log10(ta[i] / sa + 1e-12), 10 * math.log10(tb[i] / sb + 1e-12)
        print("  %7.0f : %6.1f %6.1f  (%+.1f)" % (e[i], da, db, da - db))
        if db > -45:
            worst = max(worst, abs(da - db))
    print("pire ecart (bandes utiles) : %.1f dB" % worst)


main()
