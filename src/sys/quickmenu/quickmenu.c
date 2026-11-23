/* quickmenu - les réglages rapides du bureau (23/11/2026 ; docs/DESIGN-ELEGANT.md 6.3), comme
 * ceux de GNOME : lancé par la barre de menus quand on clique dans sa zone d'état (l'heure, la
 * batterie, le réseau, le son), un volet sous la barre, à droite :
 *   * en haut, la batterie (s'il y en a une, PC : acpid) ; à droite deux boutons ronds :
 *     Réglages (le panneau de configuration) et Éteindre (/sys/shutdown, le dialogue de fin de
 *     session) ;
 *   * le volume de la sortie principale (le haut-parleur : muet ; le curseur, qu'on peut faire
 *     glisser ; la flèche : le volet des sorties, soundmenu) ;
 *   * deux tuiles : le réseau (Wi-Fi : son réseau, ou l'adresse en filaire ; un clic : le menu
 *     Wi-Fi, sinon les réglages) et « Ne pas déranger » (korin.notify.dnd, gardé sur le bus :
 *     notifyd ne montre plus les bulles), qu'un clic bascule.
 * Se ferme sur Échap, quand une autre fenêtre prend le clavier, après une action qui ouvre autre
 * chose, ou par un second clic dans la zone d'état (service « quickmenu » : un second quickmenu
 * le ferme et s'arrête). L'état du réseau et de « Ne pas déranger » suit le bus ; le son est relu
 * toutes les 200 ms (génération de sound). Un seul source pour le Pi et le PC. */
#include "sys_consts.h"
#include "sounddraw.h"
#include "wifidraw.h"
#include "wifilist.h"
#include "uikit_import.h"
#include "netkit_import.h"
#include "audiokit_import.h"
#include "buskit_import.h"

/* src/inc/netd.inc */
#define NIF_WIFI 2
#define WIFI_ST_READY 2
#define WIFI_ST_CONNECTED 4

#define QW 360       /* largeur */
#define M 4          /* retrait de ui_shadow */
#define PADX 18
#define TOP_Y 16     /* la rangée du haut */
#define RB 38        /* bouton rond */
#define VOL_Y 70     /* le volume */
#define SL_X 50
#define SL_W (QW - SL_X - 104)
#define CHEV_W 34
#define TILE_Y 114   /* les tuiles */
#define TILE_H 56
#define TILE_GAP 10
#define TILE_W ((QW - 2 * PADX - TILE_GAP) / 2)
#define QH (TILE_Y + TILE_H + 20)
#define WF_HEAD 48   /* la carte Wi-Fi dépliée : son en-tête, */
#define WF_FOOT 40   /* son pied (Paramètres Wi-Fi…) */
#define WF_GAP 12    /* entre les tuiles et la carte */

enum { H_NONE, H_GEAR, H_POWER, H_SPEAKER, H_CHEVRON, H_NET, H_DND };

static const char s_nom[] = "quickmenu";
static const char title[] = "Réglages rapides";
static const char s_dnd[] = "Ne pas déranger";
static const char s_on[] = "Activé";
static const char s_off[] = "Désactivé";
static const char s_wifi[] = "Wi-Fi";
static const char s_wired[] = "Filaire";
static const char s_net[] = "Réseau";
static const char s_offline[] = "hors ligne";
static const char s_notco[] = "non connecté";
static const char s_bat[] = "Batterie";
static const char s_charge[] = "en charge";
static const char t_dnd[] = "korin.notify.dnd";
static const char t_net[] = "korin.net.*";
static const char t_bat[] = "korin.power.battery";
static const char p_control[] = "/apps/control.app/main";
static const char p_shutdown[] = "/sys/shutdown";
static const char p_soundmenu[] = "/sys/soundmenu";

static struct krt_ui ui;
static long canal;
static long scr[4];
static u8 msg[MSG_SIZE];
static long win_x, win_y;
static long hov;
static long dragging;
static long dnd;
static long dnd_sub, net_sub, bat_sub;
static const u8 *st;     /* état de sound (0 : pas de service) */
static long st_gen = -2;
static u8 netinfo[32], wst[64], nact[16];
static char line[96];
static long bat_pct = -1;
static char bat_state = 'n';
static long wifi_open;      /* la liste des réseaux dépliée sous les tuiles (common/wifilist.c) */
static long wl_started;     /* (wl_start : une fois) */
static const char s_wf_param[] = "Paramètres Wi-Fi…";

static u32 tj(long off) { return *(const u32 *)((const u8 *)ui_tokens() + off); }
static const char *tr(const char *s) { return lang_tr(s); }

/* la teinte du dash de la vue d'ensemble (un voile clair sur son fond sombre : ≈ #3B3D3F), quel que soit le thème
 * (demande du 23/11/2026) : écrite dans les jetons de ce programme seul (la liste Wi-Fi les lit aussi), remise après
 * un changement de thème */
static void dash_palette(void)
{
	u8 *k = (u8 *)ui_tokens();
	u32 acc = *(u32 *)(k + TJ_ACCENT);
	static const struct { u16 off; u32 c; } pal[] = {
		{TJ_MENU, 0x3B3D3F},	   {TJ_FACE, 0x3B3D3F},	       {TJ_SURFACE, 0x45474A},
		{TJ_SUNKEN, 0x4A4C4F},	   {TJ_RAISED, 0x55575A},      {TJ_RAISED_LOW, 0x505255},
		{TJ_LINE, 0x515356},	   {TJ_LINE_STRONG, 0x6A6D70}, {TJ_TEXT1, 0xF2F3F4},
		{TJ_TEXT2, 0xC5C7CA},	   {TJ_TEXT3, 0x989BA0},       {TJ_HOVER, 0x55575A},
		{TJ_DARK, 1},
	};
	for (unsigned i = 0; i < sizeof pal / sizeof pal[0]; i++)
		*(u32 *)(k + pal[i].off) = pal[i].c;
	*(u32 *)(k + TJ_ACCENT_LIGHT) = (u32)ui_mix(0x3B3D3F, acc & 0xFFFFFF, 90);
}
static long spawn(const char *path, const char *args)
{
	return ksys(SYS_SPAWN, (long)path, (long)args, 0, 0, 0, 0);
}

/* ---- état */

static const u8 *main_out(void)
{
	if (!st)
		return 0;
	u64 id = *(const u64 *)(st + SS_MAIN);
	for (long i = 0; i < SS_OUT_MAX; i++) {
		const u8 *o = st + SS_OUTS + 64 * i;
		if (SS_U32(o, SO_ID) && SS_U32(o, SO_ID) == id)
			return o;
	}
	return 0;
}

/* -> 1 si le son a changé */
static long snd_changed(void)
{
	if (!st)
		return 0;
	long g = *(const long *)(st + SS_GEN);
	if (g == st_gen)
		return 0;
	st_gen = g;
	return 1;
}

static void net_read(void)
{
	net_info((long)netinfo);
	wifi_status((long)wst);
	net_active((long)nact);
}

static void dnd_read(void)
{
	char v[8];
	long n = bus_get((long)t_dnd, (long)v, sizeof v - 1);
	dnd = n > 0 && v[0] == '1';
}

static void bat_read(void)
{
	char v[64];
	long n = bus_get((long)t_bat, (long)v, sizeof v - 1);
	if (n <= 0) {
		bat_state = 'n';
		return;
	}
	v[n < (long)sizeof v - 1 ? n : (long)sizeof v - 1] = 0;
	const char *p = v;
	while (*p == ' ')
		p++;
	long x = 0;
	while (*p >= '0' && *p <= '9')
		x = x * 10 + (*p++ - '0');
	bat_pct = x;
	for (int k = 0; k < 2; k++) { /* minutes, secteur */
		while (*p == ' ')
			p++;
		if (*p == '-')
			p++;
		while (*p >= '0' && *p <= '9')
			p++;
	}
	while (*p == ' ')
		p++;
	bat_state = *p ? *p : 'n';
	if (bat_state != 'd' && bat_state != 'c' && bat_state != 'f' && bat_state != 'i')
		bat_state = 'n'; /* (pas de batterie, ou une valeur qu'on ne comprend pas) */
}

/* ---- la carte Wi-Fi */

/* -> la hauteur du volet : les tuiles, puis la carte Wi-Fi si elle est dépliée */
static long qh(void)
{
	return QH + (wifi_open ? WF_GAP + WF_HEAD + wl_height () + 8 + WF_FOOT : 0);
}

/* la carte : un fond un peu plus marqué que le volet, son en-tête (un disque d'accent et ses barres, « Wi-Fi », l'état),
 * les réseaux (wifilist), Paramètres Wi-Fi… */
static void wifi_card(void)
{
	long y = QH - 20 + WF_GAP, h = WF_HEAD + wl_height () + 8 + WF_FOOT;
	u32 face = (u32)ui_mix(tj(TJ_MENU), tj(TJ_DARK) ? 0x00FFFFFF : 0, tj(TJ_DARK) ? 18 : 10);
	ui_rbox(PADX, y, QW - 2 * PADX, h, 16, face, face);
	u32 acc = tj(TJ_ACCENT);
	ui_rbox(PADX + 12, y + 10, 30, 30, 15, acc, acc);
	u32 ink = (u32)ui_ink(acc);
	wd_bars(PADX + 19, y + 18, *(u32 *)wst == WIFI_ST_CONNECTED ? wd_level(*(i32 *)(wst + 4)) : 4, ink, (u32)ui_mix(ink, acc, 140));
	ui_text((long)"Wi-Fi", PADX + 52, y + 31, tj(TJ_TEXT1), 16, (long)ui.font_b, 0);
	ui_text((long)wl_state_text(), PADX + 112, y + 30, tj(TJ_TEXT3), 12, (long)ui.font, QW - 2 * PADX - 124);
	long ly = wl_draw(y + WF_HEAD) + 4;
	ui_fill(PADX + 12, ly, QW - 2 * PADX - 24, 1, tj(TJ_LINE));
	wl_hit_add(PADX, ly + 2, QW - 2 * PADX, WF_FOOT - 6, 3, 0);
	ui_text((long)tr(s_wf_param), PADX + 16, ly + 24, tj(TJ_TEXT1), 13, (long)ui.font, 0);
}

/* la taille du volet suit la carte */
static void fit(void)
{
	if (ui.h != qh())
		ui_resize(QW, qh());
}

/* ---- dessin */

/* un bouton rond (glyphe), survolé : plus dense */
static void round_btn(long x, long y, const char *glyph, long h, long danger)
{
	int dark = tj(TJ_DARK) != 0;
	u32 face = hov == h ? (u32)ui_mix(tj(TJ_SUNKEN), dark ? 0x00FFFFFF : 0, dark ? 30 : 18) : tj(TJ_SUNKEN);
	ui_rbox(x, y, RB, RB, RB / 2, face, face);
	u32 ink = hov == h && danger ? tj(TJ_DANGER) : tj(TJ_TEXT1);
	long g = 20;
	ui_glyph((long)glyph, x + (RB - g) / 2, y + (RB - g) / 2, g, ink);
}

/* une tuile : une pilule, pleine de l'accent si active ; icône (dessinée par icon), titre, détail */
static void tile(long x, long y, long active, long h, const char *t, const char *sub, void (*icon)(long, long, u32, u32))
{
	u32 face, ink, ink2;
	if (active) {
		face = tj(TJ_ACCENT);
		if (hov == h)
			face = (u32)ui_tone(face, 112);
		ink = (u32)ui_ink(face);
		ink2 = ink;
	} else {
		face = tj(TJ_SUNKEN);
		if (hov == h)
			face = (u32)ui_mix(face, tj(TJ_DARK) ? 0x00FFFFFF : 0, tj(TJ_DARK) ? 30 : 18);
		ink = tj(TJ_TEXT1);
		ink2 = tj(TJ_TEXT2);
	}
	ui_rbox(x, y, TILE_W, TILE_H, TILE_H / 2, face, face);
	icon(x + 18, y + (TILE_H - 14) / 2, ink, active ? ink : tj(TJ_TEXT3));
	ui_text((long)t, x + 46, y + 24, ink, 13, (long)ui.font_b, TILE_W - 56);
	ui_text((long)sub, x + 46, y + 42, ink2, 12, (long)ui.font, TILE_W - 56);
}

static void icon_net(long x, long y, u32 on, u32 off)
{
	u32 ip = *(u32 *)netinfo;
	if (*(u32 *)(nact + 8) != NIF_WIFI && ip) { /* filaire (le dessin de la barre de menus) */
		ui_rbox(x + 5, y, 6, 5, 1, on, on);
		ui_fill(x + 7, y + 5, 2, 3, on);
		ui_fill(x + 2, y + 7, 12, 1, on);
		ui_fill(x + 2, y + 7, 1, 3, on);
		ui_fill(x + 13, y + 7, 1, 3, on);
		ui_rbox(x, y + 9, 5, 5, 1, on, on);
		ui_rbox(x + 11, y + 9, 5, 5, 1, on, on);
		return;
	}
	long lit = *(u32 *)wst == WIFI_ST_CONNECTED ? wd_level(*(i32 *)(wst + 4)) : 0;
	wd_bars(x, y, lit, on, off);
}

/* une lune (Ne pas déranger) : un disque, creusé par un autre */
static void icon_dnd(long x, long y, u32 on, u32 off)
{
	(void)off;
	ui_rbox(x + 1, y, 14, 14, 7, on, on);
	u32 face = dnd ? tj(TJ_ACCENT) : tj(TJ_SUNKEN);
	ui_rbox(x + 6, y - 3, 13, 13, 6, face, face);
}

static void net_texts(const char **t, const char **sub)
{
	u32 ip = *(u32 *)netinfo;
	long wifi = *(u32 *)wst >= WIFI_ST_READY;
	if (*(u32 *)(nact + 8) != NIF_WIFI && ip) {
		*t = tr(s_wired);
		char *e = line;
		for (long i = 0; i < 4; i++) {
			e = fmt_dec((ip >> (24 - 8 * i)) & 0xFF, e);
			if (i < 3)
				*e++ = '.';
		}
		*e = 0;
		*sub = line;
		return;
	}
	*t = tr(wifi ? s_wifi : s_net);
	u32 n = *(u32 *)(wst + 48); /* le nom du réseau (SSID en +16, sa longueur en +48 : netd.inc) */
	if (*(u32 *)wst == WIFI_ST_CONNECTED && n) {
		if (n > 32)
			n = 32;
		memcpy(line, wst + 16, n);
		line[n] = 0;
		*sub = line;
	} else {
		*sub = tr(ip ? s_net : wifi ? s_notco : s_offline);
	}
}

static void draw(void)
{
	fit();
	wl_nhits_reset();
	ui_shadow(ui.w, ui.h);
	u32 bg = tj(TJ_MENU);
	ui_rbox(M, M, ui.w - 2 * M, ui.h - 2 * M, 14, bg, bg);
	/* la rangée du haut : la batterie, puis Réglages et Éteindre */
	if (bat_state != 'n' && bat_pct >= 0) {
		char *p = str_cat(line, tr(s_bat));
		p = str_cat(p, "  ");
		p = fmt_dec((u64)bat_pct, p);
		p = str_cat(p, " %");
		if (bat_state == 'c') {
			p = str_cat(p, ", ");
			str_cat(p, tr(s_charge));
		}
		ui_text((long)line, PADX + 4, TOP_Y + 24, tj(TJ_TEXT1), 13, (long)ui.font_b, 200);
	}
	round_btn(QW - PADX - 2 * RB - 10, TOP_Y, "glyph-gear", H_GEAR, 0);
	round_btn(QW - PADX - RB, TOP_Y, "glyph-power", H_POWER, 1);
	/* le volume */
	const u8 *o = main_out();
	if (o) {
		u32 mute = SS_U32(o, SO_MUTE);
		if (hov == H_SPEAKER)
			ui_rbox(PADX - 4, VOL_Y - 8, 32, 30, 15, tj(TJ_SUNKEN), tj(TJ_SUNKEN));
		sd_speaker(PADX + 4, VOL_Y, mute, tj(TJ_TEXT1), tj(TJ_TEXT3), &ui);
		sd_slider(SL_X, VOL_Y - 1, SL_W, SS_U32(o, SO_VOLUME), mute);
		char *e = fmt_dec(SS_U32(o, SO_VOLUME), line);
		str_cat(e, " %");
		ui_text((long)line, SL_X + SL_W + 10, VOL_Y + 12, tj(TJ_TEXT2), 12, (long)ui.font, 0);
		long cx = QW - PADX - CHEV_W; /* la flèche : les sorties */
		if (hov == H_CHEVRON)
			ui_rbox(cx, VOL_Y - 8, CHEV_W, 30, 15, tj(TJ_SUNKEN), tj(TJ_SUNKEN));
		ui_text((long)"\xE2\x80\xBA", cx + 13, VOL_Y + 14, tj(TJ_TEXT1), 18, (long)ui.font_b, 0);
	} else {
		ui_text((long)tr("aucune sortie"), SL_X, VOL_Y + 12, tj(TJ_TEXT3), 12, (long)ui.font, 0);
	}
	/* les tuiles */
	const char *t, *sub;
	net_texts(&t, &sub);
	tile(PADX, TILE_Y, *(u32 *)netinfo != 0 || wifi_open, H_NET, t, sub, icon_net);
	if (*(u32 *)wst >= WIFI_ST_READY) { /* (le Wi-Fi : un chevron, la liste se déplie) */
		u32 c = *(u32 *)netinfo != 0 || wifi_open ? (u32)ui_ink(tj(TJ_ACCENT)) : tj(TJ_TEXT2);
		ui_text((long)(wifi_open ? "\xE2\x8C\x84" : "\xE2\x80\xBA"), PADX + TILE_W - 22, TILE_Y + 35, c, 18, (long)ui.font_b, 0);
	}
	tile(PADX + TILE_W + TILE_GAP, TILE_Y, dnd, H_DND, tr(s_dnd), tr(dnd ? s_on : s_off), icon_dnd);
	if (wifi_open)
		wifi_card();
	ui_update_all();
}

/* ---- entrées */

static long hit(long x, long y)
{
	if (y >= TOP_Y && y < TOP_Y + RB) {
		if (x >= QW - PADX - 2 * RB - 10 && x < QW - PADX - RB - 10)
			return H_GEAR;
		if (x >= QW - PADX - RB && x < QW - PADX)
			return H_POWER;
	}
	if (y >= VOL_Y - 8 && y < VOL_Y + 22) {
		if (x >= PADX - 4 && x < PADX + 28)
			return H_SPEAKER;
		if (x >= QW - PADX - CHEV_W && x < QW - PADX)
			return H_CHEVRON;
	}
	if (y >= TILE_Y && y < TILE_Y + TILE_H) {
		if (x >= PADX && x < PADX + TILE_W)
			return H_NET;
		if (x >= PADX + TILE_W + TILE_GAP && x < QW - PADX)
			return H_DND;
	}
	return H_NONE;
}

static void set_volume(long x)
{
	const u8 *o = main_out();
	if (!o)
		return;
	ak_out_volume(SS_U32(o, SO_ID), sd_slider_value(x, SL_X, SL_W), SS_U32(o, SO_MUTE));
	snd_changed();
	draw();
}

/* x, y = clic -> 1 : fermer */
static long click(long x, long y)
{
	char arg[24];
	long h = hit(x, y);
	switch (h) {
	case H_GEAR:
		spawn(p_control, 0);
		return 1;
	case H_POWER:
		spawn(p_shutdown, 0);
		return 1;
	case H_SPEAKER: {
		const u8 *o = main_out();
		if (o)
			ak_out_volume(SS_U32(o, SO_ID), SS_U32(o, SO_VOLUME), SS_U32(o, SO_MUTE) ^ 1);
		snd_changed();
		draw();
		return 0;
	}
	case H_CHEVRON:
		*fmt_dec((u64)(win_x + QW - 30), arg) = 0;
		spawn(p_soundmenu, arg);
		return 1;
	case H_NET:
		if (*(u32 *)wst >= WIFI_ST_READY) { /* le Wi-Fi : la liste se déplie dans le volet (ou se replie) */
			wifi_open = !wifi_open;
			if (wifi_open) {
				wl_ui = &ui;
				wl_x = PADX - 4;
				wl_w = QW - 2 * PADX + 8;
				wl_selrow = -1;
				wl_msgp = 0;
				if (!wl_started)
					wl_started = 1, wl_start(1); /* (l'état, un balayage, le thread des travaux) */
				else
					wl_launch(1);
			}
			draw();
			return 0;
		}
		spawn(p_control, "wifi");
		return 1;
	case H_DND:
		dnd = !dnd;
		bus_publish((long)t_dnd, (long)(dnd ? "1" : "0"), -1, BUS_RETAIN);
		draw();
		return 0;
	}
	if (wifi_open && y >= QH - 20) { /* la carte Wi-Fi : un réseau, Se connecter, Paramètres Wi-Fi… */
		long a, r = wl_click(x, y, &a);
		if (r == 3) {
			spawn(p_control, "wifi");
			return 1;
		}
		if (r)
			draw();
		return 0;
	}
	if (st && y >= VOL_Y - 6 && y < VOL_Y + SD_SLIDER_H + 4 && x >= SL_X - 6 && x < SL_X + SL_W + 6) {
		dragging = 1; /* le curseur : volume, et on glisse */
		set_volume(x);
	}
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *arg = krt_line; /* bord droit de la zone d'état (facultatif) */
	canal = korin_sys2(SYS_SVC_REGISTER, s_nom, 0);
	if (canal < 0) { /* déjà ouvert : on le ferme */
		long ch = korin_sys2(SYS_SVC_OPEN, s_nom, 0);
		if (ch >= 0) {
			*(u64 *)msg = 1;
			korin_sys2(SYS_SVC_CALL, ch, msg);
		}
		return 0;
	}
	ui_init((long)&ui);
	dash_palette();
	ui_screen((long)scr);
	long right = arg && *arg ? (long)parse_dec(arg) : scr[0];
	long x = right - QW + 4;
	if (x > scr[0] - QW - 2)
		x = scr[0] - QW - 2;
	if (x < 2)
		x = 2;
	win_x = x;
	win_y = scr[3] + 2;
	st = (const u8 *)ak_state();
	snd_changed();
	net_read();
	dnd_read();
	bat_read();
	net_sub = bus_subscribe((long)t_net, 0);
	dnd_sub = bus_subscribe((long)t_dnd, 0);
	bat_sub = bus_subscribe((long)t_bat, 0);
	if (ui_window_ex(win_x, win_y, QW, QH, (long)tr(title), WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_ALPHA | WF_GRAB) < 0)
		return 0;
	draw();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, 0); /* un second quickmenu : fermer */
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
		} else if (t == WEV_THEME) { /* (le kit a relu le thème : la teinte du dash, de nouveau) */
			dash_palette();
			draw();
		} else if (t == WEV_KEY) {
			if (kind && *(u32 *)(ui.ev + EV_CODE) == KEY_ESC)
				return 0;
			if (kind && wifi_open && wl_key()) /* (la clé d'un réseau) */
				draw();
		} else if (t == WEV_BUS) {
			u32 sub = *(u32 *)(ui.ev + BEV_SUB);
			if (wifi_open)
				wl_bus(sub); /* (korin.net.wifi : la liste relit l'état au prochain tour) */
			if (sub == (u32)net_sub)
				net_read();
			else if (sub == (u32)dnd_sub)
				dnd_read();
			else if (sub == (u32)bat_sub)
				bat_read();
			draw();
		} else if (t == WEV_MOUSE) {
			long mx = *(i32 *)(ui.ev + EV_X), my = *(i32 *)(ui.ev + EV_Y);
			if (kind == 1) { /* (WF_GRAB, comme un menu : un appui ailleurs le ferme) */
				if (mx < 0 || my < 0 || mx >= ui.w || my >= ui.h)
					return 0;
				if (click(mx, my))
					return 0;
			} else if (kind == 2) {
				dragging = 0;
			} else if (dragging) {
				set_volume(mx);
			} else {
				long h = hit(mx, my);
				if (h != hov) {
					hov = h;
					draw();
				}
			}
		}
		long redraw = 0;
		if (!t && snd_changed())
			redraw = 1;
		if (wifi_open) { /* l'état du Wi-Fi, le balayage, la connexion */
			long r = wl_tick();
			if (r == 2) { /* connecté : la carte se replie */
				net_read();
				wifi_open = 0;
			}
			if (r)
				redraw = 1;
		}
		if (redraw)
			draw();
	}
}
