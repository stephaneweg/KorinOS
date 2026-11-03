"""cut_webm.py ENTREE SORTIE DEBUT_S DUREE_S : un extrait d'un fichier WebM / Matroska, sans
reencoder (pas de ffmpeg) : les Clusters de DEBUT_S a DEBUT_S + DUREE_S (le premier commence par
une image cle ; les blocs apres la fin coupes), temps ramenes a 0, avec Info (duree), Tracks, et des Cues neuves (une par
Cluster : la recherche marche). Sert aux videos d'essai de MediaKit (skel/sd0/home/video).
Les Clusters doivent avoir une taille connue (fichiers de ffmpeg, mkvmerge)."""
import struct
import sys

ID_SEGMENT, ID_INFO, ID_TRACKS, ID_CLUSTER = 0x18538067, 0x1549A966, 0x1654AE6B, 0x1F43B675
ID_TIMECODE, ID_SIMPLEBLOCK, ID_BLOCKGROUP, ID_BLOCK, ID_REFBLOCK = 0xE7, 0xA3, 0xA0, 0xA1, 0xFB
ID_DURATION, ID_TCSCALE, ID_TRACKENTRY, ID_TRACKNUMBER, ID_TRACKTYPE = 0x4489, 0x2AD7B1, 0xAE, 0xD7, 0x83


def rd_id(b, p):
    c = b[p]
    n = 1 if c & 0x80 else 2 if c & 0x40 else 3 if c & 0x20 else 4
    return int.from_bytes(b[p:p + n], "big"), p + n


def rd_size(b, p):
    c = b[p]
    n = 1
    while not (c & (0x80 >> (n - 1))):
        n += 1
    v = c & (0xFF >> n)
    for i in range(1, n):
        v = (v << 8) | b[p + i]
    if v == (1 << (7 * n)) - 1:
        v = None                        # taille inconnue
    return v, p + n


def children(b, p, end):
    while p < end:
        eid, q = rd_id(b, p)
        size, q = rd_size(b, q)
        if size is None:
            raise SystemExit("element de taille inconnue : non pris")
        yield eid, p, q, q + size
        p = q + size


def enc_id(eid):
    n = 1 if eid < 0x100 else 2 if eid < 0x10000 else 3 if eid < 0x1000000 else 4
    return eid.to_bytes(n, "big")


def enc_size(n, width=None):
    if width is None:
        width = 1
        while n >= (1 << (7 * width)) - 1:
            width += 1
    return ((1 << (7 * width)) | n).to_bytes(width, "big") if width < 8 else bytes([1]) + n.to_bytes(7, "big")


def elem(eid, data, width=None):
    return enc_id(eid) + enc_size(len(data), width) + data


def uint(eid, v, n=None):
    if n is None:
        n = max(1, (v.bit_length() + 7) // 8)
    return elem(eid, v.to_bytes(n, "big"))


def main():
    src, dst, start, dur = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
    b = open(src, "rb").read()
    eid, p = rd_id(b, 0)
    size, p = rd_size(b, p)
    ebml = b[0:p + size]
    eid, p = rd_id(b, p + size)
    assert eid == ID_SEGMENT
    size, seg = rd_size(b, p)
    seg_end = len(b) if size is None else seg + size
    info = tracks = None
    clusters = []
    tcscale = 1000000
    video = None
    for eid, s, d, e in children(b, seg, seg_end):
        if eid == ID_INFO:
            info = (s, d, e)
            for i2, s2, d2, e2 in children(b, d, e):
                if i2 == ID_TCSCALE:
                    tcscale = int.from_bytes(b[d2:e2], "big")
        elif eid == ID_TRACKS:
            tracks = b[s:e]
            for i2, s2, d2, e2 in children(b, d, e):
                if i2 != ID_TRACKENTRY:
                    continue
                num = typ = None
                for i3, s3, d3, e3 in children(b, d2, e2):
                    if i3 == ID_TRACKNUMBER:
                        num = int.from_bytes(b[d3:e3], "big")
                    elif i3 == ID_TRACKTYPE:
                        typ = int.from_bytes(b[d3:e3], "big")
                if typ == 1 and video is None:
                    video = num
        elif eid == ID_CLUSTER:
            clusters.append((d, e))
    unit = tcscale / 1e9                  # secondes par tic
    t0, t1 = start / unit, (start + dur) / unit
    keep = []
    base = None
    last = 0
    for d, e in clusters:
        tc = None
        key = None
        blocks = []
        for i2, s2, d2, e2 in children(b, d, e):
            if i2 == ID_TIMECODE:
                tc = int.from_bytes(b[d2:e2], "big")
            elif i2 in (ID_SIMPLEBLOCK, ID_BLOCKGROUP):
                blocks.append((i2, s2, d2, e2))
        rels = []
        for i2, s2, d2, e2 in blocks:
            bt, rel, isref = None, 0, False
            if i2 == ID_SIMPLEBLOCK:
                bt, q = rd_size(b, d2)
                rel = struct.unpack(">h", b[q:q + 2])[0]
                isref = not (b[q + 2] & 0x80)
            else:
                for i3, s3, d3, e3 in children(b, d2, e2):
                    if i3 == ID_BLOCK:
                        bt, q = rd_size(b, d3)
                        rel = struct.unpack(">h", b[q:q + 2])[0]
                    elif i3 == ID_REFBLOCK:
                        isref = True
            if bt == video and key is None:
                key = not isref
            rels.append(rel)
        if base is None:
            if tc >= t0 and key:
                base = tc
                t1 = base + dur / unit      # (la duree compte depuis l'image cle)
            else:
                continue
        if tc >= t1:
            break
        body = uint(ID_TIMECODE, tc - base)
        for (i2, s2, d2, e2), r in zip(blocks, rels):
            if tc + r < t1:                 # (les blocs apres la fin : coupes)
                body += b[s2:e2]
                last = max(last, tc + r)
        keep.append((tc - base, body))
    if not keep:
        raise SystemExit("aucun Cluster gardé (début après la fin ?)")
    # Info : la duree (tics, flottant 64 bits) ; le reste tel quel
    s, d, e = info
    ibody = b""
    for i2, s2, d2, e2 in children(b, d, e):
        if i2 != ID_DURATION:
            ibody += b[s2:e2]
    ibody += elem(ID_DURATION, struct.pack(">d", float(last - base + 40e-3 / unit)))
    head = elem(ID_INFO, ibody) + tracks
    # Cues : une par Cluster, positions sur 8 octets (tailles fixes)
    def cues(pos):
        out = b""
        for (t, _), cp in zip(keep, pos):
            tp = uint(0xF7, video) + uint(0xF1, cp, 8)
            out += elem(0xBB, uint(0xB3, t, 8) + elem(0xB7, tp))
        return elem(0x1C53BB6B, out)
    clus = [elem(ID_CLUSTER, body) for _, body in keep]
    cues_len = len(cues([0] * len(keep)))
    pos = []
    p = len(head) + cues_len
    for c in clus:
        pos.append(p)
        p += len(c)
    segbody = head + cues(pos) + b"".join(clus)
    out = ebml + enc_id(ID_SEGMENT) + enc_size(len(segbody), 8) + segbody
    open(dst, "wb").write(out)
    print("%s : %d Clusters, %.2f s, %d octets" % (dst, len(keep), (last - base) * unit, len(out)))


main()
