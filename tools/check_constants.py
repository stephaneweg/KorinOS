# check_constants.py - verifie qu'aucune constante "NOM = valeur" n'est definie
# plusieurs fois avec des valeurs differentes dans un meme executable.
#
# FASMARM accepte en silence qu'on redefinisse une constante : le code place
# avant la seconde definition voit l'une, celui place apres voit l'autre (le
# pilote wifi lisait sa table de 64 en 64 au lieu de 48 : WE_SIZE de rdpd dans
# armpi.inc). Pour chaque programme assemble par build.ps1 (src/<cat>/<nom>/
# <nom>.asm, src/svc/modules/*.asm, le noyau pour le Pi 4 et le Pi 5), on suit
# les include et on releve les definitions ; les valeurs sont comparees comme
# texte (espaces et commentaires retires).
#
#   python -I tools/check_constants.py [racine]   -> code 1 et la liste si conflit
import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..'))
SRC = os.path.join(ROOT, 'src')

# Redefinitions voulues : interrupteur de kit.inc (0 / 1 autour des tables de
# kits), compteur de macro de fasmarm.
ALLOWED = {'KIT_BUILD', 'counter'}
# Directives de FASM qui ressemblent a "nom = ..." (match =pi4, ARCH { ... })
DIRECTIVES = {'match', 'if', 'else', 'while', 'repeat', 'rept', 'irp', 'irps', 'define', 'restore'}

INC_RE = re.compile(r"include\s+'([^']+)'", re.I)
DEF_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]*?)\s*(;.*)?$")


def read_lines(path):
    with open(path, 'rb') as f:
        data = f.read()
    for enc in ('utf-8-sig', 'latin-1'):
        try:
            return data.decode(enc).splitlines()
        except UnicodeDecodeError:
            pass
    return []


def walk(path, main_dir, seen, defs, skip):
    path = os.path.normpath(path)
    if path in seen or not os.path.exists(path):
        return
    seen.add(path)
    for i, line in enumerate(read_lines(path), 1):
        code = line.split(';', 1)[0]
        if not code.strip():
            continue
        m = INC_RE.search(code)
        if m:
            rel = m.group(1)
            if skip and skip in rel:
                continue
            for base in (os.path.dirname(path), main_dir):
                cand = os.path.join(base, rel)
                if os.path.exists(cand):
                    walk(cand, main_dir, seen, defs, skip)
                    break
            continue
        m = DEF_RE.match(line)
        if m:
            name = m.group(1)
            if name in ALLOWED or name.lower() in DIRECTIVES:
                continue
            val = re.sub(r'\s+', ' ', m.group(2).strip())
            defs.setdefault(name, []).append((val, os.path.relpath(path, ROOT), i))


def conflicts(main, skip=None):
    defs = {}
    walk(main, os.path.dirname(main), set(), defs, skip)
    return {n: v for n, v in defs.items() if len({x[0] for x in v}) > 1}


def programs():
    for cat in ('apps', 'bin', 'sys', 'svc', 'kits'):
        d = os.path.join(SRC, cat)
        if os.path.isdir(d):
            for n in sorted(os.listdir(d)):
                f = os.path.join(d, n, n + '.asm')
                if os.path.exists(f):
                    yield f, None
    md = os.path.join(SRC, 'svc', 'modules')
    if os.path.isdir(md):
        for n in sorted(os.listdir(md)):
            if n.endswith('.asm'):
                yield os.path.join(md, n), None
    k = os.path.join(SRC, 'kernel', 'kernel.asm')
    yield k, 'arch/pi5'			# noyau du Pi 4
    yield k, 'arch/pi4'			# noyau du Pi 5


def main():
    found = 0
    for prog, skip in programs():
        bad = conflicts(prog, skip)
        if not bad:
            continue
        label = os.path.relpath(prog, ROOT)
        if skip:
            label += ' (%s)' % ('pi4' if skip == 'arch/pi5' else 'pi5')
        print('constantes redefinies avec des valeurs differentes dans %s :' % label)
        for name in sorted(bad):
            print('  %s' % name)
            for val, f, line in bad[name]:
                print('    %-30s %s:%d' % (val[:30], f, line))
            found += 1
    if found:
        print('%d conflit(s) : renommer l\'une des constantes (FASMARM ne le signale pas)' % found)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
