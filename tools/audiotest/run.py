"""run.py - essais des decodeurs d'AudioKit sur le PC (docs/SON.md 9)

    python -I tools/audiotest/run.py files            fichiers d'essai (mkfiles.py)
    python -I tools/audiotest/run.py test <f>...      decode par le banc (QEMU virt,
                                                      akhost.asm) et compare a la reference
    python -I tools/audiotest/run.py ref <f>...       PCM de reference seulement

La reference : build/audiotest/refdec.exe (build_refdec.sh : minimp3,
stb_vorbis, dr_flac, libopus d'Onyx), ou le WAV lui-meme. Mesures : trames,
ecart maximal, rapport signal / bruit (dB) ; temps du banc (QEMU TCG : a
titre indicatif, la vitesse se mesure sous ARMPI par codectest).
"""
import os
import struct
import subprocess
import sys
import time
import functools
print = functools.partial(print, flush=True)

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BUILD = os.path.join(ROOT, 'build', 'audiotest')
FASM = os.path.join(ROOT, 'tools', 'fasmarm', 'fasmarm.exe')
QEMU = r"C:\Program Files\qemu\qemu-system-aarch64.exe"
REFDEC = os.path.join(BUILD, 'refdec.exe')


def assemble():
    src = os.path.join(ROOT, 'tools', 'audiotest')
    out = os.path.join(BUILD, 'akhost.elf')
    r = subprocess.run([FASM, 'akhost.asm', out], cwd=src, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-2000:])
        sys.exit(1)
    return out


def host(mode, inp, outp, timeout=60):
    elf = os.path.join(BUILD, 'akhost.elf')
    rel_in = os.path.relpath(inp, ROOT).replace('\\', '/')
    rel_out = os.path.relpath(outp, ROOT).replace('\\', '/')
    args = [QEMU, '-M', 'virt', '-cpu', 'cortex-a72', '-m', '1024', '-display', 'none',
            '-serial', 'null', '-monitor', 'none',
            '-semihosting-config', f'enable=on,target=native,arg=akhost,arg={mode},arg={rel_in},arg={rel_out}',
            '-kernel', elf]
    t = time.time()
    logp = os.path.join(BUILD, 'host.log')
    with open(logp, 'wb') as lf:
        p = subprocess.Popen(args, cwd=ROOT, stdout=lf, stderr=subprocess.STDOUT)
        try:
            p.wait(timeout=timeout)
            extra = ''
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            extra = '*** DELAI DEPASSE ***'
    with open(logp, 'rb') as lf:
        log = lf.read().decode('utf-8', 'replace')
    lines = log.splitlines()
    if len(lines) > 60:
        log = '\n'.join(lines[:30] + ['...'] + lines[-30:])
    return log + extra, time.time() - t


def wav_pcm(path):
    with open(path, 'rb') as f:
        d = f.read()
    pos = 12
    fmt = None
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        n = struct.unpack('<I', d[pos + 4:pos + 8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', d[pos + 8:pos + 24])
        if cid == b'data':
            data = d[pos + 8:pos + 8 + n]
            break
        pos += 8 + n + (n & 1)
    tag, ch, rate, _, ba, bits = fmt
    if tag == 3:
        x = np.frombuffer(data, '<f4').reshape(-1, ch)
        return np.clip(np.round(x * 32768), -32768, 32767).astype(np.int16), rate, ch
    if bits == 8:
        x = (np.frombuffer(data, 'u1').astype(np.int32) - 128) << 8
    elif bits == 16:
        x = np.frombuffer(data, '<i2').astype(np.int32)
    elif bits == 24:
        b = np.frombuffer(data, 'u1').reshape(-1, 3).astype(np.int32)
        x = ((b[:, 0] << 8 | b[:, 1] << 16 | b[:, 2] << 24) >> 16)
    else:
        x = np.frombuffer(data, '<i4') >> 16
    return x.astype(np.int16).reshape(-1, ch), rate, ch


def reference(path):
    ref = os.path.splitext(path)[0] + '.ref'
    if path.endswith('.wav'):
        x, rate, ch = wav_pcm(path)
        x.tofile(ref)
        return ref, rate, ch, len(x)
    r = subprocess.run([REFDEC, path, ref], capture_output=True, text=True)
    if r.returncode:
        print('refdec :', r.stderr)
        return None, 0, 0, 0
    rate, ch, fr = map(int, r.stdout.split())
    return ref, rate, ch, fr


def compare(a, b, ch):
    x = np.fromfile(a, '<i2').astype(np.float64)
    y = np.fromfile(b, '<i2').astype(np.float64)
    n = min(len(x), len(y))
    d = x[:n] - y[:n]
    sig = np.sum(y[:n] ** 2)
    noise = np.sum(d ** 2)
    snr = 10 * np.log10(sig / noise) if noise > 0 else float('inf')
    return len(x) // ch, len(y) // ch, int(np.max(np.abs(d))) if n else -1, snr


def test(path, seek=False):
    ref, rate, ch, fr = reference(path)
    out = os.path.splitext(path)[0] + '.out'
    log, dt = host('seek' if seek else 'dec', path, out)
    if seek and ref:
        y = np.fromfile(ref, '<i2')[rate * ch:]
        ref = ref + '.seek'
        y.tofile(ref)
    print(f'== {os.path.basename(path)}')
    for line in log.splitlines():
        if line.strip():
            print('   ', line)
    if ref and os.path.exists(out):
        nx, ny, mx, snr = compare(out, ref, ch)
        dur = fr / rate if rate else 0
        print(f'    trames {nx} (reference {ny}), ecart max {mx}, RSB {snr:.1f} dB, '
              f'banc {dt:.2f} s pour {dur:.2f} s')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd = sys.argv[1]
    if cmd == 'files':
        subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'audiotest', 'mkfiles.py'),
                        os.path.join(BUILD, 'files')], check=True)
    elif cmd == 'ref':
        for p in sys.argv[2:]:
            print(p, reference(p))
    elif cmd == 'mdct':
        assemble()
        for N in [int(v) for v in sys.argv[2:]]:
            M = N // 2
            X = np.random.default_rng(N).standard_normal(M).astype(np.float32)
            inp = os.path.join(BUILD, 'mdct.in')
            with open(inp, 'wb') as f:
                f.write(struct.pack('<I', N) + X.tobytes())
            out = os.path.join(BUILD, 'mdct.out')
            log, dt = host('mdct', inp, out)
            y = np.fromfile(out, np.float32)
            n = np.arange(N)[:, None]
            k = np.arange(M)[None, :]
            ref = (X.astype(np.float64)[None, :] * np.cos(2 * np.pi / N * (n + 0.5 + N / 4) * (k + 0.5))).sum(1)
            print(N, 'ecart relatif max', np.abs(y - ref).max() / np.abs(ref).max() if len(y) == N else log)
    elif cmd == 'pkt':
        # API par paquets : pktmux.py puis ak_codec_* ; comparee a la reference du fichier
        assemble()
        for p in sys.argv[2:]:
            p = os.path.abspath(p)
            pk = os.path.splitext(p)[0] + '.akpk'
            subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'audiotest', 'pktmux.py'), p, pk], check=True)
            ref, rate, ch, fr = reference(p)
            out = os.path.splitext(p)[0] + '.pkt'
            log, dt = host('pkt', pk, out)
            print('==', os.path.basename(p), '(paquets)')
            print('   ', ' | '.join(log.strip().splitlines()))
            if ref and os.path.exists(out):
                nx, ny, mx, snr = compare(out, ref, ch)
                print(f'    trames {nx} (reference {ny}), ecart max {mx}, RSB {snr:.1f} dB')
    elif cmd == 'enc':
        # run.py enc <ew|em128|eo64> entree.wav sortie
        assemble()
        log, dt = host(sys.argv[2], os.path.abspath(sys.argv[3]), os.path.abspath(sys.argv[4]))
        print(log.strip(), '(%.2f s)' % dt)
    elif cmd in ('test', 'seek'):
        assemble()
        for p in sys.argv[2:]:
            test(os.path.abspath(p), cmd == 'seek')


if __name__ == '__main__':
    main()
