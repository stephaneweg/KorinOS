/*
 * applets/display.cpp -- le volet Écran du panneau de configuration :
 *   * la RÉSOLUTION de l'écran, changée à chaud (Elegant : sys/elegant/screen.c) comme displayconf d'Onyx. La liste
 *     vient d'Elegant : sa table, les modes de l'écran d'après son EDID, la taille actuelle. Appliquer : l'écran
 *     change tout de suite, puis « Garder cette résolution ? » : sans réponse en 15 s, ou sur Revenir, la taille
 *     d'avant revient (Elegant le fait aussi seul, panneau mort ou écran noir). Gardée : « ecran=LxH » dans
 *     /etc/options.txt, lu par le noyau au démarrage ;
 *   * la section INTERFACE (docs/MODE-POCHE.md) : deux cartes illustrées, Bureau et Poche ; la taille des
 *     contrôles, Normale ou Tactile (« density = normal | touch » dans /etc/theme.txt, lue par UIKit ; Appliquer
 *     l'écrit et publie korin.theme à nouveau) ; « Appliquer et relancer » écrit « shell = desktop | poche » dans
 *     /etc/system.ini (lu par Elegant au démarrage) puis redémarre (init2, INIT_OP_REBOOT : un redémarrage
 *     complet relance la session).
 *
 * Port en C++ de applets/display.inc (Korin). De displayconf d'Onyx (user/Apps/displayconf/main.cpp) vient le
 * principe (la taille à chaud, « Garder cette résolution ? » 15 s) ; le reste (modes d'après l'EDID, options.txt,
 * section Interface) est de Korin.
 */
#include <armpi/userconf.h>
#include <string.h>
#include <armpi/syscall.h>
#include <buskit_abi.h>
#include "../control.h"

enum
{
	/* (WS_OP_SCREEN_SET, _KEEP, _MODES, WS_SM_N, WSMF_*, INIT_OP_REBOOT : uikit_abi.h, de src/inc) */
	SYS_WS_CALL = 0x51,			/* src/inc/syscalls.inc : (message 512 o) -> résultat (réponse dans le
						 * message) */
	EC_MAX = WS_SM_N,
	EC_LABEL = 32,				/* un libellé */
	EC_KEEP_MS = 15000,			/* le temps de répondre */
	EC_WAIT_MS = 17000,			/* … puis Elegant revient de lui-même */
	EC_DLG_W = 440,
	EC_DLG_H = 132,
	EC_IF_Y = 154,				/* ligne de base du titre « Interface » */
	EC_CARD_Y = EC_IF_Y + 12,
	EC_CARD_H = 128,
	EC_VIG_W = 112,				/* vignette */
	EC_VIG_H = 72,
	EC_DENS_Y = EC_CARD_Y + EC_CARD_H + 14,
	EC_SEG_W = 220,
	EC_SEG_H = 30,
	EC_WARN_Y = EC_DENS_Y + EC_SEG_H + 22,	/* ligne de base de l'avertissement */
	EC_BTN_Y = EC_WARN_Y + 12,
	EC_BTN_W = 200,
	EC_BTN_H = 30,
};

/* le message d'Elegant (WM_* de src/inc/armpi.inc) */
struct WsMsg
{
	long op, a, b, c, d;
	uint32_t len, pid;
	unsigned char data[512 - 48];
};
/* un mode (WSM_*) */
struct WsMode
{
	uint16_t w, h;
	uint32_t flags;
};

static const char ec_title[] = "\xC3\x89" "cran";
static const char ec_s_title[] = "R\xC3\xA9solution";
static const char ec_s_note[] = "Le natif est le plus net.";
static const char ec_s_native[] = " (natif)";
static const char ec_s_edid[] = " (\xC3\xA9" "cran)";
static const char ec_s_done[] = "Gard\xC3\xA9" "e, aussi au prochain d\xC3\xA9marrage.";
static const char ec_s_back[] = "La r\xC3\xA9solution d'avant est revenue.";
static const char ec_s_refused[] = "Cette r\xC3\xA9solution est refus\xC3\xA9" "e.";
static const char ec_s_q1[] = "Garder cette r\xC3\xA9solution ? Retour dans ";
static const char ec_s_interface[] = "Interface";
static const char ec_s_desktop[] = "Bureau";
static const char ec_s_pocket[] = "Poche";
static const char ec_s_desktop_d[] = "Fen\xC3\xAAtres, panel en bas";
static const char ec_s_pocket_d[] = "Plein \xC3\xA9" "cran, accueil, tactile";
static const char ec_s_size[] = "Taille des contr\xC3\xB4les";
static const char ec_s_warn[] = "Passer \xC3\xA0 Poche relance la session. Les apps ouvertes sont ferm\xC3\xA9" "es.";
static const char ec_s_relaunch[] = "Appliquer et relancer";
static const char ec_s_reboot[] = "Red\xC3\xA9marrage...";
static const char ec_s_failed[] = "init2 ne r\xC3\xA9pond pas : red\xC3\xA9marrez \xC3\xA0 la main.";

static long ec_n;				/* modes */
static long ec_cur[2];				/* taille actuelle */
static long ec_t0;				/* (confirmation) départ, ms */
static long ec_left;				/* secondes affichées */
static long ec_w_label, ec_w_bar;
static WsMode ec_modes[EC_MAX];
static char ec_labels[EC_MAX][EC_LABEL];
static char ec_line[96];
static long ec_shell;				/* system.ini : 1 poche */
static long ec_dens;				/* (lecture) theme.txt : 1 touch */
static long ec_dens_read;			/* la densité du fichier */
static long ec_choice;				/* carte choisie : 0 Bureau, 1 Poche */
static long ec_w_seg;				/* Normale | Tactile */
static long ec_cw;				/* largeur d'une carte */
static long ec_cx[2];				/* leurs x */
static WsMsg ec_msg;

/* op, a, b, c -> résultat (réponse dans ec_msg) */
static long ec_call (long op, long a, long b, long c)
{
	ec_msg.op = op;
	ec_msg.a = a;
	ec_msg.b = b;
	ec_msg.c = c;
	ec_msg.len = 0;
	return armpi_syscall (SYS_WS_CALL, (long) &ec_msg, 0, 0, 0, 0, 0);
}

/* s = fin de chaîne, n = nombre -> après les chiffres (0 final mis) */
static char *ec_dec (char *s, unsigned long n)
{
	unsigned long p = 1;
	while (p * 10 <= n)
		p *= 10;
	for (; p; p /= 10)
	{
		*s++ = (char) ('0' + n / p);
		n %= p;
	}
	*s = 0;
	return s;
}

/* « LxH » -> sur le 0 final */
static char *ec_size (char *s, long w, long h)
{
	s = ec_dec (s, (unsigned long) w);
	*s++ = 'x';
	return ec_dec (s, (unsigned long) h);
}

/* mode -> 1 si c'est la taille actuelle */
static int ec_checked (long i) { return ec_modes[i].w == ec_cur[0] && ec_modes[i].h == ec_cur[1]; }

/* la liste revient sur la taille actuelle */
static void ec_recheck (void)
{
	fm_choose (0, -1);
	for (long i = 0; i < ec_n; i++)
		if (ec_checked (i))
		{
			fm_choose (0, i);
			break;
		}
}

/* ---- « Garder cette résolution ? » ---- */

/* ec_line <- la question, secondes qui restent -> 1 si elle a changé */
static int ec_text (void)
{
	long s = EC_KEEP_MS + 999 - (sys_ms () - ec_t0);
	if (s < 0)
		s = 0;
	s /= 1000;
	if (s == ec_left)
		return 0;
	ec_left = s;
	ec_line[0] = 0;
	char *p = tr_cat (ec_line, ec_s_q1);
	p = ec_dec (p, (unsigned long) s);
	strcpy (p, " s.");
	return 1;
}

/* (minuteur du dialogue) */
static void ec_tick (long dlg)
{
	long rest = EC_KEEP_MS - (sys_ms () - ec_t0);
	if (rest <= 0)
	{
		ui_dialog_close (dlg, 0);
		return;
	}
	ui_w_set_value (ec_w_bar, rest);
	if (ec_text ())
		ui_w_set_text (ec_w_label, (long) ec_line);
	ui_w_draw (0);
	ui_update_all ();
}

/* -> 1 Garder, 0 Revenir (ou le temps écoulé) */
static long ec_confirm (void)
{
	ec_t0 = sys_ms ();
	ec_left = 0;
	long dlg = ui_dialog_begin (EC_DLG_W, EC_DLG_H, (long) ec_title);	/* (UIKit traduit titre et boutons) */
	if (!dlg)
		return 0;
	ec_text ();
	ec_w_label = ui_label (16, 14, EC_DLG_W - 32, 24, (long) ec_line, 0);	/* la question */
	ec_w_bar = ui_progress (16, 44, EC_DLG_W - 32, 10, EC_KEEP_MS, EC_KEEP_MS);	/* le temps qui reste */
	long edge = ui_dialog_button (dlg, 0, (long) "Garder", 1, UB_DEFAULT, EC_DLG_W - 12);
	ui_dialog_button (dlg, 1, (long) "Revenir", 0, 0, edge);
	ui_dialog_cancel (dlg, 0);
	ui_dialog_timer (dlg, (long) ec_tick, 200);
	long r = ui_dialog_run (dlg, 0, 0);
	ui_dialog_end (dlg);
	return r;
}

/* ---- Interface : Bureau ou Poche, taille des contrôles ---- */

/* (ini_read de system.ini) */
static void ec_ini_sys (char *key, char *val, void *)
{
	if (!strcmp (key, "shell"))
		ec_shell = !strcmp (val, "poche");
}

/* (ini_read de theme.txt) */
static void ec_ini_theme (char *key, char *val, void *)
{
	if (!strcmp (key, "density"))
		ec_dens = !strcmp (val, "touch");
}

/* -> 1 si theme.txt dit « density = touch » (aussi pour le volet Thème, qui réécrit le fichier) */
long display_density_file (void)
{
	ec_dens = 0;
	char cp_[160];
	ini_read (korin_conf_path ("theme.txt", cp_, sizeof cp_, 0), ec_ini_theme);
	return ec_dens;
}

/* ec_dens_read -> « density=touch | normal » dans theme.txt */
static void ec_dens_write (void) { char cp_[160]; ini_set (korin_conf_path ("theme.txt", cp_, sizeof cp_, 1), "density", ec_dens_read ? "touch" : "normal"); }

/* la taille des contrôles choisie : dans theme.txt si elle a changé, puis korin.theme publié à nouveau (les
 * programmes relisent le thème) */
static void ec_dens_apply (void)
{
	if (!ec_w_seg)
		return;
	long v = ui_w_get_value (ec_w_seg) == 1;
	if (v == ec_dens_read)
		return;
	ec_dens_read = v;
	ec_dens_write ();
	long n = bus_get ((long) theme_topic, (long) theme_bus, 63);	/* la valeur gardée de korin.theme, reprise */
	if (n <= 0)
		return;
	if (n > 63)
		n = 63;
	theme_bus[n] = 0;
	bus_publish ((long) theme_topic, (long) theme_bus, -1, BUS_RETAIN);
}

/* une petite fenêtre de vignette */
static void ec_win (long x, long y, long w, long h)
{
	ui_rbox (x, y, w, h, 3, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
	ui_rbox (x + 1, y + 1, w - 2, h - 2, 2, tok (TJ_FACE), tok (TJ_FACE));
	ui_fill (x + 4, y + 4, w - 8, 3, tok (TJ_LINE));
}

/* x, y = coin, pocket = 0 bureau / 1 poche : la petite image du mode */
static void ec_thumb (long x, long y, long pocket)
{
	ui_rbox (x, y, EC_VIG_W, EC_VIG_H, 5, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));	/* l'écran */
	ui_rbox (x + 1, y + 1, EC_VIG_W - 2, EC_VIG_H - 2, 4, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
	ui_fill (x + 1, y + 1, EC_VIG_W - 2, 6, tok (TJ_BAR));	/* la barre du haut */
	if (!pocket)				/* bureau : deux fenêtres, le panel */
	{
		ec_win (x + 10, y + 14, 56, 36);
		ec_win (x + 44, y + 22, 58, 34);
		ui_fill (x + 1, y + EC_VIG_H - 9, EC_VIG_W - 2, 8, tok (TJ_DOCK));
		for (long i = 0; i < 5; i++)
		{
			uint32_t c = tok (i ? TJ_LINE_STRONG : TJ_ACCENT);
			ui_rbox (x + i * 10 + EC_VIG_W / 2 - 24, y + EC_VIG_H - 7, 6, 4, 1, c, c);
		}
		return;
	}
	ui_fill (x + 1, y + 7, EC_VIG_W - 2, EC_VIG_H - 8, tok (TJ_FACE));	/* poche : l'accueil plein écran */
	ui_rbox (x + 8, y + 11, 30, 7, 2, tok (TJ_TEXT1), tok (TJ_TEXT1));	/* l'heure en grand */
	for (long i = 0; i < 10; i++)		/* des icônes, 5 x 2 */
	{
		uint32_t c = tok ((i & 3) ? TJ_LINE_STRONG : TJ_ACCENT);
		ui_rbox (x + (i % 5) * 19 + 10, y + (i / 5) * 16 + 24, 12, 12, 3, c, c);
	}
	ui_fill (x + 1, y + EC_VIG_H - 13, EC_VIG_W - 2, 12, tok (TJ_SURFACE));	/* la bande « Ouvertes » */
	for (long i = 0; i < 3; i++)
		ui_rbox (x + i * 26 + 8, y + EC_VIG_H - 11, 22, 8, 4, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
}

/* (ap.draw) les lignes de réglages, puis la section Interface */
static void ec_draw (void)
{
	long x = SIDE_W + AP_MARGIN, w = ui.w - (SIDE_W + 2 * AP_MARGIN);
	if (ec_w_seg)				/* Normale | Tactile à sa place, avant que fm_draw dessine les widgets */
		ui_w_move (ec_w_seg, x + w - EC_SEG_W, EC_DENS_Y, EC_SEG_W, EC_SEG_H);
	fm_draw ();
	ui_text ((long) TR (ec_s_interface), x + 2, EC_IF_Y, tok (TJ_TEXT1), 14, FB (), 0);	/* titre */
	long cw = (long) ((unsigned long) (w - 12) >> 1);	/* deux cartes */
	ec_cw = cw;
	for (long i = 0; i < 2; i++)
	{
		long cx = x + i * (cw + 12), sel = ec_choice == i;
		ec_cx[i] = cx;
		uint32_t c = tok (sel ? TJ_ACCENT : TJ_LINE);	/* contour (choisie : ACCENT, 2 px) */
		ui_rbox (cx, EC_CARD_Y, cw, EC_CARD_H, FM_RADIUS, c, c);
		c = tok (sel ? TJ_ACCENT_LIGHT : TJ_FACE);
		ui_rbox (cx + 1 + sel, EC_CARD_Y + 1 + sel, cw - 2 - 2 * sel, EC_CARD_H - 2 - 2 * sel, FM_RADIUS - 1, c, c);
		ec_thumb (cx + (long) ((unsigned long) (cw - EC_VIG_W) >> 1), EC_CARD_Y + 12, i);	/* la vignette, centrée */
		const char *t = TR (i ? ec_s_pocket : ec_s_desktop);	/* nom, description */
		long tw = ui_text_width ((long) t, 14, FB ());
		ui_text ((long) t, cx + (long) ((unsigned long) (cw - tw) >> 1), EC_CARD_Y + EC_VIG_H + 32,
			 tok (sel ? TJ_ACCENT_DARK : TJ_TEXT1), 14, FB (), 0);
		t = TR (i ? ec_s_pocket_d : ec_s_desktop_d);
		tw = cw - ui_text_width ((long) t, 12, F ());
		if (tw < 0)
			tw = 0;
		ui_text ((long) t, cx + (tw >> 1), EC_CARD_Y + EC_VIG_H + 50, tok (TJ_TEXT2), 12, F (), cw - 8);
	}
	ui_text ((long) TR (ec_s_size), x + 2, EC_DENS_Y + EC_SEG_H / 2 + 5, tok (TJ_TEXT1), 14, F (), 0);	/* Taille des
													 * contrôles */
	if (ec_w_seg)				/* (par-dessus le fond du volet) */
		ui_w_draw (ec_w_seg);
	ui_text ((long) TR (ec_s_warn), x + 2, EC_WARN_Y, tok (TJ_TEXT2), 13, F (), w);	/* l'avertissement, le bouton */
	ui_button (x, EC_BTN_Y, EC_BTN_W, EC_BTN_H, (long) TR (ec_s_relaunch), ec_shell != ec_choice);	/* principal si
												 * le mode change */
}

/* Appliquer et relancer : shell et densité écrits, puis le redémarrage */
static void ec_relaunch (void)
{
	if (ec_w_seg)				/* la taille des contrôles */
	{
		ec_dens_read = ui_w_get_value (ec_w_seg) == 1;
		ec_dens_write ();
	}
	ini_set (P_SYSTEM, "shell", ec_choice ? "poche" : "desktop");	/* le mode */
	ap.msg = ec_s_reboot;			/* « Redémarrage... », vu un instant */
	draw ();
	sys_sleep (400);
	long ch = svc_open ("init2", 1000);
	if (ch >= 0)
	{
		memset (&ec_msg, 0, 48);
		ec_msg.op = INIT_OP_REBOOT;
		if (svc_call (ch, &ec_msg) >= 0)
			sys_sleep (5000);	/* (le Pi s'arrête pendant ce temps) */
	}
	ap.msg = ec_s_failed;
}

/* (ap.xclick) -> 1 si pris (une carte, le bouton) */
static int ec_xclick (long x, long y)
{
	if (y >= EC_CARD_Y && y < EC_CARD_Y + EC_CARD_H)	/* les cartes */
	{
		for (long i = 0; i < 2; i++)
			if (x >= ec_cx[i] && x < ec_cx[i] + ec_cw)
			{
				ec_choice = i;
				ap.msg = 0;
				return 1;
			}
		return 0;
	}
	if (y >= EC_BTN_Y && y < EC_BTN_Y + EC_BTN_H && x >= SIDE_W + AP_MARGIN && x < SIDE_W + AP_MARGIN + EC_BTN_W)
	{					/* Appliquer et relancer */
		ec_relaunch ();
		return 1;
	}
	return 0;
}

/* (ap.close) la liste Normale | Tactile, puis celles des lignes */
static void ec_close (void)
{
	long w = ec_w_seg;
	ec_w_seg = 0;
	if (w)
		ui_w_destroy (w);
	fm_close ();
}

/* (ouverture) l'état des fichiers, la liste Normale | Tactile, le dessin et les clics de la section */
static void ec_if_open (void)
{
	ec_shell = 0;
	ini_read (P_SYSTEM, ec_ini_sys);	/* shell = poche ? */
	ec_dens_read = display_density_file ();	/* density = touch ? */
	ec_choice = ec_shell;
	ec_w_seg = ui_segmented (ui.w - (AP_MARGIN + (long) EC_SEG_W), EC_DENS_Y, EC_SEG_W, EC_SEG_H,
				 (long) "Normale|Tactile");	/* (replacée au dessin ; le texte tel quel, comme l'asm) */
	ui_w_set_value (ec_w_seg, ec_dens_read);
	ui_w_on (ec_w_seg, (long) fm_change, 0);	/* (un changement efface le message) */
	ap.draw = ec_draw;
	ap.close = ec_close;
	ap.xclick = ec_xclick;
}

static void ec_open (void)
{
	ec_n = 0;
	long n = ec_call (WS_OP_SCREEN_MODES, 0, 0, 0);
	if (n > 0)
	{
		if (n > EC_MAX)
			n = EC_MAX;
		ec_n = n;
		ec_cur[0] = ec_msg.c;		/* taille actuelle */
		ec_cur[1] = ec_msg.d;
		memcpy (ec_modes, ec_msg.data, (size_t) n * sizeof (WsMode));	/* les modes */
	}
	fm_begin (0, ec_s_note);
	fm_line (ec_s_title);
	for (long i = 0; i < ec_n; i++)
	{
		char *p = ec_size (ec_labels[i], ec_modes[i].w, ec_modes[i].h);	/* le libellé */
		if (ec_modes[i].flags & WSMF_NATIVE)
			tr_cat (p, ec_s_native);
		else if (ec_modes[i].flags & WSMF_EDID)
			tr_cat (p, ec_s_edid);
		fm_item (0, ec_labels[i], ec_checked (i));
	}
	ec_if_open ();
}

/* Appliquer : la taille choisie tout de suite, gardée si l'utilisateur le dit ; puis la taille des contrôles */
static void ec_apply (void)
{
	long v = fm_value (0);
	if (v >= 0)
	{
		long w = ec_modes[v].w, h = ec_modes[v].h;
		const char *msg = ec_s_done;
		if (w != ec_cur[0] || h != ec_cur[1])	/* (déjà à cette taille : rien à confirmer) */
		{
			if (ec_call (WS_OP_SCREEN_SET, w, h, EC_WAIT_MS) < 0)
				msg = ec_s_refused;
			else
			{
				long r = ec_confirm ();
				ec_call (WS_OP_SCREEN_KEEP, r, 0, 0);	/* (r : 1 garder ; Revenir, ou plus de temps) */
				if (r == 1)
					ec_cur[0] = w, ec_cur[1] = h;
				else
				{
					ec_recheck ();
					msg = ec_s_back;
				}
			}
		}
		if (msg == ec_s_done)		/* gardée : options.txt */
		{
			char value[EC_LABEL];
			ec_size (value, w, h);
			ini_set (P_OPTIONS, "ecran", value);
		}
		ap.msg = msg;
		ap_fit ();
	}
	ec_dens_apply ();			/* la taille des contrôles */
}

const Applet display_applet = { "ecran", "\xC3\x89" "cran", "\xC3\x89" "cran", "ecran", ec_open, ec_apply };
