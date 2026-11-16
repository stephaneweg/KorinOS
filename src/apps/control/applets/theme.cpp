/*
 * applets/theme.cpp -- le volet Thème du panneau de configuration : le thème du bureau (/etc/theme.txt, clés
 * d'Onyx) et sa couleur d'accent, avec un aperçu (un petit bureau dans les couleurs du thème coché) et les pastilles
 * des huit accents de UIKit. Appliquer écrit le fichier et publie korin.theme (busd, docs/BUS.md) : UIKit le relit
 * dans chaque programme et redessine cadres et widgets ; le fond, la barre de menus et le panel se redessinent
 * (WEV_THEME). Sans busd : ces trois-là sont relancés, les autres applications le prendront à leur prochain
 * lancement.
 *
 * Port en C++ de applets/theme.inc (Korin). Le volet Thème d'Onyx (user/Apps/theme/main.cpp : un aperçu à la
 * Windows 98, une couleur par partie, les fonds d'écran) n'est pas repris : Korin n'a que deux thèmes (Givre,
 * Onyx Noir) et un accent, et c'est ce que l'utilisateur a validé. Le petit bureau de l'aperçu en garde l'idée.
 */
#include <armpi/userconf.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <buskit_abi.h>
#include "../control.h"

enum
{
	TH_N = 2,				/* thèmes */
	TH_PW = COL_W - 8,			/* aperçu */
	TH_PH = 150,
	TH_NACC = 8,				/* couleurs d'accent (uk_accents de UIKit) */
	TH_AD = 22,				/* leur pastille */
	TH_AP = 30,				/* leur pas */
	TH_AY = AP_HEAD + 8 + TH_PH + 58,	/* haut des pastilles, pour le clic (comme l'asm : 8 px sous le dessin) */
	TH_TXT = 2048,
};

/* ---- partagés avec le volet Écran (control.h) ---- */
const char *const theme_topic = "korin.theme";	/* BUS_TOPIC_THEME (src/inc/bus.inc) : « <thème>, <accent> » */
char theme_bus[64];				/* sa valeur */

static const char *const th_names[TH_N] = { "Givre", "Onyx Noir" };
/* les lanceurs du dock de l'aperçu (le 2e prend l'accent ; en dur comme l'asm : c'est un dessin) */
static const uint32_t th_launchers[4] = { 0x00E25B4B, 0, 0x0030A46C, 0x00F2A93B };
static const char *const th_acc_names[TH_NACC] = {	/* (les noms de uk_accents, UIKit) */
	"bleu", "orange", "vert", "jaune", "violet", "fuchsia", "sarcelle", "graphite",
};
static const uint32_t th_acc_colors[TH_NACC][2] = {	/* (leurs couleurs : Givre, Onyx Noir ; celles de UIKit) */
	{ 0x002563D8, 0x002F72DC }, { 0x00CF3F0A, 0x00D04510 }, { 0x005FBF12, 0x00A6E22E }, { 0x00F2B705, 0x00FFD02E },
	{ 0x007340E0, 0x007E52E6 }, { 0x00C2188F, 0x00D12A9C }, { 0x000B7F7A, 0x000D827C }, { 0x003E4A5B, 0x00A4AEBD },
};
static const char *const th_acc_labels[TH_NACC] = {
	"Bleu", "Rouge orang\xC3\xA9", "Vert vif", "Jaune", "Violet", "Fuchsia", "Sarcelle", "Graphite",
};
static const char th_s_title[] = "Th\xC3\xA8me du bureau";
static const char th_s_note[] = "Certaines applications ouvertes ne le";
static const char th_s_note2[] = "prendront qu'\xC3\xA0 leur relance.";
static const char th_s_preview[] = "Aper\xC3\xA7u";
static const char th_s_done[] = "Th\xC3\xA8me appliqu\xC3\xA9.";
static const char th_s_accent[] = "Couleur d'accent";
static const char th_header[] =
	"# Le look du bureau (cles d'Onyx), ecrit par le panneau de configuration" CRLF
	"# (applet Theme). Autres cles : voir docs/DESIGN-ELEGANT.md." CRLF
	"theme = ";

static long th_acc;				/* l'accent choisi */
static char th_current[32];			/* le thème du fichier (puis celui appliqué) */
static char th_file[512];
static char th_actual[TH_TXT];			/* theme.txt tel quel */
static char th_buf[TH_TXT];
static uint32_t th_j[TJ_SIZE / 4];		/* les jetons du thème coché (aperçu) */

/* dst, src, n : copie de n - 1 octets au plus (str_copy_n de l'asm) -> la fin */
static char *copy_n (char *dst, const char *src, size_t n)
{
	size_t i = 0;
	for (; i + 1 < n && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
	return dst + i;
}

/* -> 1 si égales sans tenir compte des capitales (ASCII, comme ci_eq de l'asm : « | 0x20 ») */
static int ci_eq (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		if ((*a | 0x20) != (*b | 0x20))
			return 0;
		if (!*a)
			return 1;
	}
}

/* (ini_read) « theme », « accent » */
static void th_line (char *key, char *val, void *)
{
	if (!strcmp (key, "theme"))
		copy_n (th_current, val, sizeof th_current);
	else if (!strcmp (key, "accent"))
		for (long i = 0; i < TH_NACC; i++)
			if (ci_eq (th_acc_names[i], val))
			{
				th_acc = i;
				break;
			}
}

/* th_actual <- /etc/theme.txt (vide : sans fichier) */
static void th_read (void)
{
	th_actual[0] = 0;
	char cp_[160];
	int fd = open (korin_conf_path ("theme.txt", cp_, sizeof cp_, 0), O_RDONLY);
	if (fd < 0)
		return;
	long n = read (fd, th_actual, TH_TXT - 1);
	th_actual[n > 0 ? n : 0] = 0;
	close (fd);
}

/* le thème du panneau redevient celui du fichier (th_actual) */
static void th_back (void)
{
	copy_n (th_buf, th_actual, TH_TXT);
	ui_theme_text ((long) th_buf);
}

/* (ap.extra) x, y = coin, w = largeur : un petit bureau dans les couleurs du
 * thème coché (ses jetons : pris un instant par le panneau, puis rendus), le nom, les pastilles d'accent */
static void th_preview (long x, long y, long)
{
	long c = applet_choice (0);
	if (c < 0)
		return;
	char *p = stpcpy (th_buf, "theme = ");	/* « theme = <nom> », « accent = <nom> » */
	p = stpcpy (p, th_names[c]);
	p = stpcpy (p, "\naccent = ");
	stpcpy (p, th_acc_names[th_acc]);
	ui_theme_text ((long) th_buf);
	memcpy (th_j, (const void *) ui_tokens (), TJ_SIZE);	/* ses jetons, copiés */
	th_back ();
	const uint32_t *j = th_j;
#define J(t) (j[(t) / 4])
	uint32_t bg = J (TJ_DARK) ? 0x001A1E26 : 0x00D6DEE9;	/* le fond calme (voronoy), selon le thème (en dur,
								 * comme l'asm) */
	ui_rbox (x, y, TH_PW, TH_PH, 6, bg, bg);
	uint32_t bar = J (TJ_BAR) & 0xFFFFFF;	/* barre de menus */
	ui_rbox (x + 6, y + 6, TH_PW - 12, 12, 4, bar, bar);
	uint32_t ink = (uint32_t) ui_ink (bar);	/* ses menus : traits d'encre */
	for (long m = 0; m < 3; m++)
		ui_fill (x + 14 + m * 22, y + 11, 14, 2, ink);
	ui_rbox (x + 20, y + 26, TH_PW - 40, TH_PH - 62, 6, J (TJ_LINE_STRONG), J (TJ_LINE_STRONG));	/* fenêtre : contour,
													 * titre, corps */
	ui_rbox (x + 21, y + 27, TH_PW - 42, TH_PH - 64, 5, J (TJ_FACE), J (TJ_FACE));
	uint32_t title = J (TJ_TITLE_ACTIVE) & 0xFFFFFF;
	ui_fill (x + 22, y + 28, TH_PW - 44, 14, title);
	ui_rbox (x + TH_PW - 38, y + 32, 7, 7, 3, J (TJ_DANGER), J (TJ_DANGER));	/* ses boutons */
	ui_fill (x + 30, y + 34, 40, 2, ui_ink (title));	/* son titre : un trait d'encre */
	ui_rbox (x + 30, y + 52, TH_PW - 60, 26, 4, J (TJ_SURFACE), J (TJ_SURFACE));	/* un champ */
	for (long l = 0; l < 2; l++)		/* son texte */
		ui_fill (x + 36, y + 58 + l * 10, TH_PW - 110 - l * 16, 3, J (l ? TJ_TEXT3 : TJ_TEXT1));
	ui_rbox (x + TH_PW - 112, y + 86, 38, 14, 4, J (TJ_RAISED), J (TJ_RAISED_LOW));	/* deux boutons, le principal
											 * accentué */
	ui_rbox (x + TH_PW - 68, y + 86, 38, 14, 4, J (TJ_ACCENT), J (TJ_ACCENT));
	uint32_t dock = J (TJ_DOCK) & 0xFFFFFF;	/* le dock et ses lanceurs */
	ui_rbox (x + TH_PW / 2 - 44, y + TH_PH - 24, 88, 18, 6, dock, dock);
	for (long d = 0; d < 4; d++)
	{
		uint32_t c2 = d == 1 ? J (TJ_ACCENT) : th_launchers[d];
		ui_rbox (x + d * 20 + TH_PW / 2 - 38, y + TH_PH - 21, 12, 12, 3, c2, c2);
	}
#undef J
	/* (les jetons du panneau, à nouveau) « Aperçu », sous le dessin */
	ui_text ((long) TR (th_s_preview), x, y + TH_PH + 18, tok (TJ_TEXT3), 12, F (), 0);
	ui_text ((long) TR (th_s_accent), x, y + TH_PH + 44, tok (TJ_TEXT1), 13, FB (), 0);	/* l'accent : ses pastilles */
	long dark = applet_choice (0) == 1;	/* (la colonne du thème coché) */
	for (long i = 0; i < TH_NACC; i++)
	{
		long px = x + i * TH_AP;
		if (th_acc == i)		/* choisie : un anneau */
		{
			ui_rbox (px - 3, y + TH_PH + 50 - 3, TH_AD + 6, TH_AD + 6, 14, tok (TJ_TEXT1), tok (TJ_TEXT1));
			ui_rbox (px - 1, y + TH_PH + 50 - 1, TH_AD + 2, TH_AD + 2, 12, tok (TJ_SURFACE), tok (TJ_SURFACE));
		}
		uint32_t a = th_acc_colors[i][dark];	/* (en dur comme l'asm : les couleurs de UIKit) */
		ui_rbox (px, y + TH_PH + 50, TH_AD, TH_AD, 11, a, a);
	}
	ui_text ((long) TR (th_acc_labels[th_acc]), x, y + TH_PH + 90, tok (TJ_TEXT2), 12, F (), 0);	/* son nom */
}

/* (ap.xclick) une pastille ? -> 1 si prise (redessiner) */
static int th_click (long x, long y)
{
	unsigned long dy = (unsigned long) (y - TH_AY);
	if (dy >= TH_AD)
		return 0;
	long dx = x - (SIDE_W + AP_MARGIN + COL_W);
	if (dx < 0)
		return 0;
	unsigned long i = (unsigned long) dx / TH_AP;
	if ((unsigned long) dx - i * TH_AP >= TH_AD || i >= TH_NACC)
		return 0;
	th_acc = (long) i;
	return 1;
}

static void th_open (void)
{
	th_read ();				/* thème actuel */
	copy_n (th_current, "Givre", sizeof th_current);
	th_acc = 0;
	char cq_[160];
	ini_read (korin_conf_path ("theme.txt", cq_, sizeof cq_, 0), th_line);
	ap.extra = th_preview;
	ap.xclick = th_click;
	ap.surface = 1;
	applet_row (th_s_title, R_TITLE);
	for (long i = 0; i < TH_N; i++)		/* un choix par thème */
		applet_row (th_names[i], R_CHOICE, 0, ci_eq (th_names[i], th_current), i);
	applet_row (th_s_note, R_NOTE);
	applet_row (th_s_note2, R_NOTE);
}

/* Appliquer : /etc/theme.txt, korin.theme publié (sans busd : la session relancée), le panneau rethémé */
static void th_apply (void)
{
	long c = applet_choice (0);
	if (c < 0)
		return;
	copy_n (th_current, th_names[c], sizeof th_current);	/* le thème coché : celui de korin.theme */
	char *p = stpcpy (th_file, th_header);	/* en-tête, « theme = <nom> », « accent = <nom> » */
	p = stpcpy (p, th_names[c]);
	p = stpcpy (p, CRLF "accent = ");
	p = stpcpy (p, th_acc_names[th_acc]);
	p = stpcpy (p, CRLF);
	if (display_density_file ())		/* la taille des contrôles (volet Écran) reste dans le fichier */
		stpcpy (p, "density = touch" CRLF);
	char cw_[160];
	file_write (korin_conf_path ("theme.txt", cw_, sizeof cw_, 1), th_file);
	th_read ();
	th_back ();
	ui_title ((long) TR ("R\xC3\xA9glages"));	/* (le cadre du panneau aussi : redessiné) */
	p = copy_n (theme_bus, th_current, 32);	/* korin.theme, « <thème>, <accent> » : les programmes UIKit se
						 * redessinent (docs/BUS.md) */
	p = stpcpy (p, ", ");
	stpcpy (p, th_acc_names[th_acc]);
	if (bus_publish ((long) theme_topic, (long) theme_bus, -1, BUS_RETAIN) < 0)
	{
		relaunch ("voronoy", "/sys/voronoy");	/* sans busd : la session relancée */
		relaunch ("menubar", "/sys/menubar");
		relaunch ("panel", "/sys/panel");
	}
	ap.msg = th_s_done;
}

const Applet theme_applet = { "theme", "Th\xC3\xA8me", "Th\xC3\xA8me", "theme", th_open, th_apply };
