/* pocket.c : mode « poche » (shell=poche ; docs/MODE-POCHE.md, docs/DESIGN-ELEGANT.md 18 ;
 * pocket.inc) : une coquille pour le PocketTerm, dans le même serveur. Une app devant, en plein
 * écran sous la barre de menus ; l'accueil (fenêtre WF_HOME de /sys/home) derrière toutes
 * les apps ; les autres apps restent derrière, cachées (MINI_BEHIND : ni composées, ni
 * cliquables).
 *
 * Placement (pk_place, à la création) :
 * - fenêtre à cadre, redimensionnable ou de taille fixe : agrandie à la zone sous la barre (le
 *   chemin du bouton agrandir : win_restore, WEV_RESIZE) ; une fenêtre qui garde une taille plus
 *   petite est centrée dans la zone ;
 * - dialogue WF_MODAL : garde sa taille, centré ; un voile TEXTE1 à 30 % (veil_fill) est posé
 *   sous lui, sur l'app ou l'accueil ;
 * - l'accueil (WF_HOME) : toute la zone sous la barre, comme une agrandie ;
 * - sans cadre : à sa place (menus, volets, toasts, barre).
 * Le cadre n'a que deux glyphes : ‒ (l'accueil : korin.shell.home) et × ; agrandir disparaît ;
 * ni déplacement ni bords (core.c, POLF_POCKET).
 *
 * Touches système (pk_key, avant l'application) :
 *	Super seul		korin.shell.home ; un second Super revient à l'app
 *	Alt+Tab, Super+Tab	korin.shell.switch (« alt » : relâcher Alt choisira ; Alt
 *				relâché : « commit »)
 *	Super+N			korin.shell.quick
 *	Super+L			korin.shell.sleep
 *	F11			barre du haut masquée / remontée (plein écran)
 *
 * Sortie de secours : la session poche attend PK_GRACE ms après la prise de l'écran ; Maj
 * appuyée (ou tenue au branchement du clavier) pendant ce temps fait démarrer le bureau à la
 * place. « shell=desktop » dans /etc/options.txt (ou /options.txt) l'emporte sur
 * system.ini (elegant.c : mode_choose). */
#include "elegant.h"

#define PK_GRACE 2000		  /* ms avant la session (Maj : le bureau) */
#define PK_VEIL_COL 0x0014171Cu	  /* TEXTE1 de Givre (encre du voile, les deux thèmes) */
#define PK_VEIL_A 77		  /* 30 % (sur 256) */
#define PK_FACE_LIGHT 0x00F5F6F8u /* FACE de Givre, d'Onyx Noir : le fond sans accueil */
#define PK_FACE_DARK 0x0015171Bu

#define HID_TAB 0x2B
#define HID_N 0x11
#define HID_L 0x0F
#define HID_F11 0x44
#define HID_LSHIFT 0xE1
#define HID_RSHIFT 0xE5
#define HID_LALT 0xE2
#define HID_RALT 0xE6
#define HID_LGUI 0xE3
#define HID_RGUI 0xE7
#ifndef MOD_SHIFT
#define MOD_SHIFT 0x22 /* (uk_consts.h : 34, la même) */
#endif
#define MOD_ALT 0x44
#define MOD_GUI 0x88

static void pk_init(void);
static void pk_back(void);
static void pk_tick(void);
static void pk_restack(void);
static void pk_place(struct win *w);
static long pk_key(const u8 *in);
static void pk_gone(struct win *w);
static void pk_raise(struct win *w);

const struct policy mode_pocket = {
	"poche", 0, pk_init, pk_back, dk_none, dk_no, dk_no, pk_tick, pk_restack, POLF_POCKET,
	pk_place, pk_key, pk_gone, pk_raise,
};

/* sans /etc/session.poche (chaînes à la suite, puis une vide ; modifiable comme le
 * segment de l'asm, session_run coupe la ligne au premier espace) */
static char session_p_def[] = "/sys/menubar --poche\0"
			      "/sys/home\0"
			      "/sys/quick\0"
			      "/sys/switcher\0"
			      "/sys/notifyd\0";
static const char m_escape[] = "elegant : Maj au demarrage, le bureau a la place du mode poche\n";

static u64 pk_start;		/* ms : lancement de la session (0 : fait) */
static long pk_home;		/* 1 : l'accueil devant */
static struct win *pk_front;	/* l'app devant (0 : aucune) */
static long pk_any;		/* 1 : au moins une app ouverte */
struct win *pk_veil;		/* le dialogue sous lequel est le voile (-1 : à refaire) */
static u64 pk_bar_off;		/* 1 : barre masquée (F11) */
static long pk_super;		/* Super appuyé, seul jusqu'ici */
static long pk_alttab;		/* Alt+Tab en cours (Alt tenu) */
static u64 pk_eat;		/* code HID dont le relâché est avalé */
static u32 pk_face = PK_FACE_LIGHT; /* fond sans accueil */

/* après la prise de l'écran : la session attend PK_GRACE ms (sortie de secours) */
static void pk_init(void)
{
	pk_start = (u64)korin_sys0(SYS_TIME_MS) + PK_GRACE;
	/* le fond : korin.theme n'est publié qu'au changement de thème, theme.txt donne celui
	 * du démarrage (« Onyx Noir » : sombre) */
	read_key(conf_path("theme.txt", 0), "theme");
	pk_face = ini_val[0] == 'O' ? PK_FACE_DARK : PK_FACE_LIGHT;
}

static void pk_bus_ev(u8 *ev);

/* à chaque tour : la session (après l'attente), le bus */
static void pk_tick(void)
{
	if (pk_start && (u64)korin_sys0(SYS_TIME_MS) >= pk_start) {
		pk_start = 0;
		session_start("/etc/session.poche", session_p_def);
	}
	sh_tick(pk_bus_ev);
}

/* Maj pendant l'attente : le bureau à la place du mode poche */
static void pk_escape(void)
{
	pk_start = 0;
	policy = &mode_desktop;
	ak_puts(m_escape);
	if (bp_canal) /* le bus sait déjà « poche » : corrigé */
		bp_publish(sh_topic_mode, str_mode_desktop, BUS_RETAIN);
	dk_init();
	dirty_all();
}

static void pk_show_home(void);

/* événement du bus (WEV_BUS) */
static void pk_bus_ev(u8 *ev)
{
	u64 sub = M32(ev, BEV_SUB);
	if (sub == (u64)sh_sub_home) {
		pk_show_home();
	} else if (sub == (u64)sh_sub_theme) {
		pk_face = ev[BEV_DATA] == 'O' ? PK_FACE_DARK : PK_FACE_LIGHT; /* « Onyx Noir, ... » */
		dirty_all();
	}
}

/* fond uni (le thème), dans le clip : sans accueil seulement */
static void pk_back(void)
{
	bb_fill(clip_r.x0, clip_r.y0, clip_r.x1 - clip_r.x0, clip_r.y1 - clip_r.y0, pk_face);
}

/* ---- les commandes */

/* -> la fenêtre de l'accueil (0 : aucune) */
static struct win *pk_find_home(void)
{
	for (int i = 0; i < MAX_WINDOWS; i++)
		if (windows[i].used && (windows[i].flags & WF_HOME))
			return &windows[i];
	return 0;
}

/* l'accueil : korin.shell.home sur le bus. /sys/home le traite lui-même (WS_OP_RAISE de sa
 * fenêtre WF_HOME, puis de l'app d'avant au second appel : pk_raise) ; sans programme
 * d'accueil, l'événement revient ici (pk_bus_ev) ; sans busd, tout de suite */
void pk_home_cmd(void)
{
	if (sh_publish(sh_topic_home, sh_val_1))
		return;
	pk_home = 1; /* (sans busd : l'accueil devant) */
	wins_changed();
}

/* (sans programme d'accueil seulement) les apps cachées */
static void pk_show_home(void)
{
	if (pk_find_home())
		return;
	pk_home = 1;
	wins_changed();
}

/* Super seul : korin.shell.home (l'accueil fait l'aller et le retour) ; sans programme
 * d'accueil : l'accueil vide, ou (déjà montré) l'app d'avant */
static void pk_super_key(void)
{
	if (pk_find_home() || !pk_home) {
		pk_home_cmd();
		return;
	}
	if (!pk_any) /* une app derrière ? */
		return;
	pk_home = 0;
	wins_changed();
}

/* F11 : la barre du haut (fenêtres WF_RESERVE en haut) masquée ou remontée ; les fenêtres
 * agrandies suivent (reserve_update) */
static void pk_bar_toggle(void)
{
	pk_bar_off ^= 1;
	for (int i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || !(w->flags & WF_RESERVE) || w->y)
			continue;
		w->mini = (w->mini & ~(u64)MINI_BAR) | pk_bar_off << 4; /* (MINI_BAR) */
	}
	reserve_update();
	dirty_all();
	wins_changed();
}

/* entrée IN_KEY -> 1 si prise par la coquille */
static long pk_key(const u8 *in)
{
	u64 code = M64(in, 8);	/* code HID */
	u64 press = M64(in, 16); /* 1 appui, 0 relâché */
	u64 mods = M64(in, 24);	/* modificateurs */
	if (pk_start) { /* l'attente : Maj -> le bureau */
		if ((mods & MOD_SHIFT) || code == HID_LSHIFT || code == HID_RSHIFT)
			pk_escape();
		return 0;
	}
	if (code == HID_LGUI || code == HID_RGUI) { /* Super : avalé ; seul -> l'accueil */
		if (press) {
			pk_super = 1;
		} else {
			long alone = pk_super;
			pk_super = 0;
			if (alone)
				pk_super_key();
		}
		return 1;
	}
	if (!press) {
		if (pk_eat == code) { /* relâché d'une touche prise */
			pk_eat = 0;
			return 1;
		}
		/* Alt relâché après Alt+Tab : le commutateur passe à l'app choisie */
		if ((code == HID_LALT || code == HID_RALT) && pk_alttab) {
			pk_alttab = 0;
			sh_publish(sh_topic_switch, "commit");
		}
		return 0;
	}
	pk_super = 0; /* une autre touche : Super n'est plus seul */
	const char *topic, *val;
	if (code == HID_F11) {
		pk_bar_toggle();
	} else {
		if (mods & MOD_GUI) { /* Super+Tab, N, L */
			val = sh_val_1;
			if (code == HID_TAB)
				topic = sh_topic_switch;
			else if (code == HID_N)
				topic = sh_topic_quick;
			else if (code == HID_L)
				topic = sh_topic_sleep;
			else
				return 0;
		} else {
			if (!(mods & MOD_ALT) || code != HID_TAB) /* Alt+Tab */
				return 0;
			pk_alttab = 1;
			val = mods & MOD_SHIFT ? "alt-prev" : "alt";
			topic = sh_topic_switch;
		}
		sh_publish(topic, val);
	}
	pk_eat = code; /* son relâché aussi */
	return 1;
}

/* ---- placement et ordre */

/* -> 1 si c'est une app : normale, à cadre (dialogues compris), hors accueil */
long pk_is_app(const struct win *w)
{
	if (w->flags & (WF_FRAMELESS | WF_BACK | WF_TOP))
		return 0;
	return !(w->flags & WF_HOME);
}

/* -> 1 si le mode poche tient sa place (une app, ou l'accueil) : WS_OP_WIN_PLACE, WIN_MOVE,
 * WIN_DRAG sans effet */
long pk_owns(const struct win *w)
{
	if (!(policy->flags & POLF_POCKET))
		return 0;
	if (w->flags & WF_HOME)
		return 1;
	return pk_is_app(w);
}

/* agrandie à la zone sous la barre (win_restore, comme le bouton agrandir ; reserve_update la
 * suivra) ; WEV_RESIZE si sa taille diffère */
static void pk_maximize(struct win *w)
{
	long *r = win_restore[w - windows];
	if (!r[WR_ON]) {
		r[WR_X] = w->x;
		r[WR_Y] = w->y;
		r[WR_W] = w->w;
		r[WR_H] = w->h;
		r[WR_ON] = 1;
	}
	dirty_window(w);
	long zw, zh;
	screen_work(&w->x, &w->y, &zw, &zh);
	if (w->w != zw || w->h != zh)
		resize_post(w, zw, zh);
	dirty_window(w);
}

/* centrée dans la zone sous la barre (pas au-dessus d'elle) -> 1 si elle a bougé */
static long pk_center(struct win *w)
{
	long x, y, zw, zh;
	screen_work(&x, &y, &zw, &zh);
	long dx = zw - w->w, dy = zh - w->h;
	if (dx < 0)
		dx = 0;
	if (dy < 0)
		dy = 0;
	x += (u64)dx >> 1;
	y += (u64)dy >> 1;
	if (x == w->x && y == w->y)
		return 0;
	dirty_window(w);
	w->x = x;
	w->y = y;
	dirty_window(w);
	return 1;
}

/* fenêtre créée */
static void pk_place(struct win *w)
{
	if (w->flags & WF_HOME) {
		pk_maximize(w);
	} else if (pk_is_app(w)) {
		if (w->flags & WF_MODAL) {
			pk_center(w);
		} else {
			pk_home = 0; /* une app de plus : devant */
			pk_maximize(w);
		}
	}
	wins_changed();
}

/* fenêtre ramenée devant (WS_OP_RAISE : le commutateur, la bande de l'accueil) : une app
 * efface l'accueil ; l'accueil le montre */
static void pk_raise(struct win *w)
{
	if (w->flags & WF_HOME)
		pk_home = 1;
	else if (pk_is_app(w))
		pk_home = 0;
	wins_changed();
}

/* fenêtre qui va disparaître : l'app devant se ferme -> l'accueil (si son programme n'a plus
 * d'autre fenêtre) */
static void pk_gone(struct win *w)
{
	if (pk_veil == w) /* (le voile sera refait) */
		pk_veil = (struct win *)-1;
	if (pk_front == w) {
		u64 pid = w->pid; /* une autre fenêtre de ce programme ? */
		for (int i = 0; i < MAX_WINDOWS; i++) {
			struct win *o = &windows[i];
			if (!o->used || o == w || o->pid != pid || (o->mini & ~(u64)MINI_BEHIND))
				continue;
			if (pk_is_app(o)) {
				wins_changed();
				return;
			}
		}
		pk_home = 1;
	}
	wins_changed();
}

/* hide = 1 : cachée (MINI_BEHIND), 0 : montrée -> 1 si elle a changé */
static long pk_hide(struct win *w, u64 hide)
{
	u64 m = w->mini, n = (m & ~(u64)MINI_BEHIND) | hide << 3;
	w->mini = n;
	return m != n;
}

/* POL_WINS (liste, ordre, focus changés) : qui est devant, qui est caché ; places (une
 * agrandie plus petite que la zone est centrée, un dialogue aussi), voile, focus */
static void pk_restack(void)
{
	long changed = 0; /* quelque chose a changé (tout recomposer) */
	/* places : agrandies et dialogues centrés */
	for (int i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || !pk_is_app(w))
			continue;
		/* (pas encore agrandie : pk_place le fera) */
		if ((w->flags & WF_MODAL) || win_restore[i][WR_ON])
			pk_center(w);
	}
	/* de l'avant vers l'arrière : la première app (et les dialogues au-dessus d'elle)
	 * montrée, les autres cachées */
	struct win *front = 0; /* app devant */
	struct win *veil = 0;  /* dialogue le plus bas montré (voile) */
	struct win *top = 0;   /* fenêtre montrée la plus en avant */
	long any = 0;	       /* une app existe */
	for (long k = win_count; k > 0;) {
		struct win *w = win_z[--k];
		if (!pk_is_app(w))
			continue;
		if (w->mini & ~(u64)MINI_BEHIND) /* réduite, rideau... : pas candidate */
			continue;
		any = 1;
		u64 hide = 1; /* cachée par défaut */
		if (!pk_home && !front) {
			hide = 0;
			if (!top)
				top = w;
			if (w->flags & WF_MODAL)
				veil = w;
			else
				front = w;
		}
		changed |= pk_hide(w, hide);
	}
	pk_front = front;
	pk_any = any;
	/* l'accueil : caché sous l'app devant, même plus petite que la zone (Réglages,
	 * Terminal : centrées ; autour, le fond uni de pk_back, pas la bande « Ouvertes » ni la
	 * recherche, qui prendraient les clics) */
	struct win *home = pk_find_home();
	if (home)
		changed |= pk_hide(home, front != 0);
	/* les fenêtres du fond (applets) : cachées s'il y a un accueil */
	for (int i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used || !(w->flags & WF_BACK) || (w->flags & WF_HOME))
			continue;
		changed |= pk_hide(w, home != 0);
	}
	if (pk_veil != veil) { /* le voile */
		pk_veil = veil;
		changed = 1;
	}
	if (changed) {
		desk_forget(&capture_win); /* (plus de capture sur une cachée) */
		desk_forget(&drag_win);
		dirty_all();
	}
	/* le focus : la fenêtre montrée la plus en avant, sinon l'accueil ; pas pris à une
	 * fenêtre du dessus (volets, commutateur) */
	struct win *f = top ? top : home;
	struct win *cur = win_focus;
	if (cur == f)
		return;
	if (cur && cur->used && !cur->mini && (cur->flags & (WF_TOP | WF_FRAMELESS)))
		return; /* (menus, popups d'une app) */
	win_set_focus(f);
}

/* ---- le voile : TEXTE1 à 30 % sur ce qui est déjà dans le tampon arrière (zone clip_r),
 * juste avant le dialogue (compose_clip) */
void veil_fill(void)
{
	long x0 = clip_r.x0, y0 = clip_r.y0;
	long w = clip_r.x1 - x0, h = clip_r.y1 - y0; /* largeur, hauteur */
	if (w <= 0 || h <= 0)
		return;
	u8 *row = (u8 *)bb + y0 * bb_pitch + (x0 << 2);
	/* encre * a (deux canaux à la fois : rouge et bleu, puis vert) */
	const u64 ink_rb = (u64)(PK_VEIL_COL & 0x00FF00FF) * PK_VEIL_A;
	const u64 ink_g = (u64)(PK_VEIL_COL & 0x0000FF00) * PK_VEIL_A;
	const u64 inv = 256 - PK_VEIL_A;
	for (; h > 0; h--, row += bb_pitch) {
		u32 *p = (u32 *)row;
		for (long i = 0; i < w; i++) {
			u64 v = p[i];
			u64 rb = (((v & 0x00FF00FF) * inv + ink_rb) >> 8) & 0x00FF00FF;
			u64 g = (((v & 0x0000FF00) * inv + ink_g) >> 8) & 0x0000FF00;
			p[i] = (u32)(rb | g);
		}
	}
}
