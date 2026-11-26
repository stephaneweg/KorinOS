"""Disque d'essai du démarrage depuis la partition KORIN OS (docs/etude/PC-PILOTES.md,
« Menu de démarrage »), pour QEMU seulement : un disque GPT qui imite celui du ZBook, avec
la partition EFI de Windows (un FAUX Windows, et rien de Korin) et la partition KORIN OS
(le chargeur, le noyau, l'initrd).

  python -I tools/pc/mkkorindisk.py IMAGE [--no-windows | --bad-windows] [--cfg "texte"]
                                     [--layout zbook] [--sysroot DIR]
  puis : python -I tools/pc/qemu_pc.py test --disk IMAGE --vars build/pc/ovmf_vars_korin.fd …
  ou, en NVMe : python -I tools/pc/qemu_nvme.py IMAGE --boot-disk …

Disque de 160 Mo, entrées GPT dans l'ordre de Windows :
  p1  EFI System, FAT32 « SYSTEM » (65 Mo) :
        EFI/Microsoft/Boot/bootmgfw.efi   le faux Windows (tools/pc/fakewin/bootmgfw.asm)
        EFI/Microsoft/Boot/BCD            un faux BCD (texte), que le faux Windows cherche
  p2  Microsoft Reserved (1 Mo), p3 Basic data « C: » (NTFS imité, 4 Mo)
  p5  Basic data, FAT32 « KORIN OS » (65 Mo) :
        EFI/Korin/korin.efi               le chargeur (build/pc/esp/EFI/BOOT/BOOTX64.EFI)
        korin/kernel.elf, korin/initrd.tar  de build/pc/esp
        korin/boot.cfg                    seulement avec --cfg (« \\n » sépare les lignes)
        startup.nsh                       pour l'essai seulement : le shell UEFI d'OVMF lance
                                          \\EFI\\Korin\\korin.efi sur ce volume (comme F9, « Boot
                                          From EFI File » sur le ZBook), puisque aucun volume n'a
                                          de \\EFI\\BOOT\\BOOTX64.EFI
--no-windows : la partition EFI n'a que son BCD (pas de bootmgfw.efi) ;
--bad-windows : bootmgfw.efi est un fichier de texte (LoadImage doit échouer) ;
--layout zbook : les entrées du vrai ZBook (13/11) : KORIN OS en entrée 4 (blk.nvme0p4), puis
  p5 récupération de Windows (NTFS imité, attributs 8000000000000001) ; sans : KORIN OS en p5 ;
--sysroot DIR : cet arbre aussi à la racine de KORIN OS (build/pc/sysroot de tools/pc/sysroot.sh :
  bin, sys/svc, lib, etc, res, home) : le démarrage jusqu'au shell.
Rien de Korin sur p1, ni chargeur de secours \\EFI\\BOOT\\BOOTX64.EFI nulle part.
"""
import argparse
import importlib.util
import os
import shutil
import struct
import subprocess
import tempfile
import uuid
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
_spec = importlib.util.spec_from_file_location("mkdisk", os.path.join(ROOT, "tools", "mkdisk.py"))
mkdisk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mkdisk)

SECTOR = 512
MB = 2048                     # secteurs
TOTAL = 160 * MB

T_EFI = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
T_MSR = "E3C9E316-0B5C-4DB8-817D-F92DF00215AE"
T_BASIC = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"
T_RECOVERY = "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC"

# (entrée, type, début, taille, nom, attributs, contenu)
LAYOUTS = {
    "old": [
        (1, T_EFI, 1 * MB, 65 * MB, "EFI system partition", 0, "efi"),
        (2, T_MSR, 66 * MB, 1 * MB, "Microsoft reserved partition", 0, "msr"),
        (3, T_BASIC, 67 * MB, 4 * MB, "Basic data partition", 0, "ntfs"),
        (5, T_BASIC, 71 * MB, 65 * MB, "Basic data partition", 0, "korin"),
    ],
    "zbook": [
        (1, T_EFI, 1 * MB, 65 * MB, "EFI system partition", 0, "efi"),
        (2, T_MSR, 66 * MB, 1 * MB, "Microsoft reserved partition", 0, "msr"),
        (3, T_BASIC, 67 * MB, 4 * MB, "Basic data partition", 0, "ntfs"),
        (4, T_BASIC, 71 * MB, 65 * MB, "Basic data partition", 0, "korin"),
        (5, T_RECOVERY, 136 * MB, 16 * MB, "", 0x8000000000000001, "ntfs"),
    ],
}

STARTUP_NSH = (
    "@echo -off\r\n"
    "# Essai sous QEMU seulement (tools/pc/mkkorindisk.py) : lance le chargeur de Korin\r\n"
    "# depuis la partition KORIN OS, comme F9 > Boot From EFI File sur le ZBook.\r\n"
    "for %i in 0 1 2 3 4 5 6 7 8 9\r\n"
    "  if exist fs%i:\\EFI\\Korin\\korin.efi then\r\n"
    "    fs%i:\r\n"
    "    echo Essai : fs%i:\\EFI\\Korin\\korin.efi\r\n"
    "    \\EFI\\Korin\\korin.efi\r\n"
    "  endif\r\n"
    "endfor\r\n"
)


class Fat32Small(mkdisk.Fat32):
    SPC = 1                   # clusters de 512 octets : plus de 65 525 clusters, du vrai FAT32 pour OVMF
    # (65 Mo : à 64 Mo pile, le calcul de la taille de FAT de mkdisk.Fat32 oscille sans fin)


def fat32(path_dir, label, sectors, start):
    fat = Fat32Small(sectors)
    fat.LABEL = label
    root, _ = fat.alloc_chain(64 * 1024)
    fat.add_tree(path_dir, root, None)
    meta, blobs = fat.image(label)
    meta = bytearray(meta)
    for o in (0, 6 * SECTOR):  # secteurs cachés : le vrai début de la partition
        struct.pack_into("<I", meta, o + 28, start)
    return bytes(meta), blobs


def guid_le(s):
    return uuid.UUID(s).bytes_le


def uniq(n):
    return uuid.uuid5(uuid.NAMESPACE_URL, "korin-boot-test/p%d" % n)


def make(img, windows, cfg, esp_src, loader, layout="old", sysroot=None):
    for f in (loader, os.path.join(esp_src, "korin", "kernel.elf")):
        if not os.path.isfile(f):
            raise SystemExit("absent : %s (construire d'abord : wsl sh tools/pc/build.sh)" % f)
    work = tempfile.mkdtemp(prefix="korin_boot_")
    try:
        efi = os.path.join(work, "efi")
        mb = os.path.join(efi, "EFI", "Microsoft", "Boot")
        os.makedirs(mb)
        with open(os.path.join(mb, "BCD"), "wb") as f:
            f.write(b"faux BCD pour l'essai du menu de Korin\n")
        if windows == "fake":
            log = os.path.join(work, "fakewin.log")
            with open(log, "wb") as lf:
                # (KORIN_FASM : un fasm pour Linux, tools/cloud/setup.sh)
                r = subprocess.call([os.environ.get("KORIN_FASM") or os.path.join(ROOT, "ref", "fasm", "FASM.EXE"),
                                     os.path.join(HERE, "fakewin", "bootmgfw.asm"),
                                     os.path.join(mb, "bootmgfw.efi")], stdout=lf, stderr=lf)
            if r:
                raise SystemExit(open(log, encoding="utf-8", errors="replace").read())
        elif windows == "bad":
            with open(os.path.join(mb, "bootmgfw.efi"), "wb") as f:
                f.write(b"pas un programme UEFI\n")

        korin = os.path.join(work, "korin")
        if sysroot:
            shutil.copytree(sysroot, korin)
        os.makedirs(os.path.join(korin, "EFI", "Korin"))
        os.makedirs(os.path.join(korin, "korin"), exist_ok=True)  # (un arbre qui a déjà korin/ : ab.txt, a/, b/)
        shutil.copyfile(loader, os.path.join(korin, "EFI", "Korin", "korin.efi"))
        for n in ("kernel.elf", "initrd.tar"):
            s = os.path.join(esp_src, "korin", n)
            if os.path.isfile(s):
                shutil.copyfile(s, os.path.join(korin, "korin", n))
        if cfg is not None:
            with open(os.path.join(korin, "korin", "boot.cfg"), "w", encoding="ascii", newline="") as f:
                f.write(cfg.replace("\\n", "\n") + "\n")
        with open(os.path.join(korin, "startup.nsh"), "w", encoding="ascii", newline="") as f:
            f.write(STARTUP_NSH)

        with open(img, "wb") as f:
            mkdisk.creuse(f)
            f.seek(TOTAL * SECTOR - 1)
            f.write(b"\0")
            mbr = bytearray(SECTOR)  # MBR protecteur
            struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\x00\x02\x00", 0xEE, b"\xff\xff\xff", 1, TOTAL - 1)
            mbr[510:512] = b"\x55\xAA"
            f.seek(0)
            f.write(mbr)
            entries = bytearray(128 * 128)
            for num, typ, start, size, name, attrs, kind in LAYOUTS[layout]:
                entries[(num - 1) * 128:num * 128] = struct.pack(
                    "<16s16sQQQ72s", guid_le(typ), uniq(num).bytes_le, start, start + size - 1, attrs,
                    name.encode("utf-16-le").ljust(72, b"\0"))
                if kind in ("efi", "korin"):
                    label, d = ("SYSTEM", efi) if kind == "efi" else ("KORIN OS", korin)
                    meta, blobs = fat32(d, label, size, start)
                    f.seek(start * SECTOR)
                    f.write(meta)
                    for off, blob in blobs.items():
                        f.seek(start * SECTOR + off)
                        f.write(blob)
                elif kind == "ntfs":
                    bs = bytearray(SECTOR)
                    bs[0:3] = b"\xEB\x52\x90"
                    bs[3:11] = b"NTFS    "
                    bs[510:512] = b"\x55\xAA"
                    f.seek(start * SECTOR)
                    f.write(bs)
            ecrc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
            disk_guid = uuid.uuid5(uuid.NAMESPACE_URL, "korin-boot-test/disk").bytes_le

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
        print("image %s : %d Mo, GPT (%s)" % (img, TOTAL // MB, layout))
        kp = [num for num, _, _, _, _, _, kind in LAYOUTS[layout] if kind == "korin"][0]
        for name, d in (("p1 SYSTEM (EFI)", efi), ("p%d KORIN OS" % kp, korin)):
            print("  %s :" % name)
            count = 0
            for base, _, files in os.walk(d):
                for n in sorted(files):
                    p = os.path.join(base, n)
                    rel = os.path.relpath(p, d).replace("\\", "/")
                    count += 1
                    if sysroot and "/" in rel and rel.split("/")[0] in ("bin", "sys", "lib", "res"):
                        continue  # (l'arbre système : compté, pas listé)
                    print("    %s  %d octets" % (rel, os.path.getsize(p)))
            print("    (%d fichiers)" % count)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--no-windows", action="store_true")
    g.add_argument("--bad-windows", action="store_true")
    ap.add_argument("--cfg", default=None)
    ap.add_argument("--esp", default=os.path.join(ROOT, "build", "pc", "esp"),
                    help="d'où viennent kernel.elf et initrd.tar (défaut build/pc/esp)")
    ap.add_argument("--loader", default="", help="le chargeur (défaut <esp>/EFI/BOOT/BOOTX64.EFI)")
    ap.add_argument("--layout", choices=sorted(LAYOUTS), default="old")
    ap.add_argument("--sysroot", default=None, help="arbre système copié à la racine de KORIN OS")
    ap.add_argument("--korin-mb", type=int, default=65,
                    help="taille de KORIN OS en Mo (layout zbook ; défaut 65, assez pour l'arbre sans les apps)")
    a = ap.parse_args()
    if a.korin_mb != 65:  # (l'arbre avec les apps, PC_APPS=1 : plus de 65 Mo)
        global TOTAL
        if a.layout != "zbook" or a.korin_mb < 65:
            raise SystemExit("--korin-mb : layout zbook seulement, 65 Mo au moins")
        k = a.korin_mb
        LAYOUTS["zbook"][3] = (4, T_BASIC, 71 * MB, k * MB, "Basic data partition", 0, "korin")
        LAYOUTS["zbook"][4] = (5, T_RECOVERY, (71 + k) * MB, 16 * MB, "", 0x8000000000000001, "ntfs")
        TOTAL = (71 + k + 16 + 8) * MB
    if a.sysroot and not os.path.isdir(a.sysroot):
        raise SystemExit("--sysroot : dossier absent : %s (wsl sh tools/pc/sysroot.sh)" % a.sysroot)
    loader = a.loader or os.path.join(a.esp, "EFI", "BOOT", "BOOTX64.EFI")
    windows = "none" if a.no_windows else "bad" if a.bad_windows else "fake"
    make(os.path.abspath(a.image), windows, a.cfg, a.esp, loader, a.layout,
         os.path.abspath(a.sysroot) if a.sysroot else None)


if __name__ == "__main__":
    main()
