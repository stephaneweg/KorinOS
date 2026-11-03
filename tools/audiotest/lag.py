"""lag.py - decalage entre deux PCM s16 (sortie du banc, reference) :
    python -I tools/audiotest/lag.py sortie.raw reference.raw canaux [debut]"""
import sys
import numpy as np

a = np.fromfile(sys.argv[1], '<i2').astype(np.float64)
b = np.fromfile(sys.argv[2], '<i2').astype(np.float64)
ch = int(sys.argv[3])
st = int(sys.argv[4]) if len(sys.argv) > 4 else 0
a = a.reshape(-1, ch)[:, 0]
b = b.reshape(-1, ch)[:, 0]
best = None
seg = b[st + 3000:st + 13000]
for lag in range(-3000, 3000):
    x = a[st + 3000 + lag:st + 13000 + lag]
    if len(x) != len(seg):
        continue
    e = np.sum((x - seg) ** 2)
    if best is None or e < best[0]:
        best = (e, lag)
e, lag = best
n = min(len(a) - max(lag, 0), len(b)) - 10
x = a[max(lag, 0):max(lag, 0) + n] if lag >= 0 else a[:n]
y = b[:n] if lag >= 0 else b[-lag:-lag + n]
d = x - y
snr = 10 * np.log10(np.sum(y ** 2) / max(np.sum(d ** 2), 1e-9))
print('decalage', lag, 'ecart max', int(np.max(np.abs(d))), 'RSB %.1f dB' % snr)
w = np.nonzero(np.abs(d) > 64)[0]
print('ecarts > 64 :', len(w), w[:10])
