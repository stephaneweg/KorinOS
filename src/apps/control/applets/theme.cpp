/*
 * applets/theme.cpp -- le volet Thème du panneau de configuration : le thème du bureau (theme.txt, clés d'Onyx), sa
 * couleur d'accent et le fond d'écran (wallpaper.ini : src/sys/common/wallpaper.h), avec un aperçu (un petit bureau
 * dans les couleurs du thème coché, sur le fond choisi, peint par le peintre de voronoy). Appliquer écrit les
 * fichiers (personnels : HOME:/etc) et publie korin.theme (busd, docs/BUS.md) : UIKit le relit dans chaque programme
 * et redessine cadres et widgets ; le fond, la barre de menus et le panel se redessinent (WEV_THEME). Un fond changé
 * seul publie korin.wallpaper : voronoy le repeint. Sans busd : ces trois-là sont relancés, les autres applications
 * le prendront à leur prochain lancement.
 *
 * Port en C++ de applets/theme.inc (Korin). Du volet Thème d'Onyx (user/Apps/theme/main.cpp) sont repris les fonds
 * d'écran (cellules, dégradé, bulles, uni, motif de /res/wallpapers, image, et leur teinte) ; pas la couleur de chaque
 * partie : Korin n'a que deux thèmes (Givre, Onyx Noir) et un accent, et c'est ce que l'utilisateur a validé. La
 * teinte suit le thème : l'accent du thème, ou l'une des couleurs d'accent de UIKit, mêlée à la palette calme.
 */
#include <armpi/userconf.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <buskit_abi.h>
#include "../control.h"
#include "../../../sys/common/wallpaper.h"
#include "../../paint/imgload.h"
#include "../../paint/pngsave.hpp"

enum
{
	TH_N = 2,				/* thèmes */
	TH_PW = COL_W - 8,			/* aperçu */
	TH_PH = 150,
	TH_NACC = 8,				/* couleurs d'accent (uk_accents de UIKit) */
	TH_AD = 22,				/* leur pastille */
	TH_AP = 30,				/* leur pas */
	TH_AY = TH_PH + 50,			/* haut des pastilles, sous le haut de l'aperçu */
	TH_NT = TH_NACC + 2,			/* teintes : aucune, l'accent, les huit */
	TH_TD = 16,				/* leur pastille */
	TH_TP = 20,				/* leur pas */
	TH_TY = TH_PH + 126,			/* haut des pastilles de teinte */
	TH_LY = TH_PH + 176,			/* la ligne du motif ou de l'image */
	TH_AB = 24,				/* ses flèches */
	TH_NPAT = 32,				/* motifs */
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
static const char *const th_acc_labels[TH_NACC] = {
	"Bleu", "Rouge orang\xC3\xA9", "Vert", "Jaune", "Violet", "Fuchsia", "Sarcelle", "Graphite",
};
/* les fonds, dans l'ordre de wallpaper.h (WP_*) */
static const char *const th_wp_labels[WP_NMODES] = {
	"Cellules", "D\xC3\xA9grad\xC3\xA9", "Bulles", "Uni", "Motif", "Image",
};
/* les motifs livrés : leur nom (les autres fichiers de /res/wallpapers : leur nom de fichier) */
static const char *const th_pat_files[] = {
	"bokeh", "contours", "dunes", "facets", "hexagons", "low-poly", "silk", "waves",
};
static const char *const th_pat_labels[] = {
	"Bokeh", "Courbes de niveau", "Dunes", "Facettes", "Hexagones", "Polygones", "Soie", "Vagues",
};
static const char th_s_title[] = "Th\xC3\xA8me du bureau";
static const char th_s_wp[] = "Fond d'\xC3\xA9" "cran";
static const char th_s_tile[] = "Image en mosa\xC3\xAFque";
static const char th_s_note[] = "Certaines applications";	/* (trois lignes : une colonne fait COL_W) */
static const char th_s_note2[] = "ouvertes ne le prendront";
static const char th_s_note3[] = "qu'\xC3\xA0 leur relance.";
static const char th_s_preview[] = "Aper\xC3\xA7u";
static const char th_s_done[] = "Th\xC3\xA8me appliqu\xC3\xA9.";
static const char th_s_accent[] = "Couleur d'accent";
static const char th_s_tint[] = "Teinte du fond";
static const char th_s_none[] = "Aucune teinte";
static const char th_s_tacc[] = "L'accent du th\xC3\xA8me";
static const char th_s_choose[] = "Choisir...";
static const char th_s_noimg[] = "Aucune image choisie";
static const char th_s_badimg[] = "Image illisible.";
static const char th_header[] =
	"# Le look du bureau (cles d'Onyx), ecrit par le panneau de configuration" CRLF
	"# (applet Theme). Autres cles : voir docs/DESIGN-ELEGANT.md." CRLF
	"theme = ";

enum { R_THEME0 = 1, R_MODE0 = 4, R_TILE = R_MODE0 + WP_NMODES };	/* les lignes (th_open) */

static long th_acc;				/* l'accent choisi */
static long th_tint;				/* la teinte : 0 aucune, 1 l'accent, 2.. un accent nommé */
static char th_current[32];			/* le thème du fichier (puis celui appliqué) */
static char th_file[512];
static char th_actual[TH_TXT];			/* theme.txt tel quel */
static char th_buf[TH_TXT];
static uint32_t th_j[TJ_SIZE / 4];		/* les jetons du thème coché (aperçu) */
static uint32_t th_acc_col[TH_NACC];		/* les accents dans le thème coché */
static wp_conf th_wp;				/* le fond (wallpaper.ini, puis les choix) */
static char th_wp_saved[1024];			/* wallpaper.ini tel quel (Appliquer : changé ?) */
static char th_pats[TH_NPAT][48];		/* les motifs de /res/wallpapers */
static long th_npat, th_pat;
static long th_pat_img, th_pat_loaded = -1;	/* le motif chargé (ui_image_load) et son rang */
static unsigned *th_pic;			/* l'image choisie (img_load), en attente d'Appliquer */
static int th_pic_w, th_pic_h;
static long th_cache_img;			/* la copie de l'image (wallpaper.png, ui_image_load) */
static int th_cache_tile = -1;			/* son style quand elle a été écrite (-1 : inconnu) */
static const char *th_err;			/* message d'erreur du choix d'image */

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

/* th_actual <- theme.txt (vide : sans fichier) */
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

/* ---- le fond d'écran ---- */

/* th_wp <- wallpaper.ini (personnel, sinon /etc), th_wp_saved tel quel */
static void th_wp_read (void)
{
	wp_defaults (&th_wp);
	th_wp_saved[0] = 0;
	char cp_[160];
	int fd = open (korin_conf_path (WP_INI, cp_, sizeof cp_, 0), O_RDONLY);
	if (fd < 0)
		return;
	long n = read (fd, th_wp_saved, sizeof th_wp_saved - 1);
	th_wp_saved[n > 0 ? n : 0] = 0;
	close (fd);
	static char t[sizeof th_wp_saved];
	memcpy (t, th_wp_saved, sizeof t);
	wp_parse (&th_wp, t);
}

static int th_cmp (const void *a, const void *b) { return strcmp ((const char *) a, (const char *) b); }

/* th_pats <- les images de /res/wallpapers (les livrées et celles qu'on y a mises), triées ; th_pat : celle du réglage */
static void th_pats_scan (void)
{
	th_npat = 0;
	if (DIR *d = opendir (WP_DIR))
	{
		while (struct dirent *e = readdir (d))
		{
			size_t n = strlen (e->d_name);
			if (n < 5 || n >= sizeof th_pats[0] || th_npat >= TH_NPAT || e->d_name[0] == '.')
				continue;
			const char *x = e->d_name + n - 4;
			if (ci_eq (x, ".png") || ci_eq (x, ".bmp"))
				copy_n (th_pats[th_npat++], e->d_name, sizeof th_pats[0]);
		}
		closedir (d);
	}
	qsort (th_pats, (size_t) th_npat, sizeof th_pats[0], th_cmp);
	th_pat = 0;
	for (long i = 0; i < th_npat; i++)
		if (ci_eq (th_pats[i], th_wp.pattern))
			th_pat = i;
}

/* le nom montré d'un motif : celui des livrés, sinon le fichier sans son extension */
static const char *th_pat_label (long i)
{
	static char b[48];
	copy_n (b, th_pats[i], sizeof b);
	char *dot = strrchr (b, '.');
	if (dot)
		*dot = 0;
	for (size_t k = 0; k < sizeof th_pat_files / sizeof th_pat_files[0]; k++)
		if (ci_eq (b, th_pat_files[k]))
			return TR (th_pat_labels[k]);
	return b;
}

/* le motif th_pat chargé pour l'aperçu (une fois par motif) */
static void th_pat_load (void)
{
	if (th_pat_loaded == th_pat || !th_npat)
		return;
	if (th_pat_img)
		ui_image_free (th_pat_img);
	char p[200];
	snprintf (p, sizeof p, "%s/%s", WP_DIR, th_pats[th_pat]);
	th_pat_img = ui_image_load ((long) p);
	th_pat_loaded = th_pat;
}

/* les choix du volet -> th_wp (le mode, la mosaïque, la teinte, le motif) */
static void th_wp_choices (void)
{
	long m = applet_choice (1);
	th_wp.mode = m >= 0 ? (int) m : WP_VORONOI;
	Row *r = row_ptr (R_TILE);
	th_wp.tile = r && r->checked;
	copy_n (th_wp.tint, th_tint == 0 ? "" : th_tint == 1 ? "accent" : th_acc_names[th_tint - 2], sizeof th_wp.tint);
	if (th_npat)
		copy_n (th_wp.pattern, th_pats[th_pat], sizeof th_wp.pattern);
}

/* la couleur de la teinte t dans les jetons j de l'aperçu */
static uint32_t th_tint_col (long t, const uint32_t *j)
{
	return t == 1 ? j[TJ_ACCENT / 4] & 0xFFFFFF : th_acc_col[t - 2];
}

/* un coin arrondi de r px, à la couleur c (le dessin peint pixel par pixel n'a pas de coins) */
static void th_corners (long x, long y, long w, long h, long r, uint32_t c)
{
	for (long dy = 0; dy < r; dy++)
		for (long dx = 0; dx < r; dx++)
		{
			long ex = 2 * (r - dx) - 1, ey = 2 * (r - dy) - 1;
			if (ex * ex + ey * ey <= 4 * r * r)
				continue;
			ui_fill (x + dx, y + dy, 1, 1, c);
			ui_fill (x + w - 1 - dx, y + dy, 1, 1, c);
			ui_fill (x + dx, y + h - 1 - dy, 1, 1, c);
			ui_fill (x + w - 1 - dx, y + h - 1 - dy, 1, 1, c);
		}
}

/* le fond de l'aperçu : le peintre de voronoy, dans les pixels de la fenêtre */
static void th_wp_preview (long x, long y, const uint32_t *j)
{
	if (x < 0 || y < 0 || x + TH_PW > ui.w || y + TH_PH > ui.h)
		return;
	th_wp_choices ();
	long scr[4];
	if (ui_screen ((long) scr) < 0)
		scr[0] = 1920;
	wp_target t;
	t.px = ui.surf + y * ui.w + x;
	t.w = TH_PW;
	t.h = TH_PH;
	t.stride = ui.w;
	t.scale_w = scr[0];
	t.dark = j[TJ_DARK / 4] != 0;
	t.tinted = th_tint != 0;
	t.tint = th_tint ? th_tint_col (th_tint, j) : 0;
	t.seed = 0x5EED;				/* (les mêmes cellules à chaque dessin) */
	t.img = 0;
	t.iw = t.ih = 0;
	if (th_wp.mode == WP_PATTERN)
	{
		th_pat_load ();
		if (th_pat_img)
		{
			long h, px;
			t.iw = uikit_image_size (th_pat_img, &h, &px);
			t.ih = h;
			t.img = (const uint32_t *) px;
		}
	}
	else if (th_wp.mode == WP_IMAGE)
	{
		if (th_pic)
		{
			t.img = th_pic;
			t.iw = th_pic_w;
			t.ih = th_pic_h;
		}
		else if (th_cache_img)
		{
			long h, px;
			t.iw = uikit_image_size (th_cache_img, &h, &px);
			t.ih = h;
			t.img = (const uint32_t *) px;
			if (th_wp.tile != th_cache_tile)		/* (la copie a l'autre style : pas d'aperçu juste) */
				t.img = 0;
		}
	}
	wp_paint (&th_wp, &t);
	th_corners (x, y, TH_PW, TH_PH, 6, tok (TJ_SURFACE));
}

/* (ap.extra) x, y = coin, w = largeur : un petit bureau dans les couleurs du thème coché (ses jetons : pris un instant
 * par le panneau, puis rendus), sur le fond choisi, les pastilles d'accent et de teinte, le motif ou l'image */
static void th_preview (long x, long y, long)
{
	long c = applet_choice (0);
	if (c < 0)
		return;
	for (long i = 0; i < TH_NACC; i++)	/* les accents dans ce thème (les couleurs de UIKit) */
	{
		char *p = stpcpy (th_buf, "theme = ");
		p = stpcpy (p, th_names[c]);
		p = stpcpy (p, "\naccent = ");
		stpcpy (p, th_acc_names[i]);
		ui_theme_text ((long) th_buf);
		th_acc_col[i] = tok (TJ_ACCENT) & 0xFFFFFF;
	}
	char *p = stpcpy (th_buf, "theme = ");	/* « theme = <nom> », « accent = <nom> » */
	p = stpcpy (p, th_names[c]);
	p = stpcpy (p, "\naccent = ");
	stpcpy (p, th_acc_names[th_acc]);
	ui_theme_text ((long) th_buf);
	memcpy (th_j, (const void *) ui_tokens (), TJ_SIZE);	/* ses jetons, copiés */
	th_back ();
	const uint32_t *j = th_j;
#define J(t) (j[(t) / 4])
	th_wp_preview (x, y, j);
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
	/* (les jetons du panneau, à nouveau) « Aperçu », sous le dessin */
	ui_text ((long) TR (th_s_preview), x, y + TH_PH + 18, tok (TJ_TEXT3), 12, F (), 0);
	ui_text ((long) TR (th_s_accent), x, y + TH_PH + 44, tok (TJ_TEXT1), 13, FB (), 0);	/* l'accent : ses pastilles */
	for (long i = 0; i < TH_NACC; i++)
	{
		long px = x + i * TH_AP;
		if (th_acc == i)		/* choisie : un anneau */
		{
			ui_rbox (px - 3, y + TH_AY - 3, TH_AD + 6, TH_AD + 6, 14, tok (TJ_TEXT1), tok (TJ_TEXT1));
			ui_rbox (px - 1, y + TH_AY - 1, TH_AD + 2, TH_AD + 2, 12, tok (TJ_SURFACE), tok (TJ_SURFACE));
		}
		ui_rbox (px, y + TH_AY, TH_AD, TH_AD, 11, th_acc_col[i], th_acc_col[i]);
	}
	ui_text ((long) TR (th_acc_labels[th_acc]), x, y + TH_PH + 90, tok (TJ_TEXT2), 12, F (), 0);	/* son nom */
	/* la teinte du fond : aucune (la couleur calme, barrée), l'accent du thème, les huit accents */
	ui_text ((long) TR (th_s_tint), x, y + TH_TY - 6, tok (TJ_TEXT1), 13, FB (), 0);
	for (long i = 0; i < TH_NT; i++)
	{
		long px = x + i * TH_TP;
		if (th_tint == i)
		{
			ui_rbox (px - 2, y + TH_TY - 2, TH_TD + 4, TH_TD + 4, 10, tok (TJ_TEXT1), tok (TJ_TEXT1));
			ui_rbox (px - 1, y + TH_TY - 1, TH_TD + 2, TH_TD + 2, 9, tok (TJ_SURFACE), tok (TJ_SURFACE));
		}
		uint32_t col = i ? th_tint_col (i, j) : wp_base (J (TJ_DARK) != 0, 0, 0);
		ui_rbox (px, y + TH_TY, TH_TD, TH_TD, 8, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
		ui_rbox (px + 1, y + TH_TY + 1, TH_TD - 2, TH_TD - 2, 7, col, col);
		if (i == 0)			/* aucune : un trait en travers */
			for (long k = 3; k < TH_TD - 3; k++)
				ui_fill (px + TH_TD - 1 - k, y + TH_TY + k, 2, 1, tok (TJ_TEXT3));
		else if (i == 1)		/* l'accent du thème : un point au centre */
			ui_rbox (px + TH_TD / 2 - 3, y + TH_TY + TH_TD / 2 - 3, 6, 6, 3, ui_ink (col), ui_ink (col));
	}
	const char *tn = th_tint == 0 ? th_s_none : th_tint == 1 ? th_s_tacc : th_acc_labels[th_tint - 2];
	ui_text ((long) TR (tn), x, y + TH_TY + TH_TD + 16, tok (TJ_TEXT2), 12, F (), 0);
	/* le motif (‹ son nom ›) ou l'image (Choisir..., son nom) */
	long ly = y + TH_LY;
	if (th_wp.mode == WP_PATTERN && th_npat)
	{
		for (long k = 0; k < 2; k++)
		{
			long bx = k ? x + TH_PW - TH_AB : x;
			ui_rbox (bx, ly, TH_AB, TH_AB, 6, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
			ui_rbox (bx + 1, ly + 1, TH_AB - 2, TH_AB - 2, 5, tok (TJ_RAISED), tok (TJ_RAISED_LOW));
			for (long r = 0; r < 5; r++)	/* un chevron */
			{
				long cx = bx + TH_AB / 2 + (k ? r - 2 : 2 - r);
				ui_fill (cx, ly + 7 + r, 2, 1, tok (TJ_TEXT1));
				ui_fill (cx, ly + 16 - r, 2, 1, tok (TJ_TEXT1));
			}
		}
		const char *n = th_pat_label (th_pat);
		long tw = ui_text_width ((long) n, 13, F ());
		ui_text ((long) n, x + (TH_PW - tw) / 2, ly + 17, tok (TJ_TEXT1), 13, F (), TH_PW - 2 * TH_AB - 8);
	}
	else if (th_wp.mode == WP_IMAGE)
	{
		ap_button (x, ly - 2, th_s_choose, 0);
		const char *n = th_err ? TR (th_err) : th_wp.image[0] ? th_wp.image : TR (th_s_noimg);
		const char *s = strrchr (n, '/');
		ui_text ((long) (s && !th_err ? s + 1 : n), x + BTN_W + 8, ly + 17, tok (th_err ? TJ_DANGER : TJ_TEXT2), 12, F (),
			 (long) TH_PW - BTN_W - 8);
	}
#undef J
}

/* « Choisir... » : une image, lue tout de suite (son aperçu ; sa copie s'écrit à Appliquer) -> 1 */
static int th_choose (void)
{
	char path[192];
	copy_n (path, th_wp.image, sizeof path);
	char dir[192] = "HOME:/";
	struct stat st;
	if (stat (WP_PUBLIC, &st) == 0 && S_ISDIR (st.st_mode))	/* les fonds du paquet wallpapers (dossier public) */
		copy_n (dir, WP_PUBLIC, sizeof dir);
	char *s = strrchr (path, '/');
	if (s && s > path)
	{
		memcpy (dir, path, (size_t) (s - path));
		dir[s - path] = 0;
	}
	ui_file_filter ((long) "*.png;*.jpg;*.jpeg;*.jpe;*.gif;*.bmp;*.webp;*.pcx");
	long ok = ui_file_open ((long) dir, (long) path, sizeof path);
	ui_file_filter (0);
	if (!ok || !path[0])
		return 1;
	int w = 0, h = 0;
	unsigned *px = img_load (path, &w, &h);
	if (!px || w <= 0 || h <= 0)
	{
		delete [] px;
		th_err = th_s_badimg;
		return 1;
	}
	delete [] th_pic;
	th_pic = px;
	th_pic_w = w;
	th_pic_h = h;
	th_err = 0;
	copy_n (th_wp.image, path, sizeof th_wp.image);
	return 1;
}

/* (ap.xclick) une pastille, une flèche, Choisir ? -> 1 si pris (redessiner) */
static int th_click (long x, long y)
{
	long x0 = SIDE_W + AP_MARGIN + COL_W, y0 = AP_HEAD + 8;
	long dx = x - x0, dy = y - y0;
	if (dx < 0)
		return 0;
	if (dy >= TH_AY && dy < TH_AY + TH_AD)	/* un accent (avant la largeur de l'aperçu : les huit pastilles vont
						   jusqu'à 8 x TH_AP, au-delà de TH_PW ; la dernière ne répondait pas) */
	{
		long i = dx / TH_AP;
		if (dx - i * TH_AP >= TH_AD || i >= TH_NACC)
			return 0;
		th_acc = i;
		return 1;
	}
	if (dx >= TH_PW)
		return 0;
	if (dy >= TH_TY - 2 && dy < TH_TY + TH_TD + 2)	/* une teinte */
	{
		long i = dx / TH_TP;
		if (i >= TH_NT)
			return 0;
		th_tint = i;
		return 1;
	}
	if (dy < TH_LY - 2 || dy >= (long) TH_LY + BTN_H)
		return 0;
	th_wp_choices ();
	if (th_wp.mode == WP_PATTERN && th_npat && dy < TH_LY + TH_AB)	/* ‹ › : le motif d'avant, d'après */
	{
		if (dx < TH_AB)
			th_pat = (th_pat + th_npat - 1) % th_npat;
		else if (dx >= TH_PW - TH_AB)
			th_pat = (th_pat + 1) % th_npat;
		else
			return 0;
		return 1;
	}
	if (th_wp.mode == WP_IMAGE && dx < BTN_W)
		return th_choose ();
	return 0;
}

static void th_open (void)
{
	th_read ();				/* thème actuel */
	copy_n (th_current, "Givre", sizeof th_current);
	th_acc = 0;
	char cq_[160];
	ini_read (korin_conf_path ("theme.txt", cq_, sizeof cq_, 0), th_line);
	th_wp_read ();				/* le fond */
	th_tint = 0;
	if (ci_eq (th_wp.tint, "accent"))
		th_tint = 1;
	for (long i = 0; i < TH_NACC; i++)
		if (ci_eq (th_wp.tint, th_acc_names[i]))
			th_tint = i + 2;
	th_pats_scan ();
	delete [] th_pic;
	th_pic = 0;
	th_err = 0;
	if (th_cache_img)
		ui_image_free (th_cache_img);
	th_cache_img = 0;
	th_cache_tile = -1;
	if (th_wp.mode == WP_IMAGE)		/* la copie de l'image, pour l'aperçu */
	{
		char cc_[160];
		th_cache_img = ui_image_load ((long) korin_conf_path (WP_CACHE, cc_, sizeof cc_, 0));
		th_cache_tile = th_wp.tile;
	}
	ap.extra = th_preview;
	ap.xclick = th_click;
	ap.surface = 1;
	applet_row (th_s_title, R_TITLE);	/* (les rangs : R_THEME0, R_MODE0, R_TILE) */
	for (long i = 0; i < TH_N; i++)		/* un choix par thème */
		applet_row (th_names[i], R_CHOICE, 0, ci_eq (th_names[i], th_current), i);
	applet_row (th_s_wp, R_TITLE);
	for (long i = 0; i < WP_NMODES; i++)	/* un choix par fond */
		applet_row (th_wp_labels[i], R_CHOICE, 1, th_wp.mode == i, i);
	applet_row (th_s_tile, R_CHECK, 0, th_wp.tile);
	applet_row (th_s_note, R_NOTE);
	applet_row (th_s_note2, R_NOTE);
	applet_row (th_s_note3, R_NOTE);
}

/* la copie de l'image à la taille de l'écran (cover : l'écran rempli ; tile : l'image, réduite à l'écran s'il le
 * faut), en PNG dans HOME:/etc/wallpaper.png -> 1 si écrite */
static int th_cache_write (void)
{
	unsigned *src = th_pic;
	int sw = th_pic_w, sh = th_pic_h;
	if (!src)				/* (le style a changé : l'image d'origine relue) */
	{
		src = img_load (th_wp.image, &sw, &sh);
		if (!src || sw <= 0 || sh <= 0)
		{
			delete [] src;
			return 0;
		}
	}
	long scr[4];
	if (ui_screen ((long) scr) < 0)
	{
		scr[0] = 1920;
		scr[1] = 1080;
	}
	long w = scr[0], h = scr[1];
	if (th_wp.tile)				/* la mosaïque : l'image entière, pas plus grande que l'écran */
	{
		long kw = (sw + w - 1) / w, kh = (sh + h - 1) / h, k = kw > kh ? kw : kh;
		if (k < 1)
			k = 1;
		w = sw / k;
		h = sh / k;
		if (w < 1)
			w = 1;
		if (h < 1)
			h = 1;
	}
	unsigned *out = new unsigned[(size_t) w * h];
	wp_conf c;
	wp_defaults (&c);
	c.mode = WP_IMAGE;			/* (cover, sans teinte : l'image telle quelle à cette taille) */
	wp_target t = { (uint32_t *) out, w, h, w, 0, 0, 0, 0, src, sw, sh, w };
	wp_paint (&c, &t);
	if (src != th_pic)
		delete [] src;
	for (long i = 0; i < w * h; i++)
		out[i] |= 0xFF000000u;
	unsigned n = 0;
	unsigned char *png = pngsave::png_encode (out, (int) w, (int) h, false, &n);
	delete [] out;
	if (!png)
		return 0;
	char cp_[160];
	int fd = open (korin_conf_path (WP_CACHE, cp_, sizeof cp_, 1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	long wr = fd >= 0 ? write (fd, png, n) : -1;
	if (fd >= 0)
		close (fd);
	delete [] png;
	return wr == (long) n;
}

/* Appliquer : theme.txt et wallpaper.ini, korin.theme publié (un fond changé seul : korin.wallpaper ; sans busd :
 * la session relancée), le panneau rethémé */
static void th_apply (void)
{
	long c = applet_choice (0);
	if (c < 0)
		return;
	char before[sizeof theme_bus];
	copy_n (before, theme_bus, sizeof before);
	copy_n (th_current, th_names[c], sizeof th_current);	/* le thème coché : celui de korin.theme */
	char *p = stpcpy (th_file, th_header);	/* en-tête, « theme = <nom> », « accent = <nom> » */
	p = stpcpy (p, th_names[c]);
	p = stpcpy (p, CRLF "accent = ");
	p = stpcpy (p, th_acc_names[th_acc]);
	p = stpcpy (p, CRLF);
	if (display_density_file ())		/* la taille des contrôles (volet Écran) reste dans le fichier */
		stpcpy (p, "density = touch" CRLF);
	int theme_changed = strcmp (th_file, th_actual) != 0;
	char cw_[160];
	file_write (korin_conf_path ("theme.txt", cw_, sizeof cw_, 1), th_file);
	/* le fond : wallpaper.ini, et la copie de l'image si elle est nouvelle ou d'un autre style */
	th_wp_choices ();
	int wp_changed = 0;
	if (th_wp.mode == WP_IMAGE && th_wp.image[0] && (th_pic || th_cache_tile != th_wp.tile || !th_cache_img))
	{
		if (th_cache_write ())
		{
			wp_changed = 1;
			delete [] th_pic;
			th_pic = 0;
			if (th_cache_img)
				ui_image_free (th_cache_img);
			char cc_[160];
			th_cache_img = ui_image_load ((long) korin_conf_path (WP_CACHE, cc_, sizeof cc_, 0));
			th_cache_tile = th_wp.tile;
		}
		else
			th_err = th_s_badimg;
	}
	static char wtxt[1024];
	wp_format (&th_wp, wtxt, sizeof wtxt);
	if (strcmp (wtxt, th_wp_saved))
	{
		file_write (korin_conf_path (WP_INI, cw_, sizeof cw_, 1), wtxt);
		copy_n (th_wp_saved, wtxt, sizeof th_wp_saved);
		wp_changed = 1;
	}
	th_read ();
	th_back ();
	ui_title ((long) TR ("R\xC3\xA9glages"));	/* (le cadre du panneau aussi : redessiné) */
	p = copy_n (theme_bus, th_current, 32);	/* korin.theme, « <thème>, <accent> » : les programmes UIKit se
						 * redessinent (docs/BUS.md) ; voronoy relit aussi le fond */
	p = stpcpy (p, ", ");
	stpcpy (p, th_acc_names[th_acc]);
	long r = 0;
	if (theme_changed || strcmp (before, theme_bus) || !wp_changed)
		r = bus_publish ((long) theme_topic, (long) theme_bus, -1, BUS_RETAIN);
	else				/* le fond seul : voronoy le repeint (docs/BUS.md) */
	{
		char v[24];
		snprintf (v, sizeof v, "%ld", sys_ms ());
		r = bus_publish ((long) WP_TOPIC, (long) v, -1, 0);
	}
	if (r < 0)
	{
		relaunch ("voronoy", "/sys/voronoy");	/* sans busd : la session relancée */
		relaunch ("menubar", "/sys/menubar");
		relaunch ("panel", "/sys/panel");
	}
	ap.msg = th_s_done;
}

const Applet theme_applet = { "theme", "Th\xC3\xA8me", "Th\xC3\xA8me", "theme", th_open, th_apply };
