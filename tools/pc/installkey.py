"""installkey : la clé USB d'installation de Korin PC (docs/etude/INSTALLATEUR.md §2, docs/DESIGN-INSTALLATEUR.md).

  python -I tools/pc/installkey.py build [--sysroot DIR] [--out IMAGE] [--extra-mb N]
      l'image build/pc/installkey.img : une GPT, une seule partition EFI System en FAT32 « KORIN INST » :
        EFI/BOOT/BOOTX64.EFI          korin.efi (le firmware démarre ce chemin sur un support amovible)
        korin/kernel.elf, initrd.tar  ceux de build/pc/esp (le même noyau, le même initrd que le système)
        korin/local.tar               superposé à l'initrd par le chargeur : init.txt de la clé (hwmanager et
                                      pcid d'abord : xHCI, usbstorage, puis / = la partition de la clé, blk.boot)
                                      et nvme.txt sans règle (le disque interne en lecture seule)
        korin/boot.cfg                Korin tout de suite, sans menu
        le système « live »           build/pc/sysroot (sans home/), et etc/install.ini (Setup en mode install)
  python -I tools/pc/installkey.py run [--target IMAGE] [qemu_nvme.py options...]
      démarre la clé sous QEMU (usb-storage sur xHCI, amorçable) avec un disque NVMe cible (défaut : une image
      de tools/pc/mkkorindisk.py, le faux Windows, en snapshot : jamais écrite)

L'écriture sur une vraie clé : tools/pc/installkey.ps1 (à venir), qui refuse tout disque non amovible.
"""
import argparse
import importlib.util
import io
import os
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import uuid
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
_spec = importlib.util.spec_from_file_location("mkdisk", os.path.join(ROOT, "tools", "mkdisk.py"))
mkdisk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mkdisk)

SECTOR = 512
MB = 2048
T_EFI = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
LABEL = "KORIN INST"
PC = os.path.join(ROOT, "build", "pc")

# l'init.txt de la clé (monté par local.tar à la place de celui de l'initrd)
KEY_INIT = """# Korin OS : init.txt de la cle d'installation (tools/pc/installkey.py, docs/etude/INSTALLATEUR.md 2.2).
# / est la partition de la cle (blk.boot : celle d'ou korin.efi a demarre, servie par usbstorage).
# hwmanager et pcid d'abord : pcid annonce le PCIe, hwmanager lance usbd (xHCI) depuis l'initrd, puis
# usbstorage pour la cle ; le disque NVMe (s'il y en a un) est lance de meme, en lecture seule (nvme.txt
# de local.tar, sans regle).
hwmanager
pcid
wait blk.boot 30000
vfs / blk.boot fatfs
"""

KEY_NVME = """# nvme.txt de la cle d'installation : aucune regle, le disque interne reste en lecture seule ;
# install = yes : le mode installation (docs/etude/INSTALLATEUR.md 8) : l'ecriture ne s'ouvre que par le plan
# valide (BLK_PLAN), verifie par le pilote. Jamais dans un systeme installe.
install = yes
"""

KEY_BOOTCFG = "# cle d'installation (tools/pc/installkey.py) : Korin tout de suite\ndefault = korin\ntimeout = 0\n"

INSTALL_INI = """# Korin OS demarre depuis la cle d'installation (tools/pc/installkey.py) : Setup en mode installation
[install]
key = yes
"""


def guid_le(s):
    return uuid.UUID(s).bytes_le


def local_tar():
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode="w", format=tarfile.USTAR_FORMAT) as t:
        for name, text in (("init.txt", KEY_INIT), ("nvme.txt", KEY_NVME)):
            data = text.encode("ascii")
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            ti.mode = 0o644
            t.addfile(ti, io.BytesIO(data))
    return b.getvalue()


def tree_size(d):
    n = 0
    for base, _, files in os.walk(d):
        for f in files:
            n += (os.path.getsize(os.path.join(base, f)) + 4095) // 4096 * 4096 + 4096
    return n


def build(a):
    sysroot = os.path.abspath(a.sysroot)
    esp = os.path.join(PC, "esp")
    for f in (os.path.join(esp, "EFI", "BOOT", "BOOTX64.EFI"), os.path.join(esp, "korin", "kernel.elf"),
              os.path.join(sysroot, "bin")):
        if not os.path.exists(f):
            raise SystemExit("absent : %s (d'abord : wsl sh tools/pc/build.sh, tools/pc/sysroot.sh)" % f)
    work = tempfile.mkdtemp(prefix="korin_key_")
    try:
        tree = os.path.join(work, "key")
        shutil.copytree(sysroot, tree, ignore=shutil.ignore_patterns("home"))
        os.makedirs(os.path.join(tree, "home"), exist_ok=True)
        os.makedirs(os.path.join(tree, "EFI", "BOOT"), exist_ok=True)
        os.makedirs(os.path.join(tree, "korin"), exist_ok=True)
        shutil.copyfile(os.path.join(esp, "EFI", "BOOT", "BOOTX64.EFI"), os.path.join(tree, "EFI", "BOOT", "BOOTX64.EFI"))
        for n in ("kernel.elf", "initrd.tar"):
            shutil.copyfile(os.path.join(esp, "korin", n), os.path.join(tree, "korin", n))
        with open(os.path.join(tree, "korin", "local.tar"), "wb") as f:
            f.write(local_tar())
        with open(os.path.join(tree, "korin", "boot.cfg"), "w", encoding="ascii", newline="") as f:
            f.write(KEY_BOOTCFG)
        with open(os.path.join(tree, "etc", "install.ini"), "w", encoding="ascii", newline="") as f:
            f.write(INSTALL_INI)
        if a.disktest:  # (essai : le mode installation du pilote, lancé en uid 0 par init2 ; FS : ext4 par défaut)
            with open(os.path.join(tree, "etc", "services.txt"), "a", encoding="ascii", newline="") as f:
                f.write("# essai du mode installation (installkey.py --disktest)\n/bin/gpt disktest blk.nvme0 %s\n"
                        % a.disktest)
        size = tree_size(tree) * 115 // 100 + a.extra_mb * 1024 * 1024
        part = (size // SECTOR + MB - 1) // MB * MB
        start = MB
        total = start + part + 34 + MB
        fat = mkdisk.Fat32(part)
        fat.LABEL = LABEL
        rootcl, _ = fat.alloc_chain(64 * 1024)
        fat.add_tree(tree, rootcl, None)
        meta, blobs = fat.image(LABEL)
        meta = bytearray(meta)
        for o in (0, 6 * SECTOR):  # secteurs cachés : le vrai début de la partition
            struct.pack_into("<I", meta, o + 28, start)
        img = os.path.abspath(a.out)
        os.makedirs(os.path.dirname(img), exist_ok=True)
        with open(img, "wb") as f:
            mkdisk.creuse(f)
            f.seek(total * SECTOR - 1)
            f.write(b"\0")
            mbr = bytearray(SECTOR)  # MBR protecteur
            struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\x00\x02\x00", 0xEE, b"\xff\xff\xff", 1, total - 1)
            mbr[510:512] = b"\x55\xAA"
            f.seek(0)
            f.write(mbr)
            entries = bytearray(128 * 128)
            part_guid = uuid.uuid4()
            entries[0:128] = struct.pack("<16s16sQQQ72s", guid_le(T_EFI), part_guid.bytes_le, start, start + part - 1, 0,
                                         "Korin OS installation".encode("utf-16-le").ljust(72, b"\0"))
            f.seek(start * SECTOR)
            f.write(meta)
            for off, blob in blobs.items():
                f.seek(start * SECTOR + off)
                f.write(blob)
            ecrc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
            disk_guid = uuid.uuid4().bytes_le

            def header(my, alt, ent):
                h = bytearray(struct.pack("<8sIIIIQQQQ16sQIII", b"EFI PART", 0x00010000, 92, 0, 0, my, alt, 34,
                                          total - 34, disk_guid, ent, 128, 128, ecrc))
                struct.pack_into("<I", h, 16, zlib.crc32(bytes(h)) & 0xFFFFFFFF)
                return bytes(h).ljust(SECTOR, b"\0")

            f.seek(1 * SECTOR)
            f.write(header(1, total - 1, 2))
            f.write(entries)
            f.seek((total - 33) * SECTOR)
            f.write(entries)
            f.write(header(total - 1, 1, total - 33))
        print("cle %s : %d Mo, partition EFI « %s » de %d Mo (GUID %s)" % (img, total // MB, LABEL, part // MB, part_guid))
    finally:
        shutil.rmtree(work, ignore_errors=True)


def run(a, rest):
    key = os.path.abspath(a.key).replace("\\", "/")
    target = os.path.abspath(a.target)
    if not os.path.isfile(target):
        subprocess.check_call([sys.executable, "-I", os.path.join(HERE, "mkkorindisk.py"), target, "--layout", "zbook"])
    q = ["-device qemu-xhci,id=ukey -drive if=none,id=key,format=raw,file=%s -device usb-storage,bus=ukey.0,drive=key,bootindex=0" % key]
    args = [sys.executable, "-I", os.path.join(HERE, "qemu_nvme.py"), target, "--boot-disk",
            "--vars", os.path.join(PC, "ovmf_vars_key.fd")]
    if not a.write:  # (--write : la cible est écrite, pour la juger ensuite : sgdisk, e2fsck)
        args.append("--snapshot")
    for x in q:
        args += ["--qemu", x]
    subprocess.call(args + rest)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--sysroot", default=os.path.join(PC, "sysroot"))
    b.add_argument("--out", default=os.path.join(PC, "installkey.img"))
    b.add_argument("--extra-mb", type=int, default=32)
    b.add_argument("--disktest", nargs="?", const="ext4", default=None, choices=("ext4", "exfat", "fat32", "ext4read"))
    r = sub.add_parser("run")
    r.add_argument("--key", default=os.path.join(PC, "installkey.img"))
    r.add_argument("--target", default=os.path.join(PC, "installtarget.img"))
    r.add_argument("--write", action="store_true")
    a, rest = ap.parse_known_args()
    if a.cmd == "build":
        build(a)
    else:
        run(a, rest)


if __name__ == "__main__":
    main()
