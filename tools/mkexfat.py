"""Formateur exFAT pour les images de carte SD d'ARMPI (appele par mkdisk.py).

build_exfat(f, start_lba, nsectors, host_dir) ecrit dans le fichier image f
(deja a la bonne taille) un volume exFAT a partir du secteur start_lba :
  * region de boot + copie de secours, avec somme de controle ;
  * une FAT, le bitmap d'allocation, la table de majuscules (compressee) ;
  * racine avec etiquette "ARMPI2", fichiers et sous-dossiers recopies
    depuis host_dir (noms longs / accentues en UTF-16) ;
  * fichiers contigus marques NoFatChain, SAUF les fichiers dont le nom
    contient "chaine" : ceux-la sont fragmentes volontairement et decrits
    par une chaine dans la FAT (pour tester les deux chemins de lecture).

Reference : Microsoft exFAT File System Specification.
Usage autonome : python -I tools/mkexfat.py image.img start_lba nsectors dossier
"""
import os
import struct
import sys
import time

SECTOR = 512
SPC_SHIFT = 3                       # 8 secteurs par cluster (4 Ko)
SPC = 1 << SPC_SHIFT
CSIZE = SPC * SECTOR
EOC = 0xFFFFFFFF


def boot_checksum(sectors):
    chk = 0
    for i, b in enumerate(sectors):
        if i in (106, 107, 112):
            continue
        chk = ((0x80000000 if chk & 1 else 0) + (chk >> 1) + b) & 0xFFFFFFFF
    return chk


def upcase_table():
    """Table de majuscules compressee (0xFFFF, n = n caracteres identite)."""
    m = []
    for c in range(0x10000):
        u = c
        if not 0xD800 <= c <= 0xDFFF:
            s = chr(c).upper()
            if len(s) == 1 and ord(s) <= 0xFFFF:
                u = ord(s)
        m.append(u)
    out = []
    i = 0
    while i < 0x10000:
        if m[i] == i:
            j = i
            while j < 0x10000 and m[j] == j:
                j += 1
            n = j - i
            if n > 2:
                out += [0xFFFF, n]
            else:
                out += list(range(i, j))
            i = j
        else:
            out.append(m[i])
            i += 1
    data = struct.pack("<%dH" % len(out), *out)
    chk = 0
    for b in data:
        chk = ((0x80000000 if chk & 1 else 0) + (chk >> 1) + b) & 0xFFFFFFFF
    return data, chk, m


def name_hash(name16, upmap):
    h = 0
    for c in name16:
        u = upmap[c]
        for b in (u & 0xFF, u >> 8):
            h = ((0x8000 if h & 1 else 0) + (h >> 1) + b) & 0xFFFF
    return h


def set_checksum(entries):
    chk = 0
    for i, b in enumerate(entries):
        if i in (2, 3):
            continue
        chk = ((0x8000 if chk & 1 else 0) + (chk >> 1) + b) & 0xFFFF
    return chk


def dos_ts(t):
    lt = time.localtime(t)
    return (((lt.tm_year - 1980) << 25) | (lt.tm_mon << 21) | (lt.tm_mday << 16)
            | (lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec // 2))


class ExFat:
    def __init__(self, nsectors):
        self.fat_off = 128
        cc = (nsectors - 256) // SPC
        self.fat_len = ((cc + 2) * 4 + SECTOR - 1) // SECTOR
        self.fat_len = (self.fat_len + SPC - 1) // SPC * SPC
        self.heap_off = self.fat_off + self.fat_len
        self.heap_off = (self.heap_off + SPC - 1) // SPC * SPC
        self.count = (nsectors - self.heap_off) // SPC
        self.fat = [0] * (self.count + 2)
        self.fat[0], self.fat[1] = 0xFFFFFFF8, 0xFFFFFFFF
        self.used = set()
        self.next = 2
        self.data = {}               # cluster -> contenu

    def alloc(self, n, chain):
        """n clusters contigus a partir du prochain libre."""
        first = self.next
        for i in range(n):
            self.used.add(first + i)
            if chain:
                self.fat[first + i] = first + i + 1 if i < n - 1 else EOC
        self.next += n
        return first

    def alloc_fragmented(self, n):
        """n clusters non contigus (un trou d'un cluster entre chaque)."""
        cl = []
        for _ in range(n):
            cl.append(self.next)
            self.used.add(self.next)
            self.next += 2           # le trou reste libre
        for a, b in zip(cl, cl[1:] + [None]):
            self.fat[a] = b if b else EOC
        return cl

    def put(self, cluster, payload):
        for i in range(0, len(payload), CSIZE):
            self.data[cluster + i // CSIZE] = payload[i:i + CSIZE]

    def put_list(self, clusters, payload):
        for k, c in enumerate(clusters):
            self.data[c] = payload[k * CSIZE:(k + 1) * CSIZE]

    def file_set(self, name, attr, first, length, nofat, mtime, upmap):
        n16 = list(name.encode("utf-16-le"))
        n16 = [n16[i] | (n16[i + 1] << 8) for i in range(0, len(n16), 2)]
        ne = (len(n16) + 14) // 15
        ts = dos_ts(mtime)
        e = bytearray(32 * (2 + ne))
        struct.pack_into("<BBHHHIII", e, 0, 0x85, 1 + ne, 0, attr, 0, ts, ts, ts)
        flags = 0x01 | (0x02 if nofat else 0)
        struct.pack_into("<BBBBHHQIIQ", e, 32, 0xC0, flags, 0, len(n16),
                         name_hash(n16, upmap), 0, length, 0, first, length)
        for k in range(ne):
            part = n16[k * 15:(k + 1) * 15]
            part += [0] * (15 - len(part))
            struct.pack_into("<BB15H", e, 64 + 32 * k, 0xC1, 0, *part)
        struct.pack_into("<H", e, 2, set_checksum(e))
        return bytes(e)

    def add_dir(self, host_dir, first_cluster, nclusters, upmap, extra=b""):
        """Ecrit le contenu du repertoire host_dir dans ses clusters."""
        blob = bytearray(extra)
        for it in sorted(os.listdir(host_dir)):
            p = os.path.join(host_dir, it)
            st = os.stat(p)
            if os.path.isdir(p):
                n = 1 + len(os.listdir(p)) * 4 * 32 // CSIZE
                c = self.alloc(n, chain=False)
                self.add_dir(p, c, n, upmap)
                blob += self.file_set(it, 0x10, c, n * CSIZE, True, st.st_mtime, upmap)
            else:
                payload = open(p, "rb").read()
                n = (len(payload) + CSIZE - 1) // CSIZE
                if not payload:
                    blob += self.file_set(it, 0x20, 0, 0, False, st.st_mtime, upmap)
                elif "chaine" in it.lower():
                    cl = self.alloc_fragmented(n)
                    self.put_list(cl, payload)
                    blob += self.file_set(it, 0x20, cl[0], len(payload), False, st.st_mtime, upmap)
                else:
                    c = self.alloc(n, chain=False)
                    self.put(c, payload)
                    blob += self.file_set(it, 0x20, c, len(payload), True, st.st_mtime, upmap)
        assert len(blob) <= nclusters * CSIZE, "repertoire trop petit"
        blob += b"\0" * (nclusters * CSIZE - len(blob))
        self.put(first_cluster, bytes(blob))


def build_exfat(f, start_lba, nsectors, host_dir, label="ARMPI2"):
    ex = ExFat(nsectors)
    up_data, up_chk, upmap = upcase_table()

    bm_len = (ex.count + 7) // 8
    bm_cl = ex.alloc((bm_len + CSIZE - 1) // CSIZE, chain=True)
    up_cl = ex.alloc((len(up_data) + CSIZE - 1) // CSIZE, chain=True)
    ex.put(up_cl, up_data)
    root_n = 4
    root_cl = ex.alloc(root_n, chain=True)

    lab16 = label.encode("utf-16-le")
    head = bytearray(96)
    struct.pack_into("<BB22s", head, 0, 0x83, len(label), lab16)
    struct.pack_into("<BB18xIQ", head, 32, 0x81, 0, bm_cl, bm_len)
    struct.pack_into("<BBHI12xIQ", head, 64, 0x82, 0, 0, up_chk, up_cl, len(up_data))
    ex.add_dir(host_dir, root_cl, root_n, upmap, extra=bytes(head))

    bitmap = bytearray(bm_len)
    for c in ex.used:
        i = c - 2
        bitmap[i >> 3] |= 1 << (i & 7)
    ex.put(bm_cl, bytes(bitmap))

    # --- region de boot (secteurs 0..11) puis copie (12..23)
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x76\x90"
    bs[3:11] = b"EXFAT   "
    used_pct = len(ex.used) * 100 // ex.count
    struct.pack_into("<QQIIIIIIHHBBBB", bs, 64, start_lba, nsectors, ex.fat_off, ex.fat_len,
                     ex.heap_off, ex.count, root_cl, 0xA5A50202, 0x0100, 0, 9, SPC_SHIFT,
                     1, 0x80)
    bs[112] = used_pct
    bs[510:512] = b"\x55\xAA"
    region = bytearray(bs)
    for _ in range(8):                       # secteurs de boot etendus
        s = bytearray(SECTOR)
        s[508:512] = b"\x00\x00\x55\xAA"
        region += s
    region += bytearray(SECTOR) * 2          # parametres OEM + reserve
    chk = boot_checksum(region)
    region += struct.pack("<I", chk) * (SECTOR // 4)

    base = start_lba * SECTOR
    f.seek(base)
    f.write(region)
    f.seek(base + 12 * SECTOR)
    f.write(region)
    fat = struct.pack("<%dI" % len(ex.fat), *ex.fat)
    f.seek(base + ex.fat_off * SECTOR)
    f.write(fat)
    for c, d in ex.data.items():
        f.seek(base + (ex.heap_off + (c - 2) * SPC) * SECTOR)
        f.write(d)
    print("exFAT : %d clusters, %d utilises" % (ex.count, len(ex.used)))


if __name__ == "__main__":
    img, lba, n, d = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    with open(img, "r+b") as fh:
        build_exfat(fh, lba, n, d)
