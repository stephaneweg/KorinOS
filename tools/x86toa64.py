"""x86toa64 - traducteur source-a-source x86-32 (syntaxe fasm) -> AArch64 (FASMARM)

But : porter nativement FASMARM (fasm 1.73.30 + backend ARM, ecrits en x86) sur
ARMPI (AArch64), sans emulateur. Le programme traduit garde exactement la
semantique du code x86, avec un modele memoire simple :

  * l'image est assemblee a l'origine 0 (format binary + en-tete ELF ecrit a la
    main) et chargee a ELF_BASE ; x28 = ELF_BASE. Toute adresse x86 (32 bits)
    est donc un decalage depuis x28 : acces memoire [x28, wN, uxtw].
  * registres : eax..edi -> w0..w7 (eax=w0 ecx=w1 edx=w2 ebx=w3 esp=w4 ebp=w5
    esi=w6 edi=w7). Temporaires : w9-w17. w24/w25 : manipulation de NZCV.
    w27 : CF conserve dans un registre. w26 : octet de parite (PF).
  * la pile x86 est emulee (esp = w4 dans la memoire de l'image) : push/pop/
    call/ret/[esp+n] ont la semantique exacte (adresses de retour = decalages).
  * drapeaux : analyse de vivacite (CF ZF SF OF PF) sur le graphe de controle ;
    chaque producteur ne calcule que les drapeaux vivants, et un "etat"
    statique decrit ou se trouve chaque drapeau x86 (condition NZCV, registre,
    constante). Etat canonique aux frontieres : CF=C, ZF=Z, SF=N, OF=V.

Usage : python -I x86toa64.py <source x86 principal> <sortie .asm>
"""
import os
import re
import sys

# ---------------------------------------------------------------------------
#  Registres
# ---------------------------------------------------------------------------
R32 = {'eax': 0, 'ecx': 1, 'edx': 2, 'ebx': 3, 'esp': 4, 'ebp': 5, 'esi': 6, 'edi': 7}
R16 = {'ax': 0, 'cx': 1, 'dx': 2, 'bx': 3, 'sp': 4, 'bp': 5, 'si': 6, 'di': 7}
R8L = {'al': 0, 'cl': 1, 'dl': 2, 'bl': 3}
R8H = {'ah': 0, 'ch': 1, 'dh': 2, 'bh': 3}
VREG = {'vax': 'eax', 'vbx': 'ebx', 'vcx': 'ecx', 'vdx': 'edx', 'vsi': 'esi', 'vdi': 'edi',
        'vsp': 'esp', 'vbp': 'ebp'}
SIZEKW = {'byte': 1, 'word': 2, 'dword': 4, 'qword': 8, 'pword': 6, 'tword': 10}
ARMSIZE = {1: 'byte', 2: 'hword', 4: 'word', 8: 'dword'}
REGNAMES = set(R32) | set(R16) | set(R8L) | set(R8H)

FLAGS = ('C', 'Z', 'S', 'O', 'P')
ALLF = frozenset('CZSO')


class TranslateError(Exception):
    pass


class Reg:
    def __init__(self, idx, size, hi=False):
        self.idx, self.size, self.hi = idx, size, hi

    def w(self):
        return 'w%d' % self.idx


class Imm:
    def __init__(self, expr):
        self.expr = expr.strip()
        self.size = 0


class Mem:
    def __init__(self, size, base, index, scale, disp):
        self.size, self.base, self.index, self.scale, self.disp = size, base, index, scale, disp


# ---------------------------------------------------------------------------
#  Constantes : evaluation en Python quand c'est un litteral simple
# ---------------------------------------------------------------------------
def lit_value(expr):
    """Valeur entiere d'un litteral fasm simple, sinon None."""
    e = expr.strip()
    neg = False
    if e.startswith('-'):
        neg, e = True, e[1:].strip()
    v = None
    m = re.fullmatch(r"'(.)'", e)
    if m:
        v = ord(m.group(1))
    elif re.fullmatch(r'0x[0-9a-fA-F]+', e):
        v = int(e, 16)
    elif re.fullmatch(r'[0-9][0-9a-fA-F]*[hH]', e):
        v = int(e[:-1], 16)
    elif re.fullmatch(r'[01]+[bB]', e):
        v = int(e[:-1], 2)
    elif re.fullmatch(r'[0-7]+[oOqQ]', e):
        v = int(e[:-1], 8)
    elif re.fullmatch(r'[0-9]+', e):
        v = int(e)
    elif re.fullmatch(r'\$[0-9a-fA-F]+', e):
        v = int(e[1:], 16)
    if v is None:
        return None
    return -v if neg else v


def arith_imm_ok(v):
    return v is not None and (0 <= v <= 4095 or (v & 0xFFF == 0 and 0 <= v <= 0xFFF000))


def logic_imm_ok(v, bits=32):
    """Immediat logique AArch64 encodable (motif repete de 1 consecutifs)."""
    if v is None:
        return False
    mask = (1 << bits) - 1
    v &= mask
    if v == 0 or v == mask:
        return False
    for size in (2, 4, 8, 16, 32, 64):
        if size > bits:
            break
        elem = v & ((1 << size) - 1)
        rep = 0
        for i in range(0, bits, size):
            rep |= elem << i
        if rep != v:
            continue
        # elem doit etre une rotation d'une suite de 1
        for r in range(size):
            rot = ((elem >> r) | (elem << (size - r))) & ((1 << size) - 1)
            if rot != 0 and (rot & (rot + 1)) == 0:
                return True
        return False
    return False


# ---------------------------------------------------------------------------
#  Decoupage des lignes
# ---------------------------------------------------------------------------
def strip_comment(line):
    out, q = [], None
    for ch in line:
        if q:
            out.append(ch)
            if ch == q:
                q = None
        elif ch in '\'"':
            q = ch
            out.append(ch)
        elif ch == ';':
            break
        else:
            out.append(ch)
    return ''.join(out).rstrip()


def split_operands(s):
    """Separe sur les virgules hors crochets, parentheses et chaines."""
    parts, cur, depth, q = [], [], 0, None
    for ch in s:
        if q:
            cur.append(ch)
            if ch == q:
                q = None
            continue
        if ch in '\'"':
            q = ch
        elif ch in '[(':
            depth += 1
        elif ch in '])':
            depth -= 1
        elif ch == ',' and depth == 0:
            parts.append(''.join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    if ''.join(cur).strip():
        parts.append(''.join(cur).strip())
    return parts


def split_push_operands(s):
    """push/pop : operandes separes par des espaces ; un mot-cle de taille se
    rattache a l'operande suivant."""
    toks, cur, depth, q = [], [], 0, None
    for ch in s:
        if q:
            cur.append(ch)
            if ch == q:
                q = None
            continue
        if ch in '\'"':
            q = ch
        elif ch in '[(':
            depth += 1
        elif ch in '])':
            depth -= 1
        if ch.isspace() and depth == 0:
            if cur:
                toks.append(''.join(cur))
                cur = []
            continue
        cur.append(ch)
    if cur:
        toks.append(''.join(cur))
    out, pending = [], None
    for t in toks:
        if t.lower() in SIZEKW:
            pending = t
            continue
        out.append((pending + ' ' + t) if pending else t)
        pending = None
    return out


# ---------------------------------------------------------------------------
#  Operandes
# ---------------------------------------------------------------------------
def norm_reg(name):
    n = name.lower()
    return VREG.get(n, n)


def parse_operand(text, label_sizes, cur_global):
    t = text.strip()
    t = re.sub(r'^(near|short)\s+', '', t, flags=re.I)
    tl = norm_reg(t)
    if tl in R32:
        return Reg(R32[tl], 4)
    if tl in R16:
        return Reg(R16[tl], 2)
    if tl in R8L:
        return Reg(R8L[tl], 1)
    if tl in R8H:
        return Reg(R8H[tl], 1, hi=True)
    m = re.match(r'^(byte|word|dword|qword|pword|tword)?\s*(?:ptr\s*)?(\w\w:)?\s*\[(.*)\]$', t, re.I | re.S)
    if m:
        size = SIZEKW[m.group(1).lower()] if m.group(1) else 0
        inner = m.group(3)
        inner = re.sub(r'^\s*[cdefgs]s:\s*', '', inner, flags=re.I)
        return parse_address(inner, size, label_sizes, cur_global)
    m = re.match(r'^(byte|word|dword)\s+(.*)$', t, re.I)
    if m:
        im = Imm(m.group(2))
        im.size = SIZEKW[m.group(1).lower()]
        return im
    return Imm(t)


def split_terms(expr):
    """Termes de premier niveau separes par + et - (signe conserve)."""
    terms, cur, depth, q, sign = [], [], 0, None, '+'
    i = 0
    s = expr
    while i < len(s):
        ch = s[i]
        if q:
            cur.append(ch)
            if ch == q:
                q = None
        elif ch in '\'"':
            q = ch
            cur.append(ch)
        elif ch == '(':
            depth += 1
            cur.append(ch)
        elif ch == ')':
            depth -= 1
            cur.append(ch)
        elif ch in '+-' and depth == 0:
            prev = ''.join(cur).strip()
            # signe unaire en debut, ou exposant (1e-5) improbable ici
            if prev == '' and not terms and ch == '-':
                sign = '-' if sign == '+' else '+'
            elif prev == '':
                if ch == '-':
                    sign = '-' if sign == '+' else '+'
            else:
                terms.append((sign, prev))
                cur = []
                sign = ch
        else:
            cur.append(ch)
        i += 1
    if ''.join(cur).strip():
        terms.append((sign, ''.join(cur).strip()))
    return terms


def parse_address(inner, size, label_sizes, cur_global):
    base = index = None
    scale = 1
    disp = []
    for sign, term in split_terms(inner):
        tl = term.replace(' ', '').lower()
        regs = None
        m = re.fullmatch(r'([a-z]{2,3})\*([0-9]+)', tl) or re.fullmatch(r'([0-9]+)\*([a-z]{2,3})', tl)
        if m:
            a, b = m.group(1), m.group(2)
            r, k = (a, b) if not a.isdigit() else (b, a)
            r = norm_reg(r)
            if r in R32:
                if sign != '+' or index is not None:
                    raise TranslateError('adresse non geree : ' + inner)
                k = int(k)
                if k in (3, 5, 9):          # [r*5] = [r+r*4]
                    if base is not None:
                        raise TranslateError('adresse non geree : ' + inner)
                    base, k = R32[r], k - 1
                index, scale = R32[r], k
                continue
        r = norm_reg(tl)
        if r in R32:
            if sign != '+':
                raise TranslateError('registre soustrait : ' + inner)
            if base is None:
                base = R32[r]
            elif index is None:
                index = R32[r]
            else:
                raise TranslateError('trop de registres : ' + inner)
            continue
        disp.append((sign, term))
    dexpr = ''
    for sign, term in disp:
        if dexpr == '':
            dexpr = ('-' if sign == '-' else '') + '(' + term + ')'
        else:
            dexpr += sign + '(' + term + ')'
    if size == 0:
        size = infer_label_size(dexpr, label_sizes, cur_global)
    return Mem(size, base, index, scale, dexpr)


def infer_label_size(dexpr, label_sizes, cur_global):
    for name in re.findall(r'[A-Za-z_.@?][\w.@?]*', dexpr):
        full = (cur_global + name) if name.startswith('.') else name
        if full in label_sizes:
            return label_sizes[full]
        if name in label_sizes:
            return label_sizes[name]
    return 0


# ---------------------------------------------------------------------------
#  Conditions
# ---------------------------------------------------------------------------
# condition x86 -> (fonction des drapeaux x86, drapeaux lus)
X86COND = {
    'o': (lambda f: f['O'], 'O'), 'no': (lambda f: not f['O'], 'O'),
    'b': (lambda f: f['C'], 'C'), 'c': (lambda f: f['C'], 'C'), 'nae': (lambda f: f['C'], 'C'),
    'ae': (lambda f: not f['C'], 'C'), 'nb': (lambda f: not f['C'], 'C'), 'nc': (lambda f: not f['C'], 'C'),
    'e': (lambda f: f['Z'], 'Z'), 'z': (lambda f: f['Z'], 'Z'),
    'ne': (lambda f: not f['Z'], 'Z'), 'nz': (lambda f: not f['Z'], 'Z'),
    'be': (lambda f: f['C'] or f['Z'], 'CZ'), 'na': (lambda f: f['C'] or f['Z'], 'CZ'),
    'a': (lambda f: not f['C'] and not f['Z'], 'CZ'), 'nbe': (lambda f: not f['C'] and not f['Z'], 'CZ'),
    's': (lambda f: f['S'], 'S'), 'ns': (lambda f: not f['S'], 'S'),
    'p': (lambda f: f['P'], 'P'), 'pe': (lambda f: f['P'], 'P'),
    'np': (lambda f: not f['P'], 'P'), 'po': (lambda f: not f['P'], 'P'),
    'l': (lambda f: f['S'] != f['O'], 'SO'), 'nge': (lambda f: f['S'] != f['O'], 'SO'),
    'ge': (lambda f: f['S'] == f['O'], 'SO'), 'nl': (lambda f: f['S'] == f['O'], 'SO'),
    'le': (lambda f: f['Z'] or f['S'] != f['O'], 'ZSO'), 'ng': (lambda f: f['Z'] or f['S'] != f['O'], 'ZSO'),
    'g': (lambda f: not f['Z'] and f['S'] == f['O'], 'ZSO'),
    'nle': (lambda f: not f['Z'] and f['S'] == f['O'], 'ZSO'),
}

ARMCOND = {
    'eq': lambda n, z, c, v: z, 'ne': lambda n, z, c, v: not z,
    'cs': lambda n, z, c, v: c, 'cc': lambda n, z, c, v: not c,
    'mi': lambda n, z, c, v: n, 'pl': lambda n, z, c, v: not n,
    'vs': lambda n, z, c, v: v, 'vc': lambda n, z, c, v: not v,
    'hi': lambda n, z, c, v: c and not z, 'ls': lambda n, z, c, v: not c or z,
    'ge': lambda n, z, c, v: n == v, 'lt': lambda n, z, c, v: n != v,
    'gt': lambda n, z, c, v: not z and n == v, 'le': lambda n, z, c, v: z or n != v,
}
INVCOND = {'eq': 'ne', 'ne': 'eq', 'cs': 'cc', 'cc': 'cs', 'mi': 'pl', 'pl': 'mi', 'vs': 'vc',
           'vc': 'vs', 'hi': 'ls', 'ls': 'hi', 'ge': 'lt', 'lt': 'ge', 'gt': 'le', 'le': 'gt'}

CANON = {'C': ('hw', 'cs'), 'Z': ('hw', 'eq'), 'S': ('hw', 'mi'), 'O': ('hw', 'vs')}
FLAGBIT = {'S': 31, 'Z': 30, 'C': 29, 'O': 28}


# ---------------------------------------------------------------------------
#  Lignes du programme
# ---------------------------------------------------------------------------
DATA_MAP = {'db': 'db', 'du': 'du', 'dw': 'dh', 'dd': 'dw', 'dq': 'dd',
            'rb': 'rb', 'rw': 'rh', 'rd': 'rw', 'rq': 'rd', 'file': 'file'}
DATA_SIZE = {'db': 1, 'du': 2, 'dw': 2, 'dd': 4, 'dq': 8, 'rb': 1, 'rw': 2, 'rd': 4, 'rq': 8, 'file': 1}

X86_MNEM = set('''mov movzx movsx lea add sub adc sbb and or xor cmp test inc dec neg not shl sal shr sar
rol ror rcl rcr shld shrd bt bts btr bsf bsr mul imul div idiv cdq cwde cbw xchg push pop pushd
call ret retn jmp loop loopnzd loopnz loopd jecxz stc clc cmc std cld int ud2 nop xlat xlatb pushf
popf lods lodsb lodsw lodsd stos stosb stosw stosd movs movsb movsw movsd scas scasb scasw scasd
cmps cmpsb cmpsw cmpsd rep repe repz repne repnz salc jcxz lahf sahf'''.split())
CC = r'(n?(?:o|b|c|ae|nb|e|z|be|a|nae|nbe|na|s|p|pe|po|l|ge|nl|le|g|nge|ng|nle|nc))'
JCC_RE = re.compile(r'^j' + CC + '$')
SETCC_RE = re.compile(r'^set' + CC + '$')
CMOV_RE = re.compile(r'^cmov' + CC + '$')


def is_instr(m):
    return m in X86_MNEM or bool(JCC_RE.match(m) or SETCC_RE.match(m) or CMOV_RE.match(m))


class Item:
    def __init__(self, kind, text, src):
        self.kind, self.text, self.src = kind, text, src
        self.mn = self.ops = self.label = self.target = None
        self.glob = ''
        self.succ = []
        self.reads = frozenset()
        self.writes = frozenset()
        self.live_in = frozenset()
        self.live_out = frozenset()
        self.special = None
        self.df = 0
        self.in_macro = 0
        self.addr = 0


LABEL_RE = re.compile(r'^\s*([A-Za-z_.@?$!][\w.@?$!]*)\s*:(?!=)\s*(.*)$')
DATA_RE = re.compile(r'^\s*([A-Za-z_.@?][\w.@?]*)\s+(db|du|dw|dd|dq|rb|rw|rd|rq|file)\b\s*(.*)$', re.I)
DATA_NOLABEL_RE = re.compile(r'^\s*(db|du|dw|dd|dq|rb|rw|rd|rq|file)\b\s*(.*)$', re.I)
LABELDIR_RE = re.compile(r'^\s*label\s+([A-Za-z_.@?][\w.@?]*)\s*(byte|word|dword|qword|pword|tword)?\s*(at\s+.*)?$', re.I)


def brace_delta(s):
    q = None
    d = 0
    prev = ''
    for ch in s:
        if q:
            if ch == q:
                q = None
        elif ch in '\'"':
            q = ch
        elif ch == '{':
            d += 1
        elif ch == '}':
            d -= 1
        prev = ch
    return d


def classify(lines):
    """lines : liste (texte, source). -> liste d'Items, tailles des etiquettes."""
    items = []
    sizes = {}
    glob = ''
    depth = 0
    for text, src in lines:
        s = strip_comment(text).strip()
        if not s:
            continue
        delta = brace_delta(s)
        while True:
            m = LABEL_RE.match(s)
            if not m:
                break
            name = m.group(1)
            full = glob + name if name.startswith('.') else name
            if not name.startswith('.') and not name.startswith('@'):
                glob = name
            it = Item('label', name + ':', src)
            it.label = full
            it.glob = glob
            it.in_macro = depth
            items.append(it)
            s = m.group(2).strip()
        if not s:
            depth += delta
            continue
        m = DATA_RE.match(s)
        if m and not is_instr(m.group(1).lower()) and m.group(1).lower() not in ('label', 'virtual'):
            name = m.group(1)
            full = glob + name if name.startswith('.') else name
            if not name.startswith('.'):
                glob = name
            sizes[full] = DATA_SIZE.get(m.group(2).lower(), 1)
            it = Item('data', s, src)
            it.label = full
        else:
            m = LABELDIR_RE.match(s)
            if m:
                name = m.group(1)
                full = glob + name if name.startswith('.') else name
                if m.group(2):
                    sizes[full] = SIZEKW[m.group(2).lower()]
                if not name.startswith('.'):
                    glob = name
                it = Item('labeldir', s, src)
                it.label = full
            else:
                tok = s.split(None, 1)
                mn = tok[0].lower()
                if is_instr(mn) and depth == 0 or (is_instr(mn) and mn not in ('local',)):
                    it = Item('instr', s, src)
                    it.mn = mn
                    it.ops = tok[1] if len(tok) > 1 else ''
                elif DATA_NOLABEL_RE.match(s):
                    it = Item('data', s, src)
                else:
                    it = Item('raw', s, src)
        it.glob = glob
        it.in_macro = depth
        items.append(it)
        depth += delta
    # tailles a travers les equivalences "a equ b" / "a = b"
    for it in items:
        if it.kind == 'raw':
            m = re.match(r'^([A-Za-z_][\w.@?]*)\s+(?:equ|=)\s*([A-Za-z_][\w.@?]*)\s*$', strip_comment(it.text).strip())
            if m and m.group(2) in sizes and m.group(1) not in sizes:
                sizes[m.group(1)] = sizes[m.group(2)]
    return items, sizes


# ---------------------------------------------------------------------------
#  Lectures / ecritures de drapeaux (analyse de vivacite)
# ---------------------------------------------------------------------------
W_ALL = frozenset('CZSOP')


def rep_sub(it):
    return it.ops.split(None, 1)[0].lower() if it.ops else ''


def flag_effects(it):
    """-> (lus, ecrits) pour une instruction."""
    m = it.mn
    if m in ('rep', 'repe', 'repz', 'repne', 'repnz'):
        sub = rep_sub(it)
        if sub.startswith('cmps') or sub.startswith('scas'):
            return frozenset(), W_ALL
        return frozenset(), frozenset()
    jm = JCC_RE.match(m)
    if jm:
        return frozenset(X86COND[jm.group(1)][1]), frozenset()
    sm = SETCC_RE.match(m) or CMOV_RE.match(m)
    if sm:
        return frozenset(X86COND[sm.group(1)][1]), frozenset()
    if m in ('adc', 'sbb'):
        return frozenset('C'), W_ALL
    if m in ('rcl', 'rcr'):
        return frozenset('C'), frozenset('CO')
    if m in ('add', 'sub', 'cmp', 'and', 'or', 'xor', 'test', 'neg', 'bsf', 'bsr', 'mul', 'imul',
             'div', 'idiv', 'popf'):
        return frozenset(), W_ALL
    if m.startswith('cmps') or m.startswith('scas'):
        return frozenset(), W_ALL
    if m in ('inc', 'dec'):
        return frozenset(), frozenset('ZSOP')
    if m in ('shl', 'sal', 'shr', 'sar', 'shld', 'shrd'):
        ops = split_operands(it.ops)
        if ops and ops[-1].strip().lower() == 'cl':
            return frozenset(), frozenset()
        return frozenset(), W_ALL
    if m in ('rol', 'ror'):
        ops = split_operands(it.ops)
        if ops and ops[-1].strip().lower() == 'cl':
            return frozenset(), frozenset()
        return frozenset(), frozenset('CO')
    if m in ('bt', 'bts', 'btr'):
        return frozenset(), frozenset('CSOP')
    if m in ('stc', 'clc'):
        return frozenset(), frozenset('C')
    if m == 'cmc':
        return frozenset('C'), frozenset('C')
    if m in ('pushf', 'lahf'):
        return ALLF, frozenset()
    if m == 'sahf':
        return frozenset(), W_ALL
    if m in ('loopnz', 'loopnzd'):
        return frozenset('Z'), frozenset()
    if m in ('ret', 'retn'):
        return ALLF, frozenset()
    return frozenset(), frozenset()


def branch_target(it):
    """Etiquette cible d'un saut/appel direct (nom complet), ou None (indirect)."""
    ops = it.ops.strip()
    ops = re.sub(r'^(near|short|dword)\s+', '', ops, flags=re.I).strip()
    if not ops or '[' in ops or norm_reg(ops) in REGNAMES:
        return None
    if not re.fullmatch(r'[A-Za-z_.@?!][\w.@?!]*', ops):
        return None
    if ops.startswith('.'):
        return it.glob + ops
    return ops


def build_cfg(items):
    labels = {}
    anon = []
    for i, it in enumerate(items):
        if it.label and it.kind in ('label', 'labeldir', 'data'):
            labels.setdefault(it.label, i)
            if it.label.endswith('@@'):
                anon.append(i)
    aliases = {}
    for it in items:
        if it.kind == 'raw':
            m = re.match(r'^([A-Za-z_][\w.@?]*)\s*=\s*([A-Za-z_][\w.@?]*)\s*$', strip_comment(it.text).strip())
            if m:
                aliases[m.group(1)] = m.group(2)
    for a in list(aliases):
        t, n = aliases[a], 0
        while t in aliases and n < 10:
            t, n = aliases[t], n + 1
        if t in labels and a not in labels:
            labels[a] = labels[t]
    instr_idx = [i for i, it in enumerate(items) if it.kind == 'instr']
    nxt = {}
    for a, b in zip(instr_idx, instr_idx[1:]):
        nxt[a] = b
    pos_next_instr = {}
    # premier instr a partir de chaque position (pour resoudre une etiquette)
    following = [None] * (len(items) + 1)
    for i in range(len(items) - 1, -1, -1):
        following[i] = i if items[i].kind == 'instr' else following[i + 1]
    unknown = set()
    for i in instr_idx:
        it = items[i]
        it.reads, it.writes = flag_effects(it)
        m = it.mn
        succ = []
        if m in ('jmp',) or JCC_RE.match(m) or m in ('call', 'loop', 'loopd', 'loopnz', 'loopnzd', 'jecxz', 'jcxz'):
            t = branch_target(it)
            if t is not None and t.lower() in ('@f', '@b', '@r'):
                import bisect
                k = bisect.bisect_left(anon, i)
                if t.lower() == '@f' and k < len(anon):
                    t = '@anon%d' % anon[k]
                    labels[t] = anon[k]
                elif t.lower() in ('@b', '@r') and k > 0:
                    t = '@anon%d' % anon[k - 1]
                    labels[t] = anon[k - 1]
            if t is None:
                it.special = 'indirect'
            elif t in labels:
                ti = following[labels[t]]
                it.target = t
                if m == 'call':
                    it.special = ('call', ti)
                elif ti is not None:
                    succ.append(ti)
            else:
                unknown.add(t)
                it.special = 'indirect'
                it.target = t
        if m in ('ret', 'retn', 'ud2') or (m == 'jmp'):
            pass
        elif m == 'int':
            if i in nxt:
                succ.append(nxt[i])
        else:
            if i in nxt:
                succ.append(nxt[i])
        it.succ = succ
    return labels, following, unknown


def liveness(items):
    instr_idx = [i for i, it in enumerate(items) if it.kind == 'instr']
    changed = True
    rounds = 0
    while changed:
        changed = False
        rounds += 1
        for i in reversed(instr_idx):
            it = items[i]
            out = frozenset()
            for s in it.succ:
                out |= items[s].live_in
            if it.special == 'indirect':
                out |= ALLF
            elif isinstance(it.special, tuple):
                ti = it.special[1]
                if ti is not None:
                    out |= items[ti].live_in
            new_in = it.reads | (out - it.writes)
            if it.mn in ('jmp',) and it.special == 'indirect':
                new_in = ALLF
            if out != it.live_out or new_in != it.live_in:
                it.live_out, it.live_in = out, new_in
                changed = True
    return rounds


# ---------------------------------------------------------------------------
#  Emission AArch64
# ---------------------------------------------------------------------------
def simple_eval(expr):
    """Evalue une expression faite uniquement de litteraux et + - * ( )."""
    e = expr.strip()
    if not e:
        return None
    toks = re.findall(r"'.'|[0-9][0-9a-fA-FxXhHbBoOqQ]*|[-+*()]|\S+", e)
    py = []
    for t in toks:
        if t in '+-*()':
            py.append(t)
        else:
            v = lit_value(t)
            if v is None:
                return None
            py.append(str(v))
    try:
        return int(eval(''.join(py), {'__builtins__': {}}, {}))
    except Exception:
        return None


def log2(n):
    return {1: 0, 2: 1, 4: 2, 8: 3}[n]


SHIFT_FOR = {1: 24, 2: 16, 4: 0}
MASK_FOR = {1: 0xFF, 2: 0xFFFF, 4: 0xFFFFFFFF}


class Emitter:
    def __init__(self, items, sizes, labels, following):
        self.items, self.sizes, self.labels, self.following = items, sizes, labels, following
        self.out = []
        self.state = dict(CANON)
        self.state['P'] = ('garbage', None)
        self.df_back = False
        self.it = None
        self.errors = []
        self.handler_bases = {}

    # --- sortie ------------------------------------------------------------
    def e(self, s):
        self.out.append('\t' + s)

    def error(self, msg):
        self.errors.append('%s: %s  [%s]' % (self.it.src if self.it else '?', msg,
                                              self.it.text if self.it else ''))
        self.e('brk 1\t\t; ERREUR TRADUCTION : ' + msg)

    # --- constantes ----------------------------------------------------------
    def const(self, reg, expr, mask=0xFFFFFFFF):
        v = simple_eval(expr)
        if v is not None:
            v &= mask
            lo, hi = v & 0xFFFF, (v >> 16) & 0xFFFF
            if hi == 0:
                self.e('movz %s,%d' % (reg, lo))
            elif lo == 0:
                self.e('movz %s,%d,lsl 16' % (reg, hi))
            elif (~v) & 0xFFFFFFFF <= 0xFFFF:
                self.e('movn %s,%d' % (reg, (~v) & 0xFFFF))
            else:
                self.e('movz %s,%d' % (reg, lo))
                self.e('movk %s,%d,lsl 16' % (reg, hi))
            return
        E = '((%s) and 0%Xh)' % (expr, mask)
        self.e('movz %s,%s and 0FFFFh' % (reg, E))
        if mask > 0xFFFF:
            self.e('movk %s,(%s shr 16) and 0FFFFh,lsl 16' % (reg, E))

    # --- adresses -----------------------------------------------------------
    def addr_into(self, m, dst):
        """Calcule l'adresse effective (32 bits) de m dans le registre dst."""
        cur = None
        if m.base is not None and m.index is not None:
            self.e('add %s,w%d,w%d,lsl %d' % (dst, m.base, m.index, log2(m.scale)))
            cur = dst
        elif m.base is not None:
            cur = 'w%d' % m.base
        elif m.index is not None:
            if m.scale == 1:
                cur = 'w%d' % m.index
            else:
                self.e('lsl %s,w%d,%d' % (dst, m.index, log2(m.scale)))
                cur = dst
        if m.disp:
            v = simple_eval(m.disp)
            if cur is None:
                self.const(dst, m.disp)
                return dst
            if v is not None and -4095 <= v <= 4095:
                if v > 0:
                    self.e('add %s,%s,%d' % (dst, cur, v))
                elif v < 0:
                    self.e('sub %s,%s,%d' % (dst, cur, -v))
                elif cur != dst:
                    self.e('mov %s,%s' % (dst, cur))
            else:
                self.const('w17', m.disp)
                self.e('add %s,%s,w17' % (dst, cur))
            return dst
        if cur is None:
            self.e('movz %s,0' % dst)
            return dst
        if cur != dst:
            self.e('mov %s,%s' % (dst, cur))
        return dst

    def mem_ref(self, m, tmp='w16'):
        if not m.disp and m.index is None and m.base is not None:
            return '[x28,w%d,uxtw]' % m.base
        self.addr_into(m, tmp)
        return '[x28,%s,uxtw]' % tmp

    # --- lecture / ecriture d'operandes -------------------------------------
    def load(self, op, tmp, size=None, ref=None, signed=False):
        """-> registre contenant la valeur (etendue a 32 bits) de l'operande."""
        size = size or op.size
        if isinstance(op, Reg):
            w = op.w()
            if op.hi:
                self.e(('sbfx' if signed else 'ubfx') + ' %s,%s,8,8' % (tmp, w))
                return tmp
            if op.size == 4:
                return w
            if op.size == 2:
                self.e(('sxth' if signed else 'uxth') + ' %s,%s' % (tmp, w))
                return tmp
            self.e(('sxtb' if signed else 'uxtb') + ' %s,%s' % (tmp, w))
            return tmp
        if isinstance(op, Imm):
            if size in (1, 2, 4):
                self.const(tmp, op.expr, MASK_FOR[size])
            else:
                self.const(tmp, op.expr)
            if signed and size in (1, 2):
                self.e(('sxtb' if size == 1 else 'sxth') + ' %s,%s' % (tmp, tmp))
            return tmp
        if ref is None:
            ref = self.mem_ref(op)
        if size == 1:
            self.e(('ldrsb' if signed else 'ldrb') + ' %s,%s' % (tmp, ref))
        elif size == 2:
            self.e(('ldrsh' if signed else 'ldrh') + ' %s,%s' % (tmp, ref))
        elif size == 4:
            self.e('ldr %s,%s' % (tmp, ref))
        else:
            self.error('taille memoire %s' % size)
        return tmp

    def store(self, op, src, ref=None, size=None):
        size = size or op.size
        if isinstance(op, Reg):
            w = op.w()
            if op.hi:
                self.e('bfi %s,%s,8,8' % (w, src))
            elif op.size == 4:
                if src != w:
                    self.e('mov %s,%s' % (w, src))
            elif op.size == 2:
                self.e('bfi %s,%s,0,16' % (w, src))
            else:
                self.e('bfi %s,%s,0,8' % (w, src))
            return
        if ref is None:
            ref = self.mem_ref(op)
        if size == 1:
            self.e('strb %s,%s' % (src, ref))
        elif size == 2:
            self.e('strh %s,%s' % (src, ref))
        elif size == 4:
            self.e('str %s,%s' % (src, ref))
        else:
            self.error('taille memoire %s' % size)

    def op_size(self, a, b=None):
        s = a.size or (b.size if b is not None else 0)
        if not s:
            self.error('taille d\'operande inconnue')
            s = 4
        return s

    # --- drapeaux -------------------------------------------------------------
    def flag_reg(self, f, tmp):
        """Valeur 0/1 du drapeau x86 f dans un registre."""
        k, v = self.state[f]
        if k == 'hw':
            self.e('cset %s,%s' % (tmp, v))
            return tmp
        if k == 'const':
            self.e('movz %s,%d' % (tmp, v))
            return tmp
        if k == 'reg':
            return v
        if k == 'par':
            self.e('eor %s,%s,%s,lsr 4' % (tmp, v, v))
            self.e('eor %s,%s,%s,lsr 2' % (tmp, tmp, tmp))
            self.e('eor %s,%s,%s,lsr 1' % (tmp, tmp, tmp))
            self.e('mvn %s,%s' % (tmp, tmp))
            self.e('and %s,%s,1' % (tmp, tmp))
            return tmp
        self.error('drapeau %s indefini' % f)
        self.e('movz %s,0' % tmp)
        return tmp

    def canonical(self, need):
        """Rend canoniques (dans NZCV) les drapeaux de need."""
        need = set(need) & set('CZSO')
        bad = [f for f in need if self.state[f] != CANON[f]]
        if not bad:
            return
        # un drapeau 'garbage' est indefini cote x86 aussi : rien a preserver
        bad = [f for f in bad if self.state[f][0] != 'garbage']
        if not bad:
            return
        if bad == ['C'] and self.state['C'] == ('hw', 'cc'):
            self.e('mrs x24,nzcv')
            self.e('eor x24,x24,20000000h')
            self.e('msr nzcv,x24')
            self.state['C'] = CANON['C']
            return
        if len(bad) == 1 and self.state[bad[0]][0] in ('const', 'reg'):
            f = bad[0]
            k, v = self.state[f]
            self.e('mrs x24,nzcv')
            if k == 'const':
                self.e(('orr' if v else 'bic') + ' x24,x24,%Xh' % (1 << FLAGBIT[f]))
            else:
                self.e('bfi x24,%s,%d,1' % (v.replace('w', 'x'), FLAGBIT[f]))
            self.e('msr nzcv,x24')
            self.state[f] = CANON[f]
            return
        # cas general : on recalcule tous les drapeaux connus
        keep = [f for f in 'CZSO' if self.state[f][0] != 'garbage']
        regs = {}
        tmps = ['w9', 'w10', 'w11', 'w12']
        for f, t in zip(keep, tmps):
            regs[f] = self.flag_reg(f, t)
        self.e('movz w24,0')
        for f in keep:
            self.e('orr w24,w24,%s,lsl %d' % (regs[f], FLAGBIT[f]))
        self.e('msr nzcv,x24')
        for f in keep:
            self.state[f] = CANON[f]

    def protect(self, flags):
        """Avant d'ecraser NZCV : copie dans des registres les drapeaux de flags
        (vivants et non reecrits) qui dependent du materiel."""
        regfor = {'C': 'w27', 'O': 'w25', 'Z': 'w23', 'S': 'w22'}
        for f in flags:
            if f in regfor and self.state[f][0] == 'hw':
                r = regfor[f]
                self.e('cset %s,%s' % (r, self.state[f][1]))
                self.state[f] = ('reg', r)

    def cond_arm(self, xcond):
        """Condition AArch64 unique equivalente a la condition x86 dans l'etat
        courant, ou None."""
        fn, needed = X86COND[xcond]
        if 'P' in needed:
            return None
        for f in needed:
            if self.state[f][0] not in ('hw', 'const'):
                return None
        table = []
        for bits in range(16):
            n, z, c, v = bool(bits & 8), bool(bits & 4), bool(bits & 2), bool(bits & 1)
            hwv = {'N': n, 'Z': z, 'C': c, 'V': v}
            fl = {}
            for f in 'CZSO':
                k, val = self.state[f]
                if k == 'hw':
                    fl[f] = ARMCOND[val](n, z, c, v)
                elif k == 'const':
                    fl[f] = bool(val)
                else:
                    fl[f] = False
            table.append(bool(fn(fl)))
        if all(table):
            return 'al'
        if not any(table):
            return 'nv'
        for name, f in ARMCOND.items():
            if all(bool(f(bool(b & 8), bool(b & 4), bool(b & 2), bool(b & 1))) == table[b] for b in range(16)):
                return name
        return None

    def cond_value(self, xcond, tmp):
        """Valeur 0/1 de la condition x86 dans tmp (sans toucher NZCV)."""
        c = self.cond_arm(xcond)
        if c == 'al':
            self.e('movz %s,1' % tmp)
            return
        if c == 'nv':
            self.e('movz %s,0' % tmp)
            return
        if c:
            self.e('cset %s,%s' % (tmp, c))
            return
        fn, needed = X86COND[xcond]
        if needed == 'P':
            r = self.flag_reg('P', tmp)
            if xcond in ('np', 'po'):
                self.e('eor %s,%s,1' % (tmp, r))
            elif r != tmp:
                self.e('mov %s,%s' % (tmp, r))
            return
        if len(needed) == 1:
            r = self.flag_reg(needed, tmp)
            neg = not fn({needed: True, 'C': True, 'Z': True, 'S': True, 'O': True, 'P': True})
            if neg:
                self.e('eor %s,%s,1' % (tmp, r))
            elif r != tmp:
                self.e('mov %s,%s' % (tmp, r))
            return
        # composee : a = !C & !Z, be = C | Z, l/ge/g/le avec S et O
        rc = {}
        for f, t in zip(needed, ['w12', 'w13', 'w14']):
            rc[f] = self.flag_reg(f, t)
        if xcond in ('a', 'nbe'):
            self.e('orr %s,%s,%s' % (tmp, rc['C'], rc['Z']))
            self.e('eor %s,%s,1' % (tmp, tmp))
        elif xcond in ('be', 'na'):
            self.e('orr %s,%s,%s' % (tmp, rc['C'], rc['Z']))
        elif xcond in ('l', 'nge'):
            self.e('eor %s,%s,%s' % (tmp, rc['S'], rc['O']))
        elif xcond in ('ge', 'nl'):
            self.e('eor %s,%s,%s' % (tmp, rc['S'], rc['O']))
            self.e('eor %s,%s,1' % (tmp, tmp))
        elif xcond in ('le', 'ng'):
            self.e('eor %s,%s,%s' % (tmp, rc['S'], rc['O']))
            self.e('orr %s,%s,%s' % (tmp, tmp, rc['Z']))
        elif xcond in ('g', 'nle'):
            self.e('eor %s,%s,%s' % (tmp, rc['S'], rc['O']))
            self.e('orr %s,%s,%s' % (tmp, tmp, rc['Z']))
            self.e('eor %s,%s,1' % (tmp, tmp))
        else:
            self.error('condition %s' % xcond)

    def set_after(self, **kw):
        for f, v in kw.items():
            self.state[f] = v


# ---------------------------------------------------------------------------
#  Traduction des instructions
# ---------------------------------------------------------------------------
class Translator(Emitter):

    def operands(self, it, n=None):
        ops = split_operands(it.ops)
        res = [parse_operand(o, self.sizes, it.glob) for o in ops]
        # taille d'un operande memoire deduite d'un registre (comme fasm)
        m = it.mn
        cand = res[:1] if m in ('shl', 'sal', 'shr', 'sar', 'rol', 'ror', 'rcl', 'rcr') else res[:2]
        if m in ('shld', 'shrd'):
            cand = res[:2]
        if m in ('movzx', 'movsx'):
            cand = []
        rs = [o.size for o in cand if isinstance(o, Reg)]
        if rs:
            for o in cand:
                if isinstance(o, Mem) and not o.size:
                    o.size = rs[0]
        return res

    def target_name(self, it):
        return branch_target(it)

    def lbl(self, name):
        """Nom d'etiquette tel qu'ecrit dans la sortie (les locales gardent leur forme)."""
        return name

    # --- sauts -----------------------------------------------------------------
    def target_live(self, it):
        t = it.target
        if t is None or t not in self.labels:
            return ALLF
        ti = self.following[self.labels[t]]
        if ti is None:
            return frozenset()
        return self.items[ti].live_in

    def branch(self, xcond, it):
        """Saut conditionnel x86 (xcond) vers la cible de it."""
        raw = re.sub(r'^(near|short)\s+', '', it.ops.strip(), flags=re.I)
        need = self.target_live(it)
        bad = [f for f in need & set('CZSO') if self.state[f] != CANON[f]]
        if bad:
            # chemin pris : il faut normaliser -> on saute par-dessus si faux
            self.branch_to(self.negate(xcond), None, skip=True)
            save = dict(self.state)
            self.canonical(need)
            self.e('@B %s' % raw)
            self.state = save
            self.close_skip()
            return
        self.branch_to(xcond, raw)

    def negate(self, xcond):
        neg = {'o': 'no', 'no': 'o', 'b': 'ae', 'c': 'nc', 'nae': 'ae', 'ae': 'b', 'nb': 'b',
               'nc': 'c', 'e': 'ne', 'z': 'nz', 'ne': 'e', 'nz': 'z', 'be': 'a', 'na': 'a',
               'a': 'be', 'nbe': 'be', 's': 'ns', 'ns': 's', 'p': 'np', 'pe': 'np', 'np': 'p',
               'po': 'p', 'l': 'ge', 'nge': 'ge', 'ge': 'l', 'nl': 'l', 'le': 'g', 'ng': 'g',
               'g': 'le', 'nle': 'le'}
        return neg[xcond]

    def branch_to(self, xcond, target, skip=False):
        """Emet un saut si xcond ; target None + skip : saut vers la fin d'un bloc
        ouvert (resolu par close_skip)."""
        tgt = '@SKIP' if skip else target
        c = self.cond_arm(xcond)
        if c == 'al':
            self.e('@B %s' % tgt)
            return
        if c == 'nv':
            return
        if c:
            self.e('@BC %s %s' % (c, tgt))
            return
        fn, needed = X86COND[xcond]
        if needed == 'P' or len(needed) == 1:
            self.cond_value(xcond, 'w15')
            self.e('@CBNZ w15 %s' % tgt)
            return
        # composee : on rend canonique puis 1 ou 2 sauts
        self.canonical(needed)
        c = self.cond_arm(xcond)
        if c:
            self.e('@BC %s %s' % (c, tgt))
            return
        if xcond in ('a', 'nbe'):            # !C & !Z
            self.e('b.cs $+8')
            self.e('@BC ne %s' % tgt)
        elif xcond in ('be', 'na'):          # C | Z
            self.e('@BC cs %s' % tgt)
            self.e('@BC eq %s' % tgt)
        else:
            self.cond_value(xcond, 'w15')
            self.e('@CBNZ w15 %s' % tgt)

    def close_skip(self):
        """Resout les @SKIP emis depuis le dernier open : saut vers ici."""
        for i in range(len(self.out) - 1, -1, -1):
            if '@SKIP' in self.out[i]:
                n = len(self.out) - i
                self.out[i] = self.out[i].replace('@SKIP', '$+%d' % (4 * n)).replace('@BC ', 'b.@').replace('@CBNZ ', 'cbnz@').replace('@B ', 'b@')
                self.out[i] = fix_local_branch(self.out[i])
                if self.out[i].strip().startswith('cbnz@'):
                    pass
            if self.out[i].strip().startswith(('@BC', '@B ', '@CBNZ')) and '@SKIP' not in self.out[i]:
                pass

    # --- appel / retour --------------------------------------------------------
    def push_reg(self, r):
        self.e('sub w4,w4,4')
        self.e('str %s,[x28,w4,uxtw]' % r)

    def do_call(self, it):
        t = self.target_name(it)
        callee_live = self.target_live(it) if t else ALLF
        after = it.live_out
        self.canonical((callee_live | after) & set('CZSO'))
        if t is None:
            op = self.operands(it)[0]
            r = self.load(op, 'w9', size=4)
            if r != 'w9':
                self.e('mov w9,%s' % r)
            # retour = apres movz, movk, sub, str, add, br
            self.e('movz w16,(($+24) and 0FFFFh)')
            self.e('movk w16,((($+20) and 0FFFFFFFFh) shr 16),lsl 16')
            self.push_reg('w16')
            self.e('add x9,x28,w9,uxtw')
            self.e('br x9')
        else:
            raw = re.sub(r'^(near|short)\s+', '', it.ops.strip(), flags=re.I)
            self.e('movz w16,(($+20) and 0FFFFh)')
            self.e('movk w16,((($+16) and 0FFFFFFFFh) shr 16),lsl 16')
            self.push_reg('w16')
            self.e('@B %s' % raw)
        self.state = dict(CANON)
        self.state['P'] = ('garbage', None)

    def do_ret(self, it):
        self.canonical(ALLF)
        self.e('ldr w16,[x28,w4,uxtw]')
        n = simple_eval(it.ops) if it.ops.strip() else 0
        self.e('add w4,w4,%d' % (4 + (n or 0)))
        self.e('add x16,x28,w16,uxtw')
        self.e('br x16')

    def do_jmp(self, it):
        t = self.target_name(it)
        if t is None:
            self.canonical(ALLF)
            op = self.operands(it)[0]
            r = self.load(op, 'w9', size=4)
            self.e('add x9,x28,%s,uxtw' % r)
            self.e('br x9')
            return
        self.canonical(self.target_live(it))
        raw = re.sub(r'^(near|short)\s+', '', it.ops.strip(), flags=re.I)
        self.e('@B %s' % raw)

    # --- arithmetique / logique --------------------------------------------------
    def need(self, it):
        return it.live_out

    def arith(self, it, op):
        dst, src = self.operands(it)
        size = self.op_size(dst, src)
        live = self.need(it)
        wflags = op != 'mov'
        writes_result = op not in ('cmp', 'test')
        need = live & W_ALL
        if op in ('adc', 'sbb'):
            need = need  # la retenue d'entree est geree plus bas
        # x86 : "or eax,eax" / "test eax,eax" -> simple test
        ref = None
        if isinstance(dst, Mem):
            ref = self.mem_ref(dst, 'w16')
        a = self.load(dst, 'w9', size, ref)
        # operande source
        sv = simple_eval(src.expr) if isinstance(src, Imm) else None
        if isinstance(src, Imm) and sv is not None:
            sv &= MASK_FOR[size]
        # --- cas 32 bits --------------------------------------------------------
        if size == 4:
            res = dst.w() if isinstance(dst, Reg) else 'w9'
            if not writes_result:
                res = 'wzr'
            if op in ('adc', 'sbb'):
                self.carry_in(op)
            b = None
            imm = None
            if isinstance(src, Imm):
                if op in ('add', 'sub', 'cmp') and arith_imm_ok(sv):
                    imm = str(sv)
                elif op in ('and', 'test') and logic_imm_ok(sv):
                    imm = '%Xh' % sv if sv > 9 else str(sv)
                    if imm[0].isalpha():
                        imm = '0' + imm
                elif op in ('or', 'xor') and logic_imm_ok(sv):
                    imm = '0%Xh' % sv
            if imm is None:
                b = self.load(src, 'w10', size)
            opnd = imm if imm is not None else b
            if need:
                self.protect(set())
            if op == 'add':
                self.e(('adds' if need else 'add') + ' %s,%s,%s' % (res, a, opnd))
                if need:
                    self.set_after(C=('hw', 'cs'), Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
            elif op in ('sub', 'cmp'):
                if op == 'cmp':
                    self.e('cmp %s,%s' % (a, opnd))
                else:
                    self.e(('subs' if need else 'sub') + ' %s,%s,%s' % (res, a, opnd))
                if need or op == 'cmp':
                    self.set_after(C=('hw', 'cc'), Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
            elif op in ('and', 'test'):
                if op == 'test':
                    self.e('tst %s,%s' % (a, opnd))
                else:
                    self.e(('ands' if need else 'and') + ' %s,%s,%s' % (res, a, opnd))
                if need or op == 'test':
                    self.set_after(**CANON)
            elif op in ('or', 'xor'):
                ins = 'orr' if op == 'or' else 'eor'
                if a == opnd and op == 'or':
                    # or r,r : seulement les drapeaux
                    if need:
                        self.e('tst %s,%s' % (a, a))
                        self.set_after(**CANON)
                    if res != a and writes_result:
                        self.e('mov %s,%s' % (res, a))
                else:
                    if a == opnd and op == 'xor':
                        self.e('movz %s,0' % res)
                        if need:
                            self.e('tst wzr,wzr')
                            self.set_after(**CANON)
                    else:
                        self.e('%s %s,%s,%s' % (ins, res, a, opnd))
                        if need:
                            self.e('tst %s,%s' % (res, res))
                            self.set_after(**CANON)
            elif op == 'adc':
                if imm is not None:
                    b = self.load(src, 'w10', size)
                self.e(('adcs' if need else 'adc') + ' %s,%s,%s' % (res, a, b))
                if need:
                    self.set_after(C=('hw', 'cs'), Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
            elif op == 'sbb':
                if imm is not None:
                    b = self.load(src, 'w10', size)
                self.e(('sbcs' if need else 'sbc') + ' %s,%s,%s' % (res, a, b))
                if need:
                    self.set_after(C=('hw', 'cc'), Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
            if 'P' in need:
                self.e('and w26,%s,0FFh' % (res if res != 'wzr' else a))
                self.state['P'] = ('par', 'w26')
            if writes_result and isinstance(dst, Mem):
                self.store(dst, res, ref)
            return
        # --- 8 / 16 bits ----------------------------------------------------------
        sh = SHIFT_FOR[size]
        if not need and op not in ('adc', 'sbb'):
            if not writes_result:
                return
            b = self.load(src, 'w10', size)
            ins = {'add': 'add', 'sub': 'sub', 'and': 'and', 'or': 'orr', 'xor': 'eor'}[op]
            self.e('%s w11,%s,%s' % (ins, a, b))
            self.store(dst, 'w11', ref)
            return
        if op in ('adc', 'sbb'):
            self.carry_in(op)
        self.e('lsl w11,%s,%d' % (a, sh))
        if isinstance(src, Imm) and sv is not None:
            self.const('w12', str((sv << sh) & 0xFFFFFFFF))
        else:
            b = self.load(src, 'w10', size)
            self.e('lsl w12,%s,%d' % (b, sh))
        topmask = '0%Xh' % (MASK_FOR[size] << sh)
        if op == 'add':
            self.e('adds w11,w11,w12')
            self.set_after(C=('hw', 'cs'), Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
        elif op in ('sub', 'cmp'):
            self.e('subs w11,w11,w12')
            self.set_after(C=('hw', 'cc'), Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
        elif op in ('and', 'test'):
            self.e('ands w11,w11,w12')
            self.set_after(**CANON)
        elif op in ('or', 'xor'):
            self.e(('orr' if op == 'or' else 'eor') + ' w11,w11,w12')
            self.e('tst w11,w11')
            self.set_after(**CANON)
        elif op == 'adc':
            self.e('orr w11,w11,0%Xh' % ((1 << sh) - 1))
            self.e('adcs w11,w11,w12')
            self.set_after(C=('hw', 'cs'), Z=('garbage', None), S=CANON['S'], O=CANON['O'])
            if 'Z' in need:
                self.fix_z(topmask)
        elif op == 'sbb':
            self.e('sbcs w11,w11,w12')
            self.set_after(C=('hw', 'cc'), Z=('garbage', None), S=CANON['S'], O=CANON['O'])
            if 'Z' in need:
                self.fix_z(topmask)
        if 'P' in need:
            self.e('lsr w26,w11,%d' % sh)
            self.e('and w26,w26,0FFh')
            self.state['P'] = ('par', 'w26')
        if writes_result:
            self.e('lsr w11,w11,%d' % sh)
            self.store(dst, 'w11', ref)

    def fix_z(self, topmask):
        """ZF exact apres adc/sbb sur 8/16 bits : C et O passent en registres."""
        self.protect(['C', 'O'])
        self.e('tst w11,%s' % topmask)
        self.state['Z'] = CANON['Z']
        self.state['S'] = CANON['S']

    def carry_in(self, op):
        """Prepare C materiel pour adcs (C = CF) ou sbcs (C = !CF)."""
        k, v = self.state['C']
        want = 'cs' if op == 'adc' else 'cc'
        if k == 'hw' and v == want:
            return
        if k == 'hw' and v == INVCOND[want]:
            self.e('mrs x24,nzcv')
            self.e('eor x24,x24,20000000h')
            self.e('msr nzcv,x24')
            self.state['C'] = ('hw', want)
            return
        r = self.flag_reg('C', 'w24')
        # C materiel = CF (adc) ou !CF (sbb) ; les autres drapeaux sont reecrits ensuite
        if op == 'adc':
            self.e('cmp %s,1' % r)        # CF=1 -> pas d'emprunt -> C=1
        else:
            self.e('cmp wzr,%s' % r)      # CF=1 -> emprunt -> C=0
        self.state['C'] = ('hw', want)


class Translator2(Translator):

    def close_skip(self, start):
        for i in range(start, len(self.out)):
            if '@SKIP' in self.out[i]:
                n = len(self.out) - i
                self.out[i] = self.out[i].replace('@SKIP', '$+%d' % (4 * n))

    def branch(self, xcond, it):
        raw = re.sub(r'^(near|short)\s+', '', it.ops.strip(), flags=re.I)
        need = self.target_live(it)
        bad = [f for f in need & set('CZSO') if self.state[f] != CANON[f]]
        if bad:
            start = len(self.out)
            save = dict(self.state)
            self.branch_to(self.negate(xcond), '@SKIP')
            self.canonical(need)
            self.e('@B %s' % raw)
            self.state = save
            self.close_skip(start)
            return
        self.branch_to(xcond, raw)

    def branch_to(self, xcond, tgt, skip=False):
        c = self.cond_arm(xcond)
        if c == 'al':
            self.e('@B %s' % tgt)
            return
        if c == 'nv':
            return
        if c:
            self.e('@BC %s %s' % (c, tgt))
            return
        fn, needed = X86COND[xcond]
        if needed == 'P' or len(needed) == 1:
            self.cond_value(xcond, 'w15')
            self.e('@CBNZ w15 %s' % tgt)
            return
        self.cond_value(xcond, 'w15')
        self.e('@CBNZ w15 %s' % tgt)

    # --- transferts -------------------------------------------------------------
    def i_mov(self, it):
        dst, src = self.operands(it)
        size = self.op_size(dst, src)
        if isinstance(dst, Reg) and dst.size == 4 and not dst.hi:
            if isinstance(src, Reg) and src.size == 4:
                if src.idx != dst.idx:
                    self.e('mov %s,%s' % (dst.w(), src.w()))
                return
            if isinstance(src, Imm):
                self.const(dst.w(), src.expr)
                return
            self.load(src, dst.w(), 4)
            return
        if isinstance(dst, Mem):
            v = self.load(src, 'w9', size)
            self.store(dst, v, None, size)
            return
        v = self.load(src, 'w9', size)
        self.store(dst, v)

    def i_movzx(self, it, signed=False):
        dst, src = self.operands(it)
        if not src.size:
            self.error('taille source movzx')
        v = self.load(src, 'w9', src.size, signed=signed)
        self.store(dst, v)

    def i_lea(self, it):
        dst, src = self.operands(it)
        if dst.size == 4:
            self.addr_into(src, dst.w())
        else:
            self.addr_into(src, 'w9')
            self.store(dst, 'w9')

    def i_xchg(self, it):
        a, b = self.operands(it)
        size = self.op_size(a, b)
        if isinstance(a, Mem):
            a, b = b, a
        if isinstance(b, Mem):
            ref = self.mem_ref(b, 'w16')
            vb = self.load(b, 'w10', size, ref)
            va = self.load(a, 'w9', size)
            self.store(b, va, ref, size)
            self.store(a, vb)
            return
        va = self.load(a, 'w9', size)
        if va == a.w() and size == 4:
            self.e('mov w9,%s' % va)
            va = 'w9'
        vb = self.load(b, 'w10', size)
        self.store(a, vb)
        self.store(b, va)

    def i_incdec(self, it, inc):
        dst, = self.operands(it)
        size = self.op_size(dst)
        need = it.live_out & set('ZSOP')
        if 'C' in it.live_out and need:
            self.protect(['C'])
        ref = self.mem_ref(dst, 'w16') if isinstance(dst, Mem) else None
        a = self.load(dst, 'w9', size, ref)
        if not need:
            self.e(('add' if inc else 'sub') + ' w11,%s,1' % a)
            self.store(dst, 'w11', ref)
            return
        if size == 4:
            self.e(('adds' if inc else 'subs') + ' w11,%s,1' % a)
            sh = 0
        else:
            sh = SHIFT_FOR[size]
            self.e('lsl w11,%s,%d' % (a, sh))
            self.e('movz w12,%d,lsl 16' % ((1 << sh) >> 16))
            self.e(('adds' if inc else 'subs') + ' w11,w11,w12')
        keepc = self.state['C']
        self.set_after(Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
        self.state['C'] = keepc if keepc[0] != 'hw' else ('garbage', None)
        if 'P' in need:
            self.e('lsr w26,w11,%d' % sh)
            self.e('and w26,w26,0FFh')
            self.state['P'] = ('par', 'w26')
        if sh:
            self.e('lsr w11,w11,%d' % sh)
        self.store(dst, 'w11', ref)

    def i_neg(self, it):
        dst, = self.operands(it)
        size = self.op_size(dst)
        need = it.live_out & W_ALL
        ref = self.mem_ref(dst, 'w16') if isinstance(dst, Mem) else None
        a = self.load(dst, 'w9', size, ref)
        sh = SHIFT_FOR[size]
        if sh:
            self.e('lsl w11,%s,%d' % (a, sh))
            self.e(('negs' if need else 'neg') + ' w11,w11')
            if 'P' in need:
                self.e('lsr w26,w11,%d' % sh)
                self.state['P'] = ('par', 'w26')
            self.e('lsr w11,w11,%d' % sh)
        else:
            self.e(('negs' if need else 'neg') + ' w11,%s' % a)
            if 'P' in need:
                self.e('and w26,w11,0FFh')
                self.state['P'] = ('par', 'w26')
        if need:
            self.set_after(C=('hw', 'cc'), Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
        self.store(dst, 'w11', ref)

    def i_not(self, it):
        dst, = self.operands(it)
        size = self.op_size(dst)
        ref = self.mem_ref(dst, 'w16') if isinstance(dst, Mem) else None
        a = self.load(dst, 'w9', size, ref)
        self.e('mvn w11,%s' % a)
        self.store(dst, 'w11', ref)

    # --- decalages ----------------------------------------------------------------
    def i_shift(self, it, kind):
        ops = self.operands(it)
        dst, cnt = ops[0], ops[1]
        size = self.op_size(dst)
        bits = size * 8
        need = it.live_out & W_ALL
        ref = self.mem_ref(dst, 'w16') if isinstance(dst, Mem) else None
        signed = kind == 'sar'
        a = self.load(dst, 'w9', size, ref, signed=signed)
        if isinstance(cnt, Reg):
            c = self.load(cnt, 'w10', 1)
            self.e('and w10,%s,31' % c)
            ins = {'shl': 'lslv', 'shr': 'lsrv', 'sar': 'asrv'}[kind]
            if 'C' in need:
                # CF = dernier bit sorti (compte non nul suppose)
                if kind == 'shl':
                    self.e('movz w13,%d' % bits)
                    self.e('sub w13,w13,w10')
                else:
                    self.e('sub w13,w10,1')
                self.e('lsrv w27,%s,w13' % a)
                self.e('and w27,w27,1')
            self.e('%s w11,%s,w10' % (ins, a))
            if size < 4:
                self.e('and w11,w11,0%Xh' % MASK_FOR[size])
            if need & set('ZSP'):
                sh = SHIFT_FOR[size]
                self.e('lsl w12,w11,%d' % sh if sh else 'mov w12,w11')
                self.e('tst w12,w12')
                self.set_after(Z=CANON['Z'], S=CANON['S'])
                if 'P' in need:
                    self.e('and w26,w11,0FFh')
                    self.state['P'] = ('par', 'w26')
            if 'C' in need:
                self.state['C'] = ('reg', 'w27')
            if 'O' in need:
                self.state['O'] = ('garbage', None)
            self.store(dst, 'w11', ref)
            return
        n = simple_eval(cnt.expr)
        if n is None:
            # compte symbolique (macro irp...) : pas de drapeaux
            ins = {'shl': 'lsl', 'shr': 'lsr', 'sar': 'asr'}[kind]
            self.e('%s w11,%s,(%s) and 31' % (ins, a, cnt.expr))
            if need:
                self.error('drapeaux apres decalage symbolique')
            self.store(dst, 'w11', ref)
            return
        n &= 31
        if n == 0:
            return
        if 'C' in need:
            if kind == 'shl':
                if bits - n >= 0:
                    self.e('ubfx w27,%s,%d,1' % (a, bits - n))
                else:
                    self.e('movz w27,0')
            else:
                if n - 1 < bits:
                    self.e('ubfx w27,%s,%d,1' % (a, n - 1))
                else:
                    self.e('lsr w27,%s,31' % a if signed else 'movz w27,0')
        if n >= 32:
            self.e('movz w11,0')
        else:
            ins = {'shl': 'lsl', 'shr': 'lsr', 'sar': 'asr'}[kind]
            self.e('%s w11,%s,%d' % (ins, a, n))
        if size < 4:
            self.e('and w11,w11,0%Xh' % MASK_FOR[size])
        if need & set('ZSOP'):
            sh = SHIFT_FOR[size]
            if sh:
                self.e('lsl w12,w11,%d' % sh)
                self.e('tst w12,w12')
            else:
                self.e('tst w11,w11')
            self.set_after(Z=CANON['Z'], S=CANON['S'])
            if 'P' in need:
                self.e('and w26,w11,0FFh')
                self.state['P'] = ('par', 'w26')
        if 'C' in need:
            self.state['C'] = ('reg', 'w27')
        else:
            self.state['C'] = ('garbage', None) if need & set('ZS') else self.state['C']
        if 'O' in need:
            if n == 1 and kind == 'shl':
                self.e('lsr w25,w11,%d' % (bits - 1))
                self.e('eor w25,w25,w27')
                self.state['O'] = ('reg', 'w25')
            elif n == 1 and kind == 'shr':
                self.e('lsr w25,%s,%d' % (a, bits - 1))
                self.e('and w25,w25,1')
                self.state['O'] = ('reg', 'w25')
            else:
                self.state['O'] = ('const', 0)
        elif need:
            self.state['O'] = ('garbage', None)
        self.store(dst, 'w11', ref)

    def i_rot(self, it, left):
        dst, cnt = self.operands(it)
        size = self.op_size(dst)
        bits = size * 8
        ref = self.mem_ref(dst, 'w16') if isinstance(dst, Mem) else None
        a = self.load(dst, 'w9', size, ref)
        need = it.live_out & set('CO')
        if isinstance(cnt, Reg):
            c = self.load(cnt, 'w10', 1)
            if size != 4:
                self.error('rotation 8/16 bits par cl')
            if left:
                self.e('neg w10,%s' % c)
                self.e('rorv w11,%s,w10' % a)
            else:
                self.e('rorv w11,%s,%s' % (a, c))
            if need:
                self.error('drapeaux apres rotation par cl')
            self.store(dst, 'w11', ref)
            return
        n = simple_eval(cnt.expr)
        if n is None:
            self.error('rotation symbolique')
            return
        n %= bits
        r = n if not left else (bits - n) % bits
        if size == 4:
            self.e('ror w11,%s,%d' % (a, r))
        else:
            # rotation a droite de r sur 'bits' bits
            self.e('lsr w11,%s,%d' % (a, r))
            self.e('lsl w12,%s,%d' % (a, bits - r))
            self.e('orr w11,w11,w12')
            self.e('and w11,w11,0%Xh' % MASK_FOR[size])
        if 'C' in need:
            if left:
                self.e('and w27,w11,1')
            else:
                self.e('lsr w27,w11,%d' % (bits - 1))
            self.state['C'] = ('reg', 'w27')
        if 'O' in need:
            self.state['O'] = ('garbage', None)
        self.store(dst, 'w11', ref)

    def i_rc(self, it, left):
        dst, cnt = self.operands(it)
        size = self.op_size(dst)
        bits = size * 8
        n = simple_eval(cnt.expr) if isinstance(cnt, Imm) else None
        if n != 1:
            self.error('rcl/rcr compte != 1')
            return
        ref = self.mem_ref(dst, 'w16') if isinstance(dst, Mem) else None
        a = self.load(dst, 'w9', size, ref)
        cf = self.flag_reg('C', 'w10')
        if cf != 'w10':
            self.e('mov w10,%s' % cf)
        if left:
            self.e('lsr w27,%s,%d' % (a, bits - 1))
            self.e('lsl w11,%s,1' % a)
            self.e('orr w11,w11,w10')
            if size < 4:
                self.e('and w11,w11,0%Xh' % MASK_FOR[size])
        else:
            self.e('and w27,%s,1' % a)
            self.e('lsr w11,%s,1' % a)
            self.e('orr w11,w11,w10,lsl %d' % (bits - 1))
        self.state['C'] = ('reg', 'w27')
        self.state['O'] = ('garbage', None)
        self.store(dst, 'w11', ref)

    def i_shxd(self, it, left):
        dst, src, cnt = self.operands(it)
        need = it.live_out & W_ALL
        ref = self.mem_ref(dst, 'w16') if isinstance(dst, Mem) else None
        a = self.load(dst, 'w9', 4, ref)
        b = self.load(src, 'w10', 4)
        if isinstance(cnt, Reg):
            c = self.load(cnt, 'w12', 1)
            self.e('ands w12,%s,31' % c if False else 'and w12,%s,31' % c)
            # compte nul : destination inchangee
            if left:
                self.e('lslv w11,%s,w12' % a)
                self.e('neg w13,w12')
                self.e('lsrv w13,%s,w13' % b)
            else:
                self.e('lsrv w11,%s,w12' % a)
                self.e('neg w13,w12')
                self.e('lslv w13,%s,w13' % b)
            self.e('orr w11,w11,w13')
            self.e('cbnz w12,$+8')
            self.e('mov w11,%s' % a)
            if 'C' in need:
                # CF = dernier bit sorti de la destination (compte non nul suppose)
                if left:
                    self.e('movz w13,32')
                    self.e('sub w13,w13,w12')
                else:
                    self.e('sub w13,w12,1')
                self.e('lsrv w27,%s,w13' % a)
                self.e('and w27,w27,1')
                self.state['C'] = ('reg', 'w27')
            if need & set('ZSP'):
                self.e('tst w11,w11')
                self.set_after(Z=CANON['Z'], S=CANON['S'])
            if 'O' in need:
                self.state['O'] = ('garbage', None)
            self.store(dst, 'w11', ref)
            return
        n = simple_eval(cnt.expr)
        if n is None:
            self.error('shld/shrd symbolique')
            return
        n &= 31
        if n == 0:
            return
        if left:
            if 'C' in need:
                self.e('ubfx w27,%s,%d,1' % (a, 32 - n))
            self.e('extr w11,%s,%s,%d' % (a, b, 32 - n))
        else:
            if 'C' in need:
                self.e('ubfx w27,%s,%d,1' % (a, n - 1))
            self.e('extr w11,%s,%s,%d' % (b, a, n))
        if need & set('ZSOP'):
            self.e('tst w11,w11')
            self.set_after(Z=CANON['Z'], S=CANON['S'])
            if 'P' in need:
                self.e('and w26,w11,0FFh')
                self.state['P'] = ('par', 'w26')
        if 'C' in need:
            self.state['C'] = ('reg', 'w27')
        elif need:
            self.state['C'] = ('garbage', None)
        if need:
            self.state['O'] = ('garbage', None)
        self.store(dst, 'w11', ref)

    # --- bits ------------------------------------------------------------------------
    def i_bt(self, it, kind):
        dst, bit = self.operands(it)
        size = dst.size or 4
        need = it.live_out & set('C')
        if isinstance(dst, Mem) and isinstance(bit, Reg):
            # chaine de bits : mot de 32 bits a ea + 4*(bit >> 5) (signe)
            b = self.load(bit, 'w12', 4)
            self.addr_into(dst, 'w16')
            self.e('asr w13,%s,5' % b)
            self.e('add w16,w16,w13,lsl 2')
            ref = '[x28,w16,uxtw]'
            self.e('ldr w9,%s' % ref)
            self.e('and w12,%s,31' % b)
            size = 4
            a, bb = 'w9', 'w12'
        else:
            ref = self.mem_ref(dst, 'w16') if isinstance(dst, Mem) else None
            a = self.load(dst, 'w9', size, ref)
            if isinstance(bit, Reg):
                b = self.load(bit, 'w12', 4)
                self.e('and w12,%s,%d' % (b, size * 8 - 1))
                bb = 'w12'
            else:
                n = simple_eval(bit.expr)
                if n is None:
                    self.error('bt symbolique')
                    return
                bb = n & (size * 8 - 1)
        if isinstance(bb, int):
            if 'C' in need or kind == 'bt':
                self.e('ubfx w27,%s,%d,1' % (a, bb))
            if kind == 'bts':
                self.e('orr w11,%s,0%Xh' % (a, 1 << bb))
            elif kind == 'btr':
                self.e('and w11,%s,0%Xh' % (a, (~(1 << bb)) & 0xFFFFFFFF))
        else:
            self.e('lsrv w27,%s,%s' % (a, bb))
            self.e('and w27,w27,1')
            if kind in ('bts', 'btr'):
                self.e('movz w13,1')
                self.e('lslv w13,w13,%s' % bb)
                self.e(('orr' if kind == 'bts' else 'bic') + ' w11,%s,w13' % a)
        self.state['C'] = ('reg', 'w27')
        if kind != 'bt':
            if isinstance(dst, Mem):
                self.store(dst, 'w11', ref, size)
            else:
                self.store(dst, 'w11')

    def i_bsx(self, it, rev):
        dst, src = self.operands(it)
        v = self.load(src, 'w9', 4)
        if rev:
            self.e('clz w10,%s' % v)
            self.e('movz w11,31')
            self.e('sub w10,w11,w10')
        else:
            self.e('rbit w10,%s' % v)
            self.e('clz w10,w10')
        self.e('tst %s,%s' % (v, v))
        self.e('csel %s,%s,w10,eq' % (dst.w(), dst.w()))
        self.set_after(**CANON)

    # --- multiplication / division -----------------------------------------------------
    def i_mul(self, it):
        src, = self.operands(it)
        size = self.op_size(src)
        need = it.live_out & set('CO')
        v = self.load(src, 'w9', size)
        if size == 4:
            self.e('umull x16,w0,%s' % v)
            self.e('lsr x2,x16,32')
            self.e('mov w0,w16')
            if need:
                self.e('cmp w2,0')
        elif size == 1:
            self.e('uxtb w10,w0')
            self.e('mul w11,w10,%s' % v)
            self.e('bfi w0,w11,0,16')
            if need:
                self.e('cmp wzr,w11,lsr 8')
        else:
            self.error('mul 16 bits')
        self.set_after(C=('hw', 'ne'), O=('hw', 'ne'), Z=('garbage', None), S=('garbage', None))

    def i_imul(self, it):
        ops = self.operands(it)
        need = it.live_out & set('CO')
        if len(ops) == 1:
            src = ops[0]
            size = self.op_size(src)
            v = self.load(src, 'w9', size, signed=True)
            if size == 1:
                self.e('sxtb w10,w0')
                self.e('mul w11,w10,%s' % v)
                self.e('bfi w0,w11,0,16')
                if need:
                    self.e('sxtb w12,w11')
                    self.e('cmp w12,w11')
            elif size == 4:
                self.e('smull x16,w0,%s' % v)
                self.e('lsr x2,x16,32')
                self.e('mov w0,w16')
                if need:
                    self.e('cmp x16,w16,sxtw')
            else:
                self.error('imul 16 bits')
            self.set_after(C=('hw', 'ne'), O=('hw', 'ne'), Z=('garbage', None), S=('garbage', None))
            return
        if len(ops) == 2:
            dst, src = ops
            a = self.load(dst, 'w9', 4)
            b = self.load(src, 'w10', 4)
        else:
            dst, a_op, b_op = ops
            a = self.load(a_op, 'w9', 4)
            b = self.load(b_op, 'w10', 4)
        self.e('smull x16,%s,%s' % (a, b))
        if need:
            self.e('cmp x16,w16,sxtw')
        self.store(dst, 'w16')
        self.set_after(C=('hw', 'ne'), O=('hw', 'ne'), Z=('garbage', None), S=('garbage', None))

    def i_div(self, it, signed):
        src, = self.operands(it)
        size = self.op_size(src)
        v = self.load(src, 'w9', size, signed=signed)
        if size == 4:
            if signed:
                self.e('sxtw x9,%s' % v)
                self.e('lsl x16,x2,32')
                self.e('orr x16,x16,x0')
                self.e('sdiv x17,x16,x9')
            else:
                self.e('mov w9,%s' % v) if v != 'w9' else None
                self.e('lsl x16,x2,32')
                self.e('orr x16,x16,x0')
                self.e('udiv x17,x16,x9')
            self.e('msub x16,x17,x9,x16')
            self.e('mov w0,w17')
            self.e('mov w2,w16')
        elif size == 1:
            self.e(('sxth' if signed else 'uxth') + ' w16,w0')
            self.e(('sdiv' if signed else 'udiv') + ' w17,w16,%s' % v)
            self.e('msub w16,w17,%s,w16' % v)
            self.e('bfi w0,w17,0,8')
            self.e('bfi w0,w16,8,8')
        else:
            self.error('div 16 bits')
        for f in 'CZSO':
            self.state[f] = ('garbage', None)

    # --- pile -------------------------------------------------------------------------
    def i_push(self, it):
        for o in split_push_operands(it.ops):
            op = parse_operand(o, self.sizes, it.glob)
            if isinstance(op, Reg) and op.size == 4:
                if op.idx == 4:
                    self.e('mov w9,w4')
                    self.push_reg('w9')
                else:
                    self.push_reg(op.w())
            else:
                v = self.load(op, 'w9', 4)
                self.push_reg(v)

    def i_pop(self, it):
        for o in split_push_operands(it.ops):
            op = parse_operand(o, self.sizes, it.glob)
            if isinstance(op, Reg) and op.size == 4:
                self.e('ldr %s,[x28,w4,uxtw]' % op.w())
                if op.idx != 4:
                    self.e('add w4,w4,4')
            else:
                self.e('ldr w9,[x28,w4,uxtw]')
                self.e('add w4,w4,4')
                self.store(op, 'w9', None, 4)

    def i_pushf(self, it):
        self.canonical(ALLF)
        self.e('mrs x9,nzcv')
        self.e('lsr x9,x9,28')
        self.push_reg('w9')

    def i_popf(self, it):
        self.e('ldr w9,[x28,w4,uxtw]')
        self.e('add w4,w4,4')
        self.e('lsl x9,x9,28')
        self.e('msr nzcv,x9')
        self.state = dict(CANON)
        self.state['P'] = ('garbage', None)

    # --- conditions ------------------------------------------------------------------
    def i_setcc(self, it, xcond):
        dst, = self.operands(it)
        self.cond_value(xcond, 'w9')
        self.store(dst, 'w9', None, 1)

    def i_cmov(self, it, xcond):
        dst, src = self.operands(it)
        size = self.op_size(dst, src)
        v = self.load(src, 'w10', size)
        c = self.cond_arm(xcond)
        if c and c not in ('al', 'nv') and size == 4:
            self.e('csel %s,%s,%s,%s' % (dst.w(), v, dst.w(), c))
            return
        self.cond_value(xcond, 'w15')
        if size == 4:
            self.e('cbz w15,$+8')
            self.e('mov %s,%s' % (dst.w(), v))
        else:
            self.e('cbz w15,$+8')
            self.store(dst, v)

    # --- chaines -----------------------------------------------------------------------
    def str_size(self, it, mn):
        last = mn[-1]
        if mn in ('lodsb', 'stosb', 'movsb', 'scasb', 'cmpsb'):
            return 1
        if mn in ('lodsw', 'stosw', 'movsw', 'scasw', 'cmpsw'):
            return 2
        if mn in ('lodsd', 'stosd', 'movsd', 'scasd', 'cmpsd'):
            return 4
        ops = split_operands(it.ops) if it.mn == mn else split_operands(it.ops.split(None, 1)[1] if len(it.ops.split(None, 1)) > 1 else '')
        for o in ops:
            m = re.match(r'^\s*(byte|word|dword)\b', o, re.I)
            if m:
                return SIZEKW[m.group(1).lower()]
        self.error('taille de chaine inconnue')
        return 1

    def ptr_step(self, reg, size, out):
        out.append(('sub' if self.df_back else 'add') + ' %s,%s,%d' % (reg, reg, size))

    def string_body(self, kind, size):
        """Corps d'une operation de chaine (liste d'instructions, sans drapeaux
        sauf scas/cmps qui finissent par une comparaison)."""
        L = {1: 'ldrb', 2: 'ldrh', 4: 'ldr'}[size]
        S = {1: 'strb', 2: 'strh', 4: 'str'}[size]
        body = []
        if kind == 'lods':
            if size == 4:
                body.append('ldr w0,[x28,w6,uxtw]')
            else:
                body.append('%s w9,[x28,w6,uxtw]' % L)
                body.append('bfi w0,w9,0,%d' % (size * 8))
            self.ptr_step('w6', size, body)
        elif kind == 'stos':
            body.append('%s w0,[x28,w7,uxtw]' % S)
            self.ptr_step('w7', size, body)
        elif kind == 'movs':
            body.append('%s w9,[x28,w6,uxtw]' % L)
            body.append('%s w9,[x28,w7,uxtw]' % S)
            self.ptr_step('w6', size, body)
            self.ptr_step('w7', size, body)
        elif kind == 'scas':
            body.append('%s w10,[x28,w7,uxtw]' % L)
            self.ptr_step('w7', size, body)
            sh = SHIFT_FOR[size]
            if sh:
                body.append('lsl w11,w0,%d' % sh)
                body.append('lsl w12,w10,%d' % sh)
                body.append('cmp w11,w12')
            else:
                body.append('cmp w0,w10')
        elif kind == 'cmps':
            body.append('%s w9,[x28,w6,uxtw]' % L)
            body.append('%s w10,[x28,w7,uxtw]' % L)
            self.ptr_step('w6', size, body)
            self.ptr_step('w7', size, body)
            sh = SHIFT_FOR[size]
            if sh:
                body.append('lsl w11,w9,%d' % sh)
                body.append('lsl w12,w10,%d' % sh)
                body.append('cmp w11,w12')
            else:
                body.append('cmp w9,w10')
        return body

    def i_string(self, it, mn, rep=None):
        kind = mn[:4]
        size = self.str_size(it, mn)
        body = self.string_body(kind, size)
        if kind in ('scas', 'cmps') and it.live_out & set('CZSO'):
            pass
        if rep is None:
            for b in body:
                self.e(b)
        elif kind in ('movs', 'stos', 'lods'):
            k = len(body)
            self.e('cbz w1,$+%d' % (4 * (k + 3)))
            for b in body:
                self.e(b)
            self.e('sub w1,w1,1')
            self.e('cbnz w1,$-%d' % (4 * (k + 1)))
        else:
            k = len(body)
            exitc = 'ne' if rep in ('repe', 'repz') else 'eq'
            # cbz ; corps ; sub ; b.exit ; cbnz
            self.e('cbz w1,$+%d' % (4 * (k + 4)))
            for b in body:
                self.e(b)
            self.e('sub w1,w1,1')
            self.e('b.%s $+8' % exitc)
            self.e('cbnz w1,$-%d' % (4 * (k + 2)))
        if kind in ('scas', 'cmps'):
            self.set_after(C=('hw', 'cc'), Z=CANON['Z'], S=CANON['S'], O=CANON['O'])
            self.state['P'] = ('garbage', None)

    # --- divers ------------------------------------------------------------------------
    def i_loop(self, it, kind):
        raw = re.sub(r'^(near|short)\s+', '', it.ops.strip(), flags=re.I)
        need = self.target_live(it)
        if kind == 'jecxz':
            self.canonical(need)
            self.e('@CBZ w1 %s' % raw)
            return
        self.e('sub w1,w1,1')
        if kind == 'loop':
            self.canonical(need)
            self.e('@CBNZ w1 %s' % raw)
            return
        # loopnz : ecx != 0 et ZF = 0
        self.canonical(need | frozenset('Z'))
        self.e('cbz w1,$+8')
        self.e('@BC ne %s' % raw)

    def i_xlat(self, it):
        self.e('uxtb w16,w0')
        self.e('add w16,w3,w16')
        self.e('ldrb w9,[x28,w16,uxtw]')
        self.e('bfi w0,w9,0,8')

    def i_int(self, it):
        self.e('bl linux_int80')

    # --- aiguillage --------------------------------------------------------------------
    def translate(self, it):
        m = it.mn
        self.it = it
        if m in ('rep', 'repe', 'repz', 'repne', 'repnz'):
            parts = it.ops.split(None, 1)
            sub = parts[0].lower()
            it2 = Item('instr', it.text, it.src)
            it2.mn, it2.ops, it2.glob, it2.live_out = sub, parts[1] if len(parts) > 1 else '', it.glob, it.live_out
            self.i_string(it2, sub, m)
            return
        jm = JCC_RE.match(m)
        if jm:
            self.branch(jm.group(1), it)
            return
        sm = SETCC_RE.match(m)
        if sm:
            self.i_setcc(it, sm.group(1))
            return
        cm = CMOV_RE.match(m)
        if cm:
            self.i_cmov(it, cm.group(1))
            return
        if m == 'mov':
            self.i_mov(it)
        elif m in ('movzx', 'movsx'):
            self.i_movzx(it, m == 'movsx')
        elif m == 'lea':
            self.i_lea(it)
        elif m in ('add', 'sub', 'and', 'or', 'xor', 'cmp', 'test', 'adc', 'sbb'):
            self.arith(it, m)
        elif m in ('inc', 'dec'):
            self.i_incdec(it, m == 'inc')
        elif m == 'neg':
            self.i_neg(it)
        elif m == 'not':
            self.i_not(it)
        elif m in ('shl', 'sal'):
            self.i_shift(it, 'shl')
        elif m in ('shr', 'sar'):
            self.i_shift(it, m)
        elif m in ('rol', 'ror'):
            self.i_rot(it, m == 'rol')
        elif m in ('rcl', 'rcr'):
            self.i_rc(it, m == 'rcl')
        elif m in ('shld', 'shrd'):
            self.i_shxd(it, m == 'shld')
        elif m in ('bt', 'bts', 'btr'):
            self.i_bt(it, m)
        elif m in ('bsf', 'bsr'):
            self.i_bsx(it, m == 'bsr')
        elif m == 'mul':
            self.i_mul(it)
        elif m == 'imul':
            self.i_imul(it)
        elif m in ('div', 'idiv'):
            self.i_div(it, m == 'idiv')
        elif m == 'cdq':
            self.e('asr w2,w0,31')
        elif m == 'cwde':
            self.e('sxth w0,w0')
        elif m == 'cbw':
            self.e('sxtb w9,w0')
            self.e('bfi w0,w9,0,16')
        elif m == 'xchg':
            self.i_xchg(it)
        elif m in ('push', 'pushd'):
            self.i_push(it)
        elif m == 'pop':
            self.i_pop(it)
        elif m == 'pushf':
            self.i_pushf(it)
        elif m == 'popf':
            self.i_popf(it)
        elif m == 'call':
            self.do_call(it)
        elif m in ('ret', 'retn'):
            self.do_ret(it)
        elif m == 'jmp':
            self.do_jmp(it)
        elif m in ('loop', 'loopd'):
            self.i_loop(it, 'loop')
        elif m in ('loopnz', 'loopnzd'):
            self.i_loop(it, 'loopnz')
        elif m == 'jecxz':
            self.i_loop(it, 'jecxz')
        elif m == 'jcxz':
            self.e('uxth w15,w1')
            raw = re.sub(r'^(near|short)\s+', '', it.ops.strip(), flags=re.I)
            self.canonical(self.target_live(it))
            self.e('@CBZ w15 %s' % raw)
        elif m == 'lahf':
            self.canonical(ALLF)
            self.e('mrs x9,nzcv')
            self.e('lsr x9,x9,28')
            self.e('bfi w0,w9,8,8')
        elif m == 'sahf':
            self.e('ubfx w9,w0,8,4')
            self.e('lsl x9,x9,28')
            self.e('msr nzcv,x9')
            self.state = dict(CANON)
            self.state['P'] = ('garbage', None)
        elif m == 'salc':
            self.cond_value('c', 'w9')
            self.e('neg w9,w9')
            self.e('bfi w0,w9,0,8')
        elif m == 'stc':
            self.state['C'] = ('const', 1)
        elif m == 'clc':
            self.state['C'] = ('const', 0)
        elif m == 'cmc':
            k, v = self.state['C']
            if k == 'hw':
                self.state['C'] = ('hw', INVCOND[v])
            elif k == 'const':
                self.state['C'] = ('const', 1 - v)
            elif k == 'reg':
                self.e('eor w27,%s,1' % v)
                self.state['C'] = ('reg', 'w27')
            else:
                self.error('cmc sur CF indefini')
        elif m == 'std':
            self.df_back = True
        elif m == 'cld':
            self.df_back = False
        elif m in ('lods', 'lodsb', 'lodsw', 'lodsd', 'stos', 'stosb', 'stosw', 'stosd', 'movs',
                   'movsb', 'movsw', 'movsd', 'scas', 'scasb', 'scasw', 'scasd', 'cmps', 'cmpsb',
                   'cmpsw', 'cmpsd'):
            self.i_string(it, m)
        elif m in ('xlat', 'xlatb'):
            self.i_xlat(it)
        elif m == 'int':
            self.i_int(it)
        elif m == 'ud2':
            self.e('brk 2')
        elif m == 'nop':
            self.e('nop')
        else:
            self.error('instruction non geree : ' + m)


# ---------------------------------------------------------------------------
#  Chargement des sources x86 (includes, continuations, correctifs FASMARM)
# ---------------------------------------------------------------------------
def read_lines(path):
    with open(path, encoding='latin-1') as f:
        raw = f.read().split('\n')
    out = []
    acc = None
    accsrc = None
    for n, line in enumerate(raw, 1):
        line = line.rstrip('\r')
        code = strip_comment(line).rstrip()
        src = '%s:%d' % (os.path.basename(path), n)
        if code.endswith('\\') and not code.endswith('\\\\'):
            part = code[:-1]
            if acc is None:
                acc, accsrc = part, src
            else:
                acc += ' ' + part.strip()
            continue
        if acc is not None:
            acc += ' ' + code.strip()
            out.append((acc, accsrc))
            acc = None
            continue
        out.append((line, src))
    return out


def load_tree(path, base_dirs):
    """Lit path en remplacant les include par leur contenu."""
    res = []
    for line, src in read_lines(path):
        s = strip_comment(line).strip()
        m = re.match(r"^include\s+'([^']+)'", s, re.I)
        if m:
            name = m.group(1).replace('\\', '/').split('/')[-1]
            found = None
            for d in base_dirs:
                for cand in (name, name.upper(), name.lower()):
                    p = os.path.join(d, cand)
                    if os.path.exists(p):
                        found = p
                        break
                if found:
                    break
            if not found:
                raise TranslateError('include introuvable : ' + name)
            res.extend(load_tree(found, base_dirs))
            continue
        res.append((line, src))
    return res


def drop_is64(lines):
    """Supprime les blocs 'if __is_64 ... [else ...] end if' (hote 32 bits)."""
    out = []
    stack = []      # pour chaque if : (est_is64, garder)
    keep = True
    for line, src in lines:
        s = strip_comment(line).strip().lower()
        if re.match(r'^if\s+__is_64\s*$', s):
            stack.append(('is64', keep))
            keep = False
            continue
        if stack and stack[-1][0] == 'is64' and s == 'else':
            keep = stack[-1][1]
            continue
        if re.match(r'^if\b', s):
            stack.append(('other', keep))
            if keep:
                out.append((line, src))
            continue
        if s == 'end if' and stack:
            kind, prev = stack.pop()
            if kind == 'other' and prev:
                out.append((line, src))
            keep = prev
            continue
        if keep:
            out.append((line, src))
    return out


def norm_instr(s):
    s = strip_comment(s).strip().lower()
    s = re.sub(r'\s+', ' ', s)
    s = re.sub(r'\s*,\s*', ',', s)
    s = re.sub(r'\[\s*', '[', s)
    s = re.sub(r'\s*\]', ']', s)
    s = re.sub(r'\s*\+\s*', '+', s)
    for k, v in VREG.items():
        s = re.sub(r'\b%s\b' % k, v, s)
    s = s.replace('byte[', 'byte [').replace('dword[', 'dword [').replace('word[', 'word [')
    s = s.replace('dword  [', 'dword [')
    return s


def split_angle(s):
    """Arguments de premier niveau d'un appel de macro avec groupes < >."""
    parts, cur, depth, q = [], [], 0, None
    for ch in s:
        if q:
            cur.append(ch)
            if ch == q:
                q = None
            continue
        if ch in '\'"':
            q = ch
        elif ch == '<':
            depth += 1
        elif ch == '>':
            depth -= 1
        elif ch == ',' and depth == 0:
            parts.append(''.join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    parts.append(''.join(cur).strip())
    return parts


def unangle(s):
    s = s.strip()
    if s.startswith('<') and s.endswith('>'):
        s = s[1:-1]
    items = split_angle(s)
    res = []
    for x in items:
        x = x.strip()
        if x.startswith('<') and x.endswith('>'):
            x = x[1:-1]
        res.append(x)
    return res


def apply_patches(lines, patch_lines, macros):
    """Applique les 'patch' et 'override' de FASMARM au niveau source."""
    labels = {}
    for i, (line, src) in enumerate(lines):
        m = LABEL_RE.match(strip_comment(line))
        if m and not m.group(1).startswith('.'):
            labels.setdefault(m.group(1), i)
    log = []
    for pl, psrc in patch_lines:
        s = strip_comment(pl).strip()
        m = re.match(r'^(patch|override)\s+(.*)$', s, re.I)
        if not m:
            continue
        kind = m.group(1).lower()
        args = split_angle(m.group(2))
        labl = args[0].strip()
        if labl not in labels:
            raise TranslateError('patch : etiquette %s introuvable (%s)' % (labl, psrc))
        li = labels[labl]
        if kind == 'override':
            new = unangle(args[1])
            lines.insert(li + 1, ('\t' + new[0] + '\t; override FASMARM', psrc))
            for k in list(labels):
                if labels[k] > li:
                    labels[k] += 1
            log.append('override %s -> %s' % (labl, new[0]))
            continue
        search = args[1].strip()
        if search in macros:
            search = [macros[search]]
        else:
            search = unangle(search)
        repl = args[2].strip()
        if repl in macros:
            repl = [macros[repl]]
        else:
            repl = unangle(repl)
        target = [norm_instr(x) for x in search]
        # recherche de la sequence apres l'etiquette
        neg = '-' in args[3] if len(args) > 3 else False
        j = li - 15 if neg else li
        found = None
        while j < len(lines) and j < li + 400:
            cand = []
            k = j
            while len(cand) < len(target) and k < len(lines):
                code = strip_comment(lines[k][0]).strip()
                mm = LABEL_RE.match(code)
                if mm:
                    code = mm.group(2).strip()
                if code:
                    cand.append((k, norm_instr(code)))
                k += 1
            if [c for _, c in cand] == target:
                found = [k2 for k2, _ in cand]
                break
            j += 1
        if not found:
            raise TranslateError('patch %s : sequence %s introuvable (%s)' % (labl, target, psrc))
        first = found[0]
        # on garde une eventuelle etiquette sur la premiere ligne
        code0 = strip_comment(lines[first][0])
        mm = LABEL_RE.match(code0)
        prefix = (mm.group(1) + ': ') if mm else ''
        newlines = [('\t' + prefix + repl[0] + '\t; patch FASMARM', psrc)] + [('\t' + r + '\t; patch FASMARM', psrc) for r in repl[1:]]
        if labl == 'finish_elf_segment':
            # le trampoline de FASMARM saute juste apres l'instruction remplacee
            newlines.append(('ARMPI_finish_elf_segment_cont:', psrc))
        for k2 in reversed(found):
            del lines[k2]
        for n, nl in enumerate(newlines):
            lines.insert(first + n, nl)
        delta = len(newlines) - len(found)
        for k in list(labels):
            if labels[k] > first:
                labels[k] += delta
        log.append('patch %s : %s -> %s (ligne %s)' % (labl, target, repl, lines[first][1]))
    return lines, log


# ---------------------------------------------------------------------------
#  Tables d'index pour les aiguillages "etiquette - base" sur 16 bits
# ---------------------------------------------------------------------------
HANDLER_BASES = ('instruction_handler', 'directive_handler')


def index_handlers(lines):
    """Remplace X-instruction_handler / X-directive_handler par des index dans
    des tables de 32 bits (les distances ARM depassent 64 Ko)."""
    order = {}
    aliases = {}
    glob = ''
    for i, (line, src) in enumerate(lines):
        code = strip_comment(line)
        for rx in (LABEL_RE, DATA_RE):
            m = rx.match(code)
            if m:
                n = m.group(1)
                if n.startswith('.'):
                    n = glob + n
                elif not n.startswith('@'):
                    glob = n
                order.setdefault(n, i)
                break
        m = re.match(r'^\s*([A-Za-z_][\w.@?]*)\s*=\s*([A-Za-z_][\w.@?]*)\s*$', code)
        if m:
            aliases[m.group(1)] = m.group(2)
    tables = {}
    for base in HANDLER_BASES:
        names = set()
        pat = re.compile(r'([A-Za-z_][\w.@?]*)\s*-\s*%s\b' % base)
        for line, src in lines:
            for n in pat.findall(strip_comment(line)):
                names.add(n)
        def pos(n, d=0):
            if n in order:
                return order[n]
            if n in aliases and d < 10:
                return pos(aliases[n], d + 1)
            return None
        for n in names:
            if n not in order and pos(n) is not None:
                order[n] = pos(n)
        names = sorted(names, key=lambda n: order.get(n, 1 << 30))
        missing = [n for n in names if n not in order]
        if missing:
            raise TranslateError('etiquettes de handler inconnues : %s' % missing[:5])
        tables[base] = names
    idx = {}
    for base, names in tables.items():
        # index 0 reserve : le code x86 utilise 0 comme valeur sentinelle (xor bx,bx)
        for k, n in enumerate(names):
            idx[(base, n)] = k + 1
    out = []
    for line, src in lines:
        code = strip_comment(line)
        if '_handler' in code:
            for base in HANDLER_BASES:
                code2 = re.sub(r'([A-Za-z_][\w.@?]*)\s*-\s*%s\b' % base,
                               lambda m: str(idx[(base, m.group(1))]), code)
                code = code2
            line = code
        out.append((line, src))
    return out, tables


def rewrite(lines, edits):
    """edits : liste (etiquette d'ancrage, motif regex de ligne normalisee,
    [nouvelles lignes]) ; la ligne est cherchee dans les 60 lignes qui suivent
    l'etiquette."""
    for anchor, pat, new in edits:
        rx = re.compile(pat)
        start = None
        for i, (line, src) in enumerate(lines):
            m = LABEL_RE.match(strip_comment(line))
            if m and m.group(1) == anchor:
                start = i
                break
        if start is None:
            raise TranslateError('ancre introuvable : ' + anchor)
        hit = False
        for i in range(start, min(len(lines), start + 400)):
            line, src = lines[i]
            code = strip_comment(line)
            m = LABEL_RE.match(code)
            if m:
                code = m.group(2)
            if rx.search(norm_instr(code)):
                lines[i:i + 1] = [('	' + n, src) for n in new]
                hit = True
                break
        if not hit:
            raise TranslateError('reecriture introuvable : %s apres %s' % (pat, anchor))
    return lines


# ---------------------------------------------------------------------------
#  Programme principal
# ---------------------------------------------------------------------------
SIZE_ARM = {'byte': 'byte', 'word': 'hword', 'dword': 'word', 'qword': 'dword'}


def conv_sizes_kw(s):
    return re.sub(r'\b(byte|word|dword|qword)\b', lambda m: SIZE_ARM[m.group(1).lower()], s, flags=re.I)


def conv_data(s):
    """Directive de donnees x86 -> FASMARM (dw->dh, dd->dw, dq->dd...)."""
    m = DATA_RE.match(s)
    if m and not is_instr(m.group(1).lower()):
        d = m.group(2).lower()
        return '%s %s %s' % (m.group(1), DATA_MAP[d], m.group(3))
    m = DATA_NOLABEL_RE.match(s)
    if m:
        d = m.group(1).lower()
        return '%s %s' % (DATA_MAP[d], m.group(2))
    return s


def estimate_size(line):
    s = line.strip()
    if not s or s.startswith(';') or s.endswith(':'):
        return 0
    tok = s.split(None, 1)[0].lower()
    if tok in ('db', 'du', 'dh', 'dw', 'dd', 'rb', 'rh', 'rw', 'rd'):
        unit = {'db': 1, 'du': 2, 'dh': 2, 'dw': 4, 'dd': 8, 'rb': 1, 'rh': 2, 'rw': 4, 'rd': 8}[tok]
        rest = s.split(None, 1)[1] if len(s.split(None, 1)) > 1 else ''
        if tok.startswith('r'):
            v = simple_eval(rest)
            return unit * (v or 1)
        n = 0
        for p in split_operands(rest):
            if p.startswith("'") and unit == 1:
                n += max(1, len(p) - 2)
            else:
                n += unit
        return n
    if re.match(r'^[A-Za-z_.@?][\w.@?]*\s+(db|du|dh|dw|dd|rb|rh|rw|rd)\b', s):
        parts = s.split(None, 1)
        return estimate_size(parts[1])
    if tok in ('align', 'if', 'else', 'end', 'macro', 'struc', 'irp', 'rept', 'repeat', 'while',
               'virtual', 'load', 'store', 'assert', 'display', 'local', 'common', 'forward',
               'label', 'purge', 'restruc', '}', '{') or '=' in s.split(None, 1)[0] or ' equ ' in s or ' = ' in s:
        return 0
    if tok.startswith('@'):
        return 4
    return 4


def finalize_branches(lines):
    """Remplace les pseudo-instructions @B/@BC/@CBZ/@CBNZ ; forme longue si la
    cible risque d'etre hors de portee."""
    pos = []
    labelpos = {}
    p = 0
    glob = ''
    for ln in lines:
        s = ln.strip()
        m = LABEL_RE.match(s)
        if m and not s.startswith('@'):
            name = m.group(1)
            full = glob + name if name.startswith('.') else name
            if not name.startswith('.'):
                glob = name
            labelpos[full] = p
        pos.append((p, glob))
        p += estimate_size(ln)
    out = []
    for (p, glob), ln in zip(pos, lines):
        s = ln.strip()
        if not s.startswith('@'):
            out.append(ln)
            continue
        parts = s.split()
        op = parts[0]
        tgt = parts[-1]
        far = False
        if not tgt.startswith('$'):
            full = glob + tgt if tgt.startswith('.') else tgt
            if full in labelpos:
                far = abs(labelpos[full] - p) > 900000
            else:
                far = True
        if op == '@B':
            out.append('\tb %s' % tgt)
        elif op == '@BC':
            c = parts[1]
            if far:
                out.append('\tb.%s $+8' % INVCOND[c])
                out.append('\tb %s' % tgt)
            else:
                out.append('\tb.%s %s' % (c, tgt))
        elif op in ('@CBZ', '@CBNZ'):
            r = parts[1]
            ins = 'cbz' if op == '@CBZ' else 'cbnz'
            if far:
                inv = 'cbnz' if ins == 'cbz' else 'cbz'
                out.append('\t%s %s,$+8' % (inv, r))
                out.append('\tb %s' % tgt)
            else:
                out.append('\t%s %s,%s' % (ins, r, tgt))
        else:
            out.append(ln)
    return out


ALIGNED_LABELS = set()


def emit_program(items, sizes, labels, following):
    tr = Translator2(items, sizes, labels, following)
    pending = []
    need_align = False
    falls = False            # l'instruction precedente peut continuer
    vdepth = 0
    df_back = False
    for i, it in enumerate(items):
        tr.it = it
        if it.kind == 'label':
            pending.append(it)
            continue
        if it.kind == 'instr':
            if pending:
                nxt = i
                if falls:
                    tr.canonical(items[nxt].live_in)
                if need_align and it.in_macro == 0 and vdepth == 0:
                    tr.out.append('\talign 4')
                for lb in pending:
                    tr.out.append(lb.text)
                if any(lb.label in ALIGNED_LABELS for lb in pending):
                    # ARM_instruction_handler lit le 1er octet d'un handler pour y
                    # chercher un marqueur 26h/64h/65h : nop (1Fh) evite toute confusion
                    tr.out.append('\tnop')
                pending = []
                tr.state = dict(CANON)
                tr.state['P'] = ('garbage', None)
            elif need_align and it.in_macro == 0 and vdepth == 0:
                tr.out.append('\talign 4')
            need_align = False
            tr.out.append('\t\t\t\t; ' + it.text.strip().replace('\t', ' ') + '  [' + it.src + ']')
            try:
                tr.translate(it)
            except Exception as ex:
                tr.error('exception %s: %s' % (type(ex).__name__, ex))
            m = it.mn
            falls = m not in ('jmp', 'ret', 'retn', 'ud2')
            continue
        # elements non executables
        if pending and vdepth == 0 and it.in_macro == 0 and \
                any(lb.label in ALIGNED_LABELS for lb in pending):
            # cible d'aiguillage suivie de donnees : le code qui suit les donnees
            # doit rester aligne (ARM_instruction_handler saute a +12 / +16)
            tr.out.append('\talign 4')
        for lb in pending:
            tr.out.append(lb.text)
        if pending:
            tr.state = dict(CANON)
            tr.state['P'] = ('garbage', None)
        pending = []
        falls = False
        s = it.text
        low = s.strip().lower()
        if it.kind == 'data':
            tr.out.append('\t' + conv_data(s))
            if vdepth == 0:
                need_align = True
        elif it.kind == 'labeldir':
            tr.out.append('\t' + conv_sizes_kw(s))
        else:
            if low.startswith('virtual'):
                vdepth += 1
            elif low.startswith('end virtual'):
                vdepth -= 1
            if re.match(r'^(load|store)\b', low):
                s = conv_sizes_kw(s)
            if re.match(r'^(format|entry|segment|section)\b', low):
                continue
            tr.out.append('\t' + s)
            if not re.match(r'^(align|if|else|end|assert|display|local|common|forward|purge|restruc|macro|struc|irp|rept|repeat|while|virtual|load|store)\b', low) \
                    and not re.match(r'^[\w.@?]+\s*(=|equ)\b', s.strip()) and vdepth == 0 and it.in_macro == 0:
                need_align = True
    for lb in pending:
        tr.out.append(lb.text)
    return tr


RUNTIME = r'''
; ===========================================================================
;  Execution ARMPI : demarrage, pile x86 emulee, appels systeme Linux i386
;  emules (int 0x80) au-dessus des appels systeme ARMPI.
; ===========================================================================
ARMPI_SYS_EXIT	= 0x10
ARMPI_SYS_PRINT	= 0x60
ARMPI_SYS_TIME	= 0x19
ARMPI_SYS_SBRK	= 0x00
ARMPI_SYS_OPEN	= 0x80
ARMPI_SYS_READ	= 0x81
ARMPI_SYS_WRITE	= 0x82
ARMPI_SYS_SEEK	= 0x83
ARMPI_SYS_CLOSE	= 0x84
ARMPI_SYS_LIB_OPEN = 0x64
ARMPI_APPKIT_FSYS = 13			; rang de fichier_sys dans appkit.abi (version 14)

armpi_entry:
	adr	x28,armpi_base
	mov	x19,x0				; ligne d'arguments (adresse absolue) ou 0
	adr	x0,armpi_appkit			; fichiers par le client d'AppKit
	mov	x1,ARMPI_APPKIT_FSYS+1		; (docs/FICHIERS.md, etape 3) ;
	mov	x8,ARMPI_SYS_LIB_OPEN		; sans lui : par le noyau
	svc	0
	tbnz	x0,63,.sans_appkit
	ldr	w1,[x0,8+ARMPI_APPKIT_FSYS*4]
	add	x1,x0,x1
	adr	x2,armpi_fsys
	str	x1,[x2]
.sans_appkit:
	movz	w20,armpi_argbuf and 0FFFFh
	movk	w20,(armpi_argbuf shr 16) and 0FFFFh,lsl 16
	movz	w21,armpi_argv and 0FFFFh
	movk	w21,(armpi_argv shr 16) and 0FFFFh,lsl 16
	movz	w9,armpi_name and 0FFFFh
	movk	w9,(armpi_name shr 16) and 0FFFFh,lsl 16
	str	w9,[x28,w21,uxtw]
	mov	w22,1				; argc
	cbz	x19,.args_done
.skip:	ldrb	w9,[x19]
	cbz	w9,.args_done
	cmp	w9,' '
	b.hi	.word
	add	x19,x19,1
	b	.skip
.word:	cmp	w22,30
	b.hs	.args_done
	add	w10,w21,w22,lsl 2
	str	w20,[x28,w10,uxtw]
	add	w22,w22,1
.copy:	ldrb	w9,[x19]
	cmp	w9,' '
	b.ls	.end_word
	strb	w9,[x28,w20,uxtw]
	add	w20,w20,1
	add	x19,x19,1
	b	.copy
.end_word:
	strb	wzr,[x28,w20,uxtw]
	add	w20,w20,1
	b	.skip
.args_done:
	; pile x86 : argc, argv[], 0, 0 (environnement vide)
	movz	w4,armpi_stack_top and 0FFFFh
	movk	w4,(armpi_stack_top shr 16) and 0FFFFh,lsl 16
	add	w9,w22,3
	sub	w4,w4,w9,lsl 2
	str	w22,[x28,w4,uxtw]
	mov	w10,0
.argv:	cmp	w10,w22
	b.hs	.argv_done
	add	w11,w21,w10,lsl 2
	ldr	w12,[x28,w11,uxtw]
	add	w11,w4,4
	add	w11,w11,w10,lsl 2
	str	w12,[x28,w11,uxtw]
	add	w10,w10,1
	b	.argv
.argv_done:
	add	w11,w4,4
	add	w11,w11,w22,lsl 2
	str	wzr,[x28,w11,uxtw]
	add	w11,w11,4
	str	wzr,[x28,w11,uxtw]
	mov	w0,0
	mov	w1,0
	mov	w2,0
	mov	w3,0
	mov	w5,0
	mov	w6,0
	mov	w7,0
	b	start

; int 0x80 : eax = numero Linux i386, ebx, ecx, edx = arguments -> eax
; Preserve tous les registres x86 (w1-w7), NZCV, x19-x28.
linux_int80:
	mrs	x9,nzcv
	stp	x9,x30,[sp,-16]!
	stp	x1,x2,[sp,-16]!
	cmp	w0,1
	b.eq	.exit
	cmp	w0,3
	b.eq	.read
	cmp	w0,4
	b.eq	.write
	cmp	w0,5
	b.eq	.open
	cmp	w0,6
	b.eq	.close
	cmp	w0,13
	b.eq	.time
	cmp	w0,19
	b.eq	.lseek
	cmp	w0,45
	b.eq	.brk
	cmp	w0,78
	b.eq	.gtod
	movn	w0,37				; -38 ENOSYS
	b	.ret
.exit:	mov	w0,w3
	mov	x8,ARMPI_SYS_EXIT
	svc	0
.read:	cmp	w3,3
	b.lo	.zero
	sub	w0,w3,3
	add	x1,x28,w1,uxtw
	mov	w2,w2
	mov	x8,ARMPI_SYS_READ
	bl	armpi_fichier
	b	.ret
.write:	cmp	w3,3
	b.lo	.print
	sub	w0,w3,3
	add	x1,x28,w1,uxtw
	mov	w2,w2
	mov	x8,ARMPI_SYS_WRITE
	bl	armpi_fichier
	b	.ret
.print:	add	x9,x28,w1,uxtw			; sortie standard (AppKit : console ou
	mov	w10,w2				; tube, etape 4), par blocs de 4096
.pr:	cbz	w10,.pr_done
	mov	w11,4096
	cmp	w10,w11
	csel	w11,w10,w11,lo
	mov	x0,x9
	mov	w1,w11
	mov	x8,ARMPI_SYS_PRINT
	bl	armpi_fichier
	add	x9,x9,x11
	sub	w10,w10,w11
	b	.pr
.pr_done:
	ldr	x2,[sp,8]
	mov	w0,w2
	b	.ret
.open:	add	x0,x28,w3,uxtw
	and	w9,w1,3
	mov	w10,1				; O_READ
	cmp	w9,1
	b.ne	.o1
	mov	w10,2				; O_WRITE
.o1:	cmp	w9,2
	b.ne	.o2
	mov	w10,3
.o2:	tst	w1,64				; O_CREAT
	b.eq	.o3
	orr	w10,w10,4
.o3:	tst	w1,512				; O_TRUNC
	b.eq	.o4
	orr	w10,w10,8
.o4:	mov	w1,w10
	mov	x8,ARMPI_SYS_OPEN
	bl	armpi_fichier
	tbnz	x0,63,.ret
	add	w0,w0,3
	b	.ret
.close:	cmp	w3,3
	b.lo	.zero
	sub	w0,w3,3
	mov	x8,ARMPI_SYS_CLOSE
	bl	armpi_fichier
	b	.ret
.time:	cbz	w3,.zero
	str	wzr,[x28,w3,uxtw]
	b	.zero
.lseek:	sub	w0,w3,3
	sxtw	x1,w1
	mov	w2,w2
	mov	x8,ARMPI_SYS_SEEK
	bl	armpi_fichier
	b	.ret
.brk:	adr	x9,armpi_brk
	ldr	w10,[x9]
	cbnz	w10,.brk_ok
	mov	x0,0
	mov	x8,ARMPI_SYS_SBRK
	svc	0
	sub	x10,x0,x28
	str	w10,[x9]
.brk_ok:
	cmp	w3,w10
	b.ls	.brk_cur
	sub	w0,w3,w10
	mov	x8,ARMPI_SYS_SBRK
	svc	0
	tbnz	x0,63,.brk_cur
	str	w3,[x9]
	mov	w10,w3
.brk_cur:
	mov	w0,w10
	b	.ret
.gtod:	mov	x8,ARMPI_SYS_TIME
	svc	0
	mov	x10,1000
	udiv	x11,x0,x10
	msub	x12,x11,x10,x0
	mul	x12,x12,x10
	str	w11,[x28,w3,uxtw]
	add	w13,w3,4
	str	w12,[x28,w13,uxtw]
.zero:	mov	w0,0
.ret:	ldp	x1,x2,[sp],16
	ldp	x9,x30,[sp],16
	msr	nzcv,x9
	ret

; x8 = appel de fichier : fichier_sys d'AppKit (memes registres rendus qu'un
; svc 0 ; x16 : relais), le noyau si AppKit manque
armpi_fichier:
	adr	x16,armpi_fsys
	ldr	x16,[x16]
	cbz	x16,.noyau
	br	x16
.noyau:	svc	0
	ret

armpi_name	db 'fasmarm',0
armpi_appkit	db 'appkit',0
		align 8
armpi_fsys	dd 0				; fichier_sys d'AppKit (0 : aucun)
armpi_brk	dw 0
'''

HEADER = r'''; ===========================================================================
;  FASMARM natif pour ARMPI - genere par tools/x86toa64.py, NE PAS EDITER.
;  Source : flat assembler 1.73.30 (c) 1999-2019 Tomasz Grysztar,
;           backend ARM FASMARM 1.44 (c) 2005-2023 revolution.
;  La licence de fasm s'applique (notices conservees dans les sources).
; ===========================================================================
	format	binary as 'elf'
	processor cpu64_v8 +cpu64_fp +cpu64_simd
	code64
ELF_BASE = 4000000000h
	org	0
armpi_base:
	db	7Fh,'ELF',2,1,1,0
	dd	0
	dh	2,183
	dw	1
	dd	ELF_BASE + armpi_entry
	dd	64
	dd	0
	dw	0
	dh	64,56,1,64,0,0
	dw	1,7
	dd	0,ELF_BASE,ELF_BASE,armpi_file_end,armpi_mem_end,1000h
'''


def build(main, out_path, dirs):
    lines = load_tree(main, dirs)
    lines = drop_is64(lines)
    # bloc des correctifs FASMARM (irpv ... fin de la macro dialogitem)
    start = end = None
    for i, (l, src) in enumerate(lines):
        s = strip_comment(l).strip()
        if start is None and s.startswith('irpv any, esp'):
            start = i
        if start is not None and s.startswith('macro dialogitem'):
            j = i
            while strip_comment(lines[j][0]).strip() != '}':
                j += 1
            end = j
            break
    block = lines[start:end + 1]
    lines[start:end + 1] = [('__is_64 = 0', block[0][1])]
    patch_lines = []
    in_caption = False
    for l, src in block:
        s = strip_comment(l).strip()
        if s.startswith('if defined _caption'):
            in_caption = True
        if s.startswith('end if') and in_caption:
            in_caption = False
            continue
        if not in_caption and re.match(r'^(patch|override)\s', s) and not s.startswith('patch_error'):
            patch_lines.append((s, src))
    # macro dw de armtable (assert >= 0) : inutile
    for i, (l, src) in enumerate(lines):
        if strip_comment(l).strip().startswith('macro dw [value]'):
            del lines[i:i + 5]
            break
    lines, log = apply_patches(lines, patch_lines,
                               {'finish_elf_exe_patch_code': 'mov ebx,[number_of_sections]'})
    # aiguillages : tables d'index au lieu de distances 16 bits
    lines = rewrite(lines, [
        ('ARM_instruction_handler', r'^add ebx,edx$', ['mov ebx,[ARMPI_ih_table+ebx*4]\t; ARMPI : table d\'index']),
        ('ARM_instruction_handler', r'^add ebx,9$', ['add ebx,12\t; ARMPI : modele aligne sur 4']),
        ('ARM_instruction_handler', r'^cmp byte \[ebx-9\],arm64v_encoding$', ['cmp byte [ebx-12],ARM64V_ENCODING']),
        ('directive_found', r'^add eax,ecx$', ["mov eax,[ARMPI_dh_table+ecx*4]	; ARMPI : table d'index"]),
        ('ARM_finish_elf_segment', r'^finish_elf_exe_patch_code$', ['mov ebx,[number_of_sections]']),
        ('ARM_finish_elf_segment', r'^jmp finish_elf_segment\+\(\$-\.patch_start\)$', ['jmp ARMPI_finish_elf_segment_cont']),
        # decode_template revient juste apres les donnees TEMPLATE qui suivent l'appel :
        # le code ARM qui suit ces donnees est aligne sur 4
        ('decode_template', r'^lea ebx,\[ebx\+ecx\+1\+template_length\]$',
         ['lea ebx,[ebx+ecx+1+TEMPLATE_length]', 'add ebx,3\t; ARMPI : retour aligne', 'and ebx,not 3']),
    ])
    lines, tables = index_handlers(lines)
    # bout de programme DOS (code 16 bits copie tel quel dans les PE) : octets x86
    for i, (l, src) in enumerate(lines):
        if strip_comment(l).strip().lower() == 'use16':
            j = i
            while strip_comment(lines[j][0]).strip().lower() != 'use32':
                j += 1
            keep = [(x, s2) for x, s2 in lines[i + 1:j]
                    if re.match(r'^\s*(stub_message|rq|default_stub_end)', strip_comment(x))]
            stub = [('	db 0Eh,1Fh,0BAh,(stub_message-default_stub) and 0FFh,(stub_message-default_stub) shr 8,0B4h,9,0CDh,21h,0B8h,1,4Ch,0CDh,21h', src)]
            lines[i:j + 1] = stub + keep
            break
    # INST_ARM64 equ es/fs/gs : prefixes de segment x86 -> octets
    newl = []
    for l, src in lines:
        s = strip_comment(l).strip()
        if re.match(r'^INST_ARM64[SV]?\s+equ\b', s):
            continue
        if s in ('INST_ARM64', 'INST_ARM64S', 'INST_ARM64V'):
            l = '\tdb\t' + {'INST_ARM64': '26h', 'INST_ARM64S': '64h', 'INST_ARM64V': '65h'}[s]
        newl.append((l, src))
    lines = newl
    items, sizes = classify(lines)
    labels, following, unknown = build_cfg(items)
    rounds = liveness(items)
    ALIGNED_LABELS.update(tables['instruction_handler'])
    tr = emit_program(items, sizes, labels, following)
    body = finalize_branches(tr.out)
    # FASMARM refuse "bfi Wd,Wn,0,w" : equivalent bfxil
    body = [re.sub(r'\bbfi(\s+\w+,\w+),0,', r'bfxil\1,0,', l) for l in body]
    with open(out_path, 'w', encoding='latin-1', newline='\n') as f:
        f.write(HEADER)
        f.write(RUNTIME)
        f.write('\n'.join(body))
        f.write('\n\n\talign 4\nARMPI_ih_table:\n\tdw 0\t\t; index 0 reserve\n')
        for n in tables['instruction_handler']:
            f.write('\tdw %s\n' % n)
        f.write('ARMPI_dh_table:\n\tdw 0\n')
        for n in tables['directive_handler']:
            f.write('\tdw %s\n' % n)
        f.write('\tdb 0\t\t; force l\'ecriture de toutes les donnees initialisees\n')
        f.write('armpi_file_end:\n')
        f.write('\talign 16\narmpi_argv rw 32\narmpi_argbuf rb 4096\narmpi_stack rb 100000h\narmpi_stack_top:\n')
        f.write('\talign 4096\narmpi_mem_end:\n')
    return tr, log, unknown, rounds, tables


if __name__ == '__main__':
    main, out = sys.argv[1], sys.argv[2]
    dirs = [os.path.dirname(main)] + sys.argv[3:]
    tr, log, unknown, rounds, tables = build(main, out, dirs)
    for l in log:
        print(l)
    print('etiquettes inconnues :', sorted(unknown)[:20])
    print('vivacite : %d tours' % rounds)
    print('tables : %d handlers, %d directives' % (len(tables['instruction_handler']), len(tables['directive_handler'])))
    print('erreurs de traduction : %d' % len(tr.errors))
    for e in tr.errors[:40]:
        print('  ' + e)
