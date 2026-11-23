#!/usr/bin/env python3
"""versions.py : lit build_files/build_environment/cmake/versions.cmake de Blender et donne, pour chaque nom
demandé (IMATH, OPENEXR…), une ligne « nom fichier url somme type » (type : MD5 ou SHA256).

    python3 -I versions.py VERSIONS_CMAKE NOM…

Seules les lignes `set(VAR valeur)` tenant sur une ligne sont lues ; `${VAR}` est développé. Les blocs
if(WIN32)/if(APPLE) sont ignorés : leurs valeurs ne servent pas à Korin, et la première définition hors de ces
blocs gagne."""
import re
import sys

SET = re.compile(r'^\s*set\((\w+)\s+(.*?)\)\s*(#.*)?$')
REF = re.compile(r'\$\{(\w+)\}')


def read(path):
    vals = {}
    skip = 0
    with open(path, encoding='utf-8') as f:
        lines = iter(f.read().splitlines())
        for line in lines:
            s = line.strip()
            # set(VAR\n valeur) : la valeur seule sur la ligne suivante (URI de VMA)
            if re.match(r'^set\(\w+$', s):
                s = s + ' ' + next(lines, '').strip()
            low = s.lower()
            if skip:
                if low.startswith('if('):
                    skip += 1
                elif low.startswith('endif('):
                    skip -= 1
                continue
            if low.startswith('if(') and re.match(r'if\((not\s+)?(win32|apple|msvc|blender_platform_windows_arm)\b', low):
                skip = 1
                continue
            m = SET.match(s)
            if not m:
                continue
            name, value = m.group(1), m.group(2).strip()
            if value.startswith('"') and value.endswith('"'):
                value = value[1:-1]
            vals.setdefault(name, value)
    return vals


def expand(vals, value, depth=0):
    if depth > 10:
        raise SystemExit('versions.py : références trop profondes : ' + value)
    return REF.sub(lambda m: expand(vals, vals.get(m.group(1), ''), depth + 1), value)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    vals = read(sys.argv[1])
    bad = 0
    for name in sys.argv[2:]:
        got = [expand(vals, vals.get(f'{name}_{k}', '')) for k in ('FILE', 'URI', 'HASH', 'HASH_TYPE')]
        if not all(got):
            print(f'versions.py : {name} incomplet ({got})', file=sys.stderr)
            bad = 1
            continue
        print(name, *got)
    sys.exit(bad)


if __name__ == '__main__':
    main()
