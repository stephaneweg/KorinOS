/*
 * welcome -- la fenetre d'accueil : presentation, echo du clavier, bouton Fermer.
 * Port en C de welcome.asm (meme fenetre, memes textes, memes touches), un seul source pour le Pi
 * et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <uikit.h>

#define WIN_W		560
#define WIN_H		340
#define ID_CLOSE	1			/* entree « Fermer » du menu Fichier */
#define KEY_W_		0x1A			/* code HID de W */

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static unsigned tok (int off) { return ((const unsigned *) ui_tokens ())[off / 4]; }

static char typed[64];
static int typed_len;

static void draw_typed (void)
{
	ui_fill (24, 218, WIN_W - 48, 32, tok (TJ_SURFACE));
	ui_frame (24, 218, WIN_W - 48, 32, tok (TJ_LINE_STRONG));
	ui_text ((long) typed, 32, 240, tok (TJ_TEXT1), 16, (long) ui.font_m, 0);
	ui_update (24, 218, WIN_W - 48, 32);
}

static void draw (void)
{
	ui_fill (0, 0, WIN_W, 70, tok (TJ_ACCENT));		/* bandeau */
	ui_text ((long) "Korin OS", 24, 46, 0x00FFFFFF, 26, (long) ui.font_b, 0);
	ui_text ((long) TR ("Un syst\xC3\xA8me 64 bits pour Raspberry Pi 4,"), 24, 108, tok (TJ_TEXT1), 16, (long) ui.font, 0);
	ui_text ((long) TR ("\xC3\xA9" "crit enti\xC3\xA8rement en assembleur AArch64 (FASMARM)."), 24, 134, tok (TJ_TEXT1), 16,
		 (long) ui.font, 0);
	ui_text ((long) TR ("SMP 4 coeurs, pr\xC3\xA9" "emptif, process isol\xC3\xA9s, FAT32, TrueType maison."), 24, 160,
		 tok (TJ_TEXT2), 14, (long) ui.font, 0);
	ui_text ((long) TR ("Tapez quelque chose :"), 24, 206, tok (TJ_TEXT1), 14, (long) ui.font_b, 0);
	ui_button (WIN_W - 130, WIN_H - 52, 110, 34, (long) TR ("Fermer"), 1);
	draw_typed ();
	ui_update_all ();
}

/* un caractere tape : ajoute en UTF-8 (40 octets au plus : la ligne repart a zero), Retour arriere
 * efface le dernier caractere */
static void on_char (unsigned c)
{
	int n = typed_len;
	if (c == 8)
	{
		if (n == 0)
			return;
		do
			n--;				/* remonter les octets de suite UTF-8 (10xxxxxx) */
		while (n > 0 && ((unsigned char) typed[n] & 0xC0) == 0x80);
	}
	else if (c < ' ')
		return;					/* autres codes de controle : ignores */
	else
	{
		if (n >= 40)
			n = 0;
		if (c < 0x80)
			typed[n++] = (char) c;
		else if (c < 0x800)
		{
			typed[n++] = (char) (0xC0 | (c >> 6));
			typed[n++] = (char) (0x80 | (c & 0x3F));
		}
		else
		{					/* (comme l'asm : trois octets au plus) */
			typed[n++] = (char) (0xE0 | ((c >> 12) & 0xFF));
			typed[n++] = (char) (0x80 | ((c >> 6) & 0x3F));
			typed[n++] = (char) (0x80 | (c & 0x3F));
		}
	}
	typed[n] = 0;
	typed_len = n;
	draw_typed ();
}

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) TR ("Bienvenue sur Korin OS"), WF_GLYPH_LIGHT) < 0)
		return 0;
	ui_menu ((long) "MFichier\nI1\tFermer\tCtrl+W\n");	/* menus dans la barre de menus globale */
	draw ();
	for (;;)
	{
		long t = ui_event (1000);
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MENU)
		{
			if (ui_ev_field (&ui, EV_CODE) == ID_CLOSE)
				break;
		}
		else if (t == WEV_MOUSE)
		{
			if (ui_ev_field (&ui, EV_KIND) == 1 && ui_hit (WIN_W - 130, WIN_H - 52, 110, 34))
				break;
		}
		else if (t == WEV_KEY)
		{
			if (ui_ev_field (&ui, EV_KIND) == 0)
				continue;			/* relache */
			/* Ctrl+W : Fermer (le raccourci du menu est traite ici) */
			if ((ui_ev_field (&ui, EV_MODS) & UI_MOD_CTRL) && ui_ev_field (&ui, EV_CODE) == KEY_W_)
				break;
			unsigned c = (unsigned) ui_ev_field (&ui, EV_CHAR);
			if (c)
				on_char (c);
		}
	}
	return 0;
}
