/* commdlg.c -- win16 : COMMDLG (dialogues communs) sur les dialogues de Korin : Ouvrir, Enregistrer sous, Couleur.
 * Les autres (Police, Imprimer, Rechercher) rendent « annulé » en attendant. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "w16.h"
#include "be.h"

/* le filtre choisi (nFilterIndex, à partir de 1) d'une liste « Texte\0*.a;*.b\0…\0\0 » */
static void pick_filter (uint32_t lpfilter, int index, char *out, int cap)
{
	out[0] = 0;
	if (!lpfilter)
		return;
	const char *p = far_ptr (lpfilter);
	if (index < 1)
		index = 1;
	for (int i = 1; *p; i++)
	{
		const char *pat = p + strlen (p) + 1;
		if (!*pat)
			break;
		if (i == index)
		{
			snprintf (out, (size_t) cap, "%s", pat);
			break;
		}
		p = pat + strlen (pat) + 1;
	}
	if (!strcmp (out, "*.*") || !strcmp (out, "*"))
		out[0] = 0;
}

/* OPENFILENAME 16 bits (décalages) */
#define OFN_FILTER 8
#define OFN_FILTERINDEX 20
#define OFN_FILE 24
#define OFN_MAXFILE 28
#define OFN_FILETITLE 32
#define OFN_MAXFILETITLE 36
#define OFN_INITIALDIR 40
#define OFN_FLAGS 48
#define OFN_FILEOFFSET 52
#define OFN_FILEEXT 54
#define OFN_DEFEXT 56

static void file_dialog (struct w16call *c, int save)
{
	uint32_t o = far_lin (c->a[0]);
	char filter[128], dir[600], name[260], out[700];
	pick_filter (rd32 (o + OFN_FILTER), (int) rd32 (o + OFN_FILTERINDEX), filter, sizeof filter);
	uint32_t lpfile = rd32 (o + OFN_FILE);
	snprintf (name, sizeof name, "%s", lpfile ? w16_raw (lpfile) : "");
	dir[0] = 0;
	uint32_t idir = rd32 (o + OFN_INITIALDIR);
	if (idir)
		dos_to_host (w16_raw (idir), dir, sizeof dir);
	else if (strchr (name, '\\'))
	{
		/* (un chemin complet proposé : son dossier) */
		char d[260];
		snprintf (d, sizeof d, "%s", name);
		*strrchr (d, '\\') = 0;
		dos_to_host (d, dir, sizeof dir);
	}
	if (!dir[0])
		dos_to_host ("D:\\", dir, sizeof dir);
	const char *base = strrchr (name, '\\');
	base = base ? base + 1 : name;
	char base8[260];
	w16_to_utf8 ((const uint8_t *) base, -1, base8, sizeof base8);
	if (strchr (base8, '*') || strchr (base8, '?'))
		base8[0] = 0;
	int ok = save ? be_file_save (dir, base8, filter[0] ? filter : 0, out, sizeof out)
		: be_file_open (dir, filter[0] ? filter : 0, out, sizeof out);
	if (!ok)
	{
		c->ret = 0;
		return;
	}
	/* (enregistrer sans extension : celle que le programme veut) */
	uint32_t defext = rd32 (o + OFN_DEFEXT);
	const char *slash = strrchr (out, '/');
	if (save && defext && !strchr (slash ? slash : out, '.'))
	{
		size_t l = strlen (out);
		snprintf (out + l, sizeof out - l, ".%s", w16_raw (defext));
	}
	char dos[300];
	uint8_t raw[300];
	host_to_dos (out, dos, sizeof dos);
	w16_from_utf8 (dos, raw, sizeof raw);
	int max = (int) rd32 (o + OFN_MAXFILE);
	if (lpfile && max > 0)
	{
		int l = (int) strlen ((char *) raw);
		if (l > max - 1)
			l = max - 1;
		memcpy (far_ptr (lpfile), raw, (size_t) l);
		((char *) far_ptr (lpfile))[l] = 0;
	}
	const char *b = strrchr ((char *) raw, '\\');
	int off = b ? (int) (b - (char *) raw) + 1 : 0;
	const char *dot = strrchr ((char *) raw + off, '.');
	wr16 (o + OFN_FILEOFFSET, (uint16_t) off);
	wr16 (o + OFN_FILEEXT, (uint16_t) (dot ? dot - (char *) raw + 1 : (int) strlen ((char *) raw)));
	uint32_t title = rd32 (o + OFN_FILETITLE);
	int tmax = (int) rd32 (o + OFN_MAXFILETITLE);
	if (title && tmax > 0)
	{
		int l = (int) strlen ((char *) raw + off);
		if (l > tmax - 1)
			l = tmax - 1;
		memcpy (far_ptr (title), raw + off, (size_t) l);
		((char *) far_ptr (title))[l] = 0;
	}
	c->ret = 1;
}

void api_GetOpenFileName16 (struct w16call *c) { file_dialog (c, 0); }
void api_GetSaveFileName16 (struct w16call *c) { file_dialog (c, 1); }

/* GetFileTitle (chemin, tampon, taille) */
void api_GetFileTitle16 (struct w16call *c)
{
	const char *p = w16_raw (c->a[0]);
	const char *b = strrchr (p, '\\');
	b = b ? b + 1 : p;
	int n = (int16_t) c->a[2];
	if (n > 0)
		snprintf (far_ptr (c->a[1]), (size_t) n, "%s", b);
	c->ret = 0;
}

/* CHOOSECOLOR 16 bits : lStructSize, hwndOwner, hInstance, rgbResult (+8) */
void api_ChooseColor16 (struct w16call *c)
{
	uint32_t o = far_lin (c->a[0]);
	uint32_t ref = rd32 (o + 8);
	uint32_t rgb = ((ref & 0xff) << 16) | (ref & 0xff00) | ((ref >> 16) & 0xff);
	long r = be_color ("Couleur", rgb);
	if (r < 0)
	{
		c->ret = 0;
		return;
	}
	uint32_t n = (uint32_t) r;
	wr32 (o + 8, ((n >> 16) & 0xff) | (n & 0xff00) | (n & 0xff) << 16);
	c->ret = 1;
}

void api_ChooseFont16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_PrintDlg16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_CommDlgExtendedError16 (struct w16call *c) { (void) c; c->ret = 0; }

#define FIND_TERM_MARK 0x4654	/* (wParam du message de fermeture de FindText, remis à 0 à la lecture) */

/* UTF-8 -> CP1252 (les caractères de Latin-1 ; les autres en « ? ») */
static void utf8_to_ansi (const char *s, char *out, int cap)
{
	int o = 0;
	const unsigned char *p = (const unsigned char *) s;
	while (*p && o < cap - 1)
	{
		unsigned cp = *p++;
		if (cp >= 0xc0 && cp < 0xe0 && (*p & 0xc0) == 0x80)
			cp = (cp & 0x1f) << 6 | (*p++ & 0x3f);
		else if (cp >= 0xe0 && cp < 0xf0 && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80)
		{
			cp = (cp & 0x0f) << 12 | (p[0] & 0x3f) << 6 | (p[1] & 0x3f);
			p += 2;
		}
		else if (cp >= 0x80)
			cp = '?';
		out[o++] = (char) (cp == 0x20ac ? 0x80 : cp < 0x100 ? cp : '?');
	}
	out[o] = 0;
}

/* FindText (FINDREPLACE) : sous Windows, un dialogue non modal qui prévient le propriétaire par le message
 * enregistré « commdlg_FindReplace ». Ici la question de Korin (be_ask) : le texte va dans lpstrFindWhat, puis
 * le message avec FR_FINDNEXT, puis avec FR_DIALOGTERM (le dialogue se ferme) ; Suivant (F3) du programme
 * continue de marcher. FINDREPLACE 16 bits : lStructSize, hwndOwner (+4), hInstance (+6), Flags (+8),
 * lpstrFindWhat (+12), lpstrReplaceWith (+16), wFindWhatLen (+20) */
void api_FindText16 (struct w16call *c)
{
	extern uint16_t user_register_message (const char *name);
	extern int user_post (uint16_t hwnd, uint16_t msg, uint16_t wp, uint32_t lp);
	uint32_t fr = c->a[0], b = far_lin (fr);
	uint16_t owner = rd16 (b + 4);
	uint32_t what = rd32 (b + 12);
	int cap = rd16 (b + 20);
	c->ret = 0;
	if (!what || cap <= 0)
		return;
	char cur[512], ans[512];
	w16_to_utf8 (far_ptr (what), -1, cur, sizeof cur);
	snprintf (ans, sizeof ans, "%s", cur);
	uint16_t msg = user_register_message ("commdlg_FindReplace");
	uint32_t flags = rd32 (b + 8) & ~(0x08u | 0x40u);	/* (sans FR_FINDNEXT ni FR_DIALOGTERM) */
	if (be_ask ("Rechercher", "Texte à rechercher :", ans, sizeof ans) && ans[0])
	{
		char a[512];
		utf8_to_ansi (ans, a, cap < (int) sizeof a ? cap : (int) sizeof a);
		memcpy (far_ptr (what), a, strlen (a) + 1);
		wr32 (b + 8, flags | 0x08 | 0x01);	/* FR_FINDNEXT, FR_DOWN */
		user_post (owner, msg, 0, fr);
	}
	/* la fermeture : postée après, et FR_DIALOGTERM n'est posé qu'à sa lecture (commdlg_on_take) : le message de
	   la recherche, lu avant, garde FR_FINDNEXT */
	user_post (owner, msg, FIND_TERM_MARK, fr);
	c->ret = 0xfff0;	/* (une poignée de dialogue : IsDialogMessage n'y trouve rien) */
}

/* appelé par GetMessage et PeekMessage (user.c) pour chaque message pris : la fermeture de FindText */
void commdlg_on_take (uint16_t msg, uint16_t *wp, uint32_t lp)
{
	extern uint16_t user_register_message (const char *name);
	if (*wp != FIND_TERM_MARK || msg != user_register_message ("commdlg_FindReplace") || !lp)
		return;
	uint32_t b = far_lin (lp);
	wr32 (b + 8, (rd32 (b + 8) & ~0x08u) | 0x40u);	/* FR_DIALOGTERM, plus FR_FINDNEXT */
	*wp = 0;
}
