"""mkfiles.py - fichiers d'essai des formats d'AudioKit (docs/SON.md 9)

    python -I tools/audiotest/mkfiles.py <dossier>

Signaux synthetiques (accords, vibrato, coups de bruit, balayage), donc
libres de droits ; WAV et FLAC ecrits ici (flacenc.py), Opus par
build/audiotest/refdec.exe --opusenc (libopus d'Onyx), les autres formats
viennent d'ailleurs (voir run.py).
"""
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flacenc  # noqa: E402


def signal(rate, seconds, ch, seed=1):
    rng = np.random.default_rng(seed)
    t = np.arange(int(rate * seconds)) / rate
    out = np.zeros((len(t), ch))
    notes = [220.0, 277.18, 329.63, 440.0, 554.37]
    for i, f in enumerate(notes):
        vib = 1 + 0.003 * np.sin(2 * np.pi * 5.5 * t + i)
        ph = 2 * np.pi * f * np.cumsum(vib) / rate
        env = 0.5 + 0.5 * np.sin(2 * np.pi * (0.4 + 0.1 * i) * t + i)
        tone = 0.08 * env * (np.sin(ph) + 0.3 * np.sin(2 * ph) + 0.15 * np.sin(3 * ph))
        for c in range(ch):
            pan = (i + 1) / (len(notes) + 1) if ch > 1 else 0.5
            out[:, c] += tone * (pan if c == 0 else 1 - pan) * 2
    # coups de bruit (attaques : blocs courts des codecs)
    for k in range(int(seconds * 3)):
        s = int((k + 0.3) * rate / 3)
        n = int(0.04 * rate)
        if s + n > len(t):
            break
        burst = rng.standard_normal((n, ch)) * np.exp(-np.arange(n) / (0.008 * rate))[:, None] * 0.25
        out[s:s + n] += burst
    # balayage aigu
    f0, f1 = 500, min(16000, rate * 0.45)
    ph = 2 * np.pi * (f0 * t + (f1 - f0) * t * t / (2 * seconds))
    for c in range(ch):
        out[:, c] += 0.05 * np.sin(ph + c)
    # silence a la fin (sous-trames constantes)
    out[-int(0.2 * rate):] = 0
    return np.clip(out, -0.99, 0.99)


def to_int(x, bits):
    m = (1 << (bits - 1)) - 1
    return np.round(x * m).astype(np.int64)


def write_wav(path, x, rate, bits, fmt=1):
    n, ch = x.shape
    if fmt == 3:
        data = x.astype('<f4').tobytes()
    elif bits == 8:
        data = (to_int(x, 8) + 128).astype('u1').tobytes()
    elif bits == 16:
        data = to_int(x, 16).astype('<i2').tobytes()
    elif bits == 24:
        v = to_int(x, 24).astype('<i4')
        b = v.view('u1').reshape(-1, 4)[:, :3]
        data = b.tobytes()
    else:
        data = to_int(x, 32).astype('<i4').tobytes()
    ba = ch * bits // 8
    info = b'INFO' + b'INAM' + struct.pack('<I', 10) + b'Essai WAV\0' + b'IART' + struct.pack('<I', 6) + b'ARMPI\0'
    chunks = b'fmt ' + struct.pack('<IHHIIHH', 16, fmt, ch, rate, rate * ba, ba, bits)
    chunks += b'LIST' + struct.pack('<I', len(info)) + info
    chunks += b'data' + struct.pack('<I', len(data)) + data
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)


def small(d):
    """les petits fichiers poses sur la carte (skel/sd0/res/audiotest)"""
    os.makedirs(d, exist_ok=True)
    write_wav(os.path.join(d, 'w8m.wav'), signal(22050, 1.0, 1, 2), 22050, 8)
    tags = {'TITLE': 'Essai FLAC', 'ARTIST': 'ARMPI', 'ALBUM': 'Formats'}
    flacenc.encode(os.path.join(d, 'f16s.flac'), to_int(signal(44100, 1.0, 2, 11), 16), 44100, 16, 4096, tags=tags)
    print('fichiers dans', d)


def main():
    if len(sys.argv) > 2 and sys.argv[1] == '--small':
        return small(sys.argv[2])
    d = sys.argv[1] if len(sys.argv) > 1 else 'build/audiotest/files'
    os.makedirs(d, exist_ok=True)
    s44 = signal(44100, 3.0, 2)
    write_wav(os.path.join(d, 'w16s.wav'), s44, 44100, 16)
    write_wav(os.path.join(d, 'w8m.wav'), signal(22050, 1.0, 1, 2), 22050, 8)
    write_wav(os.path.join(d, 'w24s.wav'), signal(48000, 1.0, 2, 3), 48000, 24)
    write_wav(os.path.join(d, 'wf32.wav'), signal(32000, 1.0, 2, 4), 32000, 32, fmt=3)
    tags = {'TITLE': 'Essai FLAC', 'ARTIST': 'ARMPI', 'ALBUM': 'Formats'}
    flacenc.encode(os.path.join(d, 'f16s.flac'), to_int(s44, 16), 44100, 16, 4096, tags=tags)
    flacenc.encode(os.path.join(d, 'f24m.flac'), to_int(signal(48000, 1.5, 1, 5), 24), 48000, 24, 4608, variable=True)
    w = to_int(signal(16000, 1.0, 2, 6), 16) & ~3          # bits perdus
    flacenc.encode(os.path.join(d, 'f16w.flac'), w, 16000, 16, 1152, modes=['ms', 'ls'])
    flacenc.encode(os.path.join(d, 'f8m.flac'), to_int(signal(8000, 1.0, 1, 7), 8), 8000, 8, 576)
    # 48 kHz pour Opus
    s48 = signal(48000, 3.0, 2, 8)
    write_wav(os.path.join(d, 'src48s.wav'), s48, 48000, 16)
    write_wav(os.path.join(d, 'src48m.wav'), signal(48000, 3.0, 1, 9), 48000, 16)
    print('fichiers dans', d)


if __name__ == '__main__':
    main()
