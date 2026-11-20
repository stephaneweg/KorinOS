#!/usr/bin/env python3
"""spec2c.py -- la table des DLL intégrées de win16 (src/apps/win16/api_table.c) depuis les .spec de winevdm.

    python3 -I tools/win16/spec2c.py

Chaque .spec de src/apps/win16/spec (format de Wine, repris de winevdm : « ordinal convention [options]
Nom(arguments) Implémentation ») donne, par ordinal : le nom exporté, la convention (pascal, register, cdecl), la
taille des arguments sur la pile 16 bits, et l'implémentation C « api_<Implémentation> », déclarée faible : une
fonction pas encore écrite vaut NULL, et win16 dit laquelle manque quand un programme l'appelle."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'src', 'apps', 'win16')
SPECS = [('KERNEL', 'krnl386.exe16.spec'), ('USER', 'user.exe16.spec'), ('GDI', 'gdi.exe16.spec')]
ARGS = {'word': 'w', 's_word': 's', 'long': 'l', 'ptr': 'p', 'str': 't', 'segptr': 'P', 'segstr': 'T',
        'double': 'd'}
LINE = re.compile(r'^\s*(\d+|@)\s+(pascal|cdecl|varargs|stub|register|variable|equate|extern)\s*(.*)$')


def parse(path):
    out = []
    for raw in open(path, encoding='utf-8', errors='replace'):
        line = raw.split('#')[0].rstrip()
        m = LINE.match(line)
        if not m or m.group(1) == '@':
            continue
        ordinal, conv, rest = int(m.group(1)), m.group(2), m.group(3)
        flags = []
        while rest.startswith('-'):
            f, _, rest = rest.partition(' ')
            flags.append(f)
            rest = rest.lstrip()
        if '-register' in flags:
            conv = 'register'
        if conv in ('stub', 'variable', 'equate', 'extern'):
            name = rest.split()[0] if rest else f'Ord{ordinal}'
            out.append((ordinal, name, 'stub', None, None, False))
            continue
        fm = re.match(r'(\w+)\s*\(([^)]*)\)\s*(\S*)', rest)
        if not fm:
            continue
        name, args, impl = fm.group(1), fm.group(2).split(), fm.group(3) or fm.group(1)
        impl = impl.split('.')[-1]
        if not re.match(r'^\w+$', impl):
            impl = None
        code = ''.join(ARGS.get(a, '?') for a in args)
        out.append((ordinal, name, conv, code, impl, '-ret16' in flags))
    return out


def main():
    conv_ids = {'pascal': 0, 'register': 1, 'cdecl': 2, 'varargs': 2, 'stub': 3}
    impls = set()
    tables = []
    for mod, spec in SPECS:
        entries = parse(os.path.join(SRC, 'spec', spec))
        tables.append((mod, entries))
        for e in entries:
            if e[4]:
                impls.add(e[4])
    o = ['/* api_table.c -- GÉNÉRÉ par tools/win16/spec2c.py depuis les .spec de spec/ (winevdm, format de Wine) :',
         ' * ne pas modifier à la main. */',
         '#include "w16.h"', '']
    for i in sorted(impls):
        o.append(f'extern void api_{i} (struct w16call *c) __attribute__ ((weak));')
    o.append('')
    for mod, entries in tables:
        o.append(f'static const struct w16_api api_{mod.lower()}[] = {{')
        for ordinal, name, conv, code, impl, ret16 in sorted(entries):
            fn = f'api_{impl}' if impl else '0'
            args = f'"{code}"' if code is not None else '0'
            o.append(f'\t{{ {ordinal}, {conv_ids[conv]}, {int(ret16)}, "{name}", {args}, {fn} }},')
        o.append('};')
        o.append('')
    o.append('const struct w16_module w16_modules[] = {')
    for mod, entries in tables:
        o.append(f'\t{{ "{mod}", api_{mod.lower()}, {len(entries)} }},')
    o.append('\t{ 0, 0, 0 }')
    o.append('};')
    path = os.path.join(SRC, 'api_table.c')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(o) + '\n')
    print(f'{path} : {sum(len(e) for _, e in tables)} entrées, {len(impls)} implémentations possibles')


if __name__ == '__main__':
    main()
