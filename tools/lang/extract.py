"""Le catalogue des textes a traduire (docs/LANGUES.md) : tout texte d'un programme, langue du source devinee.

    python -I tools/lang/extract.py build/lang        un <programme>.json par programme, et un resume

Assembleur : les chaines « db » (UTF-8 direct ou octet par octet). C/C++ : les TR ("...") et les litteraux.
Seul le relecteur decide : un texte technique (chemin, format, cle) reste hors des dictionnaires.
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SORTIE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "lang")
ESSAIS = re.compile(r"test|bench|bug|hello|crash|fault|hog")
ACCENT = re.compile(r"[àâäçéèêëîïôöùûüÿœæÀÂÇÉÈÊËÎÏÔÖÙÛÜ]")
FR = re.compile(r"(?i)\b(le|la|les|des|du|un|une|et|ou|pour|dans|avec|sans|sur|fichier|dossier|fen[eê]tre|ouvrir|fermer|"
                r"enregistrer|annuler|erreur|impossible|aucun|nouveau|nouvelle|taille|nom|ligne|jeu|partie|quitter|"
                r"copier|coller|couper|supprimer|effacer|choisir|lire|ecrire|pas|est|sont|tapez|usage)\b")
TECH = re.compile(r"^[\s\W\d]*$|^(SD\d|RAM\d|USB\d|IMG\d):|^/|^\.|^https?:|^%|^[a-z_0-9.-]+$|^[A-Z_0-9]+$|\.(txt|ini|bmp|png|so|mod|cfg|rgba|app|elf)$")


def lit(p):
    with open(p, "rb") as f:
        return f.read()


def jetons_db(l):
    """les octets d'une ligne « db », ou None si ce n'est pas du texte"""
    out, i, n = bytearray(), 0, len(l)
    while i < n:
        c = l[i]
        if c in b" \t,":
            i += 1
        elif c == 0x27:
            j = l.find(b"'", i + 1)
            if j < 0:
                return None
            out += l[i + 1:j]
            i = j + 1
        elif c == 0x22:
            j = l.find(b'"', i + 1)
            if j < 0:
                return None
            out += l[i + 1:j]
            i = j + 1
        elif c == 0x3B:
            break
        else:
            m = re.match(rb"(0x[0-9a-fA-F]+|\d+)", l[i:])
            if not m:
                return None
            t = m.group(1)
            v = int(t, 16) if t[:2] == b"0x" else int(t)
            if v > 255:
                return None
            out.append(v)
            i += m.end()
    return bytes(out)


DB = re.compile(rb"^\s*(?:([A-Za-z_.][\w.]*)[ \t:]+)?db[ \t]+(.*)$")


def asm(p):
    res, cour = [], bytearray()
    for l in lit(p).splitlines():
        m = DB.match(l)
        if not m:
            cour = bytearray()
            continue
        o = jetons_db(m.group(2))
        if o is None:
            cour = bytearray()
            continue
        cour += o
        while 0 in cour:
            k = cour.index(0)
            res.append(bytes(cour[:k]))
            del cour[: k + 1]
    r = []
    for b in res:
        try:
            s = b.decode("utf-8")
        except UnicodeDecodeError:
            continue
        r.append(s)
    return r


TR = re.compile(rb'\bTRC?\s*\(\s*(?:"[^"]*"\s*,\s*)?"((?:[^"\\\n]|\\.)*)"')
LIT = re.compile(rb'"((?:[^"\\\n]|\\.)*)"')


def c(p):
    d = re.sub(rb"//[^\n]*|/\*.*?\*/", b"", lit(p), flags=re.S)
    d = re.sub(rb"(?m)^\s*#\s*include[^\n]*", b"", d)
    tr = [m.group(1).decode("utf-8", "replace") for m in TR.finditer(d)]
    lits = [m.group(1).decode("utf-8", "replace") for m in LIT.finditer(d)]
    return tr, lits


def garde(s):
    s2 = s.strip()
    return len(s2) >= 2 and re.search(r"[A-Za-zÀ-ÿ]{2}", s2) and not TECH.search(s2)


def langue(s):
    return "fr" if ACCENT.search(s) or FR.search(s) else "?"


def dicts(d):
    """textes deja traduits dans les dictionnaires d'un programme"""
    r = set()
    for b, _, fs in os.walk(d):
        if "lang" in b.replace("\\", "/").split("/")[-1] or os.path.basename(b) == "data":
            for f in fs:
                if f.startswith("lang") or re.match(r"(fr|en)\.txt$", f):
                    for l in lit(os.path.join(b, f)).decode("utf-8", "replace").splitlines():
                        if "=" in l and not l.startswith("#"):
                            r.add(l.split("=", 1)[0].strip())
    return r


os.makedirs(SORTIE, exist_ok=True)
resume = []
for cat in ("apps", "sys", "bin", "svc"):
    base = os.path.join(ROOT, "src", cat)
    for n in sorted(os.listdir(base)):
        d = os.path.join(base, n)
        if not os.path.isdir(d) or n in ("inc", "modules") or (cat in ("bin", "svc") and ESSAIS.search(n)):
            continue
        a, tr, lits = [], [], []
        for b, ds, fs in os.walk(d):
            for f in fs:
                p = os.path.join(b, f)
                if f.endswith((".asm", ".inc")):
                    a += asm(p)
                elif f.endswith((".cpp", ".h", ".c", ".hpp")) and not f.startswith("stb_"):
                    t, l = c(p)
                    tr += t
                    lits += l
        deja = dicts(d)
        A = sorted({s for s in a if garde(s)})
        T = sorted({s for s in tr if garde(s)})
        L = sorted({s for s in lits if garde(s) and s not in tr and langue(s) == "fr"})
        if not (A or T or L):
            continue
        cat_d = {"asm": A, "tr": T, "tr_sans_dico": [s for s in T if s not in deja], "c_fr": L}
        with open(os.path.join(SORTIE, f"{cat}-{n}.json"), "w", encoding="utf-8") as f:
            json.dump(cat_d, f, ensure_ascii=False, indent=0)
        resume.append((cat, n, len(A), len(T), len(cat_d["tr_sans_dico"]), len(L), len(deja)))

print(f"{'prog':24}{'asm':>6}{'TR':>6}{'TRsd':>6}{'Cfr':>6}{'dico':>6}")
for r in resume:
    print(f"{r[0] + '/' + r[1]:24}{r[2]:>6}{r[3]:>6}{r[4]:>6}{r[5]:>6}{r[6]:>6}")
print("total", [sum(r[i] for r in resume) for i in range(2, 7)])
