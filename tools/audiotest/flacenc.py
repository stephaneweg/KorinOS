"""flacenc.py - petit encodeur FLAC (RFC 9639) pour les fichiers d'essai
d'AudioKit : sous-trames CONSTANT, VERBATIM, FIXED, LPC (Levinson-Durbin),
residu de Rice partitionne (et l'echappement), bits perdus, les quatre
modes stereo. Pas optimise : il sert a exercer tous les chemins du decodeur.
Verifie par la reference (refdec : dr_flac).

    from flacenc import encode
    encode(chemin, echantillons int (n, canaux), frequence, bits, bloc=4096,
           variable=False, tags={'TITLE': ...})
"""
import struct
import numpy as np


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, v, bits):
        if bits == 0:
            return
        v &= (1 << bits) - 1
        self.acc = (self.acc << bits) | v
        self.n += bits
        while self.n >= 8:
            self.n -= 8
            self.out.append((self.acc >> self.n) & 0xFF)
        self.acc &= (1 << self.n) - 1

    def sput(self, v, bits):
        self.put(v & ((1 << bits) - 1), bits)

    def unary(self, q):
        while q >= 32:
            self.put(0, 32)
            q -= 32
        self.put(1, q + 1)

    def align(self):
        if self.n:
            self.put(0, 8 - self.n)

    def bytes(self):
        assert self.n == 0
        return bytes(self.out)


def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def crc16(data):
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def utf8num(v):
    if v < 0x80:
        return bytes([v])
    for n in range(2, 8):
        if v < (1 << (5 * n + 1)) or n == 7:
            break
    out = []
    for _ in range(n - 1):
        out.append(0x80 | (v & 0x3F))
        v >>= 6
    lead = (0xFF << (8 - n)) & 0xFF
    out.append(lead | v)
    return bytes(reversed(out))


def rice_bits(res, k):
    u = np.where(res >= 0, 2 * res, -2 * res - 1).astype(np.int64)
    return int(np.sum(u >> k)) + len(u) * (k + 1)


def best_partition(res, bs, order, maxpo, maxk):
    best = None
    for po in range(0, maxpo + 1):
        if bs % (1 << po) or (bs >> po) < order:
            continue
        ps = bs >> po
        parts = []
        total = 0
        start = 0
        for p in range(1 << po):
            cnt = ps - (order if p == 0 else 0)
            seg = res[start:start + cnt]
            start += cnt
            if cnt == 0:
                parts.append((0, seg))
                total += 4
                continue
            bk = min(range(maxk + 1), key=lambda k: rice_bits(seg, k))
            parts.append((bk, seg))
            total += 4 + rice_bits(seg, bk)
        if best is None or total < best[0]:
            best = (total, po, parts)
    return best


def write_residual(bw, res, bs, order, escape_first=False):
    tot, po, parts = best_partition(res, bs, order, 4, 14)
    method = 0
    if any(k > 14 for k, _ in parts):
        method = 1
    bw.put(method, 2)
    bw.put(po, 4)
    pbits = 4 if method == 0 else 5
    esc = 15 if method == 0 else 31
    for i, (k, seg) in enumerate(parts):
        if escape_first and i == 0 and len(seg):
            nb = max(1, int(np.max(np.abs(seg))).bit_length() + 1)
            bw.put(esc, pbits)
            bw.put(nb, 5)
            for v in seg:
                bw.sput(int(v), nb)
            continue
        bw.put(k, pbits)
        for v in seg:
            v = int(v)
            u = 2 * v if v >= 0 else -2 * v - 1
            bw.unary(u >> k)
            if k:
                bw.put(u & ((1 << k) - 1), k)


FIXED = {0: [], 1: [1], 2: [2, -1], 3: [3, -3, 1], 4: [4, -6, 4, -1]}


def lpc_coefs(x, order, prec):
    x = x.astype(np.float64)
    w = x * np.hanning(len(x)) if len(x) > 16 else x
    r = np.array([np.dot(w[:len(w) - i], w[i:]) for i in range(order + 1)])
    if r[0] == 0:
        return None
    r[0] *= 1.0 + 1e-9
    a = np.zeros(order + 1)
    a[0] = 1.0
    e = r[0]
    for i in range(1, order + 1):
        k = -(r[i] + np.dot(a[1:i], r[i - 1:0:-1])) / e
        a[1:i + 1] = a[1:i + 1] + k * a[i - 1::-1][:i]
        e *= (1 - k * k)
        if e <= 0:
            return None
    c = -a[1:]
    cmax = np.max(np.abs(c))
    if cmax == 0:
        return None
    shift = prec - 1 - int(np.floor(np.log2(cmax))) - 1
    shift = max(0, min(15, shift))
    q = np.round(c * (1 << shift)).astype(np.int64)
    lim = (1 << (prec - 1)) - 1
    q = np.clip(q, -lim - 1, lim)
    return q, shift


def predict(x, coefs, shift):
    n = len(x)
    o = len(coefs)
    res = np.array(x[:], dtype=np.int64)
    for i in range(o, n):
        acc = 0
        for j in range(o):
            acc += int(coefs[j]) * int(x[i - 1 - j])
        res[i] = x[i] - (acc >> shift)
    return res[o:]


def predict_fast(x, coefs, shift):
    x = np.asarray(x, dtype=np.int64)
    o = len(coefs)
    acc = np.zeros(len(x) - o, dtype=np.int64)
    for j in range(o):
        acc += int(coefs[j]) * x[o - 1 - j:len(x) - 1 - j]
    return x[o:] - (acc >> shift)


def encode_subframe(bw, x, bps, kind_hint, frame_index):
    x = np.asarray(x, dtype=np.int64)
    bs = len(x)
    # bits perdus
    wasted = 0
    nz = x[x != 0]
    if len(nz):
        v = int(np.bitwise_or.reduce(nz))
        while not (v >> wasted) & 1:
            wasted += 1
    else:
        wasted = 0
    if np.all(x == x[0]):
        bw.put(0, 1)
        bw.put(0, 6)
        bw.put(0, 1)
        bw.sput(int(x[0]), bps)
        return
    if wasted:
        x = x >> wasted
        bps -= wasted
    bw.put(0, 1)
    choice = kind_hint
    if choice == 'verbatim':
        bw.put(1, 6)
    if choice != 'verbatim':
        best = None
        for o in range(5):
            if bs <= o:
                continue
            res = predict_fast(x, FIXED[o], 0)
            b = best_partition(res, bs, o, 4, 14)[0] + o * bps
            if best is None or b < best[0]:
                best = (b, 'fixed', o, None, 0, res)
        if choice in ('auto', 'lpc'):
            for o in (2, 4, 8, 12, 32):
                if bs <= o:
                    continue
                prec = 12 if o < 32 else 15
                lc = lpc_coefs(x, o, prec)
                if lc is None:
                    continue
                q, sh = lc
                res = predict_fast(x, q, sh)
                if np.max(np.abs(res)) >= (1 << 30):
                    continue
                b = best_partition(res, bs, o, 4, 14)[0] + o * bps + o * prec + 9
                if choice == 'lpc' and o == 32:
                    b = -1
                if best is None or b < best[0]:
                    best = (b, 'lpc', o, q, sh, res, prec)
        if best[1] == 'fixed':
            bw.put(8 + best[2], 6)
        else:
            bw.put(32 + best[2] - 1, 6)
    if wasted:
        bw.put(1, 1)
        bw.unary(wasted - 1)
    else:
        bw.put(0, 1)
    if choice == 'verbatim':
        for v in x:
            bw.sput(int(v), bps)
        return
    o = best[2]
    for v in x[:o]:
        bw.sput(int(v), bps)
    if best[1] == 'lpc':
        prec = best[6]
        bw.put(prec - 1, 4)
        bw.sput(best[4], 5)
        for c in best[3]:
            bw.sput(int(c), prec)
    write_residual(bw, best[5], bs, o, escape_first=(frame_index % 7 == 3))


def encode(path, samples, rate, bps, block=4096, variable=False, tags=None, modes=None):
    samples = np.asarray(samples, dtype=np.int64)
    if samples.ndim == 1:
        samples = samples[:, None]
    n, ch = samples.shape
    frames = []
    pos = 0
    fi = 0
    minfs, maxfs = 1 << 30, 0
    sizes = [block] if not variable else [block, 1152, 4608, 576, 2048, 192, 1000]
    while pos < n:
        bs = sizes[fi % len(sizes)]
        bs = min(bs, n - pos)
        blk = samples[pos:pos + bs]
        hdr = bytearray()
        hdr += bytes([0xFF, 0xF8 | (1 if variable else 0)])
        # code de taille de bloc
        codes = {192: 1, 576: 2, 1152: 3, 2304: 4, 4608: 5, 256: 8, 512: 9, 1024: 10, 2048: 11, 4096: 12, 8192: 13, 16384: 14, 32768: 15}
        extra = b''
        if bs in codes:
            bcode = codes[bs]
        elif bs <= 256:
            bcode = 6
            extra = bytes([bs - 1])
        else:
            bcode = 7
            extra = struct.pack('>H', bs - 1)
        rcodes = {88200: 1, 176400: 2, 192000: 3, 8000: 4, 16000: 5, 22050: 6, 24000: 7, 32000: 8, 44100: 9, 48000: 10, 96000: 11}
        rextra = b''
        if fi % 5 == 4:          # frequence dans l'en-tete
            if rate % 1000 == 0 and rate // 1000 < 256:
                rcode, rextra = 12, bytes([rate // 1000])
            elif rate < 65536:
                rcode, rextra = 13, struct.pack('>H', rate)
            else:
                rcode, rextra = 14, struct.pack('>H', rate // 10)
        elif fi % 5 == 2:
            rcode = 0
        else:
            rcode = rcodes.get(rate, 0)
        hdr.append(bcode << 4 | rcode)
        # mode stereo
        if ch == 2:
            m = modes[fi % len(modes)] if modes else ['ind', 'ls', 'rs', 'ms'][fi % 4]
        else:
            m = 'ind'
        cha = {'ind': ch - 1, 'ls': 8, 'rs': 9, 'ms': 10}[m]
        scode = {8: 1, 12: 2, 16: 4, 20: 5, 24: 6, 32: 7}[bps]
        if fi % 3 == 1:
            scode = 0
        hdr.append(cha << 4 | scode << 1)
        hdr += utf8num(pos if variable else fi)
        hdr += extra + rextra
        hdr.append(crc8(hdr))
        bw = BitWriter()
        for b in hdr:
            bw.put(b, 8)
        L = blk[:, 0]
        R = blk[:, 1] if ch == 2 else None
        if m == 'ind':
            chans = [(blk[:, c], bps) for c in range(ch)]
        elif m == 'ls':
            chans = [(L, bps), (L - R, bps + 1)]
        elif m == 'rs':
            chans = [(L - R, bps + 1), (R, bps)]
        else:
            chans = [((L + R) >> 1, bps), (L - R, bps + 1)]
        for ci, (x, b) in enumerate(chans):
            hint = 'auto'
            if fi % 11 == 5 and ci == 0:
                hint = 'verbatim'
            if fi % 13 == 7:
                hint = 'lpc'
            encode_subframe(bw, x, b, hint, fi)
        bw.align()
        data = bw.bytes()
        data += struct.pack('>H', crc16(data))
        frames.append(data)
        minfs = min(minfs, len(data))
        maxfs = max(maxfs, len(data))
        pos += bs
        fi += 1
    out = bytearray(b'fLaC')
    si = bytearray()
    minbs = maxbs = block
    if variable:
        minbs, maxbs = 16, max(sizes)
    si += struct.pack('>HH', minbs, maxbs)
    si += minfs.to_bytes(3, 'big') + maxfs.to_bytes(3, 'big')
    v = (rate << 44) | ((ch - 1) << 41) | ((bps - 1) << 36) | n
    si += v.to_bytes(8, 'big')
    si += bytes(16)
    blocks = [(0, bytes(si))]
    # SEEKTABLE : un point par seconde
    pts = bytearray()
    off = 0
    s = 0
    for i, f in enumerate(frames):
        if i % max(1, rate // block) == 0:
            pts += struct.pack('>QQH', s, off, min(block, n - s))
        off += len(f)
        s += sizes[i % len(sizes)] if variable else block
    pts += struct.pack('>QQH', 0xFFFFFFFFFFFFFFFF, 0, 0)
    blocks.append((3, bytes(pts)))
    if tags:
        vc = bytearray()
        ven = b'flacenc.py'
        vc += struct.pack('<I', len(ven)) + ven
        items = [f'{k}={v}'.encode('utf-8') for k, v in tags.items()]
        vc += struct.pack('<I', len(items))
        for it in items:
            vc += struct.pack('<I', len(it)) + it
        blocks.append((4, bytes(vc)))
    blocks.append((1, bytes(10)))
    for i, (t, d) in enumerate(blocks):
        last = 0x80 if i == len(blocks) - 1 else 0
        out.append(last | t)
        out += len(d).to_bytes(3, 'big')
        out += d
    for f in frames:
        out += f
    with open(path, 'wb') as fo:
        fo.write(out)
    return len(out)
