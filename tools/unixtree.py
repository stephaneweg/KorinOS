"""unixtree.py : passe le dépôt à l'arbre unique de Korin (15/11/2026, docs/etude/MULTI-UTILISATEUR.md).

« / » est le volume système (plus de SD0:), les autres volumes sont montés dans un dossier :
  SD0:/home/x   -> HOME:/x            (le dossier personnel de l'utilisateur en cours)
  SD0:/gcc      -> /devtools/gcc      (de même dotnet, python)
  SD0:/x, SD:/x -> /x
  RAM0:/tmp/x   -> /tmp/x ; RAM0:/x -> /tmp/x   (le volume en mémoire est monté sur /tmp)
  USB0:/x       -> /media/usb0/x ; SD1:/x -> /media/sd1/x ; IMG0:/x -> /media/img0/x
  NET0:/x       -> /net/net0/x ; BOOT:/x -> /boot/x
  /sd0/x (forme POSIX d'avant) -> /x ; fs.sd0 -> fs.root
Une longueur écrite en dur juste après une chaîne changée (« "SD0:/apps/", 10 ») suit la chaîne.
Les formes nues (« "SD0:" » seul, « RAM0: » sans « / ») ne sont pas touchées : listées pour une
relecture à la main.

    python -I tools/unixtree.py            # applique
    python -I tools/unixtree.py --dry      # compte seulement
    python -I tools/unixtree.py --bare     # liste les formes nues restantes
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = ('packages/', 'prebuilt/', 'sdcard/', 'img/', 'ref/', 'build/', '.agents/memory/', 'docs/memoire/',
        'tools/unixtree.py', 'firmware/')

# pas au milieu d'un nom (« USD0: », « x.SD0: »), mais permis après un échappement (« \nSD0:/sys », « \tSD0: » :
# le n ou le t d'un « \n » n'est pas la fin d'un nom ; signalé par la session CAO, 16/11)
B = r'(?:(?<=\\[ntr])|(?<![A-Za-z0-9_.]))'

RULES = [
    (re.compile(B + r'SD0:/home(?=[/"\'\s`,;)\]}<>|]|$)', re.M), 'HOME:'),
    (re.compile(B + r'SD0:/(gcc|dotnet|python)(?=[/"\'\s`,;)\]}<>|.]|$)', re.M), r'/devtools/\1'),
    (re.compile(B + r'(?:SD0|SD|sd0):/'), '/'),
    (re.compile(B + r'(?:RAM0|RAM):/tmp(?=[/"\'\s`,;)\]}<>|.]|$)', re.M), '/tmp'),
    (re.compile(B + r'(?:RAM0|RAM):/'), '/tmp/'),
    (re.compile(B + r'USB:/'), '/media/usb0/'),
    (re.compile(B + r'USB([0-9]):/'), r'/media/usb\1/'),
    (re.compile(B + r'SD([1-9]):/'), r'/media/sd\1/'),
    (re.compile(B + r'IMG([0-9]):/'), r'/media/img\1/'),
    (re.compile(B + r'NET([0-9]):/'), r'/net/net\1/'),
    (re.compile(B + r'BOOT:/'), '/boot/'),
    (re.compile(r'(?<![A-Za-z0-9_.])/sd0/'), '/'),
    (re.compile(r'(?<![A-Za-z0-9_.])/sd0(?=["\'\s`,;)\]}<>|]|$)', re.M), '/'),
    (re.compile(r'\bfs\.sd0\b'), 'fs.root'),
]

BARE = re.compile(B + r'(?:SD0|SD1|RAM0|USB0|IMG0|IMG1|NET0|BOOT|SD|RAM|USB):(?!/)')

# second passage (--bare-fix) : les formes nues qui restent après la relecture du code (commentaires,
# messages, essais) ; « RAM: » et « SD: » restent à la main (« RAM: 4 Go » n'est pas un volume)
BARE_RULES = [
    (re.compile(B + r'SD0:(?![/A-Za-z0-9])'), '/'),
    (re.compile(B + r'RAM0:(?![/A-Za-z0-9])'), '/tmp'),
    (re.compile(B + r'USB([0-9n]):(?![/A-Za-z0-9])'), r'/media/usb\1'),
    (re.compile(B + r'SD([1-9]):(?![/A-Za-z0-9])'), r'/media/sd\1'),
    (re.compile(B + r'IMG([0-9n]):(?![/A-Za-z0-9])'), r'/media/img\1'),
    (re.compile(B + r'NET([0-9n]):(?![/A-Za-z0-9])'), r'/net/net\1'),
    (re.compile(B + r'BOOT:(?![/A-Za-z0-9])'), '/boot'),
]

# une chaîne C/asm suivie de sa longueur : « "SD0:/apps/", 10 » ou « 'SD0:/x', 6 »
LITLEN = re.compile(r'(["\'])((?:[^"\'\\\n]|\\.)*)\1(\s*,\s*)(\d+)\b')


def convert(text):
    def fix_litlen(m):
        q, lit, sep, n = m.group(1), m.group(2), m.group(3), m.group(4)
        new = lit
        for rx, rep in RULES:
            new = rx.sub(rep, new)
        if new != lit and int(n) == len(lit):
            return q + new + q + sep + str(len(new))
        return m.group(0)
    text = LITLEN.sub(fix_litlen, text)
    for rx, rep in RULES:
        text = rx.sub(rep, text)
    return text


def files():
    out = subprocess.run(['git', 'grep', '-lI', '-e', ':', '--', '.'], cwd=ROOT, capture_output=True,
                         text=True, encoding='utf-8').stdout.split('\n')
    return [f for f in out if f and not f.startswith(SKIP)]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    unknown = [a for a in sys.argv[1:] if a not in ('--dry', '--bare', '--bare-fix')]
    if unknown:		# (une option inconnue, --help compris : rien n'est changé)
        print(__doc__)
        sys.exit(2 if unknown != ['--help'] and unknown != ['-h'] else 0)
    dry = '--dry' in sys.argv
    bare = '--bare' in sys.argv
    changed = 0
    for f in files():
        p = os.path.join(ROOT, f)
        try:
            raw = open(p, 'rb').read()
            text = raw.decode('utf-8')
        except (UnicodeDecodeError, OSError):
            try:
                text = raw.decode('latin-1')
            except Exception:
                continue
            enc = 'latin-1'
        else:
            enc = 'utf-8'
        if bare:
            for i, line in enumerate(text.split('\n'), 1):
                if BARE.search(line):
                    print('%s:%d: %s' % (f, i, line.strip()[:140]))
            continue
        if '--bare-fix' in sys.argv:
            new = text
            for rx, rep in BARE_RULES:
                new = rx.sub(rep, new)
        else:
            new = convert(text)
        if new != text:
            changed += 1
            if not dry:
                open(p, 'wb').write(new.encode(enc))
    if not bare:
        print('%d fichiers %s' % (changed, 'à changer' if dry else 'changés'))


if __name__ == '__main__':
    main()
