/* uk_lex.c -- UIKit : les couleurs de C# et de XML du texte multiligne
 * (UTA_CS, UTA_XML), un morceau à la fois. Port ligne à ligne de
 * uikit_lex.inc (src/kits/uikit/PORT.md).
 *
 * Un commentaire, une chaîne ou une balise peut tenir sur plusieurs lignes :
 * l'état au début de la première ligne visible se calcule depuis le début du
 * texte (uk_lex_start), puis suit les lignes dessinées (UW_TA_LEX).
 *
 *  C#  : 0, 1 dans un commentaire, 2 dans une chaîne @, 3 dans trois guillemets
 *  XML : 0 texte, 1 dans une balise, 2 dans un commentaire, 3 dans " ", 4 dans ' '
 *
 * Les registres de l'analyse de l'asm (x9 texte, x10 position, x11 fin, x12
 * état, x14 genre du morceau) sont les champs de « struct lex ». */
#include "uk.h"

struct lex {
	long text;  /* x9 */
	long pos;   /* x10 */
	long end;   /* x11 */
	long state; /* x12 */
	long kind;  /* x14 */
};

/* uk_lex_peek : x0 = décalage depuis la position -> l'octet, 0 après la fin */
static long lex_peek(struct lex *l, long d)
{
	long p = l->pos + d;
	if (UGE(p, l->end))
		return 0;
	return LDB(l->text, p);
}

/* uk_lex_name : w0 = octet -> 1 s'il fait partie d'un nom */
static long lex_name(long c)
{
	u32 b = (u32)c;
	if (b >= 0x80 || b - '0' < 10 || (b | 0x20) - 'a' < 26 || b == '_')
		return 1;
	return 0;
}

/* uk_lex_rest : position : un de plus, puis jusqu'à la fin d'un nom */
static void lex_rest(struct lex *l)
{
	for (;;) {
		l->pos++;
		if (UGE(l->pos, l->end))
			return;
		if (!lex_name(LDB(l->text, l->pos)))
			return;
	}
}

/* uk_lex_eol : position : jusqu'à la fin de la ligne */
static void lex_eol(struct lex *l)
{
	while (ULT(l->pos, l->end) && LDB(l->text, l->pos) != 10)
		l->pos++;
}

/* -- C# ---------------------------------------------------------------------- */

/* uk_lex_cs */
static void lex_cs(struct lex *l)
{
	long c = LDB(l->text, l->pos), nx, x6, x7, w4;

	if (c == 10)
		goto blank;
	if (l->state)
		goto lng;
	if (c == ' ' || c == 9 || c == 13)
		goto blank;
	nx = lex_peek(l, 1); /* le suivant */
	if (c == '/') {
		if (nx == '/') {
			lex_eol(l);
			l->kind = UKC_COMMENT;
			return;
		}
		if (nx != '*')
			goto punct;
		l->pos += 2;
		l->state = 1;
		goto lng;
	}
	if (c == '#') { /* #region... : seul en tête de ligne */
		x6 = l->pos;
		for (;;) {
			long b;
			if (!x6)
				break;
			x6--;
			b = LDB(l->text, x6);
			if (b == 10)
				break;
			if (b == ' ' || b == 9)
				continue;
			goto punct;
		}
		lex_eol(l);
		l->kind = UKC_LABEL;
		return;
	}
	if (c == '@' || c == '$') {
		x7 = 1; /* @" $" $@" @$" */
		w4 = c;
		if (nx == '@' || nx == '$') {
			w4 |= nx; /* (@ | $ = 0x64, @ seul 0x40, $ seul 0x24) */
			x7 = 2;
		}
		if (lex_peek(l, x7) == '"') {
			/* comme l'asm : x4 (le style) a été remplacé par le préfixe
			 * (mov w4,w5) ; « tbnz x4,6 » teste son bit 6 : un @ */
			if (w4 & 0x40) {
				x7++;
				l->pos += x7;
				l->state = 2;
				goto lng;
			}
			l->pos += x7;
			c = '"';
			goto str;
		}
		if (c != '@') /* @mot */
			goto punct;
		lex_rest(l);
		l->kind = UKC_WORD;
		return;
	}
	if (c == 0x27)
		goto str;
	if (c == '"') {
		if (nx != '"')
			goto str;
		if (lex_peek(l, 2) != '"')
			goto str;
		l->pos += 3;
		l->state = 3;
		goto lng;
	}
	if ((u32)(c - '0') < 10) {
		for (;;) {
			long b;
			l->pos++;
			if (UGE(l->pos, l->end))
				break;
			b = LDB(l->text, l->pos);
			if (b == '.')
				continue;
			if (!lex_name(b))
				break;
		}
		l->kind = UKC_NUMBER;
		return;
	}
	if (!lex_name(c))
		goto punct;
	lex_rest(l);
	l->kind = UKC_WORD;
	return;

str: /* chaîne : \ passe le suivant */
	l->pos++;
	for (;;) {
		long b;
		if (UGE(l->pos, l->end))
			break;
		b = LDB(l->text, l->pos);
		if (b == 10)
			break;
		l->pos++;
		if (b == 0x5C) {
			if (UGE(l->pos, l->end))
				break;
			if (LDB(l->text, l->pos) == 10)
				break;
			l->pos++;
			continue;
		}
		if (b == c)
			break;
	}
	l->kind = UKC_STRING;
	return;
punct:
	l->pos++;
	l->kind = UKC_PUNCT;
	return;
blank:
	l->pos++;
	l->kind = -1;
	return;
lng: /* un morceau long, jusqu'à sa fin ou la fin de la ligne */
	l->kind = UKC_STRING;
	if (l->state == 1)
		l->kind = UKC_COMMENT;
	for (;;) {
		long b;
		if (UGE(l->pos, l->end))
			return;
		b = LDB(l->text, l->pos);
		if (b == 10)
			return;
		nx = lex_peek(l, 1);
		if (l->state == 1) {
			if (b == '*' && nx == '/') {
				l->pos += 2;
				break;
			}
		} else if (b == '"') {
			if (l->state == 2) {
				if (nx == '"') { /* @"" : un guillemet */
					l->pos += 2;
					continue;
				}
				l->pos += 1;
				break;
			}
			if (nx == '"' && lex_peek(l, 2) == '"') {
				l->pos += 3;
				break;
			}
		}
		l->pos++;
	}
	l->state = 0; /* fermé */
}

/* x0 = mot, x1 = longueur -> 1 : mot-clé de C#, avec la casse */
static const char cs_words[] =
	"abstract\0as\0async\0await\0base\0bool\0break\0"
	"byte\0case\0catch\0char\0checked\0class\0const\0"
	"continue\0decimal\0default\0delegate\0do\0double\0"
	"dynamic\0else\0enum\0event\0explicit\0extern\0"
	"false\0finally\0fixed\0float\0for\0foreach\0get\0"
	"global\0goto\0if\0implicit\0in\0init\0int\0"
	"interface\0internal\0is\0lock\0long\0namespace\0"
	"new\0nint\0not\0nuint\0null\0object\0operator\0"
	"or\0out\0override\0params\0partial\0private\0"
	"protected\0public\0readonly\0record\0ref\0required\0"
	"return\0sbyte\0sealed\0set\0short\0sizeof\0"
	"stackalloc\0static\0string\0struct\0switch\0this\0"
	"throw\0true\0try\0typeof\0uint\0ulong\0unchecked\0"
	"unsafe\0ushort\0using\0value\0var\0virtual\0void\0"
	"volatile\0when\0where\0while\0with\0yield\0"; /* (+ le NUL final : un vide) */

long uk_cs_word(long word, long len)
{
	long p = S(cs_words), i, c;

	while (LDB(p, 0)) {
		for (i = 0;; i++) {
			c = LDB(p, i);
			if (!c) {
				if (i == len)
					return 1;
				break;
			}
			if (UGE(i, len))
				break;
			if (LDB(word, i) != c)
				break;
		}
		while (LDB(p, 0)) /* .skip : jusqu'après le NUL du mot */
			p++;
		p++;
	}
	return 0;
}

/* -- XML --------------------------------------------------------------------- */

/* uk_lex_tagch : w0 = octet -> 1 s'il fait partie d'un nom XML (: - . en plus) */
static long lex_tagch(long c)
{
	u32 b = (u32)c;
	if (b == ':' || b == '-' || b == '.')
		return 1;
	return lex_name(c);
}

/* uk_lex_tagname0 : jusqu'à la fin du nom */
static void lex_tagname0(struct lex *l)
{
	while (ULT(l->pos, l->end) && lex_tagch(LDB(l->text, l->pos)))
		l->pos++;
}

/* uk_lex_tagname : position sur le < (ou le / qui le suit) : jusqu'à la fin
 * du nom */
static void lex_tagname(struct lex *l)
{
	l->pos++;
	lex_tagname0(l);
}

/* uk_lex_xml */
static void lex_xml(struct lex *l)
{
	long c = LDB(l->text, l->pos), nx, q;

	if (c == 10)
		goto blank;
	if (l->state == 2)
		goto cmnt;
	if (UGT(l->state, 2))
		goto value;
	if (l->state)
		goto tag;
	if (c == ' ' || c == 9 || c == 13) /* texte */
		goto blank;
	if (c == '<') {
		nx = lex_peek(l, 1);
		if (nx == '!' && lex_peek(l, 2) == '-' && lex_peek(l, 3) == '-') {
			l->pos += 4;
			l->state = 2;
			goto cmnt;
		}
		if (nx == '/' || nx == '?' || nx == '!') /* <nom </nom <?nom <!nom */
			l->pos++;
		lex_tagname(l);
		l->state = 1;
		l->kind = UKC_KEYWORD;
		return;
	}
	if (c == '&') { /* &entité; */
		for (;;) {
			long b;
			l->pos++;
			if (UGE(l->pos, l->end))
				break;
			b = LDB(l->text, l->pos);
			if (b == ';') {
				l->pos++;
				break;
			}
			if (b == '<')
				break;
			if (ULE(b, ' '))
				break;
		}
		l->kind = UKC_NUMBER;
		return;
	}
	for (;;) {
		long b;
		l->pos++;
		if (UGE(l->pos, l->end))
			break;
		b = LDB(l->text, l->pos);
		if (b == 10 || b == '<' || b == '&')
			break;
	}
	l->kind = UKC_TEXT;
	return;

tag: /* dans une balise */
	if (c == ' ' || c == 9 || c == 13)
		goto blank;
	if (c == '>')
		goto close1;
	if (c == '/' || c == '?') {
		if (lex_peek(l, 1) != '>')
			goto punct;
		l->pos++;
		goto close1;
	}
	if (c == '"') {
		l->state = 3;
		l->pos++;
		goto value;
	}
	if (c == 0x27) {
		l->state = 4;
		l->pos++;
		goto value;
	}
	if (!lex_tagch(c))
		goto punct;
	lex_tagname0(l);
	l->kind = UKC_REGISTER;
	return;
close1:
	l->pos++;
	l->state = 0;
	l->kind = UKC_PUNCT;
	return;
value: /* valeur d'un attribut (comme l'asm : un état > 4 attend ') */
	q = l->state == 3 ? '"' : 0x27;
	for (;;) {
		long b;
		if (UGE(l->pos, l->end))
			break;
		b = LDB(l->text, l->pos);
		if (b == 10)
			break;
		l->pos++;
		if (b == q) {
			l->state = 1;
			break;
		}
	}
	l->kind = UKC_STRING;
	return;
cmnt: /* jusqu'à --> */
	for (;;) {
		long b;
		if (UGE(l->pos, l->end))
			break;
		b = LDB(l->text, l->pos);
		if (b == 10)
			break;
		if (b == '-' && lex_peek(l, 1) == '-' && lex_peek(l, 2) == '>') {
			l->pos += 3;
			l->state = 0;
			break;
		}
		l->pos++;
	}
	l->kind = UKC_COMMENT;
	return;
punct:
	l->pos++;
	l->kind = UKC_PUNCT;
	return;
blank:
	l->pos++;
	l->kind = -1;
}

/* x0 = texte, x1 = position (< x2), x2 = fin, x3 = état, x4 = style -> r0 =
 * fin du morceau, r1 = UKC_* (-1 : blanc), r2 = état. Un morceau s'arrête
 * toujours à la fin de la ligne. */
ukr uk_lex(long text, long pos, long end, long state, long style)
{
	struct lex l;
	ukr r;

	l.text = text;
	l.pos = pos;
	l.end = end;
	l.state = state;
	l.kind = 0;
	if (style & UTA_XML)
		lex_xml(&l);
	else
		lex_cs(&l);
	r.r0 = l.pos;
	r.r1 = l.kind;
	r.r2 = l.state;
	r.r3 = 0;
	return r;
}

/* x0 = widget : UW_TA_LEX au début de la première ligne visible */
long uk_lex_start(long w)
{
	long start, pos = 0, state = 0;

	if (!(LDX(w, UW_STYLE) & (UTA_CS | UTA_XML)))
		return 0;
	start = uk_ta_line(w, LDX(w, UW_POS)).r0; /* début de la ligne */
	while (ULT(pos, start)) {
		ukr r = uk_lex(LDX(w, UW_TEXT), pos, LDX(w, UW_TLEN), state, LDX(w, UW_STYLE));
		pos = r.r0;
		state = r.r2;
	}
	STX(w, UW_TA_LEX, state);
	return 0;
}
