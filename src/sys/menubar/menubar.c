/* menubar - la barre de menus du bureau Elegant (/sys/menubar, lancée par la session, comme
 * le menubar d'Onyx ; docs/DESIGN-ELEGANT.md 6), traduite ligne à ligne de menubar.asm :
 *   * à gauche, « Korin OS » (losange d'accent + nom) : depuis le 23/11/2026, il bascule la vue
 *     d'ensemble d'Elegant (WS_OP_OVERVIEW : les bureaux en carrousel, le dash), allumé tant
 *     qu'elle est ouverte (korin.shell.overview) ; sans elle (un Elegant plus ancien), le menu Korin
 *     d'avant : À propos, les catégories d'applications, les fenêtres ouvertes ;
 *   * à côté, le nom de l'application active (WEV_WINLIST) ; si elle a publié des menus
 *     (ui_menu ; lus par ui_menu_get toutes les 200 ms et à chaque WEV_WINLIST, refaits quand le
 *     numéro de série change) : un menu en gras à son nom (Quitter), puis ses menus ; un choix
 *     lui est renvoyé (ui_menu_command : WEV_MENU) ; un clic sur un autre titre pendant qu'un
 *     menu est ouvert ouvre celui-là ;
 *   * à droite, la zone d'état : depuis le 23/11/2026, un clic n'importe où dessus (heure,
 *     batterie, réseau, son) ouvre les réglages rapides (/sys/quickmenu : réseau, son, Ne pas
 *     déranger, Réglages, Éteindre ; un second clic les ferme) ; le mode poche garde les clics
 *     d'avant, icône par icône. L'heure (décalage de /etc/clock.cfg, réglé par l'Horloge, plus le temps
 *     depuis le démarrage), l'état du réseau (relu quand netd ou le Wi-Fi le publient sur le
 *     bus, korin.net.* ; sans busd, à chaque dessin ; un clic : le menu Wi-Fi ou l'adresse) et le
 *     haut-parleur (barre si la sortie principale est muette ; état de sound relu par AudioKit) :
 *     un clic ouvre le volet du son (soundmenu), un clic droit le menu « Avancé… » qui lance le
 *     Mixer (docs/SON.md 4.4).
 * Fenêtre sans cadre, translucide, au-dessus des autres, sans focus ; elle réserve le haut de
 * l'écran. À gauche du son, l'icône de PocketQI (une bulle et ses deux yeux), si l'assistante est
 * installée : un clic ouvre sa bulle sous l'icône (/apps/pocketqi.app/main --popup <x>), un
 * second clic la ferme (src/apps/pocketqi/README.md).
 * Mode poche (« --poche », ou korin.shell.mode = poche ; docs/MODE-POCHE.md) : barre de 36 px ;
 * le losange seul ouvre l'accueil (korin.shell.home) au lieu du menu Korin ; à droite, le
 * compteur des apps ouvertes (pids distincts de WS_OP_LIST, l'accueil n'y est pas) publie
 * korin.shell.switch ; l'heure publie korin.shell.quick, avec un point d'accent quand
 * korin.notify.unread > 0 (notifyd : le nombre de notifications non lues).
 * Un menu Aide suit les menus de l'application active si son paquet a un fichier d'aide
 * (/apps/<nom>.app/help_<langue>.md, sinon celui de l'autre langue) : « Aide de <nom> »
 * l'ouvre dans le lecteur d'aide (/apps/help.app/main <fichier>), « Sommaire de l'aide »
 * ouvre le lecteur sur son sommaire. Si l'application a déjà un menu Aide (ou Help), ces deux
 * lignes vont en tête du sien. */
#include "sys_consts.h"
#include "applist.h"
#include "sounddraw.h"
#include "wifidraw.h"
#include "glass.h"
#include "uikit_import.h"
#include "netkit_import.h"
#include "audiokit_import.h"
#include "buskit_import.h"

/* src/inc/netd.inc */
#define NIF_WIFI 2
#define WIFI_ST_READY 2
#define WIFI_ST_CONNECTED 4

#define MENU_W 220
#define ITEMS_MAX 8192
#define MB_MAX 16   /* titres : Korin, l'application, ses menus */
#define MB_PX 14    /* taille du texte de la barre */
#define MB_ITEMS 160 /* lignes d'un menu d'application (ou d'un de ses sous-menus) */
#define MB_POLL 200 /* ms entre deux lectures du menu actif */
#define POCHE_H 36  /* hauteur de la barre en mode poche */

static const char title[] = "Barre de menus";
static const char str_armpi[] = "Korin OS";
static const char str_off[] = "hors ligne";
static const char p_cfg[] = "/etc/clock.cfg";
static const char p_wifimenu[] = "/sys/wifimenu";
static const char p_soundmenu[] = "/sys/soundmenu";
static const char s_avance[] = "Avancé…\0"; /* (un 0 de plus : menu de tr_block) */
static const char p_mixer[] = "/apps/mixer.app/main";
static const char p_pocketqi[] = "/apps/pocketqi.app/main";
static const char s_popup[] = "--popup ";
static const char p_help[] = "/apps/help.app/main";
static const char ap_path[] = "/apps/";
static const char s_help_f[] = ".app/help_";
static const char s_aide[] = "Aide";
static const char s_help[] = "Help";
static const char s_aide_de[] = "Aide de";
static const char s_sommaire[] = "Sommaire de l'aide";
static const char s_losange[] = "\xE2\x97\x86"; /* U+25C6 */
static const char s_croix[] = "\xC3\x97";       /* U+00D7 */
static const char s_sep[] = "-";
static const char s_bienvenue[] = "welcome";
static const char s_apropos[] = "A propos de Korin OS…";
static const char s_fenetres[] = ">Fenêtres ouvertes";
static const char s_reseau[] = "Réseau : ";
static const char s_quitter[] = "Quitter\tCtrl+Q";
static const char s_net_topics[] = "korin.net.*";
static const char s_snd_topic[] = "korin.audio.state";
static const char s_clk_topic[] = "korin.clock.offset";
static const char s_arg_poche[] = "--poche";
static const char s_poche[] = "poche";
static const char s_un[] = "1";
static const char s_t_mode[] = "korin.shell.mode";
static const char s_t_home[] = "korin.shell.home";
static const char s_t_switch[] = "korin.shell.switch";
static const char s_t_quick[] = "korin.shell.quick";
static const char s_unread[] = "korin.notify.unread";
static const char s_t_overview[] = "korin.shell.overview";
static const char p_quickmenu[] = "/sys/quickmenu";

static struct krt_ui ui;
static long net_x;          /* icône du réseau */
static long snd_x;          /* haut-parleur */
static long pq_x;           /* PocketQI (-100 : pas installée) */
static long pq_on;          /* 1 : PocketQI installée */
static long bat_x = -100, bat_x1; /* batterie : zone de clic (-100 : pas de batterie, pas d'icône) */
static long bat_sub;        /* korin.power.battery (acpid du PC) */
static long bat_pct = -1, bat_min = -1, bat_ac = 1, bat_prof = -1, bat_auto = 1;
static char bat_state = 'n'; /* n aucune, d décharge, c charge, f pleine, i branchée */
static char batbuf[16];     /* « 19 % » */
static const u8 *snd_st;    /* tampon d'état de sound (0 : aucun) */
static long snd_gen = -2;   /* dernière génération vue */
static u8 wst[64];          /* état du Wi-Fi */
static u8 nact[16];         /* interface active (nom, type) */
static long net_sub;        /* abonnement à korin.net.* (0 : pas de bus) */
static long snd_sub;        /* korin.audio.state (0 : le son sondé) */
static long clk_sub;        /* korin.clock.offset (0 : clock.cfg relu) */
static u64 last_min;        /* minute affichée */
static long poche;          /* 1 : mode poche */
static long unread_sub;     /* abonnement à korin.notify.unread */
static u64 unread;          /* notifications non lues */
static long napps;          /* apps ouvertes (poche) */
static long cnt_x, cnt_x1;  /* compteur : zone de clic (début, fin) */
static long clk_x, clk_x1;  /* heure : zone de clic */
static char cntbuf[16];
static u32 app_pids[64];
static char argbuf[24];
static long bar_h = 30;
static long hov;            /* titre survolé + 1 (0 : aucun) */
static long ov_sub;         /* korin.shell.overview (Elegant : la vue d'ensemble) */
static long ov_shown;       /* 1 : la vue d'ensemble est ouverte (« Korin OS » allumé) */
static long tray_x0 = 1 << 30; /* zone d'état (heure, batterie, réseau, son) : début ; un clic : réglages rapides */
static long tray_hov;       /* 1 : la zone d'état est survolée */
static long open_menu;      /* titre ouvert + 1 (0 : aucun) */
static long pend_x;         /* clic qui a fermé un menu (écran ; pend_y = -1 : aucun) */
static long pend_y = -1;
static u64 t_poll;          /* dernière lecture du menu actif (ms) */
static u64 t_clock;         /* dernière mise à l'heure (ms) */
static long mb_serial;      /* numéro de série du menu montré (0 : aucun) */
static long mb_n = 1;       /* titres connus (Korin compris) */
static long mb_shown = 1;   /* titres dessinés (zones de clic valides) */
static char *mb_end;        /* fin du texte des menus */
static char *mb_ptr[MB_MAX]; /* titres ([1] : nom de l'application) */
static char *mb_app;        /* « M* » : les lignes du menu au nom de l'application (0 : aucune) */
static long mb_x0[MB_MAX], mb_x1[MB_MAX]; /* zones de clic : début, fin */
static long mb_ids[MB_ITEMS]; /* menu ouvert : id de chaque ligne */
static char app_title[WS_MENU_TITLE];
static char spec_buf[WS_MENU_MAX + 16];
static u64 offset;
static long scr[4];
static u8 netinfo[32];
static char clockbuf[16];
static char netbuf[32];
static char cfgbuf[32];
static u8 wlist[WL_PER_MSG * WL_SIZE];
static long win_ids[AP_MAX];
static u8 sub_map[AP_MAX];
static u8 cat_map[CA_MAX]; /* menu Korin : la catégorie de chaque ligne (les cachées sautées) */
/* les libellés des menus : ITEMS_MAX comme l'asm, plus une marge (l'asm pouvait déborder avec
 * beaucoup de fenêtres ou un très long menu d'application) */
static char items[ITEMS_MAX + 4096];
static char items2[ITEMS_MAX + 4096]; /* sous-menu (le menu garde les siens) */
static long help_on;        /* 1 : l'application active a une aide */
static long help_pid;       /* son process (0 : aucun) */
static long help_t = -1;    /* titre du menu Aide (-1 : pas montré) */
static char *help_tail;     /* (help_probe) fin de « .../help_ » */
static char help_path[128]; /* son fichier d'aide */
static char help_name[AP_LIB]; /* son nom affiché */
static char hbuf[128];      /* « Aide de <nom> » */
static u8 plist[64];        /* un process (SYS_PROC_INFO) */

static void draw(void);

/* ---------------------------------------------------------------------------
 *  Petits outils
 * --------------------------------------------------------------------------- */

static u32 tj(const u8 *t, long off) { return *(const u32 *)(t + off); }
static const u8 *tokens(void) { return (const u8 *)ui_tokens(); }
static long spawn(const char *path, const char *args)
{
	return ksys(SYS_SPAWN, (long)path, (long)args, 0, 0, 0, 0);
}
static long tw(const char *s, const u8 *font) { return ui_text_width((long)s, MB_PX, (long)font); }
static const char *tr(const char *s) { return lang_tr(s); }

/* une fonction d'UIKit qui rend plusieurs registres (r0..r2) et prend jusqu'à 7 arguments
 * (ui_popup_ex : drapeaux en x4, clic en x5, x6) : krt_call_r4 n'en passe que quatre */
static struct krt_r4 call_r4_7(long (*fn)(), long a0, long a1, long a2, long a3, long a4, long a5,
			       long a6)
{
	struct krt_r4 r;
#if defined(__aarch64__)
	register long x0 __asm__("x0") = a0;
	register long x1 __asm__("x1") = a1;
	register long x2 __asm__("x2") = a2;
	register long x3 __asm__("x3") = a3;
	register long x4 __asm__("x4") = a4;
	register long x5 __asm__("x5") = a5;
	register long x6 __asm__("x6") = a6;
	register long (*x9)() __asm__("x9") = fn;
	__asm__ volatile("blr x9"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x9)
			 :
			 : "x7", "x8", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "v0",
			   "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17", "v18", "v19", "v20",
			   "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
			   "memory", "cc");
	r.r0 = x0;
	r.r1 = x1;
	r.r2 = x2;
	r.r3 = x3;
#else
	/* la fonction C du kit rend une structure de quatre long (premier argument caché) ; le
	 * relais d'importation saute, la pile (x5, x6) reste en place */
	r = ((struct krt_r4(*)(long, long, long, long, long, long, long))(void *)fn)(a0, a1, a2, a3,
										      a4, a5, a6);
#endif
	return r;
}

static struct krt_r4 popup_ex(long x, long y, const char *labels, long w, long fl, long cx, long cy)
{
	return call_r4_7(ui_popup_ex, x, y, (long)labels, w, fl, cx, cy);
}

/* ui_popup : seul le choix sert */
static long popup(long x, long y, const char *labels, long w)
{
	return krt_call_r4(ui_popup, x, y, (long)labels, w).r0;
}

/* tr_block de tr.inc : chaînes à la suite (un 0 de plus à la fin : menus de ui_popup) -> leurs
 * traductions, de même, dans buf */
static void tr_block(const char *s, char *buf)
{
	while (*s) {
		const char *t = lang_tr(s);
		while ((*buf++ = *t++) != 0)
			;
		while (*s++)
			;
	}
	*buf = 0;
}

/* la première page de la liste des fenêtres à partir de first -> entrées de cette page (0 : fin) */
static long winlist_page(long first)
{
	long total = ui_winlist(first, (long)wlist);
	if (total <= first)
		return 0;
	long n = total - first;
	return n > WL_PER_MSG ? WL_PER_MSG : n;
}

static u32 wl32(long i, long off) { return *(u32 *)(wlist + i * WL_SIZE + off); }

/* ---------------------------------------------------------------------------
 *  Mode poche
 * --------------------------------------------------------------------------- */

/* arguments : « --poche » (en tête de la ligne), ou korin.shell.mode = poche -> poche */
static void poche_probe(const char *args)
{
	if (args) {
		const char *a = s_arg_poche;
		while (*a && *a == *args) {
			a++;
			args++;
		}
		if (!*a) {
			poche = 1;
			return;
		}
	}
	/* (lancée sans argument : le mode publié par Elegant) */
	long n = bus_get((long)s_t_mode, (long)cfgbuf, 31);
	if (n <= 0)
		return;
	if (n > 31)
		n = 31;
	cfgbuf[n] = 0;
	if (str_eq(cfgbuf, s_poche))
		poche = 1;
}

/* bar_h : 36 en poche ; sinon 30, 28, 26 selon l'échelle */
static void bar_height(void)
{
	bar_h = poche ? POCHE_H : 30 - 2 * ui_scale();
}

/* sujet korin.shell.* : publié (valeur 1, non gardée) */
static void shell_pub(const char *topic)
{
	bus_publish((long)topic, (long)s_un, -1, 0);
}

/* (poche) napps <- apps ouvertes : pids distincts des fenêtres listées (WS_OP_LIST : normales,
 * hors WF_NOLIST, sans l'accueil) */
static void count_apps(void)
{
	if (!poche)
		return;
	long first = 0, seen = 0, n;
	while ((n = winlist_page(first)) != 0) {
		first += n; /* (ici : de n, pas de WL_PER_MSG, comme l'asm) */
		for (long e = 0; e < n; e++) {
			u32 pid = wl32(e, WL_PID);
			long i;
			for (i = 0; i < seen; i++) /* déjà vu ? */
				if (app_pids[i] == pid)
					break;
			if (i < seen || seen >= 64)
				continue;
			app_pids[seen++] = pid;
		}
	}
	napps = seen;
}

static void clock_text(void);

/* (draw, poche) *right = bord droit libre, base = ligne de base, t = jetons : le compteur (s'il y
 * a des apps), puis l'heure et son point -> *right = à gauche de l'heure */
static void draw_poche_right(long *right, long base, const u8 *t)
{
	long x = *right;
	cnt_x = cnt_x1 = 0; /* (aucune zone) */
	if (napps) {
		*fmt_dec((u64)napps, cntbuf) = 0; /* « n » dans une pastille */
		long w = tw(cntbuf, ui.font_b) + 16; /* largeur de la pastille */
		if (w < 26)
			w = 26;
		x -= w + 8;
		cnt_x = x - 6; /* zone de clic : la pastille et 6 px autour */
		cnt_x1 = x + w + 8;
		ui_rbox(x, (long)((u64)(bar_h - 24) >> 1), w, 24, 7, tj(t, TJ_LINE_STRONG), tj(t, TJ_SUNKEN));
		long cw = tw(cntbuf, ui.font_b);
		ui_text((long)cntbuf, x + (long)((u64)(w - cw) >> 1), base, tj(t, TJ_TEXT1), MB_PX,
			(long)ui.font_b, 0);
		x -= 4;
	}
	clock_text(); /* l'heure */
	long cw = tw(clockbuf, ui.font_b);
	x -= cw + 12;
	clk_x = x - 6;
	clk_x1 = x + cw + 10;
	ui_text((long)clockbuf, x, base, tj(t, TJ_TEXT1), MB_PX, (long)ui.font_b, 0);
	if (unread) { /* des notifications non lues : un point */
		u32 c = tj(t, TJ_ACCENT);
		ui_rbox(x + cw + 1, base - 15, 6, 6, 3, c, c);
	}
	*right = x;
}

/* ---------------------------------------------------------------------------
 *  Menus
 * --------------------------------------------------------------------------- */

static char *wp; /* écriture des libellés en construction (x19 de l'asm) */

/* un libellé (texte) ajouté aux libellés en construction */
static void add_text(const char *s)
{
	while ((*wp++ = *s++) != 0)
		;
}

/* « icône<tab>texte » */
static void add_icon(const char *icon, const char *text)
{
	while (*icon)
		*wp++ = *icon++;
	*wp++ = 9;
	add_text(text);
}

/* « icône<tab>>texte » (entrée à sous-menu) */
static void add_icon_sub(const char *icon, const char *text)
{
	while (*icon)
		*wp++ = *icon++;
	*wp++ = 9;
	*wp++ = '>';
	add_text(text);
}

/* items2 <- titres des fenêtres ouvertes (win_ids : leurs id) -> nombre */
static long windows_items(void)
{
	long first = 0, count = 0, n;
	wp = items2;
	while ((n = winlist_page(first)) != 0) {
		for (long e = 0; e < n; e++) {
			if (count >= AP_MAX)
				goto done;
			win_ids[count++] = *(long *)(wlist + e * WL_SIZE + WL_ID);
			/* (titre borné à son entrée : l'asm le supposait terminé) */
			char *t = (char *)wlist + e * WL_SIZE + WL_TITLE;
			long i = 0;
			while (i < WL_SIZE - WL_TITLE - 1 && t[i])
				*wp++ = t[i++];
			*wp++ = 0;
		}
		first += WL_PER_MSG;
	}
done:
	*wp = 0;
	return count;
}

/* le menu Korin : À propos, catégories, fenêtres ouvertes */
static void armpi_menu(void)
{
	wp = items;
	add_icon(s_bienvenue, tr(s_apropos)); /* À propos (icône de Bienvenue) */
	add_text(s_sep);
	long ncat = 0;
	for (long c = 0; c < ca_n; c++) { /* une ligne par catégorie : l'icône de sa première application */
		if (cat_hidden(c)) /* (Émulateurs : par la Game Library, comme le menubar d'Onyx) */
			continue;
		cat_map[ncat++] = (u8)c;
		long a;
		for (a = 0; a < ap_n; a++)
			if (ap_cat[a] == c)
				break;
		/* (l'asm cherchait sans borne : une catégorie sans application, qui ne peut venir que
		 * d'un manifeste à deux « category », n'a pas d'icône ici) */
		add_icon_sub(a < ap_n ? ap_nom[a] : "", tr(ca_nom[c])); /* « icône<tab>>catégorie » */
	}
	add_text(s_sep);
	add_text(tr(s_fenetres));
	*wp = 0;
	/* menu gardé ouvert pendant ses sous-menus : icônes, garder ouvert */
	struct krt_r4 r = popup_ex(4, bar_h - 2, items, MENU_W, 3, 0, 0);
	for (;;) { /* r0 = choix, r1 = y de sa ligne */
		if (r.r0 < 0) { /* abandon : tout est fermé ; un clic dehors : gardé (un autre titre ?) */
			if (r.r0 == -2) {
				pend_x = r.r1;
				pend_y = r.r2;
			}
			return;
		}
		if (r.r0 == 0) { /* À propos */
			long a = app_find(s_bienvenue);
			if (a >= 0)
				app_launch(a);
			return;
		}
		long liney = r.r1; /* (y de la ligne : le sous-menu s'y ouvre) */
		u64 line = (u64)r.r0 - 2;
		long kind; /* 0 : applications, 1 : fenêtres */
		if (line < (u64)ncat) { /* les applications de la catégorie */
			u64 cat = cat_map[line];
			long n = 0;
			wp = items2;
			for (long i = 0; i < ap_n; i++) {
				if (ap_cat[i] != cat)
					continue;
				sub_map[n++] = (u8)i;
				add_icon(ap_nom[i], ap_lib[i]);
			}
			*wp = 0;
			kind = 0;
		} else { /* fenêtres ouvertes */
			if (!windows_items()) {
				r = popup_ex(0, 0, 0, 0, 1 + 2 + 4, 0, 0); /* (reprise) */
				continue;
			}
			kind = 1;
		}
		/* sous-menu à droite, sa première ligne en face de la ligne choisie */
		struct krt_r4 s = popup_ex(4 + MENU_W - 6, liney - 6, items2, kind ? 260 : 220, kind ? 0 : 1,
					   0, 0);
		if (s.r0 < 0) {
			/* clic hors du sous-menu : le menu le traite (sur lui : un choix ; ailleurs : tout
			 * se ferme) ; Échap : retour au menu */
			if (s.r0 == -2)
				r = popup_ex(0, 0, 0, 0, 1 + 2 + 4 + 8, s.r1, s.r2);
			else
				r = popup_ex(0, 0, 0, 0, 1 + 2 + 4, 0, 0);
			continue;
		}
		ui_popup_close(); /* le menu se ferme */
		if (!kind)
			app_launch(sub_map[s.r0]);
		else
			ui_raise(win_ids[s.r0]);
		return;
	}
}

/* (app_menu) l'état de la construction : les id des lignes, leur nombre, la largeur maximale, et pour
 * une ligne à sous-menu (« S<titre> ») la ligne qui suit son titre dans le texte des menus */
static long *mb_ids_p = mb_ids, mb_lines, mb_maxw;
static char *mb_sub[MB_ITEMS];
static long mb_ids2[MB_ITEMS]; /* le sous-menu ouvert : id de chaque ligne */

/* (app_menu) label = « libellé[<tab>raccourci] » (fin : NUL), id : ajoute la ligne */
static void mb_item(const char *label, long id)
{
	mb_ids_p[mb_lines++] = id;
	char *lab = wp; /* libellé */
	while (*label && *label != 9)
		*wp++ = *label++;
	*wp = 0;
	long w = tw(lab, ui.font); /* largeur du libellé */
	if (*label && label[1]) { /* raccourci (vide : aucun) */
		label++;
		*wp++ = 9;
		char *sc = wp;
		while (*label && *label != 9)
			*wp++ = *label++;
		*wp = 0;
		w += tw(sc, ui.font) + 24; /* écart libellé - raccourci */
	}
	if ((u64)w > (u64)mb_maxw)
		mb_maxw = w;
	wp++; /* (après le NUL) */
}

/* ligne suivante du texte des menus */
static char *mb_next(char *p)
{
	while (p < mb_end && *p)
		p++;
	return p < mb_end ? p + 1 : mb_end;
}

/* (app_menu) les lignes d'un menu à partir de p (après « M<titre> » ; sub : après « S<titre> »)
 * jusqu'au menu suivant (« M ») ou à la fin du sous-menu (« E »). Dans un menu, « S<titre> »
 * … « E » est un sous-menu : une ligne « >titre » (id -3), ses lignes à part (mb_sub) ; un
 * sous-menu dans un sous-menu est sauté (la barre n'en montre qu'un niveau). */
static void mb_lines_of(char *p, long sub)
{
	while (p < mb_end) {
		char c = *p;
		if (c == 'M' || (c == 'E' && sub)) /* menu suivant, fin du sous-menu : fini */
			break;
		if (mb_lines >= MB_ITEMS)
			break;
		if (c == '-') { /* séparateur */
			*wp++ = '-';
			*wp++ = 0;
			mb_ids_p[mb_lines++] = -2;
		} else if (c == 'S') { /* sous-menu : son titre, puis jusqu'à son « E » */
			char *after = mb_next(p);
			if (!sub) {
				mb_sub[mb_lines] = after;
				*wp++ = '>';
				mb_item(p + 1, -3);
			}
			long depth = 1;
			for (p = after; p < mb_end && depth; p = mb_next(p)) {
				if (*p == 'S')
					depth++;
				else if (*p == 'E')
					depth--;
				else if (*p == 'M')
					break;
			}
			continue;
		} else if (c == 'I') { /* « I<id><tab><libellé><tab><raccourci> » */
			const char *q = p + 1;
			long id = 0;
			while ((u8)(*q - '0') <= 9)
				id = id * 10 + (*q++ - '0');
			/* marques : « d » grisée, « c » cochée (le menu les reçoit en tête du libellé :
			 * 0x01, 0x02 : ui_popup_ex) */
			for (;;) {
				if (*q == 'd')
					*wp++ = 1;
				else if (*q == 'c')
					*wp++ = 2;
				else
					break;
				q++;
			}
			if (*q == 9)
				q++;
			mb_item(q, id);
		}
		p = mb_next(p);
	}
}

/* largeur d'un menu : 180 à 420 */
static long mb_width(void)
{
	u64 w = (u64)mb_maxw + 60;
	if (w < 180)
		w = 180;
	if (w > 420)
		w = 420;
	return (long)w;
}

/* t = titre (1 : menu au nom de l'application, 2... : ses menus) : le menu déroulant ; un choix
 * part au programme (ui_menu_command). Un sous-menu s'ouvre à droite de sa ligne, le menu
 * restant ouvert (comme le menu Korin). */
static void app_menu(long t)
{
	wp = items;
	mb_ids_p = mb_ids;
	mb_lines = 0;
	mb_maxw = 0;
	if (t == help_t) { /* le menu Aide : ses lignes d'abord */
		char *e = str_cat(hbuf, tr(s_aide_de)); /* « Aide de <nom> » (-10) */
		*e++ = ' ';
		str_cat(e, help_name);
		mb_item(hbuf, -10);
		mb_item(tr(s_sommaire), -11); /* « Sommaire de l'aide » (-11) */
		if ((u64)t >= (u64)mb_n) /* titre ajouté par la barre : rien de plus */
			goto show;
		*wp++ = '-'; /* menu Aide de l'application : un trait, puis ses lignes */
		*wp++ = 0;
		mb_ids[mb_lines++] = -2;
	} else if (t == 1) { /* menu de l'application : ses lignes (« M* »), puis Quitter (-1) */
		if (mb_app) {
			mb_lines_of(mb_next(mb_app - 1), 0);
			if (mb_lines) {
				*wp++ = '-';
				*wp++ = 0;
				mb_ids[mb_lines++] = -2;
			}
		}
		mb_item(tr(s_quitter), -1);
		goto show;
	}
	/* lignes après « M<titre> » */
	mb_lines_of(mb_next(mb_ptr[t] - 1), 0);
show:
	*wp = 0;
	if (!mb_lines)
		return;
	long x = mb_x0[t] - 2, w = mb_width();
	/* sous le titre (comme le menu Korin) ; « texte<tab>raccourci » ; gardé ouvert sur un sous-menu */
	struct krt_r4 r = popup_ex(x, bar_h - 2, items, w, 16 + 2, 0, 0);
	long id;
	for (;;) { /* r0 = choix, r1 = y de sa ligne */
		if (r.r0 < 0) {
			if (r.r0 == -2) { /* clic dehors : gardé (un autre titre ?) */
				pend_x = r.r1;
				pend_y = r.r2;
			}
			return;
		}
		id = mb_ids[r.r0];
		if (id != -3) { /* une entrée (le menu se ferme) */
			ui_popup_close();
			break;
		}
		/* sous-menu : ses lignes dans items2, à droite, sa première ligne en face */
		long liney = r.r1;
		char *keep = wp;
		wp = items2;
		mb_ids_p = mb_ids2;
		long lines = mb_lines, maxw = mb_maxw;
		mb_lines = 0;
		mb_maxw = 0;
		mb_lines_of(mb_sub[r.r0], 1);
		*wp = 0;
		long n = mb_lines, w2 = mb_width();
		mb_ids_p = mb_ids;
		mb_lines = lines;
		mb_maxw = maxw;
		wp = keep;
		if (!n) {
			r = popup_ex(0, 0, 0, 0, 16 + 2 + 4, 0, 0); /* (reprise) */
			continue;
		}
		struct krt_r4 s2 = popup_ex(x + w - 6, liney - 6, items2, w2, 16, 0, 0);
		if (s2.r0 < 0) {
			/* clic hors du sous-menu : le menu le traite (sur lui : un choix ; ailleurs : tout
			 * se ferme) ; Échap : retour au menu */
			if (s2.r0 == -2)
				r = popup_ex(0, 0, 0, 0, 16 + 2 + 4 + 8, s2.r1, s2.r2);
			else
				r = popup_ex(0, 0, 0, 0, 16 + 2 + 4, 0, 0);
			continue;
		}
		ui_popup_close(); /* le menu se ferme */
		id = mb_ids2[s2.r0];
		break;
	}
	if (id == -2)
		return;
	if (id == -10) /* aide de l'application */
		spawn(p_help, help_path);
	else if (id == -11) /* sommaire de l'aide */
		spawn(p_help, 0);
	else
		ui_menu_command(id, mb_serial);
}

/* x dans la barre -> titre dessous (0 : Korin ; -1 : aucun) */
static long title_at(long x)
{
	for (long i = 0; i < mb_shown; i++)
		if (x >= mb_x0[i] && x < mb_x1[i])
			return i;
	return -1;
}

/* t = titre cliqué (0 : Korin, 1 : l'application, 2... : ses menus) : ouvre son menu ; un clic
 * sur un autre titre pendant qu'il est ouvert ouvre celui-là (sur le même : il se ferme) */
static void open_menus(long t)
{
	for (;;) {
		pend_y = -1; /* (pas de clic dehors gardé) */
		open_menu = t + 1;
		draw();
		if (!t)
			armpi_menu();
		else
			app_menu(t);
		open_menu = 0;
		/* menu fermé par un clic dans la barre, sur un autre titre ? */
		if (pend_y < 0 || pend_y >= bar_h)
			return;
		long n = title_at(pend_x);
		if (n < 0 || n == t)
			return;
		t = n;
	}
}

/* texte des menus (spec_buf) -> mb_ptr (titres ; [1] : le nom de l'application), mb_n ; chaque
 * ligne devient une chaîne (NUL à la place de « \n », CR retiré) */
static void menu_parse(void)
{
	mb_n = 1;
	mb_app = 0;
	if (!mb_serial) /* pas de menu : Korin seul */
		return;
	mb_ptr[1] = app_title;
	long k = 2;
	char *p = spec_buf;
	while (p < mb_end) {
		if (*p == 'M' && p[1] == '*' && (p[2] == 10 || p[2] == 13 || !p[2]) && !mb_app)
			mb_app = p + 1; /* « M* » : les lignes du menu au nom de l'application */
		else if (*p == 'M' && k < MB_MAX)
			mb_ptr[k++] = p + 1;
		while (p < mb_end) {
			char c = *p;
			if (!c || c == 10) {
				*p++ = 0;
				break;
			}
			if (c == 13)
				*p = 0;
			p++;
		}
	}
	mb_n = k;
}

/* lit le menu de la fenêtre active (ui_menu_get) -> 1 s'il a changé (titres refaits) */
static long menu_poll(void)
{
	struct krt_r4 r = krt_call_r4(ui_menu_get, (long)spec_buf, (long)app_title, mb_serial, 0);
	if (r.r0 == mb_serial)
		return 0;
	mb_serial = r.r0;
	mb_end = spec_buf + r.r2; /* fin du texte */
	menu_parse();
	return 1;
}

/* le titre d'un menu de l'application -> 1 si c'est son menu Aide */
static long is_help_title(const char *s)
{
	if (str_eq(s, s_aide) || str_eq(s, s_help))
		return 1;
	return str_eq(s, tr(s_aide)) != 0;
}

/* (draw) x = fin du menu Korin, base = ligne de base : le nom de l'application (gras) puis ses
 * menus ; zones de clic dans mb_x0, mb_x1 */
static void draw_titles(long x, long base)
{
	x += 10; /* x du texte */
	long t = 1;
	long limit = ui.w - 158; /* limite : avant l'heure et le réseau */
	help_t = -1; /* (le menu Aide : retrouvé ci-dessous) */
	for (;; t++) {
		const char *s;
		const u8 *font = ui.font;
		if (t < mb_n) {
			s = mb_ptr[t];
			if (t == 1)
				font = ui.font_b; /* le nom de l'application, en gras */
			else if (is_help_title(s) && help_on) /* son menu Aide (ou Help) ? */
				help_t = t;
		} else if (t > mb_n) {
			break;
		} else {
			/* après les menus : Aide, si l'application en a une et pas déjà un menu Aide */
			if (!help_on || help_t >= 0 || t >= MB_MAX)
				break;
			s = tr(s_aide);
		}
		u64 w = (u64)tw(s, font);
		if (w > 220)
			w = 220;
		if (x + (long)w > limit) { /* plus de place */
			if ((u64)help_t >= (u64)t) /* menu Aide pas montré : sans effet */
				help_t = -1;
			break;
		}
		if (t == mb_n) /* (le titre Aide ajouté : il est montré) */
			help_t = t;
		mb_x0[t] = x - 8; /* zone : 8 px de chaque côté */
		mb_x1[t] = x + (long)w + 8;
		const u8 *k = tokens(); /* ouvert, survolé */
		long box = 0;
		u32 c = 0;
		if (open_menu == t + 1) {
			c = tj(k, TJ_ACCENT_LIGHT);
			box = 1;
		} else if (hov == t + 1) {
			c = glass_hover(); /* (sur le verre : translucide) */
			box = 1;
		}
		if (box)
			glass_rbox(x - 8, 4, (long)w + 16, bar_h - 8, 5, c);
		ui_text((long)s, x, base, tj(k, TJ_TEXT1), MB_PX, (long)font, (long)w + 4);
		x += (long)w + 18;
	}
	mb_shown = t;
}

/* ---------------------------------------------------------------------------
 *  Réseau, son, PocketQI
 * --------------------------------------------------------------------------- */

/* netbuf <- adresse IPv4 (« a.b.c.d »), ou « hors ligne » */
static void net_text(void)
{
	net_info((long)netinfo);
	u32 ip = *(u32 *)netinfo;
	if (!ip) {
		str_cat(netbuf, tr(str_off));
		return;
	}
	char *e = netbuf;
	for (long i = 0; i < 4; i++) { /* (adresse : octet de poids fort en tête) */
		e = fmt_dec((ip >> (24 - 8 * i)) & 0xFF, e);
		if (i < 3)
			*e++ = '.';
	}
	*e = 0;
}

/* menu du réseau : l'adresse */
static void net_menu(void)
{
	net_text();
	wp = items;
	add_text(tr(s_reseau));
	wp--;
	add_text(netbuf);
	*wp = 0;
	popup(net_x - 160, bar_h - 2, items, 200);
}

/* un clic sur l'icône du réseau : avec le Wi-Fi, le menu Wi-Fi (wifimenu, sous l'icône ; un
 * second clic le ferme) ; sinon l'adresse */
static void net_click(void)
{
	wifi_status((long)wst);
	if (*(u32 *)wst < WIFI_ST_READY) {
		net_menu();
		return;
	}
	*fmt_dec((u64)net_x, argbuf) = 0;
	spawn(p_wifimenu, argbuf);
}

/* l'état du réseau relu (netinfo, wst, nact) */
static void net_refresh(void)
{
	net_info((long)netinfo);
	wifi_status((long)wst);
	net_active((long)nact);
}

/* (draw) icône du réseau en x, milieu de la barre : filaire (relié), barres du Wi-Fi, ou barres
 * éteintes et croix (pas de réseau) ; l'état est celui du dernier événement du bus (sans busd :
 * relu ici) */
static void net_icon(long x)
{
	if (!net_sub)
		net_refresh();
	u32 ip = *(u32 *)netinfo; /* adresse IPv4 (0 : aucune) */
	const u8 *k = tokens();
	u32 on = tj(k, TJ_TEXT1), off = tj(k, TJ_TEXT3);
	long top = (long)((u64)bar_h >> 1) - 7; /* haut de l'icône */
	if (*(u32 *)(nact + 8) != NIF_WIFI && ip) { /* Ethernet, USB : filaire */
		/* une boîte en haut, deux en bas, reliées */
		ui_rbox(x + 5, top, 6, 5, 1, on, on);
		ui_fill(x + 7, top + 5, 2, 3, on);
		ui_fill(x + 2, top + 7, 12, 1, on);
		ui_fill(x + 2, top + 7, 1, 3, on);
		ui_fill(x + 13, top + 7, 1, 3, on);
		ui_rbox(x, top + 9, 5, 5, 1, on, on);
		ui_rbox(x + 11, top + 9, 5, 5, 1, on, on);
		return;
	}
	if (*(u32 *)wst == WIFI_ST_CONNECTED) {
		wd_bars(x, top, wd_level(*(i32 *)(wst + 4)), on, off);
		return;
	}
	for (long i = 0; i < 4; i++) { /* barres de 4, 7, 10, 13, éteintes */
		long h = 4 + 3 * i;
		ui_fill(x + 4 * i, top + 13 - h, 3, h, off);
	}
	ui_text((long)s_croix, x + 11, top + 15, on, 9, (long)ui.font_b, 0);
}

/* -> 1 si l'état du son a changé (génération de sound) */
static long snd_poll(void)
{
	snd_st = (const u8 *)ak_state();
	long gen = snd_st ? *(const long *)(snd_st + SS_GEN) : -1; /* (sans service : -1) */
	long changed = gen != snd_gen;
	snd_gen = gen;
	return changed;
}

/* (draw) haut-parleur en x : barre si la principale est muette ; rien sans service sound */
static void snd_icon(long x)
{
	if (!snd_st)
		return;
	u32 mute = 0;
	u64 main_out = *(const u64 *)(snd_st + SS_MAIN);
	for (long i = 0; i < SS_OUT_MAX; i++) {
		const u8 *o = snd_st + SS_OUTS + 64 * i;
		if (SS_U32(o, SO_ID) == main_out)
			mute = SS_U32(o, SO_MUTE);
	}
	const u8 *k = tokens();
	sd_speaker(x, (long)((u64)bar_h >> 1) - 7, mute, tj(k, TJ_TEXT1), tj(k, TJ_TEXT3), &ui);
}

/* un clic sur le haut-parleur : le volet du son (soundmenu, sous l'icône ; un second clic le
 * ferme) ; clic droit : « Avancé… » -> le Mixer */
static void snd_click(void)
{
	if (*(u32 *)(ui.ev + EV_CODE) & 2) { /* boutons : 2 droit */
		tr_block(s_avance, items);
		if (popup(snd_x - 150, bar_h - 2, items, 180) == 0) /* (0 : la ligne ; -1 : rien) */
			spawn(p_mixer, 0);
		return;
	}
	*fmt_dec((u64)snd_x, argbuf) = 0;
	spawn(p_soundmenu, argbuf);
}

/* ---- batterie (PC : acpid, docs/etude/BATTERIE-PC.md) : « pct min secteur état profil auto » */
static const char s_bat_topic[] = "korin.power.battery";
static const char s_bat_reste[] = "restantes";
static const char s_bat_pleine[] = "pleine dans";
static const char s_bat_sur[] = "Sur batterie";
static const char s_bat_charge[] = "Secteur branché, en charge";
static const char s_bat_full[] = "Secteur branché, batterie pleine";
static const char s_bat_idle[] = "Secteur branché";
static const char s_bat_profil[] = "Profil d'énergie";
static const char s_bat_auto[] = "Automatique";
static const char s_bat_perf[] = "Performance";
static const char s_bat_eq[] = "Équilibré";
static const char s_bat_eco[] = "Économie";
static const char s_bat_titre[] = "Batterie";
static const char s_bat_reglages[] = "Réglages d'énergie…";
static const char p_control[] = "/apps/control.app/main";

static long parse_int(const char **p)
{
	const char *s = *p;
	while (*s == ' ')
		s++;
	long neg = *s == '-';
	if (neg)
		s++;
	long v = 0;
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	*p = s;
	return neg ? -v : v;
}

/* l'état de la batterie relu sur le bus ; sans valeur (pas d'acpid : le Pi) : pas d'icône */
static void bat_read(void)
{
	char v[64];
	long n = bus_get((long)s_bat_topic, (long)v, sizeof v - 1);
	if (n <= 0) {
		bat_state = 'n';
		return;
	}
	v[n < (long)sizeof v - 1 ? n : (long)sizeof v - 1] = 0;
	const char *p = v;
	bat_pct = parse_int(&p);
	bat_min = parse_int(&p);
	bat_ac = parse_int(&p);
	while (*p == ' ')
		p++;
	bat_state = *p ? *p++ : 'n';
	bat_prof = parse_int(&p);
	bat_auto = *p ? parse_int(&p) : 1;
}

/* (draw) la batterie en x (22 x 12 : corps 19 x 12 et borne), son niveau en couleur (alerte sous
 * 20 %, danger sous 10 %, sans secteur), un éclair en charge */
static void bat_icon(long x)
{
	const u8 *k = tokens();
	u32 ink = tj(k, TJ_TEXT1), bar = tj(k, TJ_BAR) & 0xFFFFFF;
	long top = (long)((u64)bar_h >> 1) - 6;
	ui_rbox(x, top, 19, 12, 3, ink, ink); /* le corps : contour plein, puis le creux (opaque) */
	ui_fill(x + 1, top + 1, 17, 10, bar);
	ui_fill(x + 19, top + 4, 2, 4, ink); /* la borne */
	long pct = bat_pct < 0 ? 0 : bat_pct > 100 ? 100 : bat_pct;
	long w = (15 * pct + 50) / 100;
	if (w < 1 && bat_pct > 0)
		w = 1;
	u32 lvl = ink;
	if (!bat_ac && bat_pct >= 0 && bat_pct <= 10)
		lvl = tj(k, TJ_DANGER);
	else if (!bat_ac && bat_pct >= 0 && bat_pct <= 20)
		lvl = tj(k, TJ_WARNING);
	if (w)
		ui_fill(x + 2, top + 2, w, 8, lvl);
	if (bat_state == 'c') { /* éclair, dans la couleur de la barre puis contouré */
		static const signed char bolt[][3] = {{10, 1, 2}, {9, 2, 2}, {8, 3, 2}, {7, 4, 2}, {7, 5, 5},
						      {9, 6, 2}, {8, 7, 2}, {7, 8, 2}, {6, 9, 2}, {6, 10, 1}};
		for (unsigned i = 0; i < sizeof bolt / sizeof bolt[0]; i++)
			ui_fill(x + bolt[i][0] - 1, top + bolt[i][1], bolt[i][2] + 2, 1, bar);
		for (unsigned i = 0; i < sizeof bolt / sizeof bolt[0]; i++)
			ui_fill(x + bolt[i][0], top + bolt[i][1], bolt[i][2], 1, ink);
	}
}

/* « 1 h 02 » ou « 35 min » */
static char *bat_time(char *p, long min)
{
	if (min >= 60) {
		p = fmt_dec((u64)(min / 60), p);
		p = str_cat(p, " h ");
		if (min % 60 < 10)
			*p++ = '0';
		return fmt_dec((u64)(min % 60), p);
	}
	p = fmt_dec((u64)min, p);
	return str_cat(p, " min");
}

/* un clic sur la batterie : l'état (grisé) et le profil d'énergie (coché), choisi chez acpid */
static void bat_click(void)
{
	char l1[96], *p = l1;
	*p++ = 1; /* (grisé : une information) */
	p = str_cat(p, tr(s_bat_titre));
	p = str_cat(p, " : ");
	p = fmt_dec((u64)(bat_pct < 0 ? 0 : bat_pct), p);
	p = str_cat(p, " %");
	if (bat_min >= 0 && (bat_state == 'd' || bat_state == 'c')) {
		p = str_cat(p, ", ");
		if (bat_state == 'c') {
			p = str_cat(p, tr(s_bat_pleine));
			p = str_cat(p, " ");
			p = bat_time(p, bat_min);
		} else {
			p = bat_time(p, bat_min);
			p = str_cat(p, " ");
			p = str_cat(p, tr(s_bat_reste));
		}
	}
	*p = 0;
	wp = items;
	add_text(l1);
	wp[-1] = 0;
	*wp++ = 1;
	add_text(tr(bat_state == 'c' ? s_bat_charge : bat_state == 'f' ? s_bat_full
						: bat_ac ? s_bat_idle : s_bat_sur));
	long profiles = bat_prof >= 0; /* (le processeur a des profils : HWP ou EIST ; sinon -1) */
	if (profiles) {
		add_text(s_sep);
		*wp++ = 1;
		add_text(tr(s_bat_profil));
		static const char *const names[4] = {s_bat_auto, s_bat_perf, s_bat_eq, s_bat_eco};
		for (long i = 0; i < 4; i++) {
			long on = i == 0 ? bat_auto : !bat_auto && bat_prof == i - 1;
			if (on)
				*wp++ = 2; /* coché */
			add_text(tr(names[i]));
		}
	}
	add_text(s_sep);
	add_text(tr(s_bat_reglages));
	*wp = 0;
	long r = popup(bat_x - 150, bar_h - 2, items, 260);
	if (r == (profiles ? 9 : 3)) { /* Réglages, volet Énergie */
		spawn(p_control, "power");
		return;
	}
	if (!profiles || r < 4 || r > 7)
		return;
	long ch = korin_sys2(SYS_SVC_OPEN, "acpid", 200);
	if (ch < 0)
		return;
	u64 m[MSG_SIZE / 8] __attribute__((aligned(16)));
	memset(m, 0, sizeof m);
	m[0] = 2;     /* ACPID_OP_PROFILE */
	m[1] = (u64)(r - 5); /* -1 automatique, 0..2 */
	korin_sys2(SYS_SVC_CALL, ch, m);
}

/* PocketQI installée ? (son exécutable) -> pq_on ; sinon pas d'icône, et sa zone de clic hors de
 * la barre */
static void pq_probe(void)
{
	pq_x = -100;
	long fd = sys_open(p_pocketqi, O_READ);
	if (fd < 0)
		return;
	sys_close(fd);
	pq_on = 1;
}

/* (draw) l'icône de PocketQI en x : une bulle pleine, sa pointe en bas à gauche, et les deux yeux
 * de Pocket dans la couleur de la barre */
static void pq_icon(long x)
{
	const u8 *k = tokens();
	u32 ink = tj(k, TJ_TEXT1), bar = tj(k, TJ_BAR);
	long top = (long)((u64)bar_h >> 1) - 7; /* haut de l'icône */
	ui_rbox(x, top, 16, 11, 4, ink, ink); /* la bulle */
	ui_fill(x + 3, top + 10, 4, 2, ink);  /* la pointe */
	ui_fill(x + 3, top + 12, 2, 2, ink);
	ui_fill(x + 4, top + 3, 3, 5, bar & 0xFFFFFF); /* les yeux (opaques sur la bulle) */
	ui_fill(x + 9, top + 3, 3, 5, bar & 0xFFFFFF);
}

/* un clic sur l'icône de PocketQI : sa bulle sous l'icône (« --popup x » ; un second clic : une
 * seconde bulle ferme la première et s'arrête) */
static void pq_click(void)
{
	*fmt_dec((u64)pq_x, str_cat(argbuf, s_popup)) = 0;
	spawn(p_pocketqi, argbuf);
}

/* ---------------------------------------------------------------------------
 *  Aide de l'application active
 * --------------------------------------------------------------------------- */

/* (help_probe) lang : help_path <- .../help_<langue>.md -> 1 s'il existe */
static long help_try(const char *lang)
{
	str_cat(str_cat(help_tail, lang), ".md");
	long fd = sys_open(help_path, O_READ);
	if (fd < 0)
		return 0;
	sys_close(fd);
	return 1;
}

/* l'aide de l'application active : son process (WL_PID, nom dans la liste des process :
 * « letters » pour /apps/letters.app/main), puis /apps/<nom>.app/help_<langue>.md, ou
 * help_en.md, ou help_fr.md -> help_on, help_path, help_name ; refait seulement quand le process
 * actif change */
static void help_probe(void)
{
	long pid = 0, first = 0, n;
	while ((n = winlist_page(first)) != 0) {
		for (long e = 0; e < n; e++)
			if (wl32(e, WL_STATE) & 1) {
				pid = wl32(e, WL_PID);
				goto found;
			}
		first += WL_PER_MSG;
	}
found: /* (pid 0 : aucune fenêtre active) */
	if (pid == help_pid)
		return;
	help_pid = pid;
	help_on = 0;
	if (!pid)
		return;
	if (ksys(SYS_PROC_INFO, pid, (long)plist, 0, 0, 0, 0)) /* son nom */
		return;
	char *name = (char *)plist + 24;
	name[31] = 0;
	long a = app_find(name); /* une application installée ? */
	if (a < 0)
		return;
	str_copy_n(help_name, ap_lib[a], AP_LIB); /* son nom affiché */
	help_tail = str_cat(str_cat(str_cat(help_path, ap_path), name), s_help_f);
	if (help_try(lang_get()) || help_try("en") || help_try("fr"))
		help_on = 1;
}

/* -> titre de la fenêtre active (0 : aucune) */
static const char *active_title(void)
{
	long first = 0, n;
	while ((n = winlist_page(first)) != 0) {
		for (long e = 0; e < n; e++)
			if (wl32(e, WL_STATE) & 1)
				return (const char *)wlist + e * WL_SIZE + WL_TITLE;
		first += WL_PER_MSG;
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Heure
 * --------------------------------------------------------------------------- */

/* décalage de l'heure (secondes), gardé par l'Horloge */
static void load_offset(void)
{
	offset = 0;
	long fd = sys_open(p_cfg, O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, cfgbuf, 31);
	sys_close(fd);
	if (n <= 0)
		return;
	cfgbuf[n] = 0;
	offset = parse_dec(cfgbuf);
}

/* korin.clock.offset publié : le décalage (sa valeur, comme clock.cfg) */
static void clock_from_bus(void)
{
	long n = bus_get((long)s_clk_topic, (long)cfgbuf, 31);
	if (n <= 0)
		return;
	if (n > 31) /* (l'asm ne bornait pas : une valeur de plus de 31 octets écrivait plus loin) */
		n = 31;
	cfgbuf[n] = 0;
	offset = parse_dec(cfgbuf) % 86400;
}

/* x = destination, v = 0..99 -> deux chiffres ; rend la suite */
static char *two_digits(char *d, u64 v)
{
	*d++ = (char)('0' + v / 10);
	*d++ = (char)('0' + v % 10);
	return d;
}

/* clockbuf <- « HH:MM » */
static void clock_text(void)
{
	u64 s = ((u64)sys_time_ms() / 1000 + offset) % 86400;
	char *e = two_digits(clockbuf, s / 3600);
	*e++ = ':';
	e = two_digits(e, s % 3600 / 60);
	*e = 0;
}

/* toutes les MB_POLL ms : le menu de la fenêtre active (le son : sans busd seulement) ; chaque
 * seconde : une autre minute ? (sans busd : clock.cfg relu) -> 1 si la barre est à redessiner */
static long periodic(void)
{
	u64 now = (u64)sys_time_ms();
	if (now - t_poll < MB_POLL)
		return 0;
	t_poll = now;
	if (menu_poll()) { /* un autre menu : peut-être une autre application (à redessiner) */
		help_probe();
		return 1;
	}
	if (!snd_sub && snd_poll()) /* (sans bus : le son a-t-il changé ?) */
		return 1;
	if (now - t_clock < 1000)
		return 0;
	t_clock = now;
	if (!clk_sub) /* sans bus : clock.cfg relu */
		load_offset();
	u64 min = (now / 1000 + offset) / 60; /* la minute affichée a-t-elle changé ? */
	long changed = min != last_min;
	last_min = min;
	return changed;
}

/* ---------------------------------------------------------------------------
 *  Dessin
 * --------------------------------------------------------------------------- */

static void draw(void)
{
	const u8 *k = tokens();
	long bh = bar_h;
	glass_set(bh, TJ_BAR, GLASS_EDGE_BOTTOM); /* le verre (glass.c), comme le panel ; bord en bas */
	glass_draw(ui.w);
	u32 text1 = tj(k, TJ_TEXT1);
	long end = tw(str_armpi, ui.font_b) + 31 + 8; /* menu Korin : fin de la zone */
	if (poche) /* (poche : le losange seul) */
		end = 36;
	mb_x1[0] = end; /* (titre 0 : de 0 à la fin de la zone) */
	mb_x0[0] = 0;
	mb_shown = 1;
	u32 c = 0; /* survol, ouvert */
	long box = 0;
	if (open_menu == 1 || (ov_shown && !poche)) { /* (menu ouvert, ou vue d'ensemble) */
		c = tj(k, TJ_ACCENT_LIGHT);
		box = 1;
	} else if (hov == 1) {
		c = glass_hover();
		box = 1;
	}
	if (box)
		glass_rbox(6, 4, end - 6, bh - 8, 5, c);
	long base = (long)((u64)bh >> 1) + 5; /* ligne de base : milieu + 5 */
	ui_text((long)s_losange, 12, base, tj(k, TJ_ACCENT), MB_PX + 2, (long)ui.font, 0);
	if (!poche)
		ui_text((long)str_armpi, 31, base, text1, MB_PX, (long)ui.font_b, 0);
	if (mb_serial) { /* menus de l'application active ? */
		draw_titles(end, base);
	} else {
		const char *t = active_title(); /* sinon, le titre de la fenêtre active */
		if (t) {
			ui_text((long)t, end + 10, base, text1, MB_PX, (long)ui.font_b, 320);
			/* puis le menu Aide (draw_titles : rien d'autre, mb_n vaut 1) */
			u64 w = (u64)tw(t, ui.font_b);
			if (w > 320)
				w = 320;
			draw_titles((long)w + end + 18, base);
		}
	}
	long x;
	if (poche) { /* poche : compteur, heure (et son point) */
		x = ui.w;
		draw_poche_right(&x, base, k);
	} else { /* heure, à droite */
		clock_text();
		if (tray_hov && tray_x0 < ui.w) /* la zone d'état survolée : une pilule de verre */
			glass_rbox(tray_x0, 3, ui.w - 4 - tray_x0, bh - 6, (bh - 6) / 2, glass_hover());
		x = ui.w - tw(clockbuf, ui.font_b) - 12;
		ui_text((long)clockbuf, x, base, text1, MB_PX, (long)ui.font_b, 0);
	}
	if (bat_state != 'n') { /* batterie : icône 22 x 12, et son pourcentage à gauche */
		x -= 14 + 21;
		bat_icon(x);
		char *p = fmt_dec((u64)(bat_pct < 0 ? 0 : bat_pct), batbuf);
		str_cat(p, " %");
		long tx = x - 6 - tw(batbuf, ui.font);
		if (bat_pct >= 0)
			ui_text((long)batbuf, tx, base, text1, MB_PX - 1, (long)ui.font, 0);
		bat_x = bat_pct >= 0 ? tx : x;
		bat_x1 = x + 21;
		x = bat_x + 2;
	} else
		bat_x = -100;
	x -= 14 + 16; /* réseau : icône 16 x 14 */
	net_x = x;
	net_icon(x);
	x -= 12 + 16; /* son : haut-parleur 16 x 14 */
	snd_x = x;
	snd_icon(x);
	tray_x0 = poche ? 1 << 30 : x - 10;
	if (pq_on) { /* PocketQI : bulle 16 x 14 */
		x -= 12 + 16;
		pq_x = x;
		pq_icon(x);
	}
	ui_update_all();
}

/* ---------------------------------------------------------------------------
 *  Entrée et boucle
 * --------------------------------------------------------------------------- */

/* un appui hors des titres (x : abscisse) : compteur et heure (poche), PocketQI, son, réseau */
static void press_right(long x)
{
	if (poche) { /* poche : le compteur, l'heure */
		if (x >= cnt_x && x < cnt_x1) {
			shell_pub(s_t_switch);
			return;
		}
		if (x >= clk_x && x < clk_x1) {
			shell_pub(s_t_quick);
			return;
		}
	}
	if ((u64)(x - pq_x + 4) < 24) { /* PocketQI */
		pq_click();
		return;
	}
	if (!poche && x >= tray_x0) { /* bureau : toute la zone d'état ouvre les réglages rapides (un second
					 clic les ferme) */
		*fmt_dec((u64)(ui.w - 4), argbuf) = 0;
		spawn(p_quickmenu, argbuf);
		return;
	}
	if (bat_x > 0 && x >= bat_x - 4 && x < bat_x1 + 6) { /* batterie */
		bat_click();
		return;
	}
	if ((u64)(x - snd_x + 4) < 24) { /* haut-parleur */
		snd_click();
		return;
	}
	if ((u64)(x - net_x) < 24) /* icône du réseau */
		net_click();
}

/* un événement du bus (WEV_BUS) */
static void on_bus(void)
{
	u64 sub = *(u32 *)(ui.ev + BEV_SUB);
	if (sub == (u64)snd_sub) { /* korin.audio.state : le haut-parleur */
		snd_poll();
	} else if (sub == (u64)clk_sub) { /* korin.clock.offset : l'heure réglée */
		clock_from_bus();
	} else if (sub == (u64)unread_sub) { /* korin.notify.unread : le nombre */
		u64 v = 0;
		if (*(u32 *)(ui.ev + BEV_LEN) != (u32)BUS_DELETED) {
			char *d = (char *)ui.ev + BEV_DATA;
			d[BUS_INLINE - 1] = 0; /* (un nombre court : tout est là) */
			v = parse_dec(d);
		}
		unread = v;
	} else if (sub == (u64)net_sub) { /* korin.net.* : le réseau a changé */
		net_refresh();
	} else if (sub == (u64)bat_sub) { /* korin.power.battery : la batterie */
		bat_read();
	} else if (sub == (u64)ov_sub) { /* korin.shell.overview : « Korin OS » allumé ou non */
		char v[8];
		long n = bus_get((long)s_t_overview, (long)v, sizeof v - 1);
		ov_shown = n > 0 && v[0] == '1';
	} else {
		return;
	}
	draw();
}

/* la vue d'ensemble d'Elegant ouverte ou fermée (WS_OP_OVERVIEW) -> 1 ouverte, 0 fermée, < 0 : pas
 * de vue d'ensemble (le menu Korin d'avant) */
static long overview_toggle(void)
{
	static u8 m[WS_MSG] __attribute__((aligned(16)));
	M64(m, WM_OP) = WS_OP_OVERVIEW;
	M64(m, WM_A) = (u64)-1;
	M32(m, WM_LEN) = 0;
	long r = ksys(SYS_WS_CALL, (long)m, 0, 0, 0, 0, 0);
	if (r >= 0)
		ov_shown = r;
	return r;
}

/* l'état de la vue d'ensemble relu (WS_OP_OVERVIEW, lire) : Elegant prévient aussi la liste des fenêtres
 * quand elle s'ouvre ou se ferme */
static void overview_query(void)
{
	static u8 m[WS_MSG] __attribute__((aligned(16)));
	M64(m, WM_OP) = WS_OP_OVERVIEW;
	M64(m, WM_A) = 2;
	M32(m, WM_LEN) = 0;
	long r = ksys(SYS_WS_CALL, (long)m, 0, 0, 0, 0, 0);
	ov_shown = r == 1;
}

/* abonnement (> 0), sinon 0 */
static long subscribe(const char *topic, long flags)
{
	long s = bus_subscribe((long)topic, flags);
	return s > 0 ? s : 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ui_init((long)&ui);
	poche_probe(krt_line); /* la ligne d'arguments du noyau (x0 de l'asm) : « --poche » */
	if (ui_screen((long)scr) < 0)
		return 0;
	bar_height();
	if (ui_window_ex(0, 0, scr[0], bar_h, (long)tr(title),
			 WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST | WF_LISTEN | WF_RESERVE | WF_ALPHA | WF_BLUR) < 0)
		return 0;
	applis_load();
	pq_probe();
	load_offset();
	net_sub = subscribe(s_net_topics, 0); /* le réseau : le bus */
	net_refresh();
	snd_sub = subscribe(s_snd_topic, 0); /* le son et l'heure : le bus (lus une fois ici, puis à
						chaque changement) */
	clk_sub = subscribe(s_clk_topic, 0);
	if (clk_sub) /* déjà publiée (gardée) : busd du PC (horloge CMOS), netd déjà synchronisé */
		clock_from_bus();
	snd_poll();
	bat_sub = subscribe(s_bat_topic, 0); /* la batterie (acpid du PC) */
	bat_read();
	if (!poche) /* la vue d'ensemble d'Elegant */
		ov_sub = subscribe(s_t_overview, BUS_CURRENT);
	if (poche) { /* mode poche : non lues, apps ouvertes */
		unread_sub = subscribe(s_unread, BUS_CURRENT);
		count_apps();
	}
	menu_poll();
	help_probe();
	draw();
	for (;;) {
		if (periodic()) /* menu actif, heure */
			draw();
		long type = ui_event(MB_POLL);
		if (!type)
			continue;
		if (type == WEV_WINLIST) { /* (la fenêtre active a pu changer ; la vue d'ensemble aussi) */
			if (!poche)
				overview_query();
			menu_poll();
			help_probe();
			count_apps();
			draw();
		} else if (type == WEV_SCREEN) { /* nouvelle résolution : toute la largeur */
			if (ui_screen((long)scr) < 0)
				continue;
			ui_min_size(1, 1); /* (la barre est plus basse que le minimum) */
			bar_height();
			/* écart voulu : l'asm passait en hauteur l'adresse de bar_h (x1 laissé par
			 * bar_height), pas sa valeur */
			ui_resize(scr[0], bar_h);
			draw();
		} else if (type == WEV_THEME) { /* autre thème : redessinée */
			draw();
		} else if (type == WEV_BUS) {
			on_bus();
		} else if (type == WEV_MOUSE) {
			long x = *(i32 *)(ui.ev + EV_X);
			if (*(u32 *)(ui.ev + EV_KIND) != 1) { /* survol des titres de menus, de la zone d'état */
				long h = title_at(x) + 1;
				long th = x >= tray_x0;
				if (h != hov || th != tray_hov) {
					hov = h;
					tray_hov = th;
					draw();
				}
				continue;
			}
			long t = title_at(x);
			if (t < 0) {
				press_right(x);
				continue;
			}
			if (!t && poche) { /* poche : le losange montre l'accueil */
				shell_pub(s_t_home);
				continue;
			}
			if (!t && overview_toggle() >= 0) { /* bureau : « Korin OS » bascule la vue d'ensemble */
				draw();
				continue;
			}
			open_menus(t);
			draw();
		}
	}
}
