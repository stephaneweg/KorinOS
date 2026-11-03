"""dkvec.py -- les vecteurs de dktest (DocumentKit), calcules sur le PC, dans un dossier a mettre sur la carte :

  python -I tools/gcc/test/dkvec.py OUT [ONYX]

* list.txt, x_NN_*.xml et leur .ref : de vraies parties de documents, et la reference par expat ;
* num.txt : dates (datetime), longueurs (Fraction), UTF-8 (codec strict), CP1252 ;
* fuzz.bin : XML abime ; dossier refsrc/ : xml.h d'Onyx et une petite core.h pour compiler dkref.cpp (WSL), qui
  ecrit fuzz.ref. Stdlib seulement.
"""
import datetime
import fractions
import glob
import io
import os
import random
import shutil
import struct
import sys
import xml.parsers.expat
from xml.etree import ElementTree
import zipfile


def ref_of(data):
    """(kind, a, b) d'un XML bien forme : S nom, A nom valeur, E nom, T texte (fusionne, dans la racine)"""
    out = []
    text = []
    depth = [0]

    def rec(kind, a, b=b""):
        out.append(struct.pack("<cI", kind, len(a)) + a + struct.pack("<I", len(b)) + b)

    def flush():
        if text:
            t = "".join(text).encode("utf-8")
            text.clear()
            if t:
                rec(b"T", t)

    def start(name, attrs):
        flush()
        rec(b"S", name.encode())
        pairs = [(attrs[i], attrs[i + 1]) for i in range(0, len(attrs), 2)]
        done = set()

        def rule(q):   # le lecteur : nom avec prefixe exact ; sans prefixe, aussi tout prefixe sauf xmlns
            for k, v in pairs:
                if k == q:
                    return v
                if ":" not in q and ":" in k:
                    p, l = k.split(":", 1)
                    if l == q and p != "xmlns":
                        return v
            return None
        for k, _ in pairs:
            for q in (k, k.split(":", 1)[-1]):
                if q in done or (q != k and k.startswith("xmlns:")):
                    continue
                done.add(q)
                rec(b"A", q.encode(), rule(q).encode("utf-8"))
        depth[0] += 1

    def end(name):
        flush()
        rec(b"E", name.encode())
        depth[0] -= 1

    def chars(s):
        if depth[0]:
            text.append(s)
    p = xml.parsers.expat.ParserCreate(namespace_separator=None)
    p.ordered_attributes = True
    p.buffer_text = False
    p.StartElementHandler = start
    p.EndElementHandler = end
    p.CharacterDataHandler = chars
    p.Parse(data, True)
    return b"".join(out)


TORTURE = """<?xml version="1.0" encoding="UTF-8"?>
<!-- tete -->
<?pi quelque chose?>
<w:document xmlns:w="urn:w" xmlns:r="urn:r" xmlns="urn:defaut">
 <w:body a='1' b="deux" r:id="rId1" w:val="&lt;&gt;&amp;&quot;&apos;&#65;&#x42;&#233;&#x20AC;&#x1F600;">
  <w:p><w:r><w:t xml:space="preserve"> caf\u00e9 \u20ac &#x1F600; </w:t></w:r>
  <!-- commentaire <avec> des > et - tirets -->
  <![CDATA[<brut> & &amp; ]] ]>]]><![CDATA[]]>suite&#10;&#9;fin</w:p>
  <vide/><vide2 x="y"/><vide3 x='a"b' y="c'd" />
  <text:p text:style-name="P1">a<text:s text:c="3"/>b<text:tab/>c</text:p>
 </w:body>
</w:document>
<!-- fin -->
"""

ODS = """<?xml version="1.0" encoding="UTF-8"?>
<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.3">
<office:body><office:spreadsheet><table:table table:name="Feuille &amp; 1">
<table:table-column table:number-columns-repeated="3"/>
<table:table-row table:number-rows-repeated="2"><table:table-cell office:value-type="float" office:value="3.5"><text:p>3,5</text:p></table:table-cell>
<table:table-cell table:formula="of:=SUM([.A1:.B2])&gt;1" office:value-type="date" office:date-value="2026-10-31"><text:p>31/10/2026</text:p></table:table-cell>
<table:covered-table-cell table:number-columns-repeated="1"/></table:table-row>
</table:table></office:spreadsheet></office:body></office:document-content>
"""


def parts(onyx):
    """(nom, octets) : de vraies parties"""
    def z(path, names):
        with zipfile.ZipFile(path) as a:
            for n in names:
                yield a.read(n)
    w = os.path.join(onyx, ".claude/worktrees/happy-wilbur-2816ac/docs/exports/06-KITS-GUIDE.docx")
    out = []
    if os.path.exists(w):
        for n, b in zip(("document", "styles", "rels"), z(w, ("word/document.xml", "word/styles.xml", "word/_rels/document.xml.rels"))):
            out.append(("docx_" + n, b))
    x = os.path.join(onyx, "sdcard/docs/cafe-2026.xlsx")
    for n, b in zip(("sheet1", "sheet3", "strings", "styles"), z(x, ("xl/worksheets/sheet1.xml", "xl/worksheets/sheet3.xml", "xl/sharedStrings.xml", "xl/styles.xml"))):
        out.append(("xlsx_" + n, b))
    p = os.path.join(onyx, "sdcard/docs/cafe-2026.pptx")
    with zipfile.ZipFile(p) as a:
        slides = [n for n in a.namelist() if n.startswith("ppt/slides/slide")][:2]
    for i, b in enumerate(z(p, slides)):
        out.append(("pptx_slide%d" % i, b))
    p = os.path.join(onyx, "tools/tests/slides/powerpoint.pptx")
    with zipfile.ZipFile(p) as a:
        ns = [n for n in a.namelist() if n.startswith("ppt/slides/slide") or n == "ppt/theme/theme1.xml"][:3]
    for i, b in enumerate(z(p, ns)):
        out.append(("ppt_%d" % i, b))
    o = os.path.join(onyx, "sdcard/docs/cafe-2026.odp")
    for n, b in zip(("content", "styles", "manifest"), z(o, ("content.xml", "styles.xml", "META-INF/manifest.xml"))):
        out.append(("odp_" + n, b))
    out.append(("torture", TORTURE.encode("utf-8")))
    out.append(("ods_main", ODS.encode("utf-8")))
    return out


CORE_H = """// core.h de la reference : juste ce que sheet/xml.h demande
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
namespace ss {
struct Buf {
	char *b; int n, cap;
	Buf () : b (0), n (0), cap (0) {}
	~Buf () { free (b); }
	void res (int k) { if (n + k + 1 > cap) { cap = (n + k + 1) * 2; b = (char *) realloc (b, cap); } }
	void put (char c) { res (1); b[n++] = c; b[n] = 0; }
	void putn (const char *s, int k) { res (k); memcpy (b + n, s, k); n += k; b[n] = 0; }
	void puts (const char *s) { putn (s, (int) strlen (s)); }
	void clear () { n = 0; if (b) b[0] = 0; }
	const char *str () { res (0); b[n] = 0; return b; }
	void putu (unsigned c)		// (kit : un point de code invalide donne U+FFFD)
	{
		if (c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) c = 0xFFFD;
		if (c < 0x80) put ((char) c);
		else if (c < 0x800) { put ((char) (0xC0 | c >> 6)); put ((char) (0x80 | (c & 0x3F))); }
		else if (c < 0x10000) { put ((char) (0xE0 | c >> 12)); put ((char) (0x80 | (c >> 6 & 0x3F))); put ((char) (0x80 | (c & 0x3F))); }
		else { put ((char) (0xF0 | c >> 18)); put ((char) (0x80 | (c >> 12 & 0x3F))); put ((char) (0x80 | (c >> 6 & 0x3F))); put ((char) (0x80 | (c & 0x3F))); }
	}
};
// (kit : sans blancs ni signe ni 0x, sature au-dela de U+10FFFF)
static unsigned long ref_strtoul (const char *s, char **, int base)
{
	unsigned long v = 0;
	for (;; s++)
	{
		int c = *s, d = c >= '0' && c <= '9' ? c - '0' : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10 : base == 16 && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (d < 0) break;
		v = v * base + d;
		if (v > 0x110000) v = 0x110000;
	}
	return v;
}
#define strtoul ref_strtoul
}
"""

TOKENS = [b"<", b">", b"/>", b"</", b'"', b"'", b"&", b";", b"&#x41;", b"&#65", b"&amp;", b"&#", b"&#xFFFFFFFFF;", b"&#xD800;", b"<![CDATA[",
          b"]]>", b"<!--", b"-->", b"<?", b"?>", b"<!", b"=", b" ", b"\t", b"\n", b":", b"\xc3", b"\xff", b"\x00", b'a="1"', b"val='true'",
          b'w:val="x"', b"<a/>", b"<p>", b"</p>", b"<w:p ", b"<t>"]


def fuzz(seeds, count, rnd):
    out = []
    for i in range(count):
        s = bytearray(rnd.choice(seeds))
        if len(s) > 3000:
            a = rnd.randrange(len(s) - 3000)
            s = s[a:a + 3000]
        for _ in range(rnd.randrange(1, 9)):
            m = rnd.randrange(5)
            p = rnd.randrange(len(s) + 1)
            if m == 0 and s:
                s[min(p, len(s) - 1)] = rnd.randrange(256)
            elif m == 1:
                del s[p:p + rnd.randrange(1, 12)]
            elif m == 2:
                s[p:p] = rnd.choice(TOKENS)
            elif m == 3:
                s[p:p + 1] = rnd.choice(TOKENS)
            else:
                del s[p:]                       # tronque
        out.append(bytes(s))
    return out


# EMU par unite, dans l'ordre de documentkit.inc
EMU = [1, 635, 360, 12700, 914400, 360000, 36000, 152400, 9525]
SUF = ["", "", "", "pt", "in", "cm", "mm", "pc", "px"]


def round_half_up(f):
    return int(fractions.Fraction(f) + fractions.Fraction(1, 2)) if f >= 0 else -int(-fractions.Fraction(f) + fractions.Fraction(1, 2))


def nums(rnd):
    o = []
    for _ in range(600):
        d = datetime.date(rnd.randrange(1, 10000), rnd.randrange(1, 13), rnd.randrange(1, 29))
        days = (d - datetime.date(1970, 1, 1)).days
        o.append("D %d %d %d %d %d" % (d.year, d.month, d.day, days, (d.weekday() + 1) % 7))
    for day in range(-30, 60):                  # autour de fin fevrier 1900, 2000, 2024
        for base in (datetime.date(1900, 2, 1), datetime.date(2000, 2, 1), datetime.date(2100, 2, 1), datetime.date(2024, 2, 1), datetime.date(1970, 1, 1)):
            d = base + datetime.timedelta(days=day)
            if d.year >= 1:
                o.append("D %d %d %d %d %d" % (d.year, d.month, d.day, (d - datetime.date(1970, 1, 1)).days, (d.weekday() + 1) % 7))
    for _ in range(300):
        d = datetime.date(rnd.randrange(1900, 3000), rnd.randrange(1, 13), rnd.randrange(1, 29))
        if d.year == 1900 and d.month < 3:
            continue
        o.append("X %d %d %d %d" % (d.year, d.month, d.day, (d - datetime.date(1899, 12, 30)).days))
    for day in range(1, 60):                    # janvier, fevrier 1900 : un de moins
        d = datetime.date(1899, 12, 31) + datetime.timedelta(days=day)
        o.append("X %d %d %d %d" % (d.year, d.month, d.day, day))
    for _ in range(600):
        src = rnd.randrange(3, 9)
        dst = rnd.randrange(0, 9)
        dec = rnd.randrange(0, 4)
        ip = rnd.randrange(0, 2000)
        fp = rnd.randrange(0, 10 ** dec) if dec else 0
        txt = ("-" if rnd.randrange(4) == 0 else "") + str(ip) + ("." + str(fp).rjust(dec, "0") if dec else "") + SUF[src]
        v = fractions.Fraction(txt[:-2])
        o.append("L %s %d 0 -999 %d" % (txt, dst, round_half_up(v * EMU[src] / EMU[dst])))
    for _ in range(400):
        a, b = rnd.randrange(9), rnd.randrange(9)
        v = rnd.randrange(-100000, 100000)
        o.append("T %d %d %d %d" % (v, a, b, round_half_up(fractions.Fraction(v * EMU[a], EMU[b]))))
    for _ in range(300):
        a, b = rnd.randrange(9), rnd.randrange(3, 9)
        v = rnd.randrange(0, 100000)
        dec = rnd.randrange(0, 7)
        q = fractions.Fraction(v * EMU[a], EMU[b])
        n = round_half_up(q * 10 ** dec)
        s = str(n).rjust(dec + 1, "0")
        s = (s[:-dec] + "." + s[-dec:] if dec else s) + SUF[b]
        o.append("W %d %d %d %d %s" % (v, a, b, dec, s))
    pool = [b"a", b"\xc3\xa9", b"\xe2\x82\xac", b"\xf0\x9f\x98\x80", b"\xc0", b"\xc1\xbf", b"\xe0\x80\x80", b"\xed\xa0\x80", b"\xf4\x90\x80\x80",
            b"\xf5", b"\x80", b"\xe2\x82", b"\xf0\x9f", b"\xff", b"\xef\xbf\xbf", b"\xf4\x8f\xbf\xbf", b"\xdf\xbf", b"\xe0\xa0\x80", b"\xf0\x90\x80\x80"]
    for _ in range(600):
        b = b"".join(rnd.choice(pool) for _ in range(rnd.randrange(0, 5)))
        if not b:
            continue
        try:
            n, ok = len(b.decode("utf-8")), 1
        except UnicodeDecodeError:
            ok = 0
            n = 0                               # compte comme le kit : suite valide 1, octet invalide 1
            i = 0
            while i < len(b):
                for k in (1, 2, 3, 4):
                    try:
                        b[i:i + k].decode("utf-8")
                        i += k
                        break
                    except UnicodeDecodeError:
                        pass
                else:
                    i += 1
                n += 1
        o.append("U %s %d %d" % (b.hex(), ok, n))
    t = {}
    for b in range(256):
        try:
            c = ord(bytes([b]).decode("cp1252"))
        except UnicodeDecodeError:
            c = b                               # (les 5 trous : identite, comme WHATWG)
        t[b] = c
        o.append("P %d %d" % (b, c))
    for c in (0x100, 0x2028, 0xFFFD, 0x85, 0x9F, 0x1F600):
        if c not in t.values():
            o.append("Q %d" % c)
    for c in list(range(0, 0x500, 7)) + [0x7F, 0x80, 0x7FF, 0x800, 0xD7FF, 0xE000, 0xFFFD, 0xFFFF, 0x10000, 0x10FFFF, 0x1F600]:
        o.append("C %d %s" % (c, chr(c).encode("utf-8").hex()))
    return "\n".join(o) + "\n"


def packages(onyx, card):
    """K fichier genre : des fichiers entiers ; R fichier relations, r rang id type cible mode : de vrais .rels"""
    o = []
    w = os.path.join(onyx, ".claude/worktrees/happy-wilbur-2816ac/docs/exports/06-KITS-GUIDE.docx")
    files = [(w, 1, "word/_rels/document.xml.rels"), (os.path.join(onyx, "sdcard/docs/cafe-2026.xlsx"), 2, "xl/_rels/workbook.xml.rels"),
             (os.path.join(onyx, "sdcard/docs/cafe-2026.pptx"), 3, "ppt/_rels/presentation.xml.rels"),
             (os.path.join(onyx, "tools/tests/slides/powerpoint.pptx"), 3, "ppt/_rels/presentation.xml.rels"),
             (os.path.join(onyx, "sdcard/docs/cafe-2026.odp"), 6, None), (os.path.join(onyx, "sdcard/docs/letters-tour.rtf"), 7, None)]
    for i, (path, kind, rels) in enumerate(files):
        if not os.path.exists(path):
            continue
        f = "k%d.bin" % i
        shutil.copy(path, os.path.join(card, f))
        o.append("K %s %d" % (f, kind))
        if rels:
            with zipfile.ZipFile(path) as a:
                b = a.read(rels)
            g = "r%d.xml" % i
            with open(os.path.join(card, g), "wb") as q:
                q.write(b)
            rs = list(ElementTree.fromstring(b))
            o.append("R %s %d" % (g, len(rs)))
            for n, r in enumerate(rs):
                o.append("r %d %s %s %s %s" % (n, r.get("Id"), r.get("Type").rsplit("/", 1)[-1], r.get("Target").replace(" ", "%20"), r.get("TargetMode", "-")))
    return "\n".join(o) + "\n"


def main():
    out = sys.argv[1]
    onyx = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("ONYX", "build/onyx")
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "card"))
    os.makedirs(os.path.join(out, "refsrc"))
    card = os.path.join(out, "card")
    rnd = random.Random(20261031)
    names = []
    seeds = []
    for i, (n, b) in enumerate(parts(onyx)):
        f = "x%02d%s.xml" % (i, n)
        with open(os.path.join(card, f), "wb") as o:
            o.write(b)
        with open(os.path.join(card, f + ".ref"), "wb") as o:
            o.write(ref_of(b))
        names.append(f)
        seeds.append(b)
        print(f, len(b))
    with io.open(os.path.join(card, "list.txt"), "w", newline="\n") as o:
        o.write("\n".join(names) + "\n")
    with io.open(os.path.join(card, "num.txt"), "w", newline="\n") as o:
        o.write(nums(rnd))
        o.write(packages(onyx, card))
    with open(os.path.join(card, "fuzz.bin"), "wb") as o:
        for b in fuzz(seeds, 4000, rnd):
            o.write(struct.pack("<I", len(b)) + b)
    shutil.copy(os.path.join(onyx, "user/Apps/sheet/xml.h"), os.path.join(out, "refsrc", "xml.h"))
    with io.open(os.path.join(out, "refsrc", "core.h"), "w", newline="\n") as o:
        o.write(CORE_H)


main()
