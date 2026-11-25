/* remote.c : les fenêtres une à une, pour rdpd (docs/RESEAU.md 5.4, étape 12 ; remote.inc ; le
 * client Onyx Remote montre chaque fenêtre dans une fenêtre du PC) :
 *	WS_OP_WIN_LIST	toutes les fenêtres dans l'ordre d'affichage, avec les générations du
 *			contenu et du cadre, l'état, le clavier
 *	WS_OP_WIN_READ	un rectangle du contenu ou d'une copie du cadre (ou du fond :
 *			WS_WIN_DESKTOP) dans le tampon de l'appelant
 *	WS_OP_WIN_MOVE	placer une fenêtre (déplacée sur le PC)
 *	WS_OP_WIN_CLOSE	fermer comme la case de fermeture (WEV_CLOSE)
 * WS_OP_RAISE (core.c) passe une fenêtre devant. Le tampon de l'appelant est celui de
 * WS_OP_SNAPSHOT (snapshot.c : de la taille de l'écran, créé au premier appel) : un même
 * programme ne se sert pas des deux à la fois. */
#include "elegant.h"

#define M16(m, off) (*(u16 *)((u8 *)(m) + (off)))

/* ---- G4, G5 (E36) : les opérations sur les fenêtres des autres (lire leurs pixels, l'écran entier, la liste
 * complète avec titres et pids, les fermer, les déplacer, une commande de leur menu) : l'uid 0 et les programmes
 * de cette liste seulement, reconnus par le chemin de leur image (SYS_PROC_EXE) ET l'empreinte de son contenu,
 * comparée à celle du fichier (ws_same_file : un compte crée une image au chemin de son choix, IMAGE_CREATE). Ces
 * chemins sont protégés en
 * écriture dans tous les modes de vfs (/sys, /bin ; /apps/screenshot.app et /apps/taskman.app ajoutés) ; un
 * programme lancé par un interprète (un script) n'en est pas. Le Pi (pas d'identité, E_NOSYS) : permis. */
static const char *const ws_trusted_exe[] = {
	"/sys/panel", "/sys/menubar", "/sys/dock", "/sys/home", "/sys/switcher", "/bin/korin-mcp",
	"/sys/svc/rdpd", "/sys/svc/vncd", "/bin/screenshot", "/bin/overlaytest", "/apps/screenshot.app/main",
	"/apps/taskman.app/main", 0};
static u8 ws_pex[296] __attribute__((aligned(8)));

#include "../../svc/authd/pwhash.h" /* SHA-256 */

/* l'image de ws_pex a-t-elle le contenu du fichier path ? Empreinte du fichier gardée par chemin ; relue si elle ne
 * correspond pas (fichier mis à jour), au plus une fois toutes les 10 s par chemin (une fausse image ne fait pas
 * relire le fichier à chaque appel) */
#define WS_HASHES 24
static struct {
	char path[48];
	u8 hash[32];
	u64 at;
	int have;
} ws_hashes[WS_HASHES];
static u8 ws_fbuf[4096];

static int ws_same_file(const char *path)
{
	int k = -1, freek = -1;
	for (int i = 0; i < WS_HASHES && k < 0; i++) {
		if (ws_hashes[i].path[0] && str_eq(ws_hashes[i].path, path))
			k = i;
		else if (!ws_hashes[i].path[0] && freek < 0)
			freek = i;
	}
	if (k < 0) {
		if (freek < 0 || str_len(path) >= (long)sizeof ws_hashes[0].path)
			return 0;
		k = freek;
		str_copy_n(ws_hashes[k].path, path, sizeof ws_hashes[k].path);
	}
	if (ws_hashes[k].have && !memcmp(ws_hashes[k].hash, ws_pex + 8, 32))
		return 1;
	u64 now = (u64)korin_sys0(SYS_TIME_MS);
	if (ws_hashes[k].at && now - ws_hashes[k].at < 10000)
		return 0;
	ws_hashes[k].at = now ? now : 1;
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	struct sha s;
	sha_init(&s);
	long n;
	while ((n = sys_read(fd, ws_fbuf, sizeof ws_fbuf)) > 0)
		sha_add(&s, ws_fbuf, (u64)n);
	sys_close(fd);
	sha_end(&s, ws_hashes[k].hash);
	ws_hashes[k].have = 1;
	return !memcmp(ws_hashes[k].hash, ws_pex + 8, 32);
}

static int ws_ieq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if ((*a | 0x20) != (*b | 0x20))
			return 0;
	return *a == *b;
}

int ws_trusted(void)
{
	long pid = M32(msg, WM_PID);
	long u = korin_sys1(SYS_PROC_UID, pid);
	if (u == 0 || u == E_NOSYS)
		return 1;
	long r = korin_sys2(SYS_PROC_EXE, pid, ws_pex);
	if (r == E_NOSYS)
		return 1;
	if (r < 0 || (*(u32 *)ws_pex & 5) != 1) /* (image tenue, pas de script déclaré) */
		return 0;
	ws_pex[40 + 127] = 0;
	const char *e = (const char *)ws_pex + 40, *c = e;
	while (*c && *c != ':' && c - e < 8)
		c++;
	if (*c == ':' && c[1] == '/') /* (un nom de volume devant) */
		e = c + 1;
	for (int i = 0; ws_trusted_exe[i]; i++)
		if (ws_ieq(e, ws_trusted_exe[i]))
			return ws_same_file(ws_trusted_exe[i]);
	return 0;
}

/* A5 : la carte d'élévation et l'écran de connexion, lancés en uid 0 par authd et init2 : la couche sûre. Le Pi
 * (pas d'identité des programmes) n'en a pas */
int ws_secure_caller(void)
{
	long pid = M32(msg, WM_PID);
	if (korin_sys1(SYS_PROC_UID, pid) != 0) /* (uid 0 seulement : une fausse image en uid 0 est déjà tout) */
		return 0;
	if (korin_sys2(SYS_PROC_EXE, pid, ws_pex) < 0 || (*(u32 *)ws_pex & 5) != 1)
		return 0;
	ws_pex[40 + 127] = 0;
	const char *e = (const char *)ws_pex + 40, *c = e;
	while (*c && *c != ':' && c - e < 8)
		c++;
	if (*c == ':' && c[1] == '/')
		e = c + 1;
	/* (et ce que login lance en uid 0 : ses réglages rapides, l'extinction, le Wi-Fi, le son ; en couche normale,
	 * ils passaient sous l'écran de connexion. Dans la session, ils tournent sous le compte : pas concernés) */
	return ws_ieq(e, "/sys/authcard") || ws_ieq(e, "/sys/login") || ws_ieq(e, "/sys/quickmenu") ||
	       ws_ieq(e, "/sys/shutdown") || ws_ieq(e, "/sys/wifimenu") || ws_ieq(e, "/sys/soundmenu");
}

/* G3 : les interfaces du système (panel, barre des menus, menus rapides...) ; /sys est protégé en écriture dans
 * tous les modes. Le Pi : 1 */
int ws_sysui_caller(void)
{
	if (ws_trusted())
		return 1;
	long pid = M32(msg, WM_PID);
	if (korin_sys2(SYS_PROC_EXE, pid, ws_pex) < 0 || (*(u32 *)ws_pex & 5) != 1)
		return 0;
	ws_pex[40 + 127] = 0;
	const char *e = (const char *)ws_pex + 40, *c = e;
	while (*c && *c != ':' && c - e < 8)
		c++;
	if (*c == ':' && c[1] == '/')
		e = c + 1;
	const char *pre = "/sys/", *full = e;
	while (*pre && (*e | 0x20) == *pre)
		e++, pre++;
	return !*pre && ws_same_file(full);
}

/* la fenêtre est-elle à l'appelant, ou l'appelant est-il de la liste ? */
static int ws_may(const struct win *w)
{
	return w->pid == M32(msg, WM_PID) || ws_trusted();
}

/* WS_OP_WIN_LIST : a première -> nombre de fenêtres ; msg a = id de la fenêtre qui reçoit le
 * clavier (0 : aucune) ; données : WE_PER_MSG entrées WE_* à partir de la première, dans l'ordre
 * d'affichage (couches fond, normale, dessus ; du fond vers l'avant), réduites comprises */
long req_win_list(void)
{
	struct win *kt = key_target(); /* fenêtre au clavier */
	u64 first = M64(msg, WM_A);
	long only = ws_trusted() ? -1 : (long)M32(msg, WM_PID); /* (hors liste : ses fenêtres seulement, G4) */
	u8 *e = msg + WM_DATA; /* entrée suivante */
	u64 n = 0;
	for (long layer = 0; layer < WIN_LAYERS; layer++)
		for (long i = 0; i < win_count; i++) {
			struct win *w = win_z[i];
			if (win_layer(w) != layer || (only >= 0 && (long)w->pid != only))
				continue;
			u64 k = n - first; /* dans la page demandée ? */
			n++;
			if (k >= WE_PER_MSG)
				continue;
			M32(e, WE_ID) = (u32)w->id;
			M32(e, WE_PID) = (u32)w->pid;
			M16(e, WE_X) = (u16)w->x;
			M16(e, WE_Y) = (u16)w->y;
			M16(e, WE_W) = (u16)w->w;
			M16(e, WE_H) = (u16)w->h;
			M32(e, WE_FLAGS) = (u32)w->flags;
			/* état : cachée, au clavier, réduite, son espace de travail */
			u64 st = (u64)(w->mini != 0) << 1;
			st += w == kt;
			st |= (w->mini & MINI_REDUCED) << 2;
			st |= w->desk << WES_DESK;
			M32(e, WE_STATE) = (u32)st;
			M32(e, WE_GEN) = w->gen;
			M32(e, WE_CGEN) = w->cgen;
			copy_title((char *)e + WE_TITLE, w->title, WE_SIZE - WE_TITLE);
			e += WE_SIZE;
		}
	M64(msg, WM_A) = kt && (only < 0 || (long)kt->pid == only) ? kt->id : 0;
	M32(msg, WM_LEN) = (u32)(e - (msg + WM_DATA)); /* longueur des données */
	return (long)n;
}

/* buf (pas : rw * 4), x, y = coin, rw, rh = taille du rectangle de son contenu : les boutons du
 * cadre par-dessus, comme à l'écran (draw_framed). Le client dessine le cadre sous le contenu :
 * sans eux, il ne les montrerait pas. Pixels du cadre tout ou rien */
static void win_read_caps(struct win *w, u32 *buf, long x, long y, long rw, long rh)
{
	if ((w->flags & WF_FRAMELESS) || !w->chr_a)
		return;
	long fp = (w->w + 2 * FRAME_BORDER) << 2; /* pas de l'image du cadre */
	/* le point (0, 0) du contenu */
	const u8 *base = (const u8 *)w->chr_a + FRAME_TITLE_H * fp + FRAME_BORDER * 4;
	long x1 = w->w - FRAME_CAP_MARGIN, x0 = x1 - FRAME_CAP_W; /* la zone, dans le rectangle */
	long y0 = FRAME_CAP_MARGIN, y1 = FRAME_CAP_MARGIN + FRAME_CAP_H;
	if (x0 < x)
		x0 = x;
	if (x1 > x + rw)
		x1 = x + rw;
	if (y0 < y)
		y0 = y;
	if (y1 > y + rh)
		y1 = y + rh;
	for (long yy = y0; yy < y1; yy++) {
		const u32 *row = (const u32 *)(base + yy * fp);
		for (long xx = x0; xx < x1; xx++) {
			u32 p = row[xx];
			if (p >> 24 >= 128) /* transparence */
				continue;
			buf[(yy - y) * rw + (xx - x)] = p & 0xFFFFFF;
		}
	}
}

/* rows lignes de n pixels, de src (pas sp octets) vers dst (pas dp octets) */
static void copy_rect(u8 *dst, long dp, const u8 *src, long sp, long n, long rows)
{
	for (; rows > 0; rows--) {
		const u32 *s = (const u32 *)src;
		u32 *d = (u32 *)dst;
		for (long i = 0; i < n; i++)
			d[i] = s[i];
		src += sp;
		dst += dp;
	}
}

/* WS_OP_WIN_READ : a id (WS_WIN_DESKTOP : le fond et les fenêtres de la couche du fond,
 * composés), b partie (0 contenu et boutons du cadre, 3 contenu seul, 1 cadre actif, 2 cadre
 * inactif : image l + 2 bords, h + titre + bord), c x | y << 32, d l | h << 32 -> 0 (< 0 :
 * erreur) ; le rectangle est recopié dans le tampon de l'appelant, ligne après ligne (pas :
 * l * 4) ; msg a = génération du contenu, b = du cadre, c = taille du tampon (octets), d = son
 * numéro (buf_map). l = 0 : rien de copié (le tampon et les générations). */
long req_win_read(void)
{
	long err;
	struct snap_cli *c = snap_client(M32(msg, WM_PID), &err);
	if (!c)
		return err;
	u64 id = M64(msg, WM_A);
	if (!ws_trusted()) { /* (hors liste : ses fenêtres seulement, pas le fond) */
		struct win *mine = win_by_id(id, (long)M32(msg, WM_PID));
		if (!mine)
			return E_PERM;
	}
	u64 part = M64(msg, WM_B);
	long nocaps = part == 3; /* 3 : sans les boutons */
	if (part == 3)
		part = 0;
	u64 cc = M64(msg, WM_C), dd = M64(msg, WM_D);
	u64 rx = (u32)cc, ry = cc >> 32, rw = (u32)dd, rh = dd >> 32;
	M64(msg, WM_D) = c->num; /* réponse : le tampon, sa taille */
	u64 size = (u64)(scr_w * scr_h) << 2;
	M64(msg, WM_C) = size;
	M64(msg, WM_A) = 0;
	M64(msg, WM_B) = 0;
	M32(msg, WM_LEN) = 0;
	if ((rw * rh) << 2 > size) /* le rectangle tient dans le tampon ? */
		return E_INVAL;
	if (id == WS_WIN_DESKTOP) {
		/* le fond : composé dans le tampon de l'appelant (le tampon arrière et le clip
		 * changés le temps du dessin) */
		if (!rw || !rh)
			return 0;
		if (part)
			return E_INVAL;
		if (rx + rw > (u64)scr_w || ry + rh > (u64)scr_h)
			return E_INVAL;
		u32 *keep_bb = bb; /* garder bb, bb_pitch, le clip */
		long keep_pitch = bb_pitch;
		struct rect keep_clip = clip_r;
		bb_pitch = (long)rw << 2; /* bb = tampon - (y * pas + x * 4) */
		bb = (u32 *)((u64)c->adr - ry * (u64)bb_pitch - (rx << 2));
		clip_r.x0 = (long)rx;
		clip_r.y0 = (long)ry;
		clip_r.x1 = (long)(rx + rw);
		clip_r.y1 = (long)(ry + rh);
		policy->back();
		for (long i = 0; i < win_count; i++) { /* les fenêtres du fond, dans l'ordre */
			struct win *w = win_z[i];
			if (w->mini || win_layer(w))
				continue;
			draw_window(w);
		}
		clip_r = keep_clip;
		bb = keep_bb;
		bb_pitch = keep_pitch;
		return 0;
	}
	struct win *w = win_by_id(id, -1);
	if (!w)
		return E_BADF;
	M64(msg, WM_A) = w->gen;
	M64(msg, WM_B) = w->cgen;
	if (!rw || !rh)
		return 0;
	u64 iw = (u64)w->w, ih = (u64)w->h; /* partie : image, largeur, hauteur */
	const u8 *img = (const u8 *)w->surf;
	if (part) {
		if (part > 2 || (w->flags & WF_FRAMELESS))
			return E_INVAL;
		iw += 2 * FRAME_BORDER;
		ih += FRAME_TITLE_H + FRAME_BORDER;
		img = (const u8 *)(part == 1 ? w->chr_a : w->chr_i);
	}
	if (rx + rw > iw || ry + rh > ih) /* dedans ? */
		return E_INVAL;
	copy_rect(c->adr, (long)rw << 2, img + ry * (iw << 2) + (rx << 2), (long)iw << 2, (long)rw,
		  (long)rh);
	if (part)
		return 0;
	/* contenu : sa surface attachée par-dessus, puis les boutons du cadre */
	surf_read_over(w, (u32 *)c->adr, (long)rx, (long)ry, (long)rw, (long)rh);
	if (!nocaps)
		win_read_caps(w, (u32 *)c->adr, (long)rx, (long)ry, (long)rw, (long)rh);
	return 0;
}

/* WS_OP_WIN_MOVE : a id, b x, c y (le contenu, coordonnées de l'écran) -> 0 ; fenêtres
 * normales seulement (ni fond ni dessus) ; la barre de titre reste sous la barre de menus,
 * comme en la tirant */
long req_win_move(void)
{
	struct win *w = win_by_id(M64(msg, WM_A), -1);
	if (!w)
		return E_BADF;
	if (!ws_may(w))
		return E_PERM;
	if (w->flags & (WF_BACK | WF_TOP))
		return E_INVAL;
	if (pk_owns(w)) /* (mode poche : place tenue par Elegant) */
		return E_INVAL;
	dirty_window(w);
	long x = MS64(msg, WM_B), y = MS64(msg, WM_C);
	if (!(w->flags & WF_FRAMELESS) && y < area_top + FRAME_TITLE_H)
		y = area_top + FRAME_TITLE_H;
	w->x = x;
	w->y = y;
	dirty_window(w);
	return 0;
}

/* WS_OP_WIN_CLOSE : a id (fenêtre de n'importe quel programme) -> 0 ; son programme reçoit
 * WEV_CLOSE, comme un clic sur la case de fermeture */
long req_win_close(void)
{
	struct win *w = win_by_id(M64(msg, WM_A), -1);
	if (!w)
		return E_BADF;
	if (!ws_may(w)) /* (la carte d'élévation d'un autre, par exemple : G5) */
		return E_PERM;
	for (int i = 0; i < 32; i++)
		evbuf[i] = 0;
	M32(evbuf, 0) = WEV_CLOSE;
	win_push(w, evbuf);
	return 0;
}
