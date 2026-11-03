# mkmka.py src dst : Ogg Vorbis / Ogg Opus / MP3 / FLAC -> Matroska audio (essai MediaKit)
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()

def vint(n):
    for l in range(1, 9):
        if n < (1 << (7 * l)) - 1:
            return ((1 << (7 * l)) | n).to_bytes(l, 'big')
def el(id_, payload):
    return id_.to_bytes((id_.bit_length() + 7) // 8, 'big') + vint(len(payload)) + payload
def uint(id_, v):
    return el(id_, v.to_bytes(max(1, (v.bit_length() + 7) // 8), 'big'))
def flt(id_, v):
    return el(id_, struct.pack('>d', v))

def ogg_packets(d):
    pkts, cur, pos = [], b'', 0
    while pos < len(d):
        assert d[pos:pos + 4] == b'OggS'
        gran = struct.unpack_from('<q', d, pos + 6)[0]
        ns = d[pos + 26]; segs = d[pos + 27:pos + 27 + ns]; p = pos + 27 + ns
        for s in segs:
            cur += d[p:p + s]; p += s
            if s < 255:
                pkts.append((cur, gran)); cur = b''
        pos = p
    return pkts

out = []           # (paquet, temps en ms)
if d[:4] == b'OggS':
    pk = ogg_packets(d)
    if pk[0][0][:8] == b'OpusHead':
        codec, priv, rate, ch = 'A_OPUS', pk[0][0], 48000, pk[0][0][9]
        t = 0
        for p, g in pk[2:]:
            out.append((p, t)); t += 20
    else:
        h = [p for p, g in pk[:3]]
        codec, rate, ch = 'A_VORBIS', struct.unpack_from('<I', h[0], 12)[0], h[0][11]
        lace = lambda n: b'\xff' * (n // 255) + bytes([n % 255])
        priv = b'\x02' + lace(len(h[0])) + lace(len(h[1])) + h[0] + h[1] + h[2]
        prev = 0
        for p, g in pk[3:]:
            out.append((p, max(0, prev * 1000 // rate))); prev = max(prev, g)
elif d[:4] == b'fLaC':
    codec, pos = 'A_FLAC', 4
    while True:
        last, n = d[pos] & 0x80, int.from_bytes(d[pos + 1:pos + 4], 'big')
        if d[pos] & 0x7f == 0:
            si = d[pos + 4:pos + 4 + n]
        pos += 4 + n
        if last: break
    priv = d[:pos]
    v = int.from_bytes(si[10:18], 'big'); rate = v >> 44; ch = ((v >> 41) & 7) + 1
    blk = int.from_bytes(si[2:4], 'big')
    def crc8(b):
        c = 0
        for x in b:
            c ^= x
            for _ in range(8): c = ((c << 1) ^ 7) & 255 if c & 128 else (c << 1) & 255
        return c
    starts = []
    for i in range(pos, len(d) - 16):
        if d[i] == 0xFF and d[i + 1] in (0xF8, 0xF9):
            # en-tete : taille variable (UTF-8 du numero), blocs et frequence en plus
            j = i + 4; n = d[j]; k = 1
            while n & (0x80 >> (k - 1)) and k < 8: k += 1
            j += max(1, k - 1) if n & 0x80 else 1
            bs, sr = d[i + 2] >> 4, d[i + 2] & 15
            j += 1 if bs == 6 else 2 if bs == 7 else 0
            j += 1 if sr == 12 else 2 if sr in (13, 14) else 0
            if j < len(d) and crc8(d[i:j]) == d[j]: starts.append(i)
    starts.append(len(d))
    for n, (a, b) in enumerate(zip(starts, starts[1:])):
        out.append((d[a:b], n * blk * 1000 // rate))
else:
    codec, priv, pos = 'A_MPEG/L3', None, 0
    if d[:3] == b'ID3':
        pos = 10 + ((d[6] << 21) | (d[7] << 14) | (d[8] << 7) | d[9])
    BR = {1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
          2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]}
    SR = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}
    t = 0.0; rate = ch = 0
    while pos + 4 <= len(d):
        h = int.from_bytes(d[pos:pos + 4], 'big')
        if h >> 21 != 0x7FF: pos += 1; continue
        ver = (h >> 19) & 3; bri = (h >> 12) & 15; sri = (h >> 10) & 3; pad = (h >> 9) & 1
        rate = SR[ver][sri]; ch = 1 if (h >> 6) & 3 == 3 else 2
        spf = 1152 if ver == 3 else 576
        size = (spf // 8) * BR[1 if ver == 3 else 2][bri] * 1000 // rate + pad
        out.append((d[pos:pos + size], int(t))); t += spf * 1000 / rate; pos += size

audio = flt(0xB5, float(rate)) + uint(0x9F, ch)
te = uint(0xD7, 1) + uint(0x73C5, 1) + uint(0x83, 2) + el(0x86, codec.encode()) + el(0xE1, audio)
if priv: te += el(0x63A2, priv)
if codec == 'A_OPUS':
    te += uint(0x56AA, int(struct.unpack_from('<H', priv, 10)[0] * 1e9 / 48000)) + uint(0x56BB, 80000000)
tracks = el(0x1654AE6B, el(0xAE, te))
info = el(0x1549A966, uint(0x2AD7B1, 1000000) + el(0x4D80, b'mkmka') + el(0x5741, b'mkmka')
          + flt(0x4489, float(out[-1][1] + 20)))
clusters = b''
i = 0
while i < len(out):
    base = out[i][1]; body = uint(0xE7, base)
    while i < len(out) and out[i][1] - base < 30000:
        body += el(0xA3, b'\x81' + struct.pack('>h', out[i][1] - base) + b'\x80' + out[i][0]); i += 1
    clusters += el(0x1F43B675, body)
ebml = el(0x1A45DFA3, uint(0x4286, 1) + uint(0x42F7, 1) + uint(0x42F2, 4) + uint(0x42F3, 8)
          + el(0x4282, b'matroska') + uint(0x4287, 4) + uint(0x4285, 2))
open(dst, 'wb').write(ebml + el(0x18538067, info + tracks + clusters))
print(dst, codec, rate, ch, len(out), 'paquets')
