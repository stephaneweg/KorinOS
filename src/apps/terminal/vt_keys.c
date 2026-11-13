/*
 * vt_keys.c -- le Terminal : touches -> octets pour le programme (xterm) ; port de vt_keys.inc.
 *
 * Les octets rendus par key_encode sont ecrits sur le bout maitre du pty (service vterm, docs/VTERM.md) :
 * la discipline de ligne les traite.
 *
 * Fleches ESC [ A..D (ESC O A..D en mode ?1), Debut / Fin ESC [ H / F, Inser / Suppr / Page haut /
 * Page bas ESC [ 2~ 3~ 5~ 6~, F1-F4 ESC O P..S, F5-F12 ESC [ 15~ .. 24~ ; avec Maj, Alt, Ctrl :
 * ESC [ 1 ; m X ou ESC [ n ; m ~ (m = 1 + Maj + 2 Alt + 4 Ctrl). Echap 0x1B, Tab 9, Maj+Tab ESC [ Z,
 * Retour arriere 0x7F, Entree CR, Ctrl+lettre 1..26, Ctrl+Espace 0, Ctrl+[ \ ] 27..29 ; texte en
 * UTF-8 ; Alt+touche simple : ESC devant (pour un caractere : Alt gauche seul, Alt droite etant AltGr).
 */
#include "term.h"

/* touches speciales : code HID, genre (1 CSI final, 2 CSI n ~, 3 SS3 final), final ou n */
static const unsigned char kk_tab[][3] = {
	{ 0x52, 1, 'A' }, { 0x51, 1, 'B' }, { 0x4F, 1, 'C' }, { 0x50, 1, 'D' },
	{ 0x4A, 1, 'H' }, { 0x4D, 1, 'F' },
	{ 0x49, 2, 2 }, { 0x4C, 2, 3 }, { 0x4B, 2, 5 }, { 0x4E, 2, 6 },
	{ 0x3A, 3, 'P' }, { 0x3B, 3, 'Q' }, { 0x3C, 3, 'R' }, { 0x3D, 3, 'S' },
	{ 0x3E, 2, 15 }, { 0x3F, 2, 17 }, { 0x40, 2, 18 }, { 0x41, 2, 19 },
	{ 0x42, 2, 20 }, { 0x43, 2, 21 }, { 0x44, 2, 23 }, { 0x45, 2, 24 },
};

/* 0..99 en decimal */
static unsigned char *put_dec (unsigned char *o, unsigned v)
{
	if (v >= 10)
	{
		*o++ = (unsigned char) ('0' + v / 10);
		v %= 10;
	}
	*o++ = (unsigned char) ('0' + v);
	return o;
}

/* un point de code en UTF-8 (comme utf8_put d'AppKit) */
static unsigned char *put_utf8 (unsigned char *o, unsigned cp)
{
	if (cp < 0x80)
		*o++ = (unsigned char) cp;
	else if (cp < 0x800)
	{
		*o++ = (unsigned char) (0xC0 | cp >> 6);
		*o++ = (unsigned char) (0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		*o++ = (unsigned char) (0xE0 | cp >> 12);
		*o++ = (unsigned char) (0x80 | (cp >> 6 & 0x3F));
		*o++ = (unsigned char) (0x80 | (cp & 0x3F));
	}
	else
	{
		*o++ = (unsigned char) (0xF0 | cp >> 18);
		*o++ = (unsigned char) (0x80 | (cp >> 12 & 0x3F));
		*o++ = (unsigned char) (0x80 | (cp >> 6 & 0x3F));
		*o++ = (unsigned char) (0x80 | (cp & 0x3F));
	}
	return o;
}

/* code HID, caractere, modificateurs, tampon (16 o) -> octets ecrits (0 : rien a envoyer) */
int key_encode (unsigned code, unsigned ch, unsigned mods, unsigned char *buf)
{
	unsigned char *o = buf;
	unsigned m = 1;					/* parametre de modificateurs */
	if (mods & K_MOD_SHIFT)
		m += 1;
	if (mods & K_MOD_ALT)
		m += 2;
	if (mods & K_MOD_CTRL)
		m += 4;
	for (unsigned i = 0; i < sizeof kk_tab / sizeof kk_tab[0]; i++)
	{
		if (kk_tab[i][0] != code)
			continue;
		unsigned kind = kk_tab[i][1], f = kk_tab[i][2];
		*o++ = 0x1B;
		if (kind == 2)
		{					/* ESC [ n (; m) ~ */
			*o++ = '[';
			o = put_dec (o, f);
			if (m != 1)
			{
				*o++ = ';';
				o = put_dec (o, m);
			}
			*o++ = '~';
		}
		else if (m != 1)
		{					/* ESC [ 1 ; m X */
			*o++ = '[';
			*o++ = '1';
			*o++ = ';';
			o = put_dec (o, m);
			*o++ = (unsigned char) f;
		}
		else
		{					/* SS3 : F1-F4, fleches en mode ?1 */
			*o++ = kind == 3 || (vt.modes & VM_APPCK) ? 'O' : '[';
			*o++ = (unsigned char) f;
		}
		return (int) (o - buf);
	}
	int one = -1;
	if (code == KEY_ENTER)
		one = 0x0D;
	else if (code == KEY_ESC)
		one = 0x1B;
	else if (code == KEY_BACKSPACE)
		one = 0x7F;
	else if (code == KEY_TAB)
	{
		if (!(mods & K_MOD_SHIFT))
			one = 9;
		else
		{					/* Maj+Tab : ESC [ Z */
			*o++ = 0x1B;
			*o++ = '[';
			*o++ = 'Z';
			return (int) (o - buf);
		}
	}
	else if (mods & K_MOD_CTRL)
	{
		unsigned l = (ch | 0x20) - 'a';		/* lettre (le caractere, sinon la place HID) */
		if (l >= 26)
			l = code - 4;			/* HID 0x04..0x1D : a..z */
		if (l < 26)
			one = (int) l + 1;
		else if (ch == ' ' || code == 0x2C)	/* (Espace) */
			one = 0;
		else if (ch == '[')
			one = 27;
		else if (ch == '\\')
			one = 28;
		else if (ch == ']')
			one = 29;
	}
	if (one >= 0)
	{
		if (mods & K_MOD_ALT)			/* Alt : ESC devant */
			*o++ = 0x1B;
		*o++ = (unsigned char) one;
		return (int) (o - buf);
	}
	if (ch < 0x20)
		return (int) (o - buf);
	if (mods & K_MOD_LALT)				/* (Alt droite : AltGr, le caractere tel quel) */
		*o++ = 0x1B;
	o = put_utf8 (o, ch);
	return (int) (o - buf);
}
