#!/usr/bin/env python3
"""Renomme des identifiants dans tout le dépôt, sans toucher aux chaînes ni à la prose.

    python -I tools/rename_ids.py CARTE.txt [--apply] [chemins...]

CARTE.txt : une ligne « ancien nouveau [motif,motif...] » par identifiant (# : commentaire) ;
les motifs (fnmatch sur le chemin relatif, « / ») limitent le renommage à certains fichiers.
Sans --apply : compte seulement ce qui serait changé, et signale les nouveaux noms déjà pris.

Où l'on renomme :
- sources (asm, inc, abi, c, h, cpp, cs, bi, bas, S, ld) : les mots hors commentaires et chaînes,
  et dans les commentaires les noms composés seulement ;
- documentation (md) : les mots dans le code entre accents graves ou dans les blocs de code,
  seulement les noms composés (avec « _ » ou en capitales), pour ne pas toucher à la prose ;
- scripts (py, sh, ps1, ini, txt de configuration) : les noms composés hors commentaires.
Le texte des chaînes (messages) reste tel quel : le code produit ne change pas.
Les fichiers en Latin-1 (vieux sources) sont récrits en Latin-1.
"""
import fnmatch
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
DEFAULT_PATHS = ['src', 'docs', 'tools', 'skel', 'sdcard', 'packages', '.agents', 'boot', 'build.ps1', 'AGENTS.md']
SKIP_DIRS = {'.git', 'build', 'node_modules', '__pycache__'}
ASM = {'.asm', '.inc', '.abi'}
CLIKE = {'.c', '.h', '.cpp', '.hpp', '.cc', '.cs', '.S', '.ld', '.java', '.js'}
BASIC = {'.bi', '.bas'}
DOC = {'.md'}
SCRIPT = {'.py', '.sh', '.ps1', '.ini', '.conf'}
WORD = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


def compound(name):
    return '_' in name.strip('_') or (name.isupper() and len(name) > 3)


def load_map(path):
    m = {}
    for ln, line in enumerate(open(path, encoding='utf-8'), 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2:
            sys.exit(f'{path}:{ln} : « ancien nouveau » attendu')
        old, new = parts[0], parts[1]
        globs = parts[2].split(',') if len(parts) > 2 else None
        if old in m:
            sys.exit(f'{path}:{ln} : {old} deux fois')
        m[old] = (new, globs)
    return m


def code_spans(s, kind):
    """Rend les intervalles [début, fin) de s où l'on peut renommer (hors commentaires et chaînes)."""
    spans = []
    i, n, start = 0, len(s), 0
    while i < n:
        c = s[i]
        if kind == 'asm':
            if c == ';':
                spans.append((start, i))
                j = s.find('\n', i)
                j = n if j < 0 else j
                spans.append((i, j, 'note'))
                i = start = j
                continue
            if c in '\'"':
                spans.append((start, i))
                j = i + 1
                while j < n and s[j] != c and s[j] != '\n':
                    j += 1
                i = start = j + 1
                continue
        elif kind == 'c':
            if s.startswith('//', i):
                spans.append((start, i))
                j = s.find('\n', i)
                j = n if j < 0 else j
                spans.append((i, j, 'note'))
                i = start = j
                continue
            if s.startswith('/*', i):
                spans.append((start, i))
                j = s.find('*/', i + 2)
                j = n if j < 0 else j + 2
                spans.append((i, j, 'note'))
                i = start = j
                continue
            if c in '\'"':
                spans.append((start, i))
                j = i + 1
                while j < n and s[j] != c and s[j] != '\n':
                    j += 2 if s[j] == '\\' else 1
                i = start = j + 1
                continue
        elif kind == 'basic':
            if c == "'" or s[i:i + 4].upper() == 'REM ' and (i == 0 or not s[i - 1].isalnum()):
                spans.append((start, i))
                j = s.find('\n', i)
                j = n if j < 0 else j
                spans.append((i, j, 'note'))
                i = start = j
                continue
            if c == '"':
                spans.append((start, i))
                j = s.find('"', i + 1)
                i = start = n if j < 0 else j + 1
                continue
        elif kind == 'script':
            if c == '#' or (c == ';' and (i == 0 or s[i - 1] == '\n')):
                spans.append((start, i))
                j = s.find('\n', i)
                j = n if j < 0 else j
                spans.append((i, j, 'note'))
                i = start = j
                continue
        elif kind == 'doc':
            if s.startswith('```', i):
                j = s.find('```', i + 3)
                j = n if j < 0 else j
                spans.append((i + 3, j))
                i = j + 3
                continue
            if c == '`':
                j = s.find('`', i + 1)
                if j < 0 or '\n\n' in s[i:j]:
                    i += 1
                    continue
                spans.append((i + 1, j))
                i = j + 1
                continue
        i += 1
    if kind != 'doc':
        spans.append((start, n))
    return [sp if len(sp) == 3 else (sp[0], sp[1], 'note' if kind == 'doc' else 'code') for sp in spans]


def kind_of(path):
    ext = os.path.splitext(path)[1]
    if ext in ASM:
        return 'asm'
    if ext in CLIKE:
        return 'c'
    if ext in BASIC:
        return 'basic'
    if ext in DOC:
        return 'doc'
    if ext in SCRIPT:
        return 'script'
    return None


def files(paths):
    """Les fichiers suivis par git sous ces chemins (les sources tiers téléchargées ne le sont pas)."""
    import subprocess
    out = subprocess.run(['git', '-C', ROOT, 'ls-files', '-z', '-co', '--exclude-standard', '--'] + paths,
                         capture_output=True, check=True).stdout.decode('utf-8')
    for rel in out.split('\0'):
        if rel and not set(rel.split('/')) & SKIP_DIRS:
            yield rel


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    apply = '--apply' in sys.argv
    if not args:
        sys.exit(__doc__)
    m = load_map(args[0])
    paths = args[1:] or DEFAULT_PATHS
    news = {new: old for old, (new, _) in m.items()}
    counts = {old: 0 for old in m}
    taken = {}
    changed = 0
    for rel in files(paths):
        kind = kind_of(rel)
        if kind is None:
            continue
        try:
            raw = open(os.path.join(ROOT, rel), 'rb').read()
        except OSError:
            continue
        try:
            enc = 'utf-8'
            s = raw.decode(enc)
        except UnicodeDecodeError:
            enc = 'latin-1'
            s = raw.decode(enc)
        out, last, hit = [], 0, False
        for a, b, what in code_spans(s, kind):
            for w in WORD.finditer(s, a, b):
                name = w.group()
                if name in news and name not in m and what == 'code' and kind in ('asm', 'c', 'basic'):
                    taken.setdefault(name, rel)
                if name not in m:
                    continue
                new, globs = m[name]
                if globs and not any(fnmatch.fnmatch(rel, g) for g in globs):
                    continue
                if (what == 'note' or kind == 'script') and not compound(name):
                    continue
                counts[name] += 1
                out.append(s[last:w.start()])
                out.append(new)
                last = w.end()
                hit = True
        if hit:
            out.append(s[last:])
            changed += 1
            if apply:
                with open(os.path.join(ROOT, rel), 'w', encoding=enc, newline='') as f:
                    f.write(''.join(out))
    for name, rel in sorted(taken.items()):
        print(f'déjà pris : {name} (pour {news[name]}), par ex. dans {rel}')
    for old, c in sorted(counts.items()):
        if c == 0:
            print(f'jamais trouvé : {old}')
    print(f'{sum(counts.values())} remplacements dans {changed} fichiers' + ('' if apply else ' (essai : rien écrit)'))
    return 1 if taken else 0


if __name__ == '__main__':
    sys.exit(main())
