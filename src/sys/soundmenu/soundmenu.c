/* soundmenu - le volet du son de la barre de menus (docs/SON.md 4.4), traduit ligne à ligne de
 * soundmenu.asm et de src/inc/soundlist.inc (sa partie « volet ») : lancé par menubar (un clic
 * sur le haut-parleur) avec l'abscisse de l'icône. Une petite fenêtre sans cadre sous l'icône :
 *   * le volume de la sortie principale (curseur, on peut le faire glisser), la case « Muet » ;
 *   * la liste des sorties : un clic en fait la principale (préférée, retenue dans
 *     /etc/sound.txt par sound) ;
 *   * en bas, « Mixer… » : l'application Mixer.
 * Tout passe par AudioKit (service sound) ; l'état est relu toutes les 200 ms (redessin si sa
 * génération change). Se ferme sur Échap, quand une autre fenêtre prend le clavier, ou par un
 * second clic sur l'icône (service « soundmenu » : un second soundmenu le ferme et s'arrête).
 * Un seul source pour le Pi et le PC (tools/sys/build_c.sh). */
#include "sys_consts.h"
#include "sounddraw.h"
#include "uikit_import.h"
#include "audiokit_import.h"

#define MW 280 /* largeur */
#define HEAD 34
#define SL_Y 44 /* curseur du volume */
#define SL_X 40
#define SL_W (MW - SL_X - 56)
#define MUTE_Y 74
#define LIST_Y 126 /* première sortie */
#define FOOT 46
#define BTN_W 120
#define SL_ROWH 26 /* une sortie (soundlist.inc) */
#define SND_OUT_VIRTUAL 0

static const char s_nom[] = "soundmenu";
static const char title[] = "Son";
static const char s_son[] = "Son";
static const char s_aucune[] = "aucune sortie";
static const char s_muet[] = "Muet";
static const char s_sortie[] = "Sortie principale";
static const char s_mixer[] = "Mixer\xE2\x80\xA6";
static const char p_mixer[] = "/apps/mixer.app/main";

static struct krt_ui ui;
static long canal;
static long dragging;
static long win_x, win_y;
static long scr[4];
static u8 msg[MSG_SIZE];

/* soundlist_data */
static const u8 *sl_st; /* tampon d'état de sound (ak_state) */
static long sl_gen = -1;
static const u8 *sl_mainp; /* entrée de la principale (0 : aucune) */
static long sl_nshow;
static const long sl_sx = SL_X, sl_sw = SL_W; /* curseur : x, largeur */
static long sl_list_y;
static const u8 *sl_shown[SS_OUT_MAX];
static char sl_num[24];

static u32 tj(const u8 *t, long off) { return *(const u32 *)(t + off); }
static const char *tr(const char *s) { return lang_tr(s); }

/* ---------------------------------------------------------------------------
 *  soundlist.inc
 * --------------------------------------------------------------------------- */

/* les sorties à montrer (sl_shown : leurs entrées dans l'état), la principale (sl_mainp) */
static void sl_collect(void)
{
	const u8 *st = sl_st;
	sl_gen = *(const long *)(st + SS_GEN);
	sl_mainp = 0;
	long real = 0; /* une sortie réelle ? */
	for (long i = 0; i < SS_OUT_MAX; i++) {
		const u8 *o = st + SS_OUTS + 64 * i;
		if (SS_U32(o, SO_ID) && SS_U32(o, SO_TYPE) != SND_OUT_VIRTUAL)
			real++;
	}
	long n = 0; /* montrées */
	for (long i = 0; i < SS_OUT_MAX; i++) {
		const u8 *o = st + SS_OUTS + 64 * i;
		u64 id = SS_U32(o, SO_ID);
		if (!id)
			continue;
		if (id == *(const u64 *)(st + SS_MAIN))
			sl_mainp = o;
		if (SS_U32(o, SO_TYPE) == SND_OUT_VIRTUAL && real)
			continue; /* virtuelle cachée */
		sl_shown[n++] = o;
	}
	sl_nshow = n;
}

/* -> 1 si l'état a changé depuis le dernier sl_collect (il est relu) */
static long sl_changed(void)
{
	if (!sl_st)
		return 0;
	if (*(const long *)(sl_st + SS_GEN) == sl_gen)
		return 0;
	sl_collect();
	return 1;
}

/* y : haut-parleur (x 14), curseur (sl_sx, sl_sw), valeur à droite ; rien sans principale */
static void sl_draw_slider(long y)
{
	const u8 *o = sl_mainp;
	if (!o)
		return;
	const u8 *k = (const u8 *)ui_tokens();
	sd_speaker(14, y + 1, SS_U32(o, SO_MUTE), tj(k, TJ_TEXT1), tj(k, TJ_TEXT3), &ui);
	sd_slider(sl_sx, y, sl_sw, SS_U32(o, SO_VOLUME), SS_U32(o, SO_MUTE));
	char *e = fmt_dec(SS_U32(o, SO_VOLUME), sl_num);
	*e++ = ' ';
	*e++ = '%';
	*e = 0;
	ui_text((long)sl_num, sl_sx + sl_sw + 10, y + 13, tj(k, TJ_TEXT2), 12, (long)ui.font, 0);
}

/* x = abscisse : volume de la principale d'après le curseur */
static void sl_set_volume(long x)
{
	const u8 *o = sl_mainp;
	if (!o)
		return;
	long v = sd_slider_value(x, sl_sx, sl_sw);
	ak_out_volume(SS_U32(o, SO_ID), v, SS_U32(o, SO_MUTE));
	sl_changed();
}

/* muet <-> son, sur la principale */
static void sl_toggle_mute(void)
{
	const u8 *o = sl_mainp;
	if (!o)
		return;
	ak_out_volume(SS_U32(o, SO_ID), SS_U32(o, SO_VOLUME), SS_U32(o, SO_MUTE) ^ 1);
	sl_changed();
}

/* y, w = largeur : les sorties (rond plein pour la principale) -> y sous la liste */
static long sl_draw_outputs(long y, long w)
{
	sl_list_y = y;
	const u8 *k = (const u8 *)ui_tokens();
	const u8 *mainp = sl_mainp;
	for (long i = 0; i < sl_nshow; i++, y += SL_ROWH) {
		const u8 *o = sl_shown[i];
		u32 c = o == mainp ? tj(k, TJ_ACCENT) : tj(k, TJ_LINE_STRONG);
		ui_rbox(16, y + 6, 14, 14, 7, c, c);
		if (o != mainp) {
			c = tj(k, TJ_FACE);
			ui_rbox(18, y + 8, 10, 10, 5, c, c);
		}
		ui_text((long)(o + SO_NAME), 40, y + 18, tj(k, TJ_TEXT1), 13,
			(long)(o == mainp ? ui.font_b : ui.font), w - 56);
	}
	return y;
}

/* y d'un clic -> 1 si c'est une sortie (elle devient la principale) */
static long sl_click_outputs(long y)
{
	long d = y - sl_list_y;
	if (d < 0)
		return 0;
	u64 row = (u64)d / SL_ROWH;
	if (row >= (u64)sl_nshow)
		return 0;
	ak_main(SS_U32(sl_shown[row], SO_ID));
	sl_changed();
	return 1;
}

/* ---------------------------------------------------------------------------
 *  Le volet
 * --------------------------------------------------------------------------- */

/* -> poignée de la fenêtre (à la hauteur du contenu) */
static long open_win(void)
{
	long n = sl_nshow < 1 ? 1 : sl_nshow;
	return ui_window_ex(win_x, win_y, MW, n * SL_ROWH + LIST_Y + FOOT, (long)tr(title),
			    WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_ALPHA);
}

static void draw(void)
{
	const u8 *k = (const u8 *)ui_tokens();
	ui_shadow(ui.w, ui.h); /* fond, ombre, contour */
	ui_rbox(4, 4, ui.w - 8, ui.h - 8, 8, tj(k, TJ_MENU), tj(k, TJ_MENU));
	ui_text((long)tr(s_son), 16, 26, tj(k, TJ_TEXT1), 14, (long)ui.font_b, 0); /* titre */
	const u8 *o = sl_mainp; /* la principale à droite (0 : aucune) */
	ui_text(o ? (long)(o + SO_NAME) : (long)tr(s_aucune), 60, 26, tj(k, TJ_TEXT3), 12,
		(long)ui.font, MW - 76);
	ui_fill(12, HEAD, MW - 24, 1, tj(k, TJ_LINE));
	if (o) {
		sl_draw_slider(SL_Y); /* haut-parleur, curseur, valeur */
		sd_check(16, MUTE_Y + 2, SS_U32(o, SO_MUTE), &ui); /* Muet */
		ui_text((long)tr(s_muet), 38, MUTE_Y + 14, tj(k, TJ_TEXT1), 13, (long)ui.font, 0);
	}
	ui_fill(12, LIST_Y - 26, MW - 24, 1, tj(k, TJ_LINE));
	ui_text((long)tr(s_sortie), 16, LIST_Y - 8, tj(k, TJ_TEXT3), 12, (long)ui.font_b, 0);
	long y = sl_draw_outputs(LIST_Y, MW);
	ui_fill(12, y + 6, MW - 24, 1, tj(k, TJ_LINE));
	ui_button(MW - BTN_W - 14, y + 12, BTN_W, 26, (long)tr(s_mixer), 0);
	ui_update_all();
}

/* l'état a changé (génération) : relu, fenêtre refaite si le nombre de sorties change, redessin */
static void refresh(void)
{
	long old = sl_nshow;
	if (!sl_changed())
		return;
	if (sl_nshow != old) {
		ui_close();
		open_win();
	}
	draw();
}

/* x = abscisse : volume de la principale d'après le curseur */
static void set_volume(long x)
{
	sl_set_volume(x);
	draw();
}

/* x, y = clic -> 3 : fermer */
static long click(long x, long y)
{
	if (sl_mainp && y >= SL_Y - 4) {
		if (y < SL_Y + SD_SLIDER_H + 4) { /* curseur : volume, et on glisse */
			if (x < SL_X - 6)
				return 0;
			dragging = 1;
			set_volume(x);
			return 0;
		}
		if (y < MUTE_Y + 24) { /* Muet */
			sl_toggle_mute();
			draw();
			return 0;
		}
	}
	if (sl_click_outputs(y)) { /* une sortie : la principale */
		refresh();
		draw();
		return 0;
	}
	if (y < ui.h - FOOT) /* « Mixer… » : l'application */
		return 0;
	/* pas d'application lancée en uid 0 : à l'écran de connexion, soundmenu est celui du système (D13, E40) */
	if (korin_sys0(SYS_GETUID) == 0)
		return 0;
	ksys(SYS_SPAWN, (long)p_mixer, 0, 0, 0, 0, 0);
	return 3;
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
	sl_st = (const u8 *)ak_state();
	if (!sl_st) /* pas de service sound */
		return 0;
	long x = 400;
	if (line)
		x = (long)parse_dec(line);
	ui_init((long)&ui);
	ui_screen((long)scr);
	x = x + 24 - MW; /* sous la barre de menus, aligné sur l'icône, dans l'écran */
	if (x < 4)
		x = 4;
	if (x > scr[0] - (MW + 4))
		x = scr[0] - (MW + 4);
	win_x = x;
	win_y = scr[3] + 2;
	sl_collect();
	if (open_win() < 0)
		return 0;
	draw();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, 0); /* un second soundmenu : fermer */
		if (req > 0) {
			*(u64 *)msg = 0;
			korin_sys2(SYS_SVC_REPLY, req, msg);
			return 0;
		}
		long t = ui_event(200);
		if (t == WEV_CLOSE)
			return 0;
		u32 kind = *(u32 *)(ui.ev + EV_KIND);
		if (t == WEV_FOCUS) {
			if (!kind)
				return 0;
		} else if (t == WEV_KEY) {
			if (kind && *(u32 *)(ui.ev + EV_CODE) == KEY_ESC)
				return 0;
		} else if (t == WEV_MOUSE) {
			long mx = *(i32 *)(ui.ev + EV_X), my = *(i32 *)(ui.ev + EV_Y);
			if (kind == 1) {
				if (click(mx, my) == 3)
					return 0;
			} else if (kind == 2) { /* relâché : fin du glisser */
				dragging = 0;
			} else if (dragging) { /* déplacement : le curseur suit */
				set_volume(mx);
			}
		} else if (!t) {
			refresh(); /* (rien : l'état a-t-il changé ?) */
		}
	}
}
