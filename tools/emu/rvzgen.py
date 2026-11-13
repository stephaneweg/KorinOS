"""rvzgen.py -- images d'essai RVZ / WIA pour gcemu (src/apps/gcemu/rvz.cpp), écrit d'après la description
publique du format (docs/WiaAndRvz.md de Dolphin), sans son code : on ne dépend d'aucun outil de Dolphin.

  python -I tools/emu/rvzgen.py iso SORTIE.iso [--mb N] [--seed S]
        une image GameCube d'essai (à nous) : en-tête (magique, identifiant KRVZ01), zones de données au hasard,
        zones de zéros et zones de remplissage (le générateur de Fibonacci retardé des disques, graines au
        hasard) ; leur liste dans SORTIE.iso.junk (JSON) pour l'encodeur
  python -I tools/emu/rvzgen.py rvz ENTREE.iso SORTIE.rvz [--chunk K] [--comp zstd|none] [--wia] [--level L]
        l'encodeur : groupes de K Kio (128 par défaut), Zstandard (paquet Python zstandard) ou rien ;
        RVZ : remplissage empaqueté (d'après ENTREE.iso.junk s'il existe), groupe de zéros = taille 0,
        groupe stocké tel quel s'il ne gagne rien ; --wia : un WIA (ni empaquetage ni stockage brut)
  python -I tools/emu/rvzgen.py check ENTREE.rvz ENTREE.iso
        décodeur Python indépendant de rvz.cpp : relit tout et compare
"""
import argparse
import bisect
import hashlib
import json
import os
import random
import struct
import sys

ALIGN = 0x8000


# --- le générateur de remplissage : x[n] = x[n-521] ^ x[n-32] sur des mots de 32 bits, les octets rendus
# grand-boutistes, les bits 16..23 pris aux bits 18..25 ; graine de 17 mots étendue par
# x[i] = (x[i-17] << 23) ^ (x[i-16] >> 9) ^ x[i-1], puis 4 tours
class Junk:
    K, J = 521, 32

    def __init__(self, seed_words):
        w = list(seed_words)
        for i in range(17, self.K):
            w.append(((w[i - 17] << 23) & 0xFFFFFFFF) ^ (w[i - 16] >> 9) ^ w[i - 1])
        self.w = [(x & 0xFF00FFFF) | ((x >> 2) & 0x00FF0000) for x in w]
        for _ in range(4):
            self.step()
        self.pos = 0
        self.block = self.render()

    def step(self):
        w = self.w
        for i in range(self.J):
            w[i] ^= w[i + self.K - self.J]
        for i in range(self.J, self.K):
            w[i] ^= w[i - self.J]

    def render(self):
        return struct.pack('>%dI' % self.K, *self.w)

    def skip(self, n):
        n += self.pos
        while n >= self.K * 4:
            self.step()
            n -= self.K * 4
        self.block = self.render()
        self.pos = n

    def get(self, n):
        out = bytearray()
        while n:
            k = min(n, self.K * 4 - self.pos)
            out += self.block[self.pos:self.pos + k]
            self.pos += k
            n -= k
            if self.pos == self.K * 4:
                self.step()
                self.block = self.render()
                self.pos = 0
        return bytes(out)


def junk_bytes(seed, disc_off, n):
    """n octets de remplissage d'une zone qui commence à disc_off (générateur reparti de la graine, avancé de
    disc_off % 0x8000), comme le lit le décodeur"""
    j = Junk(seed)
    j.skip(disc_off % ALIGN)
    return j.get(n)


def make_iso(path, mb, seed):
    rnd = random.Random(seed)
    size = mb << 20
    iso = bytearray(size)
    hdr = bytearray(0x440)
    hdr[0:6] = b'KRVZ01'
    struct.pack_into('>I', hdr, 0x1C, 0xC2339F3D)
    title = b'Korin RVZ test disc'
    hdr[0x20:0x20 + len(title)] = title
    iso[0:0x440] = hdr
    # un apploader minimal (PowerPC, à 0x81200000) : il donne ses trois fonctions, main demande une lecture de
    # 64 Kio à 1 Mio du disque vers 0x81300000, close rend le « jeu » (une boucle « b . ») : le disque démarre
    code = [0x3CC08120, 0x60C60040, 0x90C30000,   # entrée : *r3 = init
            0x3CC08120, 0x60C60050, 0x90C40000,   # *r4 = main
            0x3CC08120, 0x60C60070, 0x90C50000,   # *r5 = close
            0x4E800020] + [0] * 6 + \
           [0x4E800020] + [0] * 3 + \
           [0x3CC08130, 0x90C30000, 0x3CC00001, 0x90C40000, 0x3CC00010, 0x90C50000, 0x38600000, 0x4E800020] + \
           [0x3C608120, 0x60630090, 0x4E800020] + [0] * 5 + \
           [0x48000000] + [0] * 7
    app = struct.pack('>%dI' % len(code), *code)
    iso[0x2440:0x2450] = b'2026/11/13\0\0\0\0\0\0'
    struct.pack_into('>III', iso, 0x2450, 0x81200000, len(app), 0)
    iso[0x2460:0x2460 + len(app)] = app
    pos = 0x2460 + len(app)
    junk = []
    kinds = ['data', 'zero', 'junk']
    while pos < size:
        # zones de 1 Kio à 600 Kio, à des positions quelconques (franchissent groupes et blocs de 0x8000)
        n = min(size - pos, rnd.randrange(1, 600) * 1024 + rnd.randrange(0, 4) * rnd.randrange(1, 997))
        k = rnd.choice(kinds)
        if k == 'data':
            iso[pos:pos + n] = rnd.randbytes(n)
        elif k == 'junk':
            # une graine par bloc de 0x8000 commencé (comme le décodeur la repart)
            p = pos
            while p < pos + n:
                e = min(pos + n, (p // ALIGN + 1) * ALIGN)
                s = [rnd.getrandbits(32) for _ in range(17)]
                iso[p:e] = junk_bytes(s, p, e - p)
                junk.append([p, e - p, s])
                p = e
        pos += n
    with open(path, 'wb') as f:
        f.write(iso)
    with open(path + '.junk', 'w') as f:
        json.dump(junk, f)
    print('%s : %d Mio, %d zones de remplissage, sha1 %s' % (path, mb, len(junk), hashlib.sha1(iso).hexdigest()))


def compress(data, comp, level):
    if comp == 0:
        return data
    import zstandard
    return zstandard.ZstdCompressor(level=level).compress(data)


def decompress(data, comp, size):
    if comp == 0:
        return data[:size]
    import zstandard
    return zstandard.ZstdDecompressor().decompress(data, max_output_size=size)


def make_rvz(src, dst, chunk_kib, comp_name, wia, level):
    iso = open(src, 'rb').read()
    junk = []
    if not wia and os.path.exists(src + '.junk'):
        junk = json.load(open(src + '.junk'))
    jmap = {j[0]: (j[1], j[2]) for j in junk}
    jstarts = sorted(jmap)
    comp = {'none': 0, 'zstd': 5}[comp_name]
    chunk = chunk_kib * 1024
    size = len(iso)
    # une seule zone brute : tout le disque après les 0x80 octets de l'en-tête
    raw_off, raw_end = 0x80, size
    start = raw_off - raw_off % ALIGN
    ngroups = (raw_end - start + chunk - 1) // chunk
    body = bytearray()
    groups = []
    data_base = 0x48 + 0xDC + 0x100  # en-têtes, puis les tables (placées après les données : on les connaît à la fin)
    for g in range(ngroups):
        gs = start + g * chunk
        ge = min(gs + chunk, raw_end)
        part = iso[gs:ge]
        packed = 0
        payload = part
        if not wia:
            if part.count(0) == len(part):
                groups.append((0, 0, 0))
                continue
            # empaquetage : enregistrements de données et de remplissage (zones connues dans ce groupe)
            recs = bytearray()
            p = gs
            any_junk = False
            while p < ge:
                if p in jmap and p + jmap[p][0] <= ge:
                    n, seed = jmap[p]
                    recs += struct.pack('>I', 0x80000000 | n) + struct.pack('>17I', *seed)
                    p += n
                    any_junk = True
                    continue
                k = bisect.bisect_right(jstarts, p)
                q = min(ge, jstarts[k]) if k < len(jstarts) else ge
                recs += struct.pack('>I', q - p) + iso[p:q]
                p = q
            if any_junk:
                payload = bytes(recs)
                packed = len(payload)
        c = compress(payload, comp, level)
        flag = 0x80000000
        if not wia and comp and len(c) >= len(payload):
            c, flag = payload, 0  # (stocké tel quel)
        if not wia and comp == 0:
            flag = 0
        while len(body) % 4:
            body += b'\0'
        off = data_base + len(body)
        body += c
        groups.append((off >> 2, len(c) | (flag if not wia else 0), packed))
    while len(body) % 4:
        body += b'\0'
    raw_tab = struct.pack('>QQII', raw_off, raw_end - raw_off, 0, ngroups)
    if wia:
        grp_tab = b''.join(struct.pack('>II', o, s) for o, s, _ in groups)
    else:
        grp_tab = b''.join(struct.pack('>III', o, s, p) for o, s, p in groups)
    raw_c = compress(raw_tab, comp, level)
    grp_c = compress(grp_tab, comp, level)
    raw_pos = data_base + len(body)
    grp_pos = raw_pos + len(raw_c)
    h2 = bytearray(0xDC)
    struct.pack_into('>IIiI', h2, 0, 1, comp, level, chunk)
    h2[0x10:0x90] = iso[0:0x80]
    struct.pack_into('>II', h2, 0x90, 0, 0)            # pas de partitions
    struct.pack_into('>I', h2, 0xB4, 1)
    struct.pack_into('>Q', h2, 0xB8, raw_pos)
    struct.pack_into('>I', h2, 0xC0, len(raw_c))
    struct.pack_into('>I', h2, 0xC4, ngroups)
    struct.pack_into('>Q', h2, 0xC8, grp_pos)
    struct.pack_into('>I', h2, 0xD0, len(grp_c))
    total = grp_pos + len(grp_c)
    h1 = bytearray(0x48)
    h1[0:4] = b'WIA\x01' if wia else b'RVZ\x01'
    struct.pack_into('>III', h1, 4, 0x01000000 if wia else 0x01000000, 0x00090000 if wia else 0x00030000, len(h2))
    h1[0x10:0x24] = hashlib.sha1(h2).digest()
    struct.pack_into('>QQ', h1, 0x24, size, total)
    h1[0x34:0x48] = hashlib.sha1(h1[:0x34]).digest()
    with open(dst, 'wb') as f:
        f.write(h1)
        f.write(h2)
        f.write(b'\0' * (data_base - 0x48 - len(h2)))
        f.write(body)
        f.write(raw_c)
        f.write(grp_c)
    nz = sum(1 for g in groups if g[1] == 0)
    npk = sum(1 for g in groups if g[2])
    print('%s : %s %s, groupes de %d Kio : %d (%d de zéros, %d empaquetés), %d -> %d octets' %
          (dst, 'WIA' if wia else 'RVZ', comp_name, chunk_kib, ngroups, nz, npk, size, total))


def check(rvz, isop):
    d = open(rvz, 'rb').read()
    iso = open(isop, 'rb').read()
    is_rvz = d[0:3] == b'RVZ'
    h2 = d[0x48:0x48 + struct.unpack('>I', d[0x0C:0x10])[0]]
    comp, chunk = struct.unpack('>I', h2[4:8])[0], struct.unpack('>I', h2[0x0C:0x10])[0]
    size = struct.unpack('>Q', d[0x24:0x2C])[0]
    nraw, rpos, rsz, ng, gpos, gsz = (struct.unpack('>I', h2[0xB4:0xB8])[0], struct.unpack('>Q', h2[0xB8:0xC0])[0],
                                       struct.unpack('>I', h2[0xC0:0xC4])[0], struct.unpack('>I', h2[0xC4:0xC8])[0],
                                       struct.unpack('>Q', h2[0xC8:0xD0])[0], struct.unpack('>I', h2[0xD0:0xD4])[0])
    raws = decompress(d[rpos:rpos + rsz], comp, nraw * 24)
    gw = 12 if is_rvz else 8
    gt = decompress(d[gpos:gpos + gsz], comp, ng * gw)
    out = bytearray(size)
    out[0:0x80] = h2[0x10:0x90]
    for i in range(nraw):
        off, n, g0, gn = struct.unpack('>QQII', raws[i * 24:i * 24 + 24])
        s, e = off - off % ALIGN, off + n
        for k in range(gn):
            gs = s + k * chunk
            ge = min(gs + chunk, e)
            o, sz = struct.unpack('>II', gt[(g0 + k) * gw:(g0 + k) * gw + 8])
            pk = struct.unpack('>I', gt[(g0 + k) * gw + 8:(g0 + k) * gw + 12])[0] if is_rvz else 0
            cmp_ = (sz >> 31) if is_rvz else 1
            sz &= 0x7FFFFFFF if is_rvz else 0xFFFFFFFF
            if sz == 0:
                data = bytes(ge - gs)
            else:
                raw = d[o << 2:(o << 2) + sz]
                data = decompress(raw, comp, pk or (ge - gs)) if cmp_ else raw[:pk or (ge - gs)]
                if pk:
                    r = bytearray()
                    p = 0
                    while len(r) < ge - gs:
                        v = struct.unpack('>I', data[p:p + 4])[0]
                        p += 4
                        if v >> 31:
                            seed = struct.unpack('>17I', data[p:p + 68])
                            p += 68
                            r += junk_bytes(seed, gs + len(r), v & 0x7FFFFFFF)
                        else:
                            r += data[p:p + v]
                            p += v
                    data = bytes(r)
            lo = max(gs, 0x80)
            out[lo:ge] = data[lo - gs:ge - gs]
    same = bytes(out) == iso
    print('%s : %s (%d octets)' % (rvz, 'mêmes octets que l\'ISO' if same else 'DIFFÉRENT', size))
    return 0 if same else 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('iso')
    a.add_argument('out')
    a.add_argument('--mb', type=int, default=24)
    a.add_argument('--seed', type=int, default=1)
    a = sub.add_parser('rvz')
    a.add_argument('src')
    a.add_argument('out')
    a.add_argument('--chunk', type=int, default=128)
    a.add_argument('--comp', default='zstd', choices=['zstd', 'none'])
    a.add_argument('--wia', action='store_true')
    a.add_argument('--level', type=int, default=5)
    a = sub.add_parser('check')
    a.add_argument('rvz')
    a.add_argument('iso')
    o = ap.parse_args()
    if o.cmd == 'iso':
        make_iso(o.out, o.mb, o.seed)
    elif o.cmd == 'rvz':
        make_rvz(o.src, o.out, o.chunk, o.comp, o.wia, o.level)
    else:
        sys.exit(check(o.rvz, o.iso))


if __name__ == '__main__':
    main()
