"""La carte d'essai de Quake sous QEMU, jetable : build/sdroot + ce qui lance le jeu, dans build/quake/sd.img.

    python -I tools/quake/testcard.py [--sans-donnees] [args.txt...]
    (les arguments : la ligne de SD0:/quake/args.txt, par exemple +map start, ou -condebug +timedemo demo1)

Une copie de build/sdroot : etc/startup.txt lance Quake au demarrage. build.ps1 a deja rempli build/sdroot.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SD = os.path.join(ROOT, "build", "sdroot")
CARD = os.path.join(ROOT, 'build', 'quake', 'card')

shutil.rmtree(CARD, ignore_errors=True)
shutil.copytree(SD, CARD)
ex = os.path.join(ROOT, 'build', 'quake', 'build/sdroot')
shutil.copytree(ex, CARD, dirs_exist_ok=True)
if '--sans-donnees' in sys.argv:          # (le message de l'application)
    sys.argv.remove('--sans-donnees')
    shutil.rmtree(os.path.join(CARD, 'quake'), ignore_errors=True)
    os.makedirs(os.path.join(CARD, 'quake'))
with open(os.path.join(CARD, 'etc', 'startup.txt'), 'w', newline='\n') as f:
    f.write('SD0:/apps/quake.app/main\n')
args = os.path.join(CARD, 'quake', 'args.txt')
if sys.argv[1:]:
    with open(args, 'w', newline='\n') as f:
        f.write(' '.join(sys.argv[1:]) + '\n')
elif os.path.exists(args):
    os.remove(args)
ex2 = os.path.join(ROOT, 'build', 'exfat_root')
cmd = [sys.executable, '-I', os.path.join(ROOT, 'tools', 'mkdisk.py'), os.path.join(ROOT, 'build', 'quake', 'sd.img'), '--fat32', CARD]
if os.path.isdir(ex2):
    cmd += ['--exfat', ex2]
subprocess.check_call(cmd, stdout=subprocess.DEVNULL)
print('carte :', os.path.join('build', 'quake', 'sd.img'))
