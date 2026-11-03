"""qtest.py - commandes d'essai d'AudioKit sous QEMU raspi4b (ARMPI complet)

    python -I tools/audiotest/qtest.py [--get SD0:/chemin=local ...] "cmd1" "cmd2" ...

Ecrit build/audiotest/q.txt (script de tools/qemu_drive.py : 60 s de
demarrage, puis chaque commande par telnet, une connexion chacune), lance
QEMU (ports 4507, VNC 57, telnet 2329), puis copie les fichiers demandes
hors de build/sd.img (pyfatfs).
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def extract(img, path, out):
    from pyfatfs.PyFatFS import PyFatFS
    with open(img, 'rb') as f:
        mbr = f.read(512)
    lba = struct.unpack_from('<I', mbr, 446 + 8)[0]
    fs = PyFatFS(img, offset=lba * 512, read_only=True)
    with fs.openbin(path, 'r') as f:
        data = f.read()
    fs.close()
    with open(out, 'wb') as f:
        f.write(data)
    return len(data)


def main():
    args = sys.argv[1:]
    gets = []
    cmds = []
    i = 0
    while i < len(args):
        if args[i] == '--get':
            gets.append(args[i + 1])
            i += 2
        else:
            cmds.append(args[i])
            i += 1
    script = os.path.join(ROOT, 'build', 'audiotest', 'q.txt')
    os.makedirs(os.path.dirname(script), exist_ok=True)
    with open(script, 'w', encoding='utf-8', newline='\n') as f:
        f.write('wait 60\n')
        for c in cmds:
            f.write('run python -I -X utf8 tools/armpi_remote.py telnet 127.0.0.1:2329 "%s"\n' % c)
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    subprocess.run([sys.executable, '-I', '-X', 'utf8', 'tools/qemu_drive.py', script,
                    '--port', '4507', '--vnc', '57', '--fwd', '2329:23'], cwd=ROOT, env=env)
    for g in gets:
        src, dst = g.split('=', 1)
        p = src.split(':', 1)[1] if ':' in src else src
        try:
            print('extrait', src, extract(os.path.join(ROOT, 'build', 'sd.img'), p, dst), 'octets')
        except Exception as e:
            print('extraction impossible', src, e)


if __name__ == '__main__':
    main()
