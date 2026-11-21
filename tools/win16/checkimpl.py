#!/usr/bin/env python3
"""checkimpl.py -- vérifie les implémentations de win16 :

    python3 -I tools/win16/checkimpl.py [PROG.EXE ...]

1. les fonctions « api_X » écrites dans src/apps/win16 que la table (api_table.c, tirée des .spec) ne connaît pas :
   un nom mal recopié, jamais appelé ;
2. pour chaque programme donné, les fonctions qu'il importe des DLL intégrées et qui ne sont pas encore écrites."""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'src', 'apps', 'win16')


def main():
    table = open(os.path.join(SRC, 'api_table.c'), encoding='utf-8').read()
    known = set(re.findall(r'extern void (api_\w+) ', table))
    defined = set()
    for f in os.listdir(SRC):
        if f.endswith('.c') and f != 'api_table.c':
            text = open(os.path.join(SRC, f), encoding='utf-8').read()
            defined |= set(re.findall(r'^void (api_\w+) \(struct w16call', text, re.M))
            defined |= set(re.findall(r'^[A-Z]+ \((api_\w+),', text, re.M))	# (fonctions faites par une macro)
    bad = sorted(defined - known)
    print(f'{len(defined)} fonctions écrites ; inconnues de la table : {", ".join(bad) if bad else "aucune"}')
    entries = {}
    for mod, body in re.findall(r'static const struct w16_api api_(\w+)\[\] = \{(.*?)\n\};', table, re.S):
        for m in re.finditer(r'\{ (\d+), (\d), \d, "(\w+)", [^,]+, (\w+)', body):
            # (une constante, « equate » : __AHINCR, __WINFLAGS… : rien à écrire)
            entries[(mod.upper(), int(m.group(1)))] = (m.group(3), 'equate' if m.group(2) == '4' else m.group(4))
    for exe in sys.argv[1:]:
        out = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'win16', 'nedump.py'), exe],
                             capture_output=True, text=True).stdout
        missing = []
        for line in out.splitlines():
            m = re.match(r'^(\w+) : (.*)$', line)
            if not m or m.group(1) in ('segment', 'NE', 'modules'):
                continue
            mod = m.group(1)
            for x in m.group(2).split(', '):
                if x.isdigit():
                    name, fn = entries.get((mod, int(x)), (x, '0'))
                else:
                    hit = [v for k, v in entries.items() if k[0] == mod and v[0].upper() == x.upper()]
                    if not hit:
                        continue
                    name, fn = hit[0]
                if fn != 'equate' and (fn == '0' or fn not in defined) and (mod, name) not in missing:
                    missing.append((mod, name))
        print(f'{os.path.basename(exe)} : {len(missing)} fonctions pas encore écrites')
        for mod, name in missing:
            print(f'   {mod}.{name}')


if __name__ == '__main__':
    main()
