/*
 * applets/printers.cpp -- le volet Imprimantes du panneau de configuration (le printconf d'Onyx) : les imprimantes
 * -- la PDF, toujours là, et celles du réseau ajoutées par leur adresse (IPP Everywhere / AirPrint : on leur demande
 * ce qu'elles savent faire, /etc/printers.ini) --, celle par défaut, une page d'essai, et la file d'impression.
 * Tout passe par PrinterKit (service printd, docs/IMPRESSION.md). En widgets, comme le volet Panel (dock.h).
 *
 * Port en C++ de applets/printers.inc (Korin), qui fait foi : le printconf d'Onyx dessinait avec l'uikit C++
 * d'Onyx, sa page d'essai est la même (les mêmes tailles, couleurs, gris, épaisseurs et formes).
 */
#include <string.h>
#include <printerkit.h>
#include "dock.h"

enum
{
	PRA_GH = 250,				/* hauteur des deux groupes du haut */
	PRA_NPR = 12,
	PRA_NJOB = 32,
	PRA_NFOUND = 16,
	PRA_BTN = 28,
	PT_M = 56000,				/* marge de la page d'essai (millièmes de point) */
};

static const char pr_s_printers[] = "Imprimantes";
static const char pr_s_default[] = "D\xC3\xA9" "faut";
static const char pr_s_test[] = "Essai";
static const char pr_s_check[] = "V\xC3\xA9rifier";
static const char pr_s_remove[] = "Retirer";
static const char pr_s_addg[] = "Ajouter une imprimante r\xC3\xA9seau";
static const char pr_s_addn[] = "Chercher parcourt le r\xC3\xA9seau ; ou tapez son adresse (IPP Everywhere / AirPrint : sans pilote).";
static const char pr_s_address[] = "Adresse";
static const char pr_s_name[] = "Nom";
static const char pr_s_add[] = "Ajouter";
static const char pr_s_find[] = "Chercher";
static const char pr_s_queue[] = "File d'impression";
static const char pr_s_cancelj[] = "Annuler";
static const char pr_s_forget[] = "Effacer les finis";
static const char pr_t_addr[] = "L'adresse IP de l'imprimante (192.168.1.30 ; sa page r\xC3\xA9seau ou son \xC3\xA9" "cran la montre), ou ipp://h\xC3\xB4te:631/ipp/print";
static const char pr_t_name[] = "Le nom montr\xC3\xA9 dans le dialogue d'impression (vide : celui de l'imprimante)";
static const char pr_s_pdfn[] = "Demande le nom du fichier \xC3\xA0 chaque impression.";
static const char pr_s_colour[] = "Couleur";
static const char pr_s_bw[] = "Noir et blanc";
static const char pr_s_dpi[] = " ppp, jusqu'\xC3\xA0 ";
static const char pr_s_copies[] = " exemplaires";
static const char pr_s_def2[] = "  (par d\xC3\xA9" "faut)";
static const char pr_s_defset[] = "C'est l'imprimante par d\xC3\xA9" "faut.";
static const char pr_s_pdfstay[] = "L'imprimante PDF fait partie du syst\xC3\xA8me : elle reste.";
static const char pr_s_removeq[] = "Retirer l'imprimante ";
static const char pr_s_removed[] = "Retir\xC3\xA9" "e.";
static const char pr_s_asking[] = "On interroge l'imprimante...";
static const char pr_s_notready[] = "Pas pr\xC3\xAAte : ";
static const char pr_s_typefirst[] = "Tapez d'abord l'adresse de l'imprimante (son adresse IP, comme 192.168.1.30).";
static const char pr_s_asking2[] = "On demande \xC3\xA0 l'imprimante ce qu'elle sait faire...";
static const char pr_s_added[] = " est ajout\xC3\xA9" "e.";
static const char pr_s_notadded[] = "Pas ajout\xC3\xA9" "e : ";
static const char pr_s_searching[] = "Recherche des imprimantes sur le r\xC3\xA9seau...";
static const char pr_s_notus[] = "  (inutilisable)";
static const char pr_s_foundnone[] = "Aucune imprimante trouv\xC3\xA9" "e : est-elle allum\xC3\xA9" "e, sur le m\xC3\xAAme r\xC3\xA9seau ?";
static const char pr_s_foundn[] = "Choisissez-en une, puis Ajouter.";
static const char pr_s_pickjob[] = "Choisissez d'abord un travail de la file.";
static const char pr_s_testpath[] = "HOME:/documents/Page d'essai.pdf";
static const char pr_s_testtitle[] = "Page d'essai de Korin OS";
static const char pr_s_nospool[] = "La file d'impression ne peut \xC3\xAAtre \xC3\xA9" "crite (/var/spool/print).";
static const char pr_s_noqueue[] = "La page d'essai n'a pu \xC3\xAAtre mise en file (le service d'impression tourne-t-il ?).";
static const char pr_s_queued[] = "La page d'essai est dans la file.";
static const char pr_s_queuedpdf[] = "La page d'essai s'enregistre sous HOME:/documents/Page d'essai.pdf.";
static const char pr_s_tp1[] = "Imprimante : ";
static const char pr_s_tp2[] = "Papier : ";
static const char pr_s_tp3[] = "Le cadre gris est le bord de ce que cette imprimante imprime.";
static const char pr_s_tp4[] = " pt - Portez ce vieux whisky au juge";
static const char pr_s_tp5[] = "Serif italique : voix ambigu\xC3\xAB d\xE2\x80\x99un c\xC5\x93ur.";
static const char pr_s_tp6[] = "Imprim\xC3\xA9 par le service d'impression de Korin OS (printd).";
static const char *const pr_states[7] = {
	"en attente", "pr\xC3\xA9paration", "envoi", "impression", "fini", "\xC3\xA9" "chec", "annul\xC3\xA9",
};
static const unsigned char pr_sizes[5] = { 8, 10, 12, 16, 24 };
static const char *const pr_colnames[8] = { "rouge", "orange", "jaune", "vert", "cyan", "bleu", "violet", "noir" };
static const char *const pr_lwn[5] = { "0,25 pt", "0,50 pt", "1 pt", "2 pts", "4 pts" };
static const uint32_t pr_cols[8] = { 0xE03030, 0xE08020, 0xE0D020, 0x30A040, 0x20A0C0, 0x3050C0, 0x8040B0, 0x000000 };
static const unsigned short pr_lws[5] = { 250, 500, 1000, 2000, 4000 };

static long pr_w_list, pr_w_model, pr_w_addr, pr_w_state, pr_w_naddr, pr_w_nname, pr_w_found, pr_w_jobs, pr_w_status;
static long pr_npr, pr_njob, pr_nfnd, pr_ticks;
static char pr_nm[64], pr_nu[160], pr_tb[200], pr_t[512], pr_ts[200];
static PrintSetup pr_setup;
static PrintPrinter pr_pr[PRA_NPR];
static PrintFound pr_fnd[PRA_NFOUND];
static PrintJobInfo pr_job[PRA_NJOB], pr_jobn[PRA_NJOB];

/* ---- les aides ---- */

/* dst, piece : la traduction du morceau à dst (l'asm écrit à l'adresse donnée : tr_cat, lui, ajoute à la fin) -> fin */
static char *tr_at (char *dst, const char *piece)
{
	*dst = 0;
	return tr_cat (dst, piece);
}

/* dst, n -> fin : l'entier en décimal */
static char *pr_num (char *dst, unsigned long n)
{
	char t[24];
	int k = 0;
	do
		t[k++] = (char) ('0' + n % 10);
	while (n /= 10);
	while (k)
		*dst++ = t[--k];
	*dst = 0;
	return dst;
}

/* dst, src, n = taille : copie bornée */
static void pr_copy (char *dst, const char *src, size_t n)
{
	strncpy (dst, src, n - 1);
	dst[n - 1] = 0;
}

/* x, y, w, h, texte, ULB_* -> libellé dans le groupe g */
static long pr_lab (long g, long x, long y, long w, long h, const char *text, long flags)
{
	long l = ui_label (x, y, w, h, (long) text, flags);
	ui_w_set_parent (l, g);
	return l;
}

/* x, y, w, texte, fonction -> bouton dans le groupe g */
static long pr_btn (long g, long x, long y, long w, const char *text, dk_fn fn)
{
	long b = ui_pushbutton (x, y, w, PRA_BTN, (long) text, 0);
	ui_w_set_parent (b, g);
	return dk_on (b, fn);
}

/* x, y, w, h, fonction -> liste dans le groupe g */
static long pr_list (long g, long x, long y, long w, long h, dk_fn fn)
{
	long l = ui_list (x, y, w, h, 0);
	ui_w_set_parent (l, g);
	return dk_on (l, fn);
}

/* la ligne d'état, montrée tout de suite */
static void pr_status (const char *text)
{
	ui_w_set_text (pr_w_status, (long) TR (text));
	ui_w_draw (pr_w_status);
}

/* -> l'imprimante choisie (0 : aucune) */
static PrintPrinter *pr_sel (void)
{
	unsigned long v = (unsigned long) ui_w_get_value (pr_w_list);
	return v < (unsigned long) pr_npr ? &pr_pr[v] : 0;
}

static int pr_is_pdf (const PrintPrinter *p) { return !strcmp (p->kind, "pdf"); }

/* ce qu'on sait de l'imprimante choisie */
static void pr_show (void)
{
	PrintPrinter *p = pr_sel ();
	if (!p)
		return;
	ui_w_set_text (pr_w_model, (long) p->model);
	int pdf = pr_is_pdf (p);
	ui_w_set_text (pr_w_addr, (long) (pdf ? TR (pr_s_pdfn) : p->uri));
	pr_ts[0] = 0;
	if (!pdf)				/* « Couleur, 600 ppp, jusqu'à 99 exemplaires » */
	{
		char *o = tr_at (pr_ts, p->color ? pr_s_colour : pr_s_bw);
		o = stpcpy (o, ", ");
		o = pr_num (o, (unsigned) p->dpi);
		o = tr_at (o, pr_s_dpi);
		o = pr_num (o, (unsigned) p->copies);
		tr_at (o, pr_s_copies);
	}
	ui_w_set_text (pr_w_state, (long) pr_ts);
	ui_w_draw (0);
}

/* name = nom à choisir (0 : garder) : la liste relue */
static void pr_load (const char *name)
{
	long v = ui_w_get_value (pr_w_list);	/* la choisie */
	pr_npr = print_printers ((long) pr_pr, PRA_NPR);
	ui_list_clear (pr_w_list);
	long i;
	for (i = 0; i < pr_npr; i++)
	{
		char *o = stpcpy (pr_t, pr_pr[i].name);
		if (pr_pr[i].is_default)
			tr_at (o, pr_s_def2);
		ui_list_add (pr_w_list, (long) pr_t, i);
		if (name && !strcmp (name, pr_pr[i].name))
			v = i;
	}
	if ((unsigned long) v >= (unsigned long) i)	/* (hors de la liste, ou aucune : la première) */
		v = 0;
	ui_w_set_value (pr_w_list, v);
	pr_show ();
}

/* s = état -> son nom */
static const char *pr_state (long s)
{
	if ((unsigned long) s > PRINT_JOB_CANCELED)
		s = PRINT_JOB_CANCELED;
	return pr_states[s];
}

/* la file relue, si elle a changé */
static void pr_jobs_load (void)
{
	long n = print_jobs ((long) pr_jobn, PRA_NJOB);
	if (n < 0)				/* (l'asm suppose n >= 0) */
		n = 0;
	if (n == pr_njob && !memcmp (pr_jobn, pr_job, (size_t) n * sizeof (PrintJobInfo)))
		return;
	pr_njob = n;
	memcpy (pr_job, pr_jobn, sizeof pr_job);	/* (on recopie) */
	long v = ui_w_get_value (pr_w_jobs);
	ui_list_clear (pr_w_jobs);
	for (long i = 0; i < n; i++)		/* « titre - état (imprimante) » */
	{
		char *o = stpcpy (pr_t, pr_job[i].title);
		o = stpcpy (o, " - ");
		o = tr_at (o, pr_state (pr_job[i].state));
		o = stpcpy (o, " (");
		o = stpcpy (o, pr_job[i].printer);
		stpcpy (o, ")");
		ui_list_add (pr_w_jobs, (long) pr_t, i);
	}
	if ((unsigned long) v < (unsigned long) n)
		ui_w_set_value (pr_w_jobs, v);
	ui_w_draw (pr_w_jobs);
}

/* (ap.tick, 4 fois par seconde : la file chaque seconde) -> 0 */
static int pr_tick (void)
{
	if (++pr_ticks >= 4)
		pr_ticks = 0;
	if (!pr_ticks)
		pr_jobs_load ();
	return 0;
}

/* ---- les actions ---- */

static void pr_on_select (long, long code)
{
	if (code != UN_CHANGE)
		return;
	pr_show ();
	pr_status (dk_s_empty);
}

static void pr_on_job (long w, long code)
{
	if (code != UN_CHANGE)
		return;
	unsigned long v = (unsigned long) ui_w_get_value (w);
	if (v < (unsigned long) pr_njob)
		pr_status (pr_job[v].message);
}

static void pr_on_default (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	PrintPrinter *p = pr_sel ();
	if (!p || !print_printer_default ((long) p->name))
		return;
	pr_copy (pr_nm, p->name, sizeof pr_nm);	/* (pr_load relit pr_pr) */
	pr_load (pr_nm);
	pr_status (pr_s_defset);
}

static void pr_on_remove (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	PrintPrinter *p = pr_sel ();
	if (!p)
		return;
	if (pr_is_pdf (p))
	{
		pr_status (pr_s_pdfstay);
		return;
	}
	strcpy (stpcpy (tr_at (pr_t, pr_s_removeq), p->name), " ?");
	if (ui_message ((long) pr_s_printers, (long) pr_t, UM_YESNO) != 1)	/* (titre non traduit, comme l'asm) */
		return;
	print_printer_remove ((long) p->name);
	pr_load (0);
	pr_status (pr_s_removed);
}

static void pr_on_check (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	PrintPrinter *p = pr_sel ();
	if (!p)
		return;
	pr_status (pr_s_asking);
	pr_copy (pr_nm, p->name, sizeof pr_nm);
	long ok = print_printer_status ((long) pr_nm, (long) pr_tb, 160);
	pr_t[0] = 0;
	char *o = ok ? pr_t : tr_at (pr_t, pr_s_notready);
	stpcpy (o, pr_tb);
	pr_load (pr_nm);
	pr_status (pr_t);
}

static void pr_on_add (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	const char *a = (const char *) ui_w_get_text (pr_w_naddr);
	if (!*a)
	{
		pr_status (pr_s_typefirst);
		return;
	}
	pr_copy (pr_nu, a, sizeof pr_nu);
	pr_copy (pr_nm, (const char *) ui_w_get_text (pr_w_nname), sizeof pr_nm);
	pr_status (pr_s_asking2);
	if (print_printer_add ((long) pr_nm, (long) pr_nu, (long) pr_tb, 160))
	{
		pr_load (pr_tb);
		ui_w_set_text (pr_w_naddr, (long) dk_s_empty);
		ui_w_set_text (pr_w_nname, (long) dk_s_empty);
		tr_at (stpcpy (pr_t, pr_tb), pr_s_added);
	}
	else
		stpcpy (tr_at (pr_t, pr_s_notadded), pr_tb);
	ui_w_draw (0);
	pr_status (pr_t);
}

/* i = rang : l'adresse et le nom d'une imprimante trouvée dans les champs */
static void pr_found_fill (long i)
{
	ui_w_set_text (pr_w_naddr, (long) pr_fnd[i].address);
	ui_w_set_text (pr_w_nname, (long) pr_fnd[i].name);
	ui_w_draw (0);
}

static void pr_on_find (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	pr_status (pr_s_searching);
	long n = print_printers_find ((long) pr_fnd, PRA_NFOUND);
	pr_nfnd = n;
	ui_list_clear (pr_w_found);
	for (long i = 0; i < n; i++)		/* « modèle - adresse » */
	{
		char *o = stpcpy (pr_t, pr_fnd[i].model);
		o = stpcpy (o, " - ");
		o = stpcpy (o, pr_fnd[i].address);
		if (!pr_fnd[i].usable)
			tr_at (o, pr_s_notus);
		ui_list_add (pr_w_found, (long) pr_t, i);
	}
	const char *m = pr_s_foundn;
	if (!n)
		m = pr_s_foundnone;
	else if (n == 1)			/* une seule : déjà dans les champs */
	{
		ui_w_set_value (pr_w_found, 0);
		pr_found_fill (0);
	}
	pr_status (m);
	ui_w_draw (0);
}

static void pr_on_found (long w, long code)
{
	if (code != UN_CHANGE)
		return;
	unsigned long v = (unsigned long) ui_w_get_value (w);
	if (v < (unsigned long) pr_nfnd)
		pr_found_fill ((long) v);
}

static void pr_on_cancel (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	unsigned long v = (unsigned long) ui_w_get_value (pr_w_jobs);
	if (v >= (unsigned long) pr_njob)
	{
		pr_status (pr_s_pickjob);
		return;
	}
	print_job_cancel (pr_job[v].id);
	pr_jobs_load ();
}

static void pr_on_forget (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	print_jobs_forget ();
	sys_sleep (150);
	pr_jobs_load ();
}

/* ---- la page d'essai : ce que l'imprimante sait faire, sur le papier ---- */

/* j = travail, police, taille, x, y, texte (traduit), couleur */
static void pr_tx (long j, long font, long size, long x, long y, const char *text, long color)
{
	print_text (j, font, size, x, y, (long) TR (text), color);
}

static void pr_on_test (long, long code)
{
	if (code != UN_ACTIVATE)
		return;
	PrintPrinter *p = pr_sel ();
	if (!p)
		return;
	PrintSetup &s = pr_setup;		/* le réglage */
	print_setup_default ((long) &s);
	strcpy (s.printer, p->name);
	print_setup_paper ((long) &s, 0, PRINT_PORTRAIT);
	if (pr_is_pdf (p))
		strcpy (s.output, pr_s_testpath);
	long j = print_begin ((long) &s, (long) TR (pr_s_testtitle));
	if (!j)
	{
		pr_status (pr_s_nospool);
		return;
	}
	print_page (j, 0, 0);
	long f = print_font (j, (long) "DejaVu Sans", 0);
	long fb = print_font (j, (long) "DejaVu Sans", PRINT_BOLD);
	long fi = print_font (j, (long) "DejaVu Serif", PRINT_ITALIC);
	long w = (long) (unsigned) s.paper_w - PT_M * 2;	/* largeur utile */
	long y = PT_M;
	/* le bord de ce que l'imprimante imprime */
	print_frame (j, (unsigned) s.margin_l, (unsigned) s.margin_t,
		     (long) (unsigned) s.paper_w - (unsigned) s.margin_l - (unsigned) s.margin_r,
		     (long) (unsigned) s.paper_h - (unsigned) s.margin_t - (unsigned) s.margin_b, 500, 0xB0B0B0);
	pr_tx (j, fb, 26000, PT_M, y + 26000, pr_s_testtitle, 0x202020);
	y += 44000;
	print_line (j, PT_M, y, PT_M + w, y, 1500, 0x4992A7);	/* le filet */
	y += 22000;
	char *o = tr_at (pr_t, pr_s_tp1);	/* les lignes de description */
	strcpy (stpcpy (stpcpy (stpcpy (o, p->name), " ("), p->model), ")");
	pr_tx (j, f, 11000, PT_M, y, pr_t, 0);
	y += 16000;
	o = stpcpy (tr_at (pr_t, pr_s_tp2), s.media);
	o = stpcpy (o, ", ");
	o = pr_num (o, (unsigned) s.paper_w / 1000);
	o = stpcpy (o, " x ");
	o = pr_num (o, (unsigned) s.paper_h / 1000);
	tr_at (o, " points.");
	pr_tx (j, f, 11000, PT_M, y, pr_t, 0);
	y += 16000;
	pr_tx (j, f, 11000, PT_M, y, pr_s_tp3, 0);
	y += 30000;
	for (int k = 0; k < 5; k++)		/* les tailles */
	{
		long pt = pr_sizes[k];
		y += 1100 * pt;
		tr_at (pr_num (pr_t, (unsigned long) pt), pr_s_tp4);
		pr_tx (j, f, 1000 * pt, PT_M, y, pr_t, 0);
		y += 400 * pt;
	}
	y += 20000;
	pr_tx (j, fi, 14000, PT_M, y, pr_s_tp5, 0x303030);
	y += 30000;
	long cw = (long) ((unsigned long) w >> 3);	/* les couleurs : la largeur d'une case */
	for (int k = 0; k < 8; k++)
	{
		print_rect (j, k * cw + PT_M, y, cw - 6000, 40000, pr_cols[k]);
		pr_tx (j, f, 8000, k * cw + PT_M, y + 52000, pr_colnames[k], 0x404040);
	}
	y += 76000;
	cw = (long) ((unsigned long) w >> 4);	/* les gris */
	for (int k = 0; k < 16; k++)
		print_rect (j, k * cw + PT_M, y, cw + 200, 24000, 17 * k * 0x010101);
	print_frame (j, PT_M, y, w, 24000, 500, 0);
	y += 50000;
	for (int k = 0; k < 5; k++)		/* les épaisseurs */
	{
		print_line (j, PT_M, y, (long) (45 * (unsigned long) w / 100) + PT_M, y, pr_lws[k], 0);
		tr_at (pr_t, pr_lwn[k]);
		pr_tx (j, f, 8000, (long) (47 * (unsigned long) w / 100) + PT_M, y + 3000, pr_t, 0);
		y += 14000;
	}
	/* les formes : un disque, un triangle */
	long cx = (long) (80 * (unsigned long) w / 100) + PT_M, cy = y - 34000, k = 16569;	/* (0,5523 x 30 points) */
	print_path_move (j, cx + 30000, cy);
	print_path_curve (j, cx + 30000, cy + k, cx + k, cy + 30000, cx, cy + 30000);
	print_path_curve (j, cx - k, cy + 30000, cx - 30000, cy + k, cx - 30000, cy);
	print_path_curve (j, cx - 30000, cy - k, cx - k, cy - 30000, cx, cy - 30000);
	print_path_curve (j, cx + k, cy - 30000, cx + 30000, cy - k, cx + 30000, cy);
	print_path_close (j);
	print_path_fill (j, 0x4992A7);
	print_path_move (j, cx - 90000, cy + 30000);
	print_path_line (j, cx - 40000, cy + 30000);
	print_path_line (j, cx - 65000, cy - 30000);
	print_path_close (j);
	print_path_stroke (j, 1500, 0xC04040);
	y = (long) (unsigned) s.paper_h - (PT_M + 40000);	/* le bas de la page */
	pr_tx (j, f, 9000, PT_M, y, pr_s_tp6, 0x606060);
	if (print_end (j) < 0)
	{
		pr_status (pr_s_noqueue);
		return;
	}
	pr_status (pr_is_pdf (p) ? pr_s_queuedpdf : pr_s_queued);
	pr_jobs_load ();
}

/* ---- le volet ---- */

static void pr_open (void)
{
	dk_widgets ();
	ap.tick = pr_tick;
	pr_njob = -1;
	long pw = ui.w - SIDE_W, ph = ui.h - AP_HEAD;	/* le volet */
	long gw = (long) ((unsigned long) (pw - 30) >> 1);	/* largeur d'un groupe du haut */
	long x = SIDE_W + 10, y = AP_HEAD + 6;

	/* ---- les imprimantes */
	long g = dk_top (ui_groupbox (x, y, gw, PRA_GH, (long) TR (pr_s_printers)));
	long cy = y + DK_CT, cw = gw - 20;	/* haut, largeur du contenu */
	pr_w_list = pr_list (g, x + 10, cy, cw, 96, pr_on_select);
	pr_w_model = pr_lab (g, x + 10, cy + 100, cw, 20, dk_s_empty, 0);
	pr_w_addr = pr_lab (g, x + 10, cy + 120, cw, 20, dk_s_empty, ULB_DIM);
	pr_w_state = pr_lab (g, x + 10, cy + 140, cw, 20, dk_s_empty, ULB_DIM);
	cy += 166;
	long bw = (long) ((unsigned long) (cw - 18) >> 2);	/* quatre boutons */
	pr_btn (g, x + 10, cy, bw, pr_s_default, pr_on_default);
	pr_btn (g, x + 16 + bw, cy, bw, pr_s_test, pr_on_test);
	pr_btn (g, x + 22 + 2 * bw, cy, bw, pr_s_check, pr_on_check);
	pr_btn (g, x + 28 + 3 * bw, cy, bw, pr_s_remove, pr_on_remove);

	/* ---- ajouter une imprimante */
	x += gw + 10;
	g = dk_top (ui_groupbox (x, y, gw, PRA_GH, (long) TR (pr_s_addg)));
	cy = y + DK_CT;
	pr_lab (g, x + 10, cy, cw, 40, pr_s_addn, ULB_WRAP);
	cy += 44;
	pr_lab (g, x + 10, cy + 4, 62, 20, pr_s_address, 0);
	pr_w_naddr = ui_textbox (x + 74, cy, cw - 64, 26, 0);
	ui_w_set_parent (pr_w_naddr, g);
	dk_tip (pr_w_naddr, pr_t_addr);
	cy += 32;
	pr_lab (g, x + 10, cy + 4, 62, 20, pr_s_name, 0);
	pr_w_nname = ui_textbox (x + 74, cy, cw - 64, 26, 0);
	ui_w_set_parent (pr_w_nname, g);
	dk_tip (pr_w_nname, pr_t_name);
	cy += 32;
	pr_btn (g, x + 74, cy, 86, pr_s_add, pr_on_add);
	pr_btn (g, x + 166, cy, 86, pr_s_find, pr_on_find);
	cy += 34;
	pr_w_found = pr_list (g, x + 10, cy, cw, y + PRA_GH - 10 - cy, pr_on_found);

	/* ---- la file */
	y += PRA_GH + 6;
	x = SIDE_W + 10;
	long qw = pw - 20, qh = AP_HEAD + 6 + ph - y - 38;	/* largeur, hauteur */
	g = dk_top (ui_groupbox (x, y, qw, qh, (long) TR (pr_s_queue)));
	pr_w_jobs = pr_list (g, x + 10, y + DK_CT, qw - 150, qh - (DK_CT + 10), pr_on_job);
	pr_btn (g, x + qw - 130, y + DK_CT, 120, pr_s_cancelj, pr_on_cancel);
	pr_btn (g, x + qw - 130, y + DK_CT + 34, 120, pr_s_forget, pr_on_forget);

	/* ---- la ligne d'état */
	pr_w_status = dk_top (ui_label (x, y + qh + 6, qw, 22, (long) dk_s_empty, ULB_DIM));

	pr_load (0);
	pr_jobs_load ();
}

static void pr_apply (void) {}

const Applet printers_applet = { "printconf", "Imprimantes", "Imprimantes", "printconf", pr_open, pr_apply };
