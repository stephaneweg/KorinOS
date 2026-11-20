/*
 * editor -- l'editeur de texte de Korin OS (UTF-8, police mono, numeros de ligne).
 *
 * Le texte est un widget de UIKit (ui_textarea : numeros de ligne dans une marge, barres de defilement
 * verticale et horizontale, couleurs du code pour les sources .asm / .inc, annuler / retablir,
 * presse-papiers). En haut, le chemin du fichier (ui_textbox en lecture seule : on le lit et le copie).
 * Menus Fichier et Edition dans la barre de menus globale : Nouveau (Ctrl+N), Ouvrir... (Ctrl+O, le
 * dialogue), Enregistrer (Ctrl+S ; un texte jamais enregistre passe par Enregistrer sous...), Enregistrer
 * sous... (Ctrl+Maj+S), Construire (Ctrl+B : PiBuild, /apps/builder.app, sur le dossier du fichier),
 * Fermer (Ctrl+W) ; Ctrl+F cherche, F3 cherche encore ; le widget traite Ctrl+Z, Y, X, C, V, A et le
 * reste de la frappe. Nouveau, Ouvrir et Fermer demandent d'abord s'il faut enregistrer les modifications.
 *
 * Port en C de editor.asm (memes fenetre, menus, raccourcis, textes, messages), un seul source pour le Pi
 * et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/stat.h>
#include <uikit.h>

#define WIN_W		720
#define WIN_H		500
#define TB_H		40
#define SB_H		22
#define FONT_PX		15
#define TCAP		(1024 * 1024)		/* taille max d'un fichier */
#define TABW		4

#define PATH_X		8
#define PATH_W		(WIN_W - PATH_X - 8 - FRAME_CAP_W)	/* (a gauche des boutons de la fenetre) */
#define WIN_MIN_W	360
#define WIN_MIN_H	200

enum {	ID_NEW = 1, ID_OPEN = 2, ID_SAVE = 3, ID_CLOSE = 4, ID_BUILD = 5, ID_SAVE_AS = 7,	/* Fichier */
	ID_UNDO = 10, ID_REDO = 11, ID_CUT = 12, ID_COPY = 13, ID_PASTE = 14, ID_ALL = 15, ID_FIND = 16,	/* Edition */
	ID_NEXT = 17 };

#define AREA_FLAGS	(UTF_MONO | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL | UTA_TABS)

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static char *text;			/* tampon de lecture d'un fichier */
static int modified;
static int has_file;			/* 1 : pathbuf a ete lu ou enregistre */
static const char *msg;			/* message d'etat (0 : aucun) */
static const unsigned *tokens;
static long w_path, w_area;		/* widgets : chemin, texte */
static char errbuf[32], pathbuf[260], dlgbuf[260], findbuf[132];

static const char s_default[] = "/sans titre.txt";
static const char menus[] =
	"MFichier\n"
	"I1\tNouveau\tCtrl+N\n"
	"I2\tOuvrir\xE2\x80\xA6\tCtrl+O\n"
	"I3\tEnregistrer\tCtrl+S\n"
	"I7\tEnregistrer sous\xE2\x80\xA6\tCtrl+Maj+S\n"
	"-\n"
	"I5\tConstruire\tCtrl+B\n"
	"-\n"
	"I4\tFermer\tCtrl+W\n"
	"M\xC3\x89" "dition\n"
	"I10\tAnnuler la saisie\tCtrl+Z\n"
	"I11\tR\xC3\xA9tablir la saisie\tCtrl+Y\n"
	"-\n"
	"I12\tCouper\tCtrl+X\n"
	"I13\tCopier\tCtrl+C\n"
	"I14\tColler\tCtrl+V\n"
	"I15\tTout s\xC3\xA9lectionner\tCtrl+A\n"
	"-\n"
	"I16\tRechercher\xE2\x80\xA6\tCtrl+F\n"
	"I17\tSuivant\tF3\n";

static void status (void);
static void paint (void);

/* d = destination, s = morceau de texte -> la fin : copie traduite. Les espaces autour du morceau restent
 * ici (un dictionnaire ne garde pas ceux d'une cle). */
static char *tr_cat (char *d, const char *s)
{
	char k[240];
	while (*s == ' ')
		*d++ = *s++;
	int n = 0, i = 0;			/* longueur sans les espaces de queue */
	for (; s[i] && i < 239; i++)
	{
		k[i] = s[i];
		if (s[i] != ' ')
			n = i + 1;
	}
	k[n] = 0;
	const char *t = TR (k);
	while ((*d = *t++))
		d++;
	for (s += n; (*d = *s); s++)
		d++;
	return d;
}

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

/* chemin -> son dernier composant (apres le dernier /, comme l'asm) */
static const char *base_name (const char *p)
{
	const char *b = p;
	for (; *p; p++)
		if (*p == '/')
			b = p + 1;
	return b;
}

static int ends_with (const char *s, const char *suf)	/* (casse ASCII, comme str_ends) */
{
	size_t a = strlen (s), b = strlen (suf);
	if (a < b)
		return 0;
	for (size_t i = 0; i < b; i++)
	{
		char x = s[a - b + i], y = suf[i];
		if (x >= 'A' && x <= 'Z')
			x += 32;
		if (y >= 'A' && y <= 'Z')
			y += 32;
		if (x != y)
			return 0;
	}
	return 1;
}

/* le chemin (pathbuf) dans son champ */
static void path_to_field (void)
{
	ui_w_set_text (w_path, (long) pathbuf);
}

/* le chemin choisi dans un dialogue (dlgbuf) : pathbuf et le champ */
static void dlg_to_path (void)
{
	strcpy (pathbuf, dlgbuf);
	path_to_field ();
}

/* titre : « Éditeur - nom » (+ « * » si modifie) */
static void set_title (void)
{
	char t[304];
	char *e = tr_cat (t, "\xC3\x89" "diteur - ");
	strcpy (e, base_name (pathbuf));
	if (modified)
		strcat (e, " *");
	ui_title ((long) t);
}

static void mark_modified (void)
{
	if (modified)
		return;
	modified = 1;
	set_title ();
}

/* code d'erreur -> message d'etat « Erreur -N » */
static void set_err (long code)
{
	char *e = tr_cat (errbuf, "Erreur -");
	sprintf (e, "%ld", -code);
	msg = errbuf;
}

/* style du texte : couleurs du code pour un source (.asm, .inc) */
static void set_style (void)
{
	long f = AREA_FLAGS;
	if (ends_with (pathbuf, ".asm") || ends_with (pathbuf, ".inc"))
		f |= UTA_CODE;
	ui_w_style (w_area, f);
}

/* ---------------------------------------------------------------------------
 *  Fichiers
 * ------------------------------------------------------------------------- */

static void do_new (void)
{
	ui_w_set_text (w_area, (long) "");
	modified = 0;
	has_file = 0;				/* jamais enregistre */
	pathbuf[0] = 0;
	tr_cat (pathbuf, s_default);
	path_to_field ();
	set_style ();
	set_title ();
}

static void do_open (void)
{
	struct stat st;
	long n;
	if (stat (pathbuf, &st) != 0)
		n = -errno;
	else if (st.st_size >= TCAP - 1)
	{
		msg = TR ("Fichier trop gros");
		set_title ();
		return;
	}
	else
	{
		int fd = open (pathbuf, O_RDONLY);
		if (fd < 0)
			n = -errno;
		else
		{
			n = 0;
			while (n < st.st_size)		/* (read peut rendre moins : on lit tout) */
			{
				long r = read (fd, text + n, (size_t) (st.st_size - n));
				if (r < 0)
				{
					n = -errno;
					break;
				}
				if (r == 0)
					break;
				n += r;
			}
			close (fd);
		}
		if (n >= 0)
		{
			text[n] = 0;			/* texte termine par NUL, au widget */
			set_style ();
			ui_w_set_text (w_area, (long) text);
			modified = 0;
			has_file = 1;
			msg = TR ("Fichier ouvert");
			set_title ();
			return;
		}
	}
	has_file = 0;				/* le chemin n'est pas un fichier lu */
	set_err (n);
	set_title ();
}

static void do_save (void)
{
	int fd = open (pathbuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		set_err (-errno);
		return;
	}
	long len = ui_w_text_len (w_area);
	const char *s = (const char *) ui_w_get_text (w_area);
	long w = write (fd, s, (size_t) len);
	if (w < 0)
		w = -errno;
	close (fd);
	if (w < 0)
	{
		set_err (w);
		return;
	}
	modified = 0;
	has_file = 1;
	msg = TR ("Enregistr\xC3\xA9");
	set_style ();
	set_title ();
}

/* Fichier > Construire (Ctrl+B) : PiBuild, lance detache avec le chemin absolu du fichier (il prend son
 * dossier, et le nom de ce dossier). Jamais enregistre : message ; modifie : enregistre d'abord. */
static void do_assemble (void)
{
	char abs[528], cwd[256];
	if (!has_file)
	{
		msg = TR ("Enregistrez d'abord le fichier");
		return;
	}
	if (modified)
	{
		do_save ();
		if (modified)
			return;			/* echec : le message de do_save reste */
	}
	if (pathbuf[0] == '/' || strchr (pathbuf, ':'))	/* « VOL:... » ou « /... » : deja absolu */
		strcpy (abs, pathbuf);
	else
	{					/* relatif : au dossier courant (comme l'enregistrement) */
		if (!getcwd (cwd, sizeof cwd))
			cwd[0] = 0;
		size_t l = strlen (cwd);
		snprintf (abs, sizeof abs, "%s%s%s", cwd, l && cwd[l - 1] == '/' ? "" : "/", pathbuf);
	}
	static char app[] = "/apps/builder.app/main";
	char *av[] = { app, abs, 0 };
	extern char **environ;
	pid_t pid;
	int e = posix_spawn (&pid, app, 0, 0, av, environ);
	if (e)
	{
		set_err (-e);
		return;
	}
	msg = TR ("PiBuild ouvert");
}

/* Fichier > Enregistrer sous... : le dialogue (nom proposé : celui du fichier), puis l'enregistrement
 * -> 1 si enregistré */
static int do_save_as (void)
{
	/* (un fichier déjà là : son chemin, le dialogue s'ouvre dans son dossier ; un nouveau texte : le dossier
	   personnel) */
	long ok = has_file ? ui_file_save (0, (long) pathbuf, (long) dlgbuf, 256)
			   : ui_file_save ((long) "HOME:", (long) base_name (pathbuf), (long) dlgbuf, 256);
	if (!ok)
		return 0;			/* (Annuler) */
	dlg_to_path ();
	do_save ();
	return !modified;
}

/* Fichier > Enregistrer : au chemin du fichier ; un texte jamais enregistré passe par Enregistrer sous...
 * -> 1 si enregistré */
static int save_or_ask (void)
{
	if (!has_file)
		return do_save_as ();
	do_save ();
	return !modified;
}

/* avant de quitter le texte (Nouveau, Ouvrir, Fermer) : modifié, la question (Oui : enregistrer, Non :
 * perdre, Annuler : rester) -> 1 si on peut continuer */
static int may_discard (const char *question)
{
	if (!modified)
		return 1;
	long r = ui_message ((long) TR ("\xC3\x89" "diteur"), (long) TR (question), UM_YESNOCANCEL);
	if (r == 2)				/* Non */
		return 1;
	if (r != 1)				/* Annuler, Échap */
		return 0;
	if (!save_or_ask ())			/* (l'échec ou Annuler dans le dialogue : on reste) */
	{
		status ();
		return 0;
	}
	return 1;
}

static int may_close (void) { return may_discard ("Enregistrer les modifications avant de fermer ?"); }

/* w = commande (menu ou raccourci) */
static void on_command (int c)
{
	if (c == ID_CLOSE)
	{
		if (may_close ())
			exit (0);
		return;
	}
	msg = 0;				/* efface le message d'etat */
	switch (c)
	{
	case ID_NEW:
		if (may_discard ("Enregistrer les modifications avant de commencer un nouveau texte ?"))
			do_new ();
		break;
	case ID_OPEN:				/* Ouvrir... : le dialogue */
		if (may_discard ("Enregistrer les modifications avant d'ouvrir un autre fichier ?"))
		{
			/* (le dossier du fichier ouvert, sinon le dossier personnel) */
			char dir[260];
			strcpy (dir, "HOME:");
			if (has_file && strchr (pathbuf, '/'))
			{
				snprintf (dir, sizeof dir, "%s", pathbuf);
				char *sl = strrchr (dir, '/');
				sl[sl == dir ? 1 : 0] = 0;
			}
			if (!ui_file_open ((long) dir, (long) dlgbuf, 256))
				break;
			dlg_to_path ();
			do_open ();
		}
		break;
	case ID_SAVE:
		save_or_ask ();
		break;
	case ID_SAVE_AS:
		do_save_as ();
		break;
	case ID_BUILD:
		do_assemble ();
		break;
	case ID_FIND:				/* Rechercher : le mot */
		if (!ui_ask ((long) "Rechercher", (long) TR ("Texte \xC3\xA0 chercher (sans tenir compte de la casse) :"),
			     (long) findbuf, 128))
			break;
		/* fallthrough */
	case ID_NEXT:
		if (!ui_textarea_find (w_area, (long) findbuf))
			msg = TR ("Introuvable");
		break;
	default:				/* Annuler... Tout selectionner */
	{
		int op = c - (ID_UNDO - UTE_UNDO);
		if (op >= UTE_UNDO && op <= UTE_ALL && ui_textarea_edit (w_area, op) == UN_CHANGE)
			mark_modified ();
	}
	}
	status ();
}

/* -> 1 si la touche (ui.ev) est un raccourci du programme (traite) */
static int on_key (void)
{
	if (!ui_ev_field (&ui, EV_KIND))
		return 0;
	int k = ui_ev_field (&ui, EV_CODE), c = 0;
	if (k == 0x3C)
		c = ID_NEXT;				/* F3 : suivant */
	else if (ui_ev_field (&ui, EV_MODS) & UI_MOD_CTRL)
		switch (k)
		{
		case 0x16:				/* Ctrl+S, Ctrl+Maj+S */
			c = ui_ev_field (&ui, EV_MODS) & UI_MOD_SHIFT ? ID_SAVE_AS : ID_SAVE;
			break;
		case 0x12: c = ID_OPEN; break;		/* Ctrl+O */
		case 0x11: c = ID_NEW; break;		/* Ctrl+N */
		case 0x1A: c = ID_CLOSE; break;		/* Ctrl+W */
		case 0x05: c = ID_BUILD; break;		/* Ctrl+B */
		case 0x09: c = ID_FIND; break;		/* Ctrl+F */
		}
	if (!c)
		return 0;
	on_command (c);
	return 1;
}

/* ---------------------------------------------------------------------------
 *  Affichage
 * ------------------------------------------------------------------------- */

/* fonction de ui_on_resize (taille) : le chemin et le texte suivent */
static void on_resize (long w, long h)
{
	ui_w_move (w_path, PATH_X, 6, w - (PATH_X + 8 + FRAME_CAP_W), 28);
	ui_w_move (w_area, 4, TB_H + 2, w - 8, h - (TB_H + SB_H + 6));
	paint ();
	status ();
}

/* fond, bandeau du champ chemin, widgets */
static void paint (void)
{
	ui_fill (0, 0, ui.w, ui.h, tokens[TJ_FACE / 4]);
	ui_fill (0, TB_H - 1, ui.w, 1, tokens[TJ_LINE / 4]);
	ui_w_draw (0);
	ui_update_all ();
}

/* debut de ligne, position, texte -> colonne affichee (tabulations de TABW) */
static long disp_col (long i, long end, const char *s)
{
	long c = 0;
	while (i < end)
	{
		unsigned char b = (unsigned char) s[i++];
		if (b == 13)
			continue;
		if (b == 9)
			c = (c + TABW) & ~(long) (TABW - 1);
		else if ((b & 0xC0) != 0x80)
			c++;
	}
	return c;
}

/* barre d'etat : « Ligne L, colonne C  -  N octets  -  modifie  -  message » */
static void status (void)
{
	char t[320];
	ui_fill (0, ui.h - SB_H, ui.w, SB_H, tokens[TJ_SUNKEN / 4]);
	long colb;
	long line = uikit_textarea_caret (w_area, &colb);	/* ligne, colonne en octets */
	long pos = ui_w_get_value (w_area);			/* curseur (octets) */
	long col = disp_col (pos - colb, pos, (const char *) ui_w_get_text (w_area));
	char *e = tr_cat (t, "Ligne ");
	e += sprintf (e, "%ld", line + 1);
	e = tr_cat (e, ", colonne ");
	e += sprintf (e, "%ld", col + 1);
	e = stpcpy (e, "   -   ");
	e += sprintf (e, "%ld", ui_w_text_len (w_area));
	e = tr_cat (e, " octets");
	if (modified)
		e = tr_cat (e, "   -   modifi\xC3\xA9");
	if (msg)
	{
		e = stpcpy (e, "   -   ");
		strcpy (e, msg);
	}
	ui_text_fit ((long) t, 10, ui.h - 6, tokens[TJ_TEXT2 / 4], 13, (long) ui.font, ui.w - 20);
	ui_update (0, ui.h - SB_H, ui.w, SB_H);
}

int main (int argc, char **argv)
{
	char arg[260];
	arg_line (argc, argv, arg, sizeof arg);
	if (!uikit_open (&ui))
		return 1;
	text = malloc (TCAP);			/* tampon de lecture */
	if (!text)
		return 0;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) TR ("\xC3\x89" "diteur"), WF_RESIZABLE) < 0)
		return 0;
	ui_menu ((long) menus);			/* menus dans la barre de menus globale */
	tokens = (const unsigned *) ui_tokens ();
	ui_min_size (WIN_MIN_W, WIN_MIN_H);
	ui_on_resize ((long) on_resize, 0);
	w_path = ui_textbox (PATH_X, 6, PATH_W, 28, UTF_READONLY);	/* le chemin : lu, copié, pas tapé */
	w_area = ui_textarea (4, TB_H + 2, WIN_W - 8, WIN_H - TB_H - SB_H - 6, AREA_FLAGS);	/* le texte */
	ui_textarea_font (w_area, FONT_PX);
	ui_w_focus (w_area);
	strcpy (pathbuf, arg[0] ? arg : TR (s_default));	/* chemin : argument ou defaut */
	path_to_field ();
	paint ();
	if (arg[0])
		do_open ();
	else
		set_title ();
	status ();
	for (;;)
	{
		long t = ui_event (1000);
		if (t == 0)
			continue;
		if (t == WEV_CLOSE)
		{
			if (may_close ())
				break;
			continue;
		}
		if (t == WEV_MENU)
		{
			on_command (ui_ev_field (&ui, EV_CODE));
			continue;
		}
		if (t == WEV_KEY && on_key ())		/* raccourcis du programme */
			continue;
		long code, taken;
		long w = uikit_w_event (&code, &taken);
		if (!taken)
			continue;
		if (w == w_path)		/* (lecture seule : sélection, copie) */
			continue;
		if (code == UN_CHANGE)
		{				/* le texte change : modifie */
			msg = 0;
			mark_modified ();
		}
		status ();			/* (curseur deplace) */
	}
	return 0;
}
