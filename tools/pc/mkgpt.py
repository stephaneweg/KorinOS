"""Image d'essai du pilote NVMe (docs/etude/PC-PILOTES.md, « NVMe ») : un disque GPT
qui imite celui du ZBook (Windows et la partition KORIN OS), pour QEMU seulement.

  python -I tools/pc/mkgpt.py make IMAGE [--variant normal|badcrc|dup|nolabel]
  python -I tools/pc/mkgpt.py sums IMAGE SORTIE.json     sommes de chaque zone
  python -I tools/pc/mkgpt.py compare AVANT.json APRES.json

Disque de 256 Mo, entrées GPT dans l'ordre de Windows (la récupération est la 4e
entrée, placée après KORIN, 5e entrée) :
  p1  EFI System, FAT32 « SYSTEM », EFI/Microsoft/Boot/BCD (32 Mo)
  p2  Microsoft Reserved (MSR), sans système de fichiers (16 Mo)
  p3  Basic data, « C: » : secteur de démarrage NTFS imité, motif (64 Mo)
  p5  Basic data, FAT32 « KORIN OS », quelques fichiers (64 Mo)
  p4  Windows Recovery, attributs 8000000000000001, NTFS imité (32 Mo)
Variantes : badcrc (CRC des entrées faux), dup (p3 aussi en FAT32 « KORIN OS » :
règle ambiguë), nolabel (p5 s'appelle « DONNEES »).
Les GUID uniques sont fixes (affichés), pour les règles guid=.
« sums » : SHA-256 de chaque partition et de ce qui est hors des partitions (MBR,
GPT, trous, GPT de secours) ; « compare » dit les zones qui ont changé.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import sys
import tempfile
import uuid
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("mkdisk", os.path.join(HERE, "..", "mkdisk.py"))
mkdisk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mkdisk)

SECTOR = 512
TOTAL = 256 * 2048            # secteurs (256 Mo)
MB = 2048

T_EFI = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
T_MSR = "E3C9E316-0B5C-4DB8-817D-F92DF00215AE"
T_BASIC = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"
T_RECOVERY = "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC"

# (entrée, type, début, taille, nom, attributs, contenu)
LAYOUT = [
    (1, T_EFI, 1 * MB, 32 * MB, "EFI system partition", 0, "efi"),
    (2, T_MSR, 33 * MB, 16 * MB, "Microsoft reserved partition", 0, "msr"),
    (3, T_BASIC, 49 * MB, 64 * MB, "Basic data partition", 0, "ntfs"),
    (5, T_BASIC, 113 * MB, 64 * MB, "Basic data partition", 0, "korin"),
    (4, T_RECOVERY, 177 * MB, 32 * MB, "", 0x8000000000000001, "ntfs"),
]


def uniq(n):
    return uuid.uuid5(uuid.NAMESPACE_URL, "korin-nvme-test/p%d" % n)


def guid_bytes(s):
    return uuid.UUID(s).bytes_le


def fat32(path_dir, label, sectors, start):
    fat = mkdisk.Fat32(sectors)
    fat.LABEL = label
    root, _ = fat.alloc_chain(64 * 1024)
    fat.add_tree(path_dir, root, None)
    meta, blobs = fat.image(label)
    meta = bytearray(meta)
    for o in (0, 6 * SECTOR):  # secteurs cachés : le vrai début de la partition
        struct.pack_into("<I", meta, o + 28, start)
    return bytes(meta), blobs


def ntfs_like(seed):
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x52\x90"
    bs[3:11] = b"NTFS    "
    struct.pack_into("<HB", bs, 11, 512, 8)
    bs[510:512] = b"\x55\xAA"
    bs[100:100 + len(seed)] = seed
    return bytes(bs)


def pattern(n, tag, i):
    """64 Ko de motif reconnaissable (zone, rang)"""
    h = hashlib.sha256(b"%s-%d" % (tag, i)).digest()
    return (h * (n // len(h) + 1))[:n]


def make(img, variant):
    work = tempfile.mkdtemp(prefix="korin_gpt_")
    try:
        efi = os.path.join(work, "efi")
        os.makedirs(os.path.join(efi, "EFI", "Microsoft", "Boot"))
        with open(os.path.join(efi, "EFI", "Microsoft", "Boot", "BCD"), "wb") as f:
            f.write(b"faux BCD de Windows (image d'essai Korin)\n" * 50)
        korin = os.path.join(work, "korin")
        os.makedirs(os.path.join(korin, "etc"))
        os.makedirs(os.path.join(korin, "docs"))
        with open(os.path.join(korin, "LISEZMOI.TXT"), "wb") as f:
            f.write(b"Partition KORIN OS de l'image d'essai NVMe.\r\n")
        with open(os.path.join(korin, "etc", "drivers.txt"), "wb") as f:
            f.write(open(os.path.join(HERE, "..", "..", "skel", "sd0", "etc", "drivers.txt"), "rb").read())
        with open(os.path.join(korin, "docs", "gros.bin"), "wb") as f:
            f.write(b"".join(pattern(65536, b"gros", i) for i in range(48)))  # 3 Mo
        with open(img, "wb") as f:
            mkdisk.creuse(f)
            f.seek(TOTAL * SECTOR - 1)
            f.write(b"\0")
            # MBR protecteur
            mbr = bytearray(SECTOR)
            struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\x00\x02\x00", 0xEE, b"\xff\xff\xff", 1, TOTAL - 1)
            mbr[510:512] = b"\x55\xAA"
            f.seek(0)
            f.write(mbr)
            entries = bytearray(128 * 128)
            for num, typ, start, size, name, attrs, kind in LAYOUT:
                e = struct.pack("<16s16sQQQ72s", guid_bytes(typ), uniq(num).bytes_le, start, start + size - 1, attrs,
                                name.encode("utf-16-le").ljust(72, b"\0"))
                entries[(num - 1) * 128:num * 128] = e
                if kind in ("efi", "korin") or (kind == "ntfs" and num == 3 and variant == "dup"):
                    if kind == "efi":
                        label, d = "SYSTEM", efi
                    elif kind == "korin":
                        label, d = ("DONNEES" if variant == "nolabel" else "KORIN OS"), korin
                    else:
                        label, d = "KORIN OS", korin
                    meta, blobs = fat32(d, label, size, start)
                    f.seek(start * SECTOR)
                    f.write(meta)
                    for off, blob in blobs.items():
                        f.seek(start * SECTOR + off)
                        f.write(blob)
                    print("p%d : FAT32 « %s », %d Mo" % (num, label, size // MB))
                elif kind == "ntfs":
                    f.seek(start * SECTOR)
                    f.write(ntfs_like(b"partition %d" % num))
                    for i in range(1, size // 128, 16):  # du motif tous les 1 Mo
                        f.seek((start + i * 128) * SECTOR)
                        f.write(pattern(65536, b"p%d" % num, i))
                    print("p%d : NTFS imité, %d Mo" % (num, size // MB))
                else:  # MSR : du motif aussi, pour voir toute écriture
                    for i in range(0, size // 128, 16):
                        f.seek((start + i * 128) * SECTOR)
                        f.write(pattern(65536, b"msr", i))
                    print("p%d : MSR, %d Mo" % (num, size // MB))
            ecrc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
            if variant == "badcrc":
                ecrc ^= 1
            disk_guid = uuid.uuid5(uuid.NAMESPACE_URL, "korin-nvme-test/disk").bytes_le

            def header(my, alt, ent):
                h = bytearray(struct.pack("<8sIIIIQQQQ16sQIII", b"EFI PART", 0x00010000, 92, 0, 0, my, alt, 34,
                                          TOTAL - 34, disk_guid, ent, 128, 128, ecrc))
                struct.pack_into("<I", h, 16, zlib.crc32(bytes(h)) & 0xFFFFFFFF)
                return bytes(h).ljust(SECTOR, b"\0")

            f.seek(1 * SECTOR)
            f.write(header(1, TOTAL - 1, 2))
            f.write(entries)
            f.seek((TOTAL - 33) * SECTOR)
            f.write(entries)
            f.write(header(TOTAL - 1, 1, TOTAL - 33))
        print("image %s : %d Mo, GPT %s" % (img, TOTAL // MB, variant))
        for num, typ, start, size, name, attrs, kind in sorted(LAYOUT):
            print("  p%d : secteurs %d à %d, GUID %s" % (num, start, start + size - 1, str(uniq(num)).upper()))
    finally:
        shutil.rmtree(work, ignore_errors=True)


def zones():
    z = [("mbr+gpt", 0, 34)]
    pos = 34
    for num, typ, start, size, name, attrs, kind in sorted(LAYOUT, key=lambda x: x[2]):
        if start > pos:
            z.append(("trou %d" % pos, pos, start - pos))
        z.append(("p%d" % num, start, size))
        pos = start + size
    z.append(("trou %d" % pos, pos, TOTAL - 33 - pos))
    z.append(("gpt de secours", TOTAL - 33, 33))
    return z


def sums(img, out):
    res = {}
    with open(img, "rb") as f:
        for name, start, count in zones():
            h = hashlib.sha256()
            f.seek(start * SECTOR)
            left = count * SECTOR
            while left:
                b = f.read(min(left, 1 << 20))
                h.update(b)
                left -= len(b)
            res[name] = h.hexdigest()
    with open(out, "w", encoding="utf-8", newline="") as f:
        json.dump(res, f, indent=1)
    for k, v in res.items():
        print("%-16s %s" % (k, v[:16]))


def compare(a, b):
    x = json.load(open(a, encoding="utf-8"))
    y = json.load(open(b, encoding="utf-8"))
    changed = [k for k in x if x[k] != y.get(k)]
    for k in x:
        print("%-16s %s" % (k, "CHANGÉE" if k in changed else "identique"))
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["make", "sums", "compare"])
    ap.add_argument("a")
    ap.add_argument("b", nargs="?")
    ap.add_argument("--variant", default="normal", choices=["normal", "badcrc", "dup", "nolabel"])
    a = ap.parse_args()
    if a.cmd == "make":
        make(a.a, a.variant)
    elif a.cmd == "sums":
        sums(a.a, a.b)
    else:
        changed = compare(a.a, a.b)
        print("zones changées :", ", ".join(changed) if changed else "aucune")


if __name__ == "__main__":
    main()
