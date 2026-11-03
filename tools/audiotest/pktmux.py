"""pktmux.py - decoupe un fichier en paquets pour l'essai de l'API par paquets
d'AudioKit (ak_codec_*, celle de MediaKit) sur le banc (akhost, mode pkt).

    python -I tools/audiotest/pktmux.py entree.(ogg|opus|flac|mp3) sortie.akpk

Format : 'AKPK', format AKF_* (32 bits), frequence, canaux, octets de
l'extradata, extradata, puis (longueur 32 bits, paquet)..., 0xFFFFFFFF.
Extradata comme Matroska : Vorbis = 3 en-tetes en lacage Xiph ; Opus =
OpusHead ; FLAC = 'fLaC' + blocs de metadonnees ; MP3 = rien.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flacenc  # noqa: E402

AKF_FLAC, AKF_MP3, AKF_VORBIS, AKF_OPUS = 2, 3, 4, 5


def ogg_packets(d):
    pos = 0
    cur = b''
    serial = None
    while pos + 27 <= len(d) and d[pos:pos + 4] == b'OggS':
        nseg = d[pos + 26]
        lace = d[pos + 27:pos + 27 + nseg]
        body = pos + 27 + nseg
        ser = struct.unpack('<I', d[pos + 14:pos + 18])[0]
        if serial is None:
            serial = ser
        off = body
        for l in lace:
            if ser == serial:
                cur += d[off:off + l]
                if l < 255:
                    yield cur
                    cur = b''
            off += l
        pos = off


def xiph_lace(pkts):
    out = bytes([len(pkts) - 1])
    for p in pkts[:-1]:
        n = len(p)
        out += b'\xff' * (n // 255) + bytes([n % 255])
    return out + b''.join(pkts)


def mp3_frames(d):
    br1 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
    br2 = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]
    rates = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}
    pos = 0
    if d[:3] == b'ID3':
        pos = 10 + ((d[6] << 21) | (d[7] << 14) | (d[8] << 7) | d[9])
    while pos + 4 <= len(d):
        h = int.from_bytes(d[pos:pos + 4], 'big')
        if h >> 21 != 0x7FF or (h >> 17) & 3 != 1:
            pos += 1
            continue
        ver = (h >> 19) & 3
        bri = (h >> 12) & 15
        sri = (h >> 10) & 3
        if ver == 1 or bri in (0, 15) or sri == 3:
            pos += 1
            continue
        rate = rates[ver][sri]
        fl = (144000 if ver == 3 else 72000) * (br1 if ver == 3 else br2)[bri] // rate + ((h >> 9) & 1)
        yield d[pos:pos + fl], rate, 1 if (h >> 6) & 3 == 3 else 2
        pos += fl


def flac_frames(d):
    pos = 4
    while True:
        hdr = d[pos]
        n = int.from_bytes(d[pos + 1:pos + 4], 'big')
        if hdr & 0x7F == 0:
            si = d[pos + 4:pos + 4 + 34]
        pos += 4 + n
        if hdr & 0x80:
            break
    meta = d[:pos]
    v = int.from_bytes(si[10:18], 'big')
    rate, ch = v >> 44, ((v >> 41) & 7) + 1
    starts = []
    i = pos
    while i + 6 < len(d):
        if d[i] == 0xFF and d[i + 1] & 0xFE == 0xF8:
            # en-tete : CRC-8 sur une longueur plausible
            for hl in range(5, 17):
                if i + hl < len(d) and flacenc.crc8(d[i:i + hl]) == d[i + hl]:
                    starts.append(i)
                    break
        i += 1
    # garde les debuts dont le suivant est aussi un debut valide (CRC-16)
    frames = []
    k = 0
    while k < len(starts):
        s = starts[k]
        for e in starts[k + 1:] + [len(d)]:
            if e > s and flacenc.crc16(d[s:e - 2]) == int.from_bytes(d[e - 2:e], 'big'):
                frames.append(d[s:e])
                k = starts.index(e) if e in starts else len(starts)
                break
        else:
            k += 1
    return meta, rate, ch, frames


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, 'rb').read()
    ext = os.path.splitext(src)[1].lower()
    pkts = []
    if ext in ('.ogg', '.opus', '.oga'):
        allp = list(ogg_packets(d))
        if allp[0].startswith(b'\x01vorbis'):
            fmt = AKF_VORBIS
            extra = xiph_lace(allp[:3])
            ch = allp[0][11]
            rate = struct.unpack('<I', allp[0][12:16])[0]
            pkts = allp[3:]
        else:
            fmt = AKF_OPUS
            extra = allp[0]
            ch = allp[0][9]
            rate = 48000
            pkts = allp[2:]
    elif ext == '.mp3':
        fmt = AKF_MP3
        extra = b''
        fr = list(mp3_frames(d))
        rate, ch = fr[0][1], fr[0][2]
        pkts = [f for f, r, c in fr]
    elif ext == '.flac':
        fmt = AKF_FLAC
        extra, rate, ch, pkts = flac_frames(d)
    else:
        raise SystemExit('format inconnu')
    out = b'AKPK' + struct.pack('<IIII', fmt, rate, ch, len(extra)) + extra
    for p in pkts:
        out += struct.pack('<I', len(p)) + p
    out += struct.pack('<I', 0xFFFFFFFF)
    open(dst, 'wb').write(out)
    print(dst, fmt, rate, ch, len(pkts), 'paquets')


if __name__ == '__main__':
    main()
