/* wifimenu - le menu Wi-Fi de la barre de menus (comme wifimenu d'Onyx ; docs/RESEAU.md 4.6),
 * traduit ligne à ligne de wifimenu.asm et de src/inc/wifilist.inc : lancé par menubar (un clic sur
 * l'icône du réseau) avec l'abscisse de l'icône. Une petite fenêtre sans cadre sous l'icône :
 *   * les réseaux visibles, du plus fort au plus faible (« Recherche... » pendant le balayage),
 *     signal, cadenas, coche sur le réseau courant ;
 *   * un clic : connu ou ouvert -> connexion aussitôt ; inconnu et sécurisé -> la ligne s'ouvre
 *     avec un champ de clé et Se connecter ; « Connexion... » puis fermeture une fois connecté ;
 *   * en bas, « Paramètres Wi-Fi... » : le panneau de configuration sur l'applet Wi-Fi ;
 *   * l'état du Wi-Fi est relu quand le pilote le publie sur le bus (korin.net.wifi) ; sans
 *     busd, chaque seconde ;
 *   * se ferme sur Échap, quand une autre fenêtre prend le clavier, ou par un second clic sur
 *     l'icône (service « wifimenu » : un second wifimenu le ferme et s'arrête).
 * Sans Wi-Fi (le PC, un Pi sans puce) : « pas de Wi-Fi » en titre, « Aucun réseau visible ».
 * Balayage et connexion dans un thread (wl_jobs), comme l'asm. Un seul source pour le Pi et le PC
 * (tools/sys/build_c.sh). */
#include "sys_consts.h"
#include "wifidraw.h"
#include "wifilist.h"
#include "uikit_import.h"
#include "netkit_import.h"
#include "buskit_import.h"

#define MW 300 /* largeur */
#define HEAD 34
#define FOOT 34

/* la liste des réseaux : common/wifilist.c (partagée avec les réglages rapides) */

static const char s_nom[] = "wifimenu";
static const char title[] = "Wi-Fi";
static const char s_wifi[] = "Wi-Fi";
static const char s_parametres[] = "Param\xC3\xA8tres Wi-Fi...";
static const char p_control[] = "/apps/control.app/main";
static const char s_arg_wifi[] = "wifi";

static struct krt_ui ui;
static long canal;
static long win_x, win_y, win_h;
static long scr[4];
static u8 msg[MSG_SIZE];

static u32 tj(const u8 *t, long off) { return *(const u32 *)(t + off); }
static const char *tr(const char *s) { return lang_tr(s); }

/* ---------------------------------------------------------------------------
 *  Le menu
 * --------------------------------------------------------------------------- */

static long height(void) { return wl_height() + HEAD + FOOT + 8; }

/* -> poignée de la fenêtre (à la hauteur du contenu) */
static long open_window(void)
{
	win_h = height();
	return ui_window_ex(win_x, win_y, MW, win_h, (long)tr(title),
			    WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_ALPHA);
}

static void draw(void)
{
	wl_nhits_reset();
	const u8 *k = (const u8 *)ui_tokens();
	ui_shadow(ui.w, ui.h); /* fond, ombre, contour */
	ui_rbox(4, 4, ui.w - 8, ui.h - 8, 8, tj(k, TJ_MENU), tj(k, TJ_MENU));
	ui_text((long)s_wifi, 16, 26, tj(k, TJ_TEXT1), 14, (long)ui.font_b, 0); /* titre, état */
	ui_text((long)wl_state_text(), 90, 26, tj(k, TJ_TEXT3), 12, (long)ui.font, MW - 104);
	ui_fill(12, HEAD, MW - 24, 1, tj(k, TJ_LINE)); /* trait */
	long y = wl_draw(HEAD + 4) + 4; /* les réseaux, puis Paramètres Wi-Fi... */
	ui_fill(12, y, MW - 24, 1, tj(k, TJ_LINE));
	wl_hit_add(8, y + 2, MW - 16, FOOT - 6, 3, 0);
	ui_text((long)tr(s_parametres), 16, y + 21, tj(k, TJ_ACCENT), 13, (long)ui.font, 0);
	ui_update_all();
}

/* la hauteur a changé : nouvelle fenêtre ; puis dessin */
static void redraw(void)
{
	if (height() != win_h) {
		ui_close();
		open_window();
	}
	draw();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *line = krt_line; /* abscisse de l'icône */
	canal = korin_sys2(SYS_SVC_REGISTER, s_nom, 0);
	if (canal < 0) { /* déjà ouvert : on le ferme */
		long ch = korin_sys2(SYS_SVC_OPEN, s_nom, 0);
		if (ch >= 0) {
			*(u64 *)msg = 1; /* op 1 : fermer */
			korin_sys2(SYS_SVC_CALL, ch, msg);
		}
		return 0;
	}
	long x = 400;
	if (line)
		x = (long)parse_dec(line);
	ui_init((long)&ui);
	ui_screen((long)scr);
	x = x + 24 - MW; /* sous la barre de menus, aligné sur l'icône, dans l'écran (réservé en haut) */
	if (x < 4)
		x = 4;
	if (x > scr[0] - (MW + 4))
		x = scr[0] - (MW + 4);
	win_x = x;
	win_y = scr[3] + 2;
	wl_w = MW;
	wl_ui = &ui;
	if (wl_start(1) < 0) /* l'état, un balayage tout de suite, le thread des travaux */
		return 0;
	if (open_window() < 0)
		return 0;
	draw();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, 0); /* un second wifimenu : fermer */
		if (req > 0) {
			*(u64 *)msg = 0;
			korin_sys2(SYS_SVC_REPLY, req, msg);
			return 0;
		}
		long t = ui_event(200);
		if (t == WEV_CLOSE)
			return 0;
		u32 kind = *(u32 *)(ui.ev + EV_KIND);
		if (t == WEV_BUS) { /* korin.net.wifi : l'état a changé */
			if (!wl_bus(*(u32 *)(ui.ev + BEV_SUB)))
				continue;
		} else if (t == WEV_FOCUS) { /* une autre fenêtre a le clavier */
			if (!kind)
				return 0;
			continue;
		} else if (t == WEV_KEY) {
			if (!kind)
				continue;
			if (*(u32 *)(ui.ev + EV_CODE) == KEY_ESC)
				return 0;
			if (wl_key())
				redraw();
			continue;
		} else if (t == WEV_MOUSE) {
			if (kind != 1)
				continue;
			long arg;
			long r = wl_click(*(i32 *)(ui.ev + EV_X), *(i32 *)(ui.ev + EV_Y), &arg);
			if (r == 3) { /* Paramètres Wi-Fi... : le panneau, sur l'applet Wi-Fi */
				ksys(SYS_SPAWN, (long)p_control, (long)s_arg_wifi, 0, 0, 0, 0);
				return 0;
			}
			if (r)
				redraw();
			continue;
		} else if (t) {
			continue;
		}
		long r = wl_tick(); /* (rien : l'état, les travaux) */
		if (r == 2) { /* connecté : fermer */
			redraw();
			sys_sleep(900);
			return 0;
		}
		if (r)
			redraw();
	}
}
