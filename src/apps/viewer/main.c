/*
 * viewer -- visionneuse d'images (BMP, PNG) : les images de UIKit (ui_image_load, ui_imagebox). Fenetre
 * redimensionnable ; l'image est ajustee a la fenetre, ou montree en taille reelle (menu Affichage).
 * Argument : le chemin d'une image ; Fichier > Ouvrir (Ctrl+O) : dialogue.
 * Port en C de viewer.asm (memes fenetre, menus, raccourcis, textes), un seul source pour le Pi et le PC ;
 * l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <stdio.h>
#include <string.h>
#include <uikit.h>

#define WIN_W		640
#define WIN_H		480
#define SB_H		24			/* barre d'etat */
#define ID_OPEN		1
#define ID_CLOSE	2
#define ID_FIT		3
#define ID_REAL		4

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static const unsigned *tokens;
static long w_img, image;
static const char *msg;			/* message d'etat (0 : la taille de l'image) */
static long win_w = WIN_W, win_h = WIN_H;
static char path[260], titlebuf[300];

static const char menus[] =
	"MFichier\n"
	"I1\tOuvrir\xE2\x80\xA6\tCtrl+O\n"
	"-\n"
	"I2\tFermer\tCtrl+W\n"
	"MAffichage\n"
	"I3\tAjuster \xC3\xA0 la fen\xC3\xAAtre\n"
	"I4\tTaille r\xC3\xA9" "elle\n";

/* la ligne d'arguments (comme ui_arg de l'asm : le premier argument sans guillemets, sinon la ligne
 * entiere ; libposix a deja coupe aux espaces et retire les guillemets) */
static void arg_line (int argc, char **argv, char *d, int cap)
{
	d[0] = 0;
	for (int i = 1; i < argc; i++)
	{
		if (i > 1)
			strncat (d, " ", (size_t) (cap - 1 - (int) strlen (d)));
		strncat (d, argv[i], (size_t) (cap - 1 - (int) strlen (d)));
	}
}

/* l'image et la barre d'etat */
static void paint (void)
{
	char t[128];
	const char *s = msg;
	ui_w_draw (0);
	ui_fill (0, win_h - SB_H, win_w, SB_H, tokens[TJ_FACE / 4]);
	if (!s)
	{
		s = TR ("Fichier > Ouvrir (Ctrl+O)");
		if (image)
		{
			long h;
			long w = uikit_image_size (image, &h, 0);
			snprintf (t, sizeof t, "%ld \xC3\x97 %ld pixels", w, h);	/* « × » */
			s = t;
		}
	}
	ui_text ((long) s, 10, win_h - 7, tokens[TJ_TEXT2 / 4], 13, (long) ui.font, 0);
	ui_update_all ();
}

/* ouvre l'image de « path » (l'ancienne rendue) ; titre, message */
static void open_path (void)
{
	long img = ui_image_load ((long) path);
	if (img)
	{
		long old = image;
		image = img;
		ui_image_free (old);
		ui_imagebox_set (w_img, image);
		msg = 0;			/* « largeur x hauteur » */
	}
	else
		msg = TR ("Image illisible (BMP ou PNG non entrelac\xC3\xA9 attendu)");
	/* « Visionneuse - nom » (le nom : apres le dernier /, comme l'asm) */
	const char *n = path;
	for (const char *p = path; *p; p++)
		if (*p == '/')
			n = p + 1;
	snprintf (titlebuf, sizeof titlebuf, "%s%s", TR ("Visionneuse - "), n);
	ui_title ((long) titlebuf);
}

/* fonction de ui_on_resize : la boite suit la fenetre */
static void on_resize (long w, long h)
{
	win_w = w;
	win_h = h;
	ui_w_move (w_img, 0, 0, w, h - SB_H);
	paint ();
}

int main (int argc, char **argv)
{
	char arg[260];
	arg_line (argc, argv, arg, sizeof arg);
	if (!uikit_open (&ui))
		return 1;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) TR ("Visionneuse"), WF_RESIZABLE) < 0)
		return 0;
	ui_menu ((long) menus);
	tokens = (const unsigned *) ui_tokens ();
	ui_min_size (240, 180);
	ui_on_resize ((long) on_resize, 0);
	w_img = ui_imagebox (0, 0, WIN_W, WIN_H - SB_H, 0, 0);	/* tout sauf la barre */
	if (arg[0])
	{
		strcpy (path, arg);
		open_path ();
	}
	paint ();
	for (;;)
	{
		long t = ui_event (1000);
		if (t == 0)
			continue;
		if (t == WEV_CLOSE)
			break;
		int cmd = 0;
		if (t == WEV_MENU)
			cmd = ui_ev_field (&ui, EV_CODE);
		else if (t == WEV_KEY && ui_ev_field (&ui, EV_KIND) && (ui_ev_field (&ui, EV_MODS) & UI_MOD_CTRL))
		{
			int k = ui_ev_field (&ui, EV_CODE);
			if (k == 0x12)
				cmd = ID_OPEN;			/* Ctrl+O */
			else if (k == 0x1A)
				cmd = ID_CLOSE;			/* Ctrl+W */
		}
		if (!cmd)
		{
			if (t != WEV_MENU)
				uikit_w_event (0, 0);
			continue;
		}
		if (cmd == ID_CLOSE)
			break;
		if (cmd == ID_OPEN)
		{
			if (ui_file_open (0, (long) path, 256))
			{
				open_path ();
				paint ();
			}
		}
		else				/* Ajuster / Taille reelle (comme l'asm : tout autre id ajuste) */
			ui_w_style (w_img, cmd == ID_REAL ? UIM_NOSCALE : 0);
	}
	return 0;
}
