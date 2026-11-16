"""mkzip.py : le système de base du PC en un zip, à décompresser à la racine de la partition KORIN OS
(la même disposition que tools/pc/korin.ps1 install) :

  EFI/Korin/korin.efi        le chargeur (build/pc/esp/EFI/BOOT/BOOTX64.EFI)
  korin/kernel.elf, korin/initrd.tar
  bin, sys, lib, etc, res, apps, fonts, devtools, var...   l'arbre de build/pc/sysroot

Jamais home/ (les fichiers de l'utilisateur), ni etc/wifi.txt, etc/options.txt, etc/pointer.ini (ses réglages,
comme $sysKeep de korin.ps1), ni korin/boot.cfg (le menu de démarrage de la machine, réglé par korin.ps1 config).

    python -I tools/pc/mkzip.py [SORTIE.zip] [--sysroot DIR]
"""
import os, sys, zipfile, datetime

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
args = [a for a in sys.argv[1:] if not a.startswith('--')]
sysroot = os.path.join(ROOT, 'build', 'pc', 'sysroot')
if '--sysroot' in sys.argv:
    sysroot = sys.argv[sys.argv.index('--sysroot') + 1]
    args = [a for a in args if a != sysroot]
out = args[0] if args else os.path.join(ROOT, 'build', 'pc', 'korin-base-%s.zip' % datetime.date.today().isoformat())
esp = os.path.join(ROOT, 'build', 'pc', 'esp')
SKIP_TOP = {'home', 'EFI', 'korin'}
SKIP = {'etc/wifi.txt', 'etc/options.txt', 'etc/pointer.ini'}

n = size = 0
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for src, dst in ((os.path.join(esp, 'EFI', 'BOOT', 'BOOTX64.EFI'), 'EFI/Korin/korin.efi'),
                     (os.path.join(esp, 'korin', 'kernel.elf'), 'korin/kernel.elf'),
                     (os.path.join(esp, 'korin', 'initrd.tar'), 'korin/initrd.tar')):
        z.write(src, dst)
        n += 1; size += os.path.getsize(src)
    for d, dirs, files in os.walk(sysroot):
        rel = os.path.relpath(d, sysroot).replace(os.sep, '/')
        if rel == '.':
            dirs[:] = sorted(x for x in dirs if x not in SKIP_TOP)
            rel = ''
        else:
            dirs.sort()
        for f in sorted(files):
            r = (rel + '/' if rel else '') + f
            if r in SKIP:
                continue
            p = os.path.join(d, f)
            z.write(p, r)
            n += 1; size += os.path.getsize(p)
print('%s : %d fichiers, %d Mo (décompressés), %d Mo (zip)' % (out, n, size >> 20, os.path.getsize(out) >> 20))
