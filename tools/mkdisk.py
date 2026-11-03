"""Fabrique une image de carte SD pour ARMPI (tests QEMU et vraie carte).

Disque MBR de 1 Go :
  partition 1 : FAT32 (0x0C), ~896 Mo, clusters de 4 Ko, noms longs (LFN)
  partition 2 : exFAT (0x07), le reste            (si --exfat DIR)

Usage : python -I tools/mkdisk.py build/sd.img --fat32 build/sdroot [--exfat skel/sd1]
        python -I tools/mkdisk.py test.img --fat32 dossier --taille 8   (petite image :
        une seule partition FAT32, pour essayer les services de fichiers)
        python -I tools/mkdisk.py test.img --exfat dossier --taille 16   (petite image
        exFAT : une seule partition)
Le contenu des dossiers est recopie recursivement.
"""
import argparse
import os
import struct
import time

SECTOR = 512
DISK_SIZE = 1024 * 1024 * 1024
P1_START = 2048
P1_SECTORS = 896 * 2048            # 896 Mo (WebKit 310 Mo, SoundFont 31 Mo)
P2_START = P1_START + P1_SECTORS
P2_SECTORS = DISK_SIZE // SECTOR - P2_START


def creuse(f):
    """Windows : fichier creux (sparse), seuls les octets ecrits occupent le disque."""
    if os.name != "nt":
        return                                      # (ailleurs, truncate suffit)
    import ctypes
    import msvcrt
    from ctypes import wintypes
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.DeviceIoControl.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                                  wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                                  wintypes.LPVOID]
    n = wintypes.DWORD()
    k.DeviceIoControl(msvcrt.get_osfhandle(f.fileno()), 0x000900C4, None, 0, None, 0,
                      ctypes.byref(n), None)        # FSCTL_SET_SPARSE


def dos_datetime(ts):
    t = time.localtime(ts)
    d = ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
    tm = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
    return d, tm


# ---------------------------------------------------------------------------
#  FAT32
# ---------------------------------------------------------------------------
class Fat32:
    SPC = 8                        # secteurs par cluster (4 Ko)
    RESERVED = 32
    LABEL = "ARMPI"                # etiquette (secteur de demarrage et racine)

    def __init__(self, sectors, max_sectors=None):
        self.sectors = sectors
        # taille de FAT : iteration simple ; max_sectors : la FAT taillee pour une partition plus
        # grande, que Setup pourra etendre sur place (BPB, FSInfo, MBR : rien a deplacer)
        fat_sz = 1
        cible = max(sectors, max_sectors or 0)
        while True:
            data = cible - self.RESERVED - 2 * fat_sz
            clusters = data // self.SPC
            need = (clusters + 2) * 4
            new = (need + SECTOR - 1) // SECTOR
            if new == fat_sz:
                break
            fat_sz = new
        self.fat_sz = fat_sz
        self.data_start = self.RESERVED + 2 * fat_sz
        self.clusters = clusters = (sectors - self.data_start) // self.SPC
        self.fat = [0] * (clusters + 2)
        self.fat[0] = 0x0FFFFFF8
        self.fat[1] = 0x0FFFFFFF
        self.next_free = 2
        self.data = {}             # cluster -> bytes
        self.csize = self.SPC * SECTOR

    def alloc_chain(self, nbytes):
        n = max(1, (nbytes + self.csize - 1) // self.csize)
        first = self.next_free
        for i in range(n):
            c = first + i
            self.fat[c] = c + 1 if i < n - 1 else 0x0FFFFFFF
        self.next_free += n
        return first, n

    def write_chain(self, first, payload):
        for i in range(0, max(len(payload), 1), self.csize):
            self.data[first + i // self.csize] = payload[i:i + self.csize]

    @staticmethod
    def short_name(name, used):
        base, ext = os.path.splitext(name)
        ext = ext[1:]
        ok = lambda s: "".join(ch for ch in s.upper() if ch.isalnum() or ch in "_-~!#$%&'()@^`{}")
        b, e = ok(base), ok(ext)[:3]
        exact = (b == base.upper() and e == ext.upper() and len(base) <= 8 and len(ext) <= 3
                 and name == name.upper())
        if exact and (b.ljust(8) + e.ljust(3)) not in used:
            sn = b.ljust(8) + e.ljust(3)
            used.add(sn)
            return sn.encode("ascii"), False
        i = 1
        while True:
            tail = "~%d" % i
            sn = (b[:8 - len(tail)] + tail).ljust(8) + e.ljust(3)
            if sn not in used:
                used.add(sn)
                return sn.encode("ascii"), True
            i += 1

    @staticmethod
    def lfn_checksum(sn):
        s = 0
        for c in sn:
            s = (((s & 1) << 7) | (s >> 1)) + c
            s &= 0xFF
        return s

    def dir_entries(self, entries, self_cl, parent_cl, label=None):
        """entries : liste (nom, attr, cluster, taille, mtime) ; label : etiquette de
        volume (racine), la meme que dans le secteur de demarrage (fsck.fat la compare)."""
        out = bytearray()
        used = set()
        if label:
            d, t = dos_datetime(time.time())
            out += self.raw_entry(label.ljust(11).encode(), 0x08, 0, 0, d, t)
        if self_cl is not None:
            d, t = dos_datetime(time.time())
            out += self.raw_entry(b".          ", 0x10, self_cl, 0, d, t)
            out += self.raw_entry(b"..         ", 0x10, parent_cl or 0, 0, d, t)
        for name, attr, cl, size, mtime in entries:
            sn, need_lfn = self.short_name(name, used)
            if need_lfn:
                chk = self.lfn_checksum(sn)
                u = name.encode("utf-16-le") + b"\x00\x00"
                while len(u) % 26:
                    u += b"\xff\xff"
                parts = [u[i:i + 26] for i in range(0, len(u), 26)]
                for k in range(len(parts), 0, -1):
                    p = parts[k - 1]
                    seq = k | (0x40 if k == len(parts) else 0)
                    out += bytes([seq]) + p[0:10] + bytes([0x0F, 0, chk]) + p[10:22] + b"\0\0" + p[22:26]
            d, t = dos_datetime(mtime)
            out += self.raw_entry(sn, attr, cl, size, d, t)
        return bytes(out)

    @staticmethod
    def raw_entry(sn, attr, cl, size, d, t):
        return struct.pack("<11sBBBHHHHHHHI", sn, attr, 0, 0, t, d, d, cl >> 16, t, d, cl & 0xFFFF, size)

    def add_tree(self, host_dir, self_cl=None, parent_cl=None):
        """Ajoute un dossier ; renvoie le premier cluster de son repertoire."""
        items = sorted(os.listdir(host_dir)) if host_dir else []
        # taille estimee du repertoire : on reserve large (1 cluster / 32 entrees ~ LFN)
        est = (len(items) * 4 + 4) * 32
        is_root = parent_cl is None
        if self_cl is None:
            self_cl, _ = self.alloc_chain(est)
        entries = []
        for it in items:
            p = os.path.join(host_dir, it)
            st = os.stat(p)
            if os.path.isdir(p):
                sub, _ = self.alloc_chain((len(os.listdir(p)) * 4 + 4) * 32)
                self.add_tree(p, sub, self_cl)
                entries.append((it, 0x10, sub, 0, st.st_mtime))
            else:
                payload = open(p, "rb").read()
                first, _ = self.alloc_chain(len(payload)) if payload else (0, 0)
                if payload:
                    self.write_chain(first, payload)
                entries.append((it, 0x20, first, len(payload), st.st_mtime))
        blob = self.dir_entries(entries, None if is_root else self_cl,
                                 None if is_root else (parent_cl if parent_cl != 2 else 0),
                                 self.LABEL if is_root else None)
        self.write_chain(self_cl, blob)
        return self_cl

    def image(self, label=LABEL):
        bs = bytearray(SECTOR)
        bs[0:3] = b"\xEB\x58\x90"
        bs[3:11] = b"ARMPI1.0"
        struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, self.SPC, self.RESERVED, 2, 0, 0, 0xF8, 0,
                         63, 255, P1_START, self.sectors)
        struct.pack_into("<IHHIHH12sBBBI11s8s", bs, 36, self.fat_sz, 0, 0, 2, 1, 6, b"\0" * 12,
                         0x80, 0, 0x29, 0x1234ABCD, label.ljust(11).encode(), b"FAT32   ")
        bs[510:512] = b"\x55\xAA"
        fsinfo = bytearray(SECTOR)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<III", fsinfo, 484, 0x61417272, self.clusters - (self.next_free - 2), self.next_free)
        fsinfo[510:512] = b"\x55\xAA"
        out = bytearray(self.data_start * SECTOR)
        out[0:SECTOR] = bs
        out[SECTOR:2 * SECTOR] = fsinfo
        out[6 * SECTOR:7 * SECTOR] = bs
        out[7 * SECTOR:8 * SECTOR] = fsinfo
        fat_bytes = struct.pack("<%dI" % len(self.fat), *self.fat)
        fat_bytes += b"\0" * (self.fat_sz * SECTOR - len(fat_bytes))
        for k in range(2):
            o = (self.RESERVED + k * self.fat_sz) * SECTOR
            out[o:o + len(fat_bytes)] = fat_bytes
        return bytes(out), {self.data_start * SECTOR + (c - 2) * self.csize: d for c, d in self.data.items()}


def build_exfat_seul(path, exfat_dir, taille):
    """Petite image : une seule partition exFAT (0x07)."""
    total = taille * 1024 * 1024
    secteurs = total // SECTOR - P1_START
    with open(path, "wb") as f:
        creuse(f)
        f.seek(total - 1)
        f.write(b"\0")                         # (pas truncate : zeros ecrits sous Windows)
        mbr = bytearray(SECTOR)
        struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\xfe\xff\xff", 0x07, b"\xfe\xff\xff", P1_START, secteurs)
        mbr[510:512] = b"\x55\xAA"
        f.seek(0)
        f.write(mbr)
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "mkexfat", os.path.join(os.path.dirname(os.path.abspath(__file__)), "mkexfat.py"))
        mk = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mk)
        mk.build_exfat(f, P1_START, secteurs, exfat_dir)


def build(path, fat_dir, exfat_dir, taille=None, extensible=None):
    global DISK_SIZE, P1_SECTORS, P2_START, P2_SECTORS
    if taille:                                     # petite image : une partition
        DISK_SIZE = taille * 1024 * 1024
        P1_SECTORS = DISK_SIZE // SECTOR - P1_START
    elif sum(os.path.getsize(os.path.join(d, n)) for d, _, ns in os.walk(fat_dir) for n in ns) > 800 << 20:
        DISK_SIZE = 4096 * 1024 * 1024             # (GCC, SDL : une puissance de 2, pour QEMU)
        P1_SECTORS = 3840 * 2048
        P2_START = P1_START + P1_SECTORS
        P2_SECTORS = DISK_SIZE // SECTOR - P2_START
    with open(path, "wb") as f:
        creuse(f)
        f.seek(DISK_SIZE - 1)
        f.write(b"\0")                         # (pas truncate : zeros ecrits sous Windows)
        mbr = bytearray(SECTOR)
        struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\xfe\xff\xff", 0x0C, b"\xfe\xff\xff", P1_START, P1_SECTORS)
        if exfat_dir:
            struct.pack_into("<B3sB3sII", mbr, 462, 0, b"\xfe\xff\xff", 0x07, b"\xfe\xff\xff", P2_START, P2_SECTORS)
        mbr[510:512] = b"\x55\xAA"
        f.seek(0)
        f.write(mbr)

        fat = Fat32(P1_SECTORS, extensible and extensible * 1024 * 2048)
        root, _ = fat.alloc_chain(64 * 1024)          # racine : 16 clusters reserves
        fat.add_tree(fat_dir, root, None)
        meta, blobs = fat.image()
        base = P1_START * SECTOR
        f.seek(base)
        f.write(meta)
        for off, blob in blobs.items():
            f.seek(base + off)
            f.write(blob)
        print("FAT32 : %d clusters, %d utilises" % (fat.clusters, fat.next_free - 2))

        if exfat_dir:
            import importlib.util           # tools/mkexfat.py (python -I : pas de sys.path)
            spec = importlib.util.spec_from_file_location(
                "mkexfat", os.path.join(os.path.dirname(os.path.abspath(__file__)), "mkexfat.py"))
            mk = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mk)
            mk.build_exfat(f, P2_START, P2_SECTORS, exfat_dir)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--fat32")
    ap.add_argument("--exfat")
    ap.add_argument("--taille", type=int, help="taille en Mo (une partition FAT32)")
    ap.add_argument("--extensible", type=int, metavar="GO",
                    help="FAT taillee pour une partition de GO Go (Setup l'etend sur place)")
    a = ap.parse_args()
    if a.fat32:
        build(a.image, a.fat32, a.exfat, a.taille, a.extensible)
    else:                                          # --exfat seul (--taille)
        build_exfat_seul(a.image, a.exfat, a.taille or 16)
