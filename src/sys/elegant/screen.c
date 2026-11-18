/* screen.c : la résolution de l'écran, à chaud (WS_OP_SCREEN_*, applet Écran du panneau de
 * configuration ; comme kapi_screen_set d'Onyx ; screen.inc), commun au Pi et au PC. Ce qui ne
 * sert qu'au Pi (EDID par la boîte aux lettres du firmware, adresse de bus du DMA 2D) est dans
 * screen_aarch64.c ; sur le PC (screen_x86_64.c) l'EDID est inconnu, et le noyau refuse
 * SYS_WS_MODE par E_IO : une seule résolution, celle du GOP.
 *
 * Le noyau change de framebuffer (SYS_WS_MODE), même adresse chez nous ; le firmware met
 * l'image à l'échelle de la sortie HDMI. Le tampon arrière est déjà assez grand
 * (SCR_BB_BYTES) : seuls son pas, l'écran et les zones sales changent. Ensuite :
 * - les bords réservés (dock, en bas) suivent le bas de l'écran, la zone libre reste de même
 *   marge ;
 * - une fenêtre agrandie remplit la nouvelle zone (WEV_RESIZE), sa place d'avant est ramenée
 *   dans l'écran ; une fenêtre WF_RESIZABLE trop grande rétrécit (WEV_RESIZE) ; les autres sont
 *   replacées, titre visible ;
 * - chaque fenêtre reçoit WEV_SCREEN : menubar, dock et voronoy se refont ;
 * - vncd et rdpd : tampon de copie refait à leur prochain appel (snap_reset).
 *
 * Confirmation : le minuteur est ici, pas dans le panneau : si le panneau meurt, ou si l'écran
 * n'affiche plus rien, la taille d'avant revient quand même (screen_tick). */
#include "elegant.h"

static long scr_pending;   /* 1 : confirmation attendue, */
static u64 scr_deadline;   /* jusqu'à cette heure (ms) */
static long scr_old[2];    /* taille d'avant (largeur, hauteur) */

static const char m_scr[] = "elegant : ecran ";
static const char m_scr_x[] = "x";
static const char m_nl[] = "\n";
static const char m_scr_back[] = "elegant : resolution non confirmee, retour a la precedente\n";

/* WS_OP_SCREEN_SET : a largeur, b hauteur, c attente de confirmation (ms, 0 : définitif) -> 0,
 * E_BUSY, E_INVAL, E_IO */
long req_screen_set(void)
{
	if (scr_pending)
		return E_BUSY;
	long nw = MS64(msg, WM_A), nh = MS64(msg, WM_B);
	u64 wait = M64(msg, WM_C);
	if (nw == scr_w && nh == scr_h)
		return 0; /* (rien à faire) */
	scr_old[0] = scr_w;
	scr_old[1] = scr_h;
	long r = screen_switch(nw, nh);
	if (r < 0)
		return r;
	if (wait) {
		scr_deadline = (u64)korin_sys0(SYS_TIME_MS) + wait;
		scr_pending = 1;
	}
	return 0;
}

/* WS_OP_SCREEN_KEEP : a 1 garder, 0 revenir -> 0 (E_INVAL : rien n'attend) */
long req_screen_keep(void)
{
	if (!scr_pending)
		return E_INVAL;
	scr_pending = 0;
	if (!M64(msg, WM_A))
		screen_switch(scr_old[0], scr_old[1]);
	return 0;
}

/* (boucle principale) l'attente de confirmation dépassée : la taille d'avant */
void screen_tick(void)
{
	if (!scr_pending)
		return;
	if ((u64)korin_sys0(SYS_TIME_MS) < scr_deadline)
		return;
	scr_pending = 0;
	ak_puts(m_scr_back);
	screen_switch(scr_old[0], scr_old[1]);
}

/* nouvelle taille -> 0 ; E_IO : refusée par le firmware (la taille d'avant redemandée, le
 * framebuffer a pu bouger) ; sinon (autre erreur) rien n'a changé */
long screen_switch(long width, long height)
{
	long ow = scr_w, oh = scr_h; /* taille d'avant */
	present_wait(); /* le transfert en cours fini */
	long r = korin_sys3(SYS_WS_MODE, width, height, info);
	if (r < 0 && r != E_IO)
		return r;
	fb_va = (u32 *)info[0]; /* adresse, largeur, hauteur, pas */
	scr_w = (long)info[1];
	scr_h = (long)info[2];
	fb_pitch = (long)info[3];
	bb_pitch = scr_w << 2;
	screen_fb_moved();
	dirty_n = 0; /* (zones de l'ancienne taille) */
	screen_windows(ow, oh);
	snap_reset();
	need_all = 0;
	wins_changed();
	dirty_all();
	ak_puts(m_scr);
	print_dec(scr_w);
	ak_puts(m_scr_x);
	print_dec(scr_h);
	ak_puts(m_nl);
	return r;
}

/* out : coin du contenu d'une fenêtre agrandie, sa taille : la zone libre de l'écran */
void screen_work(long *x, long *y, long *width, long *height)
{
	long top = area_top, bot = area_bot;
	*width = scr_w;
	*height = scr_h - top - bot;
	*x = 0;
	*y = top;
}

/* fenêtre -> 1 si agrandie : ni arrondi, ni ombre */
long win_is_max(struct win *w)
{
	return win_restore[w - windows][WR_ON];
}

/* in/out coin, taille du contenu : dans l'écran autant que possible, entre les bords réservés,
 * barre de titre visible */
void screen_fit(long *x, long *y, long width, long height)
{
	long v = scr_w - FRAME_GAP - width;
	if (*x > v)
		*x = v;
	if (*x < FRAME_GAP)
		*x = FRAME_GAP;
	v = scr_h - area_bot - FRAME_GAP - height;
	if (*y > v)
		*y = v;
	v = area_top + FRAME_GAP;
	if (*y < v)
		*y = v;
}

/* taille d'avant : glisser en cours oublié, fenêtres replacées, curseur dans l'écran,
 * WEV_SCREEN à toutes */
void screen_windows(long old_w, long old_h)
{
	(void)old_w;
	drag_win = 0;
	resize_win = 0;
	long dy = scr_h - old_h; /* ce qui est réservé en bas y reste */
	for (int i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (w->used && (w->flags & WF_RESERVE) && w->y)
			w->y += dy;
	}
	recompute_reserve();
	for (int i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used)
			continue;
		u64 f = w->flags;
		if (f & (WF_RESERVE | WF_BACK)) /* (ils se refont sur WEV_SCREEN) */
			continue;
		long zx, zy, zw, zh;
		if (f & WF_FRAMELESS) { /* sans cadre (bulles, menus ouverts) */
			long mx = scr_w - w->w, my = scr_h - w->h;
			long x = w->x, y = w->y;
			if (x > mx)
				x = mx;
			if (x < 0)
				x = 0;
			if (y > my)
				y = my;
			if (y < 0)
				y = 0;
			w->x = x;
			w->y = y;
			continue;
		}
		long *r = win_restore[i];
		if (r[WR_ON]) { /* agrandie */
			screen_work(&zx, &zy, &zw, &zh);
			w->x = zx;
			w->y = zy;
			resize_post(w, zw, zh);
			if (r[WR_W] > zw) /* sa place d'avant, dans l'écran */
				r[WR_W] = zw;
			if (r[WR_H] > zh)
				r[WR_H] = zh;
			screen_fit(&r[WR_X], &r[WR_Y], r[WR_W], r[WR_H]);
			continue;
		}
		screen_work(&zx, &zy, &zw, &zh);
		long cw = w->w, ch = w->h;
		if (f & WF_RESIZABLE) { /* redimensionnable : dans la zone */
			long nw = cw > zw ? zw : cw, nh = ch > zh ? zh : ch;
			if (nw != cw || nh != ch) {
				cw = nw;
				ch = nh;
				resize_post(w, nw, nh);
			}
		}
		screen_fit(&w->x, &w->y, cw, ch);
	}
	if (cur_x > scr_w - 1) /* curseur */
		cur_x = scr_w - 1;
	if (cur_y > scr_h - 1)
		cur_y = scr_h - 1;
	for (int i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		if (!w->used)
			continue;
		for (int k = 0; k < 32; k++)
			evbuf[k] = 0;
		M32(evbuf, 0) = WEV_SCREEN;
		M32(evbuf, EV_X) = (u32)scr_w;
		M32(evbuf, EV_Y) = (u32)scr_h;
		win_push(w, evbuf);
	}
}

/* WS_OP_WIN_PLACE : a id, b x, c y -> 0 : une fenêtre du programme à cette place, quelle que
 * soit sa couche (le dock se recentre) */
long req_win_place(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	if (pk_owns(w)) /* (mode poche : place tenue par Elegant) */
		return 0;
	dirty_window(w);
	w->x = MS64(msg, WM_B);
	w->y = MS64(msg, WM_C);
	dirty_window(w);
	if (w->flags & WF_RESERVE)
		reserve_update();
	return 0;
}

/* WS_OP_WIN_TOP : a id, b 1 / 0 -> 0 : une fenêtre du programme toujours au-dessus des autres (WF_TOP : la
 * couche « dessus », sur tous les espaces de travail), ou de nouveau comme les autres (l'espace courant). Pour
 * un mini-lecteur (docs/etude/MUSIQUE.md) ; E_INVAL pour une fenêtre du fond (WF_BACK). */
long req_win_top(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	if (w->flags & WF_BACK)
		return E_INVAL;
	int on = MS64(msg, WM_B) != 0;
	if (on == !!(w->flags & WF_TOP))
		return 0;
	dirty_window(w);
	if (on) {
		w->flags |= WF_TOP;
		w->desk = 0;
	} else {
		w->flags &= ~(u64)WF_TOP;
		w->desk = (u64)desk_cur + 1;
	}
	win_raise(w);
	wins_changed();
	dirty_window(w);
	return 0;
}

/* fenêtre libérée : sa place dans win_restore est rendue */
void restore_clear(struct win *w)
{
	win_restore[w - windows][WR_ON] = 0;
}

/* une fenêtre WF_RESERVE a bougé (barre, dock, à une autre échelle) : bords réservés
 * recalculés ; s'ils changent, les fenêtres agrandies suivent */
void reserve_update(void)
{
	long top = area_top, bot = area_bot;
	recompute_reserve();
	if (area_top != top || area_bot != bot)
		for (int i = 0; i < MAX_WINDOWS; i++) {
			struct win *w = &windows[i];
			if (!w->used || !win_restore[i][WR_ON])
				continue;
			long zw, zh;
			dirty_window(w);
			screen_work(&w->x, &w->y, &zw, &zh);
			resize_post(w, zw, zh);
		}
	if (need_all) {
		need_all = 0;
		dirty_all();
	}
}

/* ---- les modes selon l'écran : la table d'Onyx (SystemKit, display.inc), les marques de
 * l'EDID de l'écran (natif, timings établis et standards), et ce que l'EDID ou l'écran actuel
 * ajoutent. Tout mode reste possible : le firmware met à l'échelle ; ceux de l'écran sont les
 * plus nets. */

static const u16 scr_modes[] = {640,  480,  800,  480,  800,  600,  1024, 768,  1280, 720,
				1280, 800,  1280, 1024, 1366, 768,  1440, 900,  1600, 900,
				1600, 1200, 1680, 1050, 1920, 1080, 1920, 1200, 2560, 1440, 0};

/* les entrées WSM_* de la réponse */
struct mode_list {
	u8 *e;
	u64 n;
};

/* taille, drapeaux : marque l'entrée de cette taille, sinon l'ajoute si elle est possible */
static void mode_mark(struct mode_list *l, u64 w, u64 h, u32 flags)
{
	u8 *p = l->e;
	for (u64 i = 0; i < l->n; i++, p += WSM_SIZE)
		if (*(u16 *)(p + WSM_W) == w && *(u16 *)(p + WSM_H) == h) {
			M32(p, WSM_FLAGS) |= flags;
			return;
		}
	if (l->n >= WS_SM_N || (w & 1) || w < 640 || h < 480 || w > SCR_MAX_W || h > SCR_MAX_H)
		return;
	*(u16 *)(p + WSM_W) = (u16)w;
	*(u16 *)(p + WSM_H) = (u16)h;
	M32(p, WSM_FLAGS) = flags;
	l->n++;
}

/* WS_OP_SCREEN_MODES -> nombre ; a, b natif, c, d actuel ; données : WSM_* */
long req_screen_modes(void)
{
	struct mode_list l = {msg + WM_DATA, 0};
	for (const u16 *m = scr_modes; *m; m += 2)
		mode_mark(&l, m[0], m[1], 0);
	M64(msg, WM_A) = 0;
	M64(msg, WM_B) = 0;
	const u8 *e = edid_read();
	if (e) {
		if (e[54] | e[55]) { /* 1er descripteur : le timing préféré */
			/* pixels actifs : 8 + 4 bits */
			u64 w = e[56] | (u64)(e[58] & 0xF0) << 4;
			u64 h = e[59] | (u64)(e[61] & 0xF0) << 4;
			M64(msg, WM_A) = w;
			M64(msg, WM_B) = h;
			mode_mark(&l, w, h, WSMF_NATIVE | WSMF_EDID);
		}
		if (e[35] & 0x20) /* timings établis */
			mode_mark(&l, 640, 480, WSMF_EDID);
		if (e[35] & 1)
			mode_mark(&l, 800, 600, WSMF_EDID);
		if (e[36] & 8)
			mode_mark(&l, 1024, 768, WSMF_EDID);
		if (e[36] & 1)
			mode_mark(&l, 1280, 1024, WSMF_EDID);
		for (int i = 0; i < 8; i++) { /* 8 timings standards : (a + 31) x 8, rapport dans b */
			u32 a = e[38 + 2 * i], b = e[39 + 2 * i];
			if (a <= 1)
				continue;
			u32 w = (a + 31) << 3, h;
			switch (b >> 6) {
			case 1: /* 4:3 */
				h = (w * 3) >> 2;
				break;
			case 2: /* 5:4 */
				h = (w << 2) / 5;
				break;
			case 3: /* 16:9 */
				h = (w * 9) >> 4;
				break;
			default: /* 16:10 */
				h = (w * 5) >> 3;
				break;
			}
			mode_mark(&l, w, h, WSMF_EDID);
		}
	}
	MS64(msg, WM_C) = scr_w; /* actuel : toujours dans la liste */
	MS64(msg, WM_D) = scr_h;
	mode_mark(&l, (u64)scr_w, (u64)scr_h, 0);
	M32(msg, WM_LEN) = (u32)(WSM_SIZE * l.n);
	return (long)l.n;
}
