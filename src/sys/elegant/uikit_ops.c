/* uikit_ops.c : opérations ajoutées pour les widgets de UIKit (ws_ext.inc) : presse-papiers,
 * place d'une fenêtre à l'écran, redimensionnement, agrandir, réduire, espaces de travail,
 * rideau, WIN_DRAG, formes du curseur, POINTER_LOCK, glisser-déposer (uikit_ops.inc).
 *
 * Presse-papiers du système (texte UTF-8). Tenu par Elegant : tous les programmes graphiques lui
 * parlent déjà, il vit aussi longtemps que la session, et le noyau n'a pas à garder de données
 * des programmes. UIKit (champs de texte : Ctrl+C, X, V) l'utilise par ui_clip_set /
 * ui_clip_get ; envoi et lecture en morceaux de WS_CLIP_PIECE octets. */
#include "elegant.h"

/* win_restore (agrandie : x, y, l, h d'avant, 1 ; par place de fenêtre) est défini dans
 * elegant.c (déclaré dans elegant_core.h) ; indices WR_* de elegant_ext.h */

static void ev_clear(void)
{
	for (int i = 0; i < 32; i++)
		evbuf[i] = 0;
}

/* WS_OP_CLIP_SET : a décalage, b longueur totale (<= WS_CLIP_MAX), +40 longueur du morceau ;
 * données : le morceau -> 0 (E_INVAL : hors bornes) */
long req_clip_set(void)
{
	if (!clip_buf) { /* tampon au premier envoi */
		long r = korin_sys1(SYS_SBRK, WS_CLIP_MAX);
		if (r < 0)
			return E_NOMEM;
		clip_buf = (u8 *)r;
	}
	u64 off = M64(msg, WM_A), total = M64(msg, WM_B), n = M32(msg, WM_LEN);
	if (total > WS_CLIP_MAX || n > WS_CLIP_PIECE || off + n > total)
		return E_INVAL;
	if (off == 0) { /* décalage 0 : nouveau contenu */
		clip_len = (long)total;
		clip_gen++;
	}
	u8 *d = clip_buf + off;
	const u8 *s = msg + WM_DATA;
	for (u64 i = 0; i < n; i++)
		d[i] = s[i];
	return 0;
}

/* WS_OP_CLIP_GET : a décalage -> longueur du morceau ; b longueur totale, c génération ;
 * données : le morceau */
long req_clip_get(void)
{
	u64 off = M64(msg, WM_A);
	M64(msg, WM_B) = (u64)clip_len;
	M64(msg, WM_C) = (u64)clip_gen;
	u64 n = (u64)clip_len >= off ? (u64)clip_len - off : 0; /* reste */
	if (n > WS_CLIP_PIECE)
		n = WS_CLIP_PIECE;
	M32(msg, WM_LEN) = (u32)n;
	if (!clip_buf) {
		M32(msg, WM_LEN) = 0;
		return 0;
	}
	const u8 *s = clip_buf + off;
	u8 *d = msg + WM_DATA;
	for (u64 i = 0; i < n; i++)
		d[i] = s[i];
	return (long)n;
}

/* WS_OP_WIN_POS : a id (une fenêtre du programme) -> 0 ; a x, b y du contenu à l'écran,
 * c largeur, d hauteur (menus déroulants des widgets) */
long req_win_pos(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	MS64(msg, WM_A) = w->x;
	MS64(msg, WM_B) = w->y;
	MS64(msg, WM_C) = w->w;
	MS64(msg, WM_D) = w->h;
	return 0;
}

/* ---- fenêtres redimensionnables (WF_RESIZABLE) */

/* WEV_RESIZE à son programme */
void resize_post(struct win *w, long width, long height)
{
	ev_clear();
	M32(evbuf, 0) = WEV_RESIZE;
	M32(evbuf, EV_X) = (u32)width;
	M32(evbuf, EV_Y) = (u32)height;
	win_push(w, evbuf);
}

/* x, y = curseur : appui sur son bord (droit, bas, coin) */
void resize_begin(struct win *w, long x, long y)
{
	long edges = x >= w->x + w->w;      /* bit 0 : bord droit */
	edges |= (long)(y >= w->y + w->h) << 1; /* bit 1 : bord bas */
	if (!edges)
		return; /* (bords gauche, haut : rien) */
	resize_win = w;
	resize_st[0] = x;
	resize_st[1] = y;
	resize_st[2] = w->w;
	resize_st[3] = w->h;
	resize_st[4] = edges;
}

/* curseur au relâché (x1, x2 de l'asm) : la nouvelle taille au programme */
void resize_end(long x, long y)
{
	struct win *w = resize_win;
	resize_win = 0;
	long nw = resize_st[2], nh = resize_st[3];
	if (resize_st[4] & 1)
		nw += x - resize_st[0];
	if (resize_st[4] & 2)
		nh += y - resize_st[1];
	if (nw < 100)
		nw = 100;
	if (nh < 60)
		nh = 60;
	if (nw > 2048)
		nw = 2048;
	if (nh > 2048)
		nh = 2048;
	resize_post(w, nw, nh);
}

/* agrandie à toute la zone libre de l'écran, ou rendue à sa place et sa taille d'avant (bouton
 * agrandir) */
void win_maximize(struct win *w)
{
	long *r = win_restore[w - windows];
	long nw, nh;
	dirty_window(w);
	if (r[WR_ON]) { /* rendue */
		r[WR_ON] = 0;
		w->x = r[WR_X];
		w->y = r[WR_Y];
		nw = r[WR_W];
		nh = r[WR_H];
	} else { /* agrandie */
		r[WR_X] = w->x;
		r[WR_Y] = w->y;
		r[WR_W] = w->w;
		r[WR_H] = w->h;
		r[WR_ON] = 1;
		screen_work(&w->x, &w->y, &nw, &nh);
	}
	dirty_window(w);
	resize_post(w, nw, nh);
}

/* ---- espaces de travail (WS_OP_DESK) : w->desk = l'espace d'une fenêtre + 1 ; celles d'un
 * autre espace sont cachées comme une fenêtre réduite (MINI_OFFDESK : la composition, les clics,
 * le focus sautent tout mini non nul). */

/* WS_OP_DESK : a espace (< 0 : garder), b nombre (0 : garder) -> courant | nombre << 8 |
 * compteur << 16 */
long req_desk(void)
{
	desk_set(MS64(msg, WM_A), MS64(msg, WM_B));
	return desk_cur | desk_count << 8 | (desk_gen & 0x7FFF) << 16;
}

/* espace (< 0 : garder), nombre (0 : garder) : les applique */
void desk_set(long desk, long count)
{
	long old = desk_count, n = old;
	if (count >= 1) {
		n = (u64)count <= WS_DESK_MAX ? count : WS_DESK_MAX;
		desk_count = n;
		for (int i = 0; i < MAX_WINDOWS; i++) /* ceux des espaces retirés : sur le dernier */
			if (windows[i].desk > (u64)n)
				windows[i].desk = (u64)n;
	}
	long changed = old - n; /* (nombre changé : non nul) */
	long cur = desk_cur;
	if (desk < 0)
		desk = cur;
	if ((u64)desk >= (u64)n)
		desk = n - 1;
	if (desk != cur || changed) {
		/* (vue normale, un autre bureau : il glisse ; l'image d'avant est prise ici, overview.c) */
		long slide = desk != cur && !changed && sw_prepare();
		desk_cur = desk;
		desk_apply();
		if (slide)
			sw_start(sw_dir ? sw_dir : desk > cur ? 1 : -1);
	}
	sw_dir = 0;
}

/* espace : le montre */
void desk_show(long desk)
{
	desk_set(desk, 0);
}

/* les fenêtres selon l'espace courant ; le focus, s'il était ailleurs, à la plus en avant de
 * celui-ci */
void desk_apply(void)
{
	desk_gen++;
	u64 cur = (u64)desk_cur + 1;
	for (int i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		u64 m = w->mini & ~(u64)MINI_OFFDESK;
		if (w->desk && w->desk != cur)
			m |= MINI_OFFDESK;
		w->mini = m;
	}
	desk_forget(&capture_win); /* (plus de glisser vers une fenêtre cachée) */
	desk_forget(&drag_win);
	if (!win_focus || win_focus->mini)
		focus_top();
	wins_changed();
	dirty_all();
}

/* WS_OP_WIN_DRAG : a id, fenêtre du programme, bouton tenu depuis un appui dans son contenu :
 * la suite du geste la déplace, comme sur la barre de titre */
long req_win_drag(void)
{
	struct win *w = win_by_id(M64(msg, WM_A), M32(msg, WM_PID));
	if (!w)
		return E_INVAL;
	if (pk_owns(w)) /* (mode poche : pas déplacée) */
		return E_INVAL;
	if (capture_win != w)
		return E_INVAL;
	capture_win = 0;
	drag_win = w;
	drag_dx[0] = cur_x - w->x;
	drag_dx[1] = cur_y - w->y;
	return 0;
}

/* le rideau (WF_CURTAIN) : tant qu'une telle fenêtre existe, les fenêtres normales sont cachées
 * (MINI_CURTAIN), sur tous les espaces ; le focus ne bouge pas, il leur revient quand le rideau
 * tombe */
void curtain_apply(void)
{
	u64 on = 0; /* un rideau ? */
	for (int i = 0; i < MAX_WINDOWS; i++)
		if (windows[i].used && (windows[i].flags & WF_CURTAIN))
			on = 1;
	curtain_on = (long)on;
	for (int i = 0; i < MAX_WINDOWS; i++) {
		struct win *w = &windows[i];
		u64 m = w->mini & ~(u64)MINI_CURTAIN;
		if (!(w->flags & (WF_BACK | WF_TOP)))
			m |= on << 2;
		w->mini = m;
	}
	desk_forget(&capture_win);
	desk_forget(&drag_win);
	wins_changed();
	dirty_all();
}

/* variable (fenêtre) : remise à 0 si sa fenêtre est cachée */
void desk_forget(struct win **v)
{
	if (*v && (*v)->mini)
		*v = 0;
}

/* entrée clavier -> 1 si c'est Ctrl+Alt+Gauche ou Droite : l'espace voisin (en boucle) */
long desk_key(const u8 *in)
{
	u64 code = M64(in, 8), mods = M64(in, 24);
	long dir;
	if (code == KEY_RIGHT)
		dir = 1;
	else if (code == KEY_LEFT)
		dir = -1;
	else
		return 0;
	if (!(mods & 0x11) || !(mods & 0x44)) /* (Ctrl, gauche ou droit ; Alt) */
		return 0;
	if (!M64(in, 16))
		return 1; /* (relâché : avalée aussi) */
	long d = desk_cur + dir;
	if (d < 0)
		d = desk_count - 1;
	if ((u64)d >= (u64)desk_count)
		d = 0;
	sw_dir = dir; /* (le glissement suit la touche, même en revenant au premier) */
	desk_show(d);
	return 1;
}

/* WS_OP_MAXIMIZE : a id -> 0 : comme le bouton agrandir */
long req_maximize(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	if (!(w->flags & WF_RESIZABLE))
		return E_INVAL;
	win_maximize(w);
	return 0;
}

/* WS_OP_MINIMIZE : a id -> 0 : réduite, comme le bouton du cadre (screenshot se cache avant de
 * prendre l'écran ; WS_OP_RAISE la rend) */
long req_minimize(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	win_minimize(w);
	return 0;
}

/* WS_OP_RESIZE : a id, b largeur, c hauteur -> 0 ; a nouveau tampon, b / c décalages des
 * cadres, d l'en-tête WD_*. Le contenu est blanc (le fond : la couleur d'attente), les cadres
 * invisibles jusqu'au dessin du programme ; l'ancien tampon est rendu. */
long req_resize(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	long nw = MS64(msg, WM_B), nh = MS64(msg, WM_C);
	if (nw < 1 || nh < 1 || nw > 2048 || nh > 2048)
		return E_INVAL;
	u64 content = (u64)(nw * nh) << 2;
	u64 frame = 0; /* un cadre */
	if (!(w->flags & WF_FRAMELESS))
		frame = (u64)((nw + 2 * FRAME_BORDER) * (nh + FRAME_TITLE_H + FRAME_BORDER)) << 2;
	u64 size = content + (frame << 1) + WD_SIZE + 7; /* en-tête WD_* en fin de tampon */
	if (size > BUF_SLOT)
		return E_INVAL;
	long b = korin_sys1(SYS_BUF_CREATE, size);
	if (b < 0)
		return b;
	u8 *va = (u8 *)(BUF_VA_BASE + (u64)b * BUF_SLOT);
	u64 fill = (u64)-1;
	if (w->flags & WF_BACK)
		fill = (u64)COL_DESK | (u64)COL_DESK << 32;
	fill64(va, (long)content, fill);
	fill64(va + content, (long)(frame << 1), (u64)TRANSPARENT | (u64)TRANSPARENT << 32);
	korin_sys2(SYS_BUF_GRANT, b, M32(msg, WM_PID));
	dirty_window(w); /* l'ancienne place est à recomposer */
	korin_sys1(SYS_BUF_FREE, w->buf);
	w->w = nw;
	w->h = nh;
	w->surf = (u32 *)va;
	w->buf = (u64)b;
	w->chr_a = (u32 *)(va + content);
	w->chr_i = (u32 *)(va + content + frame);
	w->dmg = (u64 *)(va + ((content + (frame << 1) + 7) & ~(u64)7));
	w->gen++;
	dirty_window(w);
	wins_dirty = 1; /* (rdpd, dock : la liste a changé) */
	if (w->flags & WF_RESERVE) /* (barre, dock : autre échelle) */
		reserve_update();
	M64(msg, WM_A) = (u64)b;
	M64(msg, WM_B) = frame ? content : 0;
	M64(msg, WM_C) = frame ? content + frame : 0;
	M64(msg, WM_D) = (u64)((u8 *)w->dmg - va);
	return 0;
}

/* ---- formes du curseur, glisser-déposer */

/* WS_OP_POINTER_LOCK : a id (une fenêtre du programme), b 1 : prend, 0 : rend -> 0 */
long req_pointer_lock(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	if (M64(msg, WM_B)) {
		lock_win = w;
		pointer_dirty();
	} else if (lock_win == w) {
		pointer_unlock();
	}
	return 0;
}

/* plus de fenêtre qui tienne le pointeur ; le curseur reparaît */
void pointer_unlock(void)
{
	lock_win = 0;
	pointer_dirty();
}

void pointer_dirty(void)
{
	long x, y, w, h;
	cursor_box(&x, &y, &w, &h);
	dirty(x, y, w, h);
}

/* WS_OP_CURSOR : a id (une fenêtre du programme), b forme -> 0 */
long req_cursor(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	u64 shape = M64(msg, WM_B);
	if (shape == UC_DRAG)
		shape = 0;
	if (shape > UC_NO)
		shape = 0;
	/* cur_req_win garde l'id de la fenêtre, pas sa fiche, comme l'asm (cursor_pick la compare à w->id) */
	cur_req_win = w->id;
	cur_req = (long)shape;
	pointer_dirty(); /* le curseur à redessiner */
	return 0;
}

/* formes du curseur (WS_OP_CURSOR, UC_*) : point chaud x, y, puis 12 x 19 (« X » noir, « . »
 * blanc, espace : transparent) ; 230 octets chacune */
#define CURSOR_BYTES 230
static const struct cursor_shape {
	u8 hx, hy;
	char px[19 * 12] __attribute__((nonstring)); /* 19 lignes de 12 */
} cursor_shapes[] = {
	{0, 0, { /* flèche */
		"X           "
		"XX          "
		"X.X         "
		"X..X        "
		"X...X       "
		"X....X      "
		"X.....X     "
		"X......X    "
		"X.......X   "
		"X........X  "
		"X.........X "
		"X..........X"
		"X......XXXXX"
		"X...X..X    "
		"X..XX..X    "
		"X.X  X..X   "
		"XX   X..X   "
		"X     X..X  "
		"      XXX   "
	}},
	{4, 9, { /* texte */
		" .XX.XX.    "
		" ..XXX..    "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		"   .X.      "
		" ..XXX..    "
		" .XX.XX.    "
	}},
	{5, 0, { /* main */
		"    XX      "
		"   X..X     "
		"   X..X     "
		"   X..X     "
		"   X..XXXX  "
		"   X..X..XX "
		"XX X..X..X.X"
		"X.XX.......X"
		"X..X.......X"
		" X.........X"
		" X.........X"
		"  X........X"
		"  X.......X "
		"   X......X "
		"   X.....X  "
		"    XXXXXX  "
		"            "
		"            "
		"            "
	}},
	{6, 9, { /* redim. horizontal */
		"            "
		"            "
		"            "
		"            "
		"            "
		"            "
		"  X      X  "
		" X.X    X.X "
		"X..XXXXXX..X"
		"X..........X"
		"X..XXXXXX..X"
		" X.X    X.X "
		"  X      X  "
		"            "
		"            "
		"            "
		"            "
		"            "
		"            "
	}},
	{5, 9, { /* redim. vertical */
		"     X      "
		"    X.X     "
		"   X...X    "
		"  X.....X   "
		"  XXX.XXX   "
		"    X.X     "
		"    X.X     "
		"    X.X     "
		"    X.X     "
		"    X.X     "
		"    X.X     "
		"    X.X     "
		"    X.X     "
		"    X.X     "
		"  XXX.XXX   "
		"  X.....X   "
		"   X...X    "
		"    X.X     "
		"     X      "
	}},
	{5, 6, { /* attente */
		"XXXXXXXXXX  "
		"X........X  "
		" X......X   "
		" X.XXXX.X   "
		"  X.XX.X    "
		"   X..X     "
		"    XX      "
		"   X..X     "
		"  X....X    "
		" X..XX..X   "
		" X.XXXX.X   "
		"X.XXXXXX.X  "
		"XXXXXXXXXX  "
		"            "
		"            "
		"            "
		"            "
		"            "
		"            "
	}},
	{5, 9, { /* croix */
		"            "
		"            "
		"            "
		"            "
		"    .X.     "
		"    .X.     "
		"    .X.     "
		"    .X.     "
		"    .X.     "
		"XXXXXXXXXXX "
		"XXXXXXXXXXX "
		"    .X.     "
		"    .X.     "
		"    .X.     "
		"    .X.     "
		"    .X.     "
		"            "
		"            "
		"            "
	}},
	{0, 0, { /* glisser */
		"X           "
		"XX          "
		"X.X         "
		"X..X        "
		"X...X       "
		"X....X      "
		"X.....X     "
		"X......X    "
		"X.......X   "
		"X........X  "
		"X.........X "
		"X..........X"
		"X......XXXXX"
		"X...X..X    "
		"X..XX.XXXXXX"
		"X.X  .X....X"
		"XX   .X....X"
		"      X....X"
		"      XXXXXX"
	}},
	{5, 9, { /* déplacer */
		"            "
		"     X      "
		"    X.X     "
		"   X...X    "
		"  XXX.XXX   "
		"    X.X     "
		"  X X.X X   "
		" XX X.X XX  "
		"X..XX.XX..X "
		"X.........X "
		"X..XX.XX..X "
		" XX X.X XX  "
		"  X X.X X   "
		"    X.X     "
		"  XXX.XXX   "
		"   X...X    "
		"    X.X     "
		"     X      "
		"            "
	}},
	{5, 9, { /* coin nwse */
		"            "
		"            "
		"XXXXXX      "
		"X....X      "
		"X...X       "
		"X...X       "
		"X..X.X      "
		"X.X X.X     "
		"XX   X.X    "
		"X     X.X   "
		"       X.X X"
		"        X.XX"
		"         X.X"
		"        X..X"
		"        X..."
		"       X...X"
		"      X....X"
		"      XXXXXX"
		"            "
	}},
	{6, 9, { /* coin nesw */
		"            "
		"            "
		"      XXXXXX"
		"      X....X"
		"       X...X"
		"       X...X"
		"      X.X..X"
		"     X.X X.X"
		"    X.X   XX"
		"   X.X     X"
		"X X.X       "
		"XX.X        "
		"X.X         "
		"X..X        "
		"...X        "
		"X...X       "
		"X....X      "
		"XXXXXX      "
		"            "
	}},
	{5, 9, { /* interdit */
		"            "
		"            "
		"            "
		"   XXXXX    "
		"  X.....X   "
		" X.XXXXX.X  "
		"X.XX  X..X.X"
		"X.X  X..X.X "
		"X.X X..X X.X"
		"X.XX..X  X.X"
		"X.X..X  XX.X"
		" X..X  X.X  "
		" X.XXXXX.X  "
		"  X.....X   "
		"   XXXXX    "
		"            "
		"            "
		"            "
		"            "
	}},
};
_Static_assert(sizeof(struct cursor_shape) == CURSOR_BYTES, "forme du curseur : 230 octets");
_Static_assert(sizeof(cursor_shapes) / CURSOR_BYTES == UC_NO + 1, "une forme par UC_*");

/* quels bords (bit 0 : droit ; bit 1 : bas) -> la forme */
static long edge_shape(long edges)
{
	if (edges == 1)
		return UC_HRESIZE;
	if (edges == 2)
		return UC_VRESIZE;
	if (edges == 3)
		return UC_NWSE;
	return UC_ARROW;
}

/* -> dessin de la forme du curseur, *hot_x, *hot_y = point chaud : glisser en cours, bord d'une
 * fenêtre redimensionnable, forme demandée par la fenêtre sous le pointeur ; sinon la flèche */
const u8 *cursor_pick(long *hot_x, long *hot_y)
{
	long shape = UC_ARROW;
	if (drag_on) {
		shape = UC_DRAG;
	} else if (resize_win) {
		shape = edge_shape(resize_st[4]); /* ses bords */
	} else if (drag_win) {
		shape = UC_MOVE;
	} else {
		long zone;
		struct win *w = win_hit(cur_x, cur_y, &zone);
		if (w) {
			if (zone == Z_BORDER) {
				if (w->flags & WF_RESIZABLE) { /* quels bords (comme resize_begin) ? */
					long edges = cur_x >= w->x + w->w;
					edges |= (long)(cur_y >= w->y + w->h) << 1;
					shape = edge_shape(edges);
				}
			} else if (zone == Z_CLIENT && w->id == cur_req_win) {
				shape = cur_req;
			}
		}
	}
	cur_shape = shape;
	const struct cursor_shape *c = &cursor_shapes[shape];
	*hot_x = c->hx;
	*hot_y = c->hy;
	return (const u8 *)c->px;
}

/* WS_OP_CURSOR_GET -> forme UC_* dessinée */
long req_cursor_get(void)
{
	return cur_shape;
}

/* image du glisser (WS_OP_DRAG_IMAGE, plus bas) */
#define GHOST_ALPHA 150 /* sur 256 */
static u32 ghost[WS_DRAG_IMG_W * WS_DRAG_IMG_H];
static long ghost_w, ghost_h, ghost_hx, ghost_hy;

/* WS_OP_DRAG : a genre, +40 octets, données -> 0 (glisser en cours ; il finit au relâché du
 * bouton) */
long req_drag(void)
{
	u64 n = M32(msg, WM_LEN);
	if (n > WS_DRAG_MAX)
		n = WS_DRAG_MAX;
	drag_type = MS64(msg, WM_A);
	drag_len = (long)n;
	const u8 *s = msg + WM_DATA;
	for (u64 i = 0; i < n; i++)
		drag_data[i] = s[i];
	pointer_dirty();
	ghost_w = 0; /* (l'image d'un glisser d'avant) */
	drag_on = 1;
	return 0;
}

/* ---- image du glisser (WS_OP_DRAG_IMAGE, 18/11/2026) : un rectangle du contenu de la fenêtre
 * (l'élément glissé), copié ici et dessiné en transparence sous le curseur jusqu'au relâché */

/* -> le rectangle du curseur, et de l'image d'un glisser en cours (zones à redessiner) */
void cursor_box(long *x, long *y, long *w, long *h)
{
	long x0 = cur_x - 8, y0 = cur_y - 10, x1 = x0 + 20, y1 = y0 + 29;
	if (drag_on && ghost_w) {
		long gx = cur_x - ghost_hx, gy = cur_y - ghost_hy;
		if (gx < x0)
			x0 = gx;
		if (gy < y0)
			y0 = gy;
		if (gx + ghost_w > x1)
			x1 = gx + ghost_w;
		if (gy + ghost_h > y1)
			y1 = gy + ghost_h;
	}
	*x = x0;
	*y = y0;
	*w = x1 - x0;
	*h = y1 - y0;
}

/* WS_OP_DRAG_IMAGE : a id, b x | y << 16 | largeur << 32 | hauteur << 48, c hx | hy << 32 -> 0 */
long req_drag_image(void)
{
	struct win *w = req_win();
	if (!w)
		return E_BADF;
	if (!drag_on || !w->surf)
		return E_INVAL;
	u64 b = M64(msg, WM_B), c = M64(msg, WM_C);
	long x = (long)(b & 0xFFFF), y = (long)(b >> 16 & 0xFFFF);
	long gw = (long)(b >> 32 & 0xFFFF), gh = (long)(b >> 48);
	long hx = (long)(int)(u32)c, hy = (long)(int)(u32)(c >> 32);
	if (x >= w->w || y >= w->h)
		return E_INVAL;
	if (gw > w->w - x)
		gw = w->w - x;
	if (gh > w->h - y)
		gh = w->h - y;
	if (gw > WS_DRAG_IMG_W) /* (coupé autour du point tenu) */
		gw = WS_DRAG_IMG_W;
	if (gh > WS_DRAG_IMG_H)
		gh = WS_DRAG_IMG_H;
	if (gw <= 0 || gh <= 0)
		return E_INVAL;
	pointer_dirty();
	for (long r = 0; r < gh; r++)
		for (long k = 0; k < gw; k++)
			ghost[r * gw + k] = w->surf[(y + r) * w->w + x + k];
	ghost_w = gw;
	ghost_h = gh;
	ghost_hx = hx;
	ghost_hy = hy;
	pointer_dirty();
	return 0;
}

/* l'image du glisser en cours, dans le clip (avant le curseur) */
void draw_ghost(void)
{
	if (!drag_on || !ghost_w)
		return;
	long px = cur_x - ghost_hx, py = cur_y - ghost_hy;
	long r0 = clip_r.y0 - py, r1 = clip_r.y1 - py, k0 = clip_r.x0 - px, k1 = clip_r.x1 - px;
	if (r0 < 0)
		r0 = 0;
	if (r1 > ghost_h)
		r1 = ghost_h;
	if (k0 < 0)
		k0 = 0;
	if (k1 > ghost_w)
		k1 = ghost_w;
	for (long r = r0; r < r1; r++) {
		u32 *d = (u32 *)((u8 *)bb + (py + r) * bb_pitch) + px;
		const u32 *s = ghost + r * ghost_w;
		for (long k = k0; k < k1; k++) {
			u32 a = s[k], o = d[k];
			u32 rb = ((a & 0xFF00FF) * GHOST_ALPHA + (o & 0xFF00FF) * (256 - GHOST_ALPHA)) >> 8;
			u32 g = ((a & 0xFF00) * GHOST_ALPHA + (o & 0xFF00) * (256 - GHOST_ALPHA)) >> 8;
			d[k] = (rb & 0xFF00FF) | (g & 0xFF00);
		}
	}
}

/* WS_OP_DROP_GET : -> octets ; a genre, b octets ; données (le dernier glisser-déposer) */
long req_drop_get(void)
{
	MS64(msg, WM_A) = drag_type;
	MS64(msg, WM_B) = drag_len;
	M32(msg, WM_LEN) = (u32)drag_len;
	u8 *d = msg + WM_DATA;
	for (long i = 0; i < drag_len; i++)
		d[i] = drag_data[i];
	return drag_len;
}

/* curseur au relâché : un glisser en cours se dépose sur le contenu de la fenêtre dessous
 * (WEV_DROP) */
void drag_drop(long x, long y)
{
	if (!drag_on)
		return;
	pointer_dirty(); /* (l'image du glisser s'en va) */
	drag_on = 0;
	ghost_w = 0;
	long zone;
	struct win *w = win_hit(x, y, &zone);
	if (!w || zone != Z_CLIENT)
		return;
	ev_clear();
	M32(evbuf, 0) = WEV_DROP;
	M32(evbuf, EV_X) = (u32)(x - w->x);
	M32(evbuf, EV_Y) = (u32)(y - w->y);
	M32(evbuf, 16) = (u32)drag_type;
	M32(evbuf, 20) = (u32)drag_len;
	M32(evbuf, EV_MODS) = (u32)cur_mods; /* Maj, Ctrl tenus au relâché (Fichiers : Maj déplace) */
	win_push(w, evbuf);
}
