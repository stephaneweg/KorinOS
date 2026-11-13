/* snapshot.c : l'écran composé pour vncd et screenshot (docs/RESEAU.md 5.4 ; snapshot.inc) :
 * op WS_OP_SNAPSHOT ; et les vignettes des fenêtres (WS_OP_WIN_THUMB, le commutateur du mode
 * poche).
 *
 * Chaque recomposition note sa zone et une génération (snap_hist, 32 dernières). Un appelant
 * (4 au plus) reçoit un tampon partagé de la taille de l'écran (créé au premier appel, accordé à
 * son pid) ; il donne la génération qu'il a déjà : on recopie du tampon arrière (tel qu'à
 * l'écran, curseur compris) l'union des zones changées depuis, et on rend la nouvelle
 * génération et ce rectangle. Génération 0 : tout l'écran. */
#include "elegant.h"

#define SNAP_HIST 32
#define SNAP_CLI 4

struct snap_rec { /* SN_* */
	u64 gen;
	long x0, y0, x1, y1;
};

static u64 snap_gen;
static struct snap_cli snap_cli[SNAP_CLI];
static struct snap_rec snap_hist[SNAP_HIST];

/* (compose, après present) union des zones recomposées (x0, y0, x1, y1), à l'écran : une
 * génération de plus */
void snap_record(long x0, long y0, long x1, long y1)
{
	u64 g = ++snap_gen;
	struct snap_rec *r = &snap_hist[g & (SNAP_HIST - 1)];
	r->gen = g;
	r->x0 = x0;
	r->y0 = y0;
	r->x1 = x1;
	r->y1 = y1;
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

/* WS_OP_SNAPSHOT : b = génération déjà vue -> 1 si copie (0 : rien de neuf) ; a largeur,
 * b hauteur, c génération, d numéro du tampon (buf_map) ; données : x0, y0, x1, y1 de la zone
 * recopiée (64 bits chacun) */
long req_snapshot(void)
{
	long err;
	struct snap_cli *c = snap_client(M32(msg, WM_PID), &err);
	if (!c)
		return err;
	/* écart voulu : l'asm lisait b (la génération vue) après y avoir écrit la hauteur de
	 * l'écran, et recopiait donc presque toujours tout l'écran ; on la lit d'abord */
	u64 seen = M64(msg, WM_B);
	MS64(msg, WM_A) = scr_w;
	MS64(msg, WM_B) = scr_h;
	M64(msg, WM_D) = c->num;
	u64 cur = snap_gen;
	M64(msg, WM_C) = cur;
	M32(msg, WM_LEN) = 32;
	long *z = (long *)(msg + WM_DATA);
	if (seen != cur) {
		/* zone : union des générations > vue (ou tout l'écran) */
		long x0 = 0, y0 = 0, x1 = scr_w, y1 = scr_h;
		if (seen && cur - seen < SNAP_HIST) { /* (trop ancienne : tout) */
			x0 = y0 = 0x7FFFFFFF; /* union vide */
			x1 = y1 = 0;
			for (u64 g = seen + 1; g <= cur; g++) {
				const struct snap_rec *r = &snap_hist[g & (SNAP_HIST - 1)];
				if (r->x0 < x0)
					x0 = r->x0;
				if (r->y0 < y0)
					y0 = r->y0;
				if (r->x1 > x1)
					x1 = r->x1;
				if (r->y1 > y1)
					y1 = r->y1;
			}
		}
		z[0] = x0;
		z[1] = y0;
		z[2] = x1;
		z[3] = y1;
		long w = x1 - x0, h = y1 - y0;
		if (w > 0 && h > 0) {
			/* source : tampon arrière ; destination : pas de largeur * 4 */
			long dp = scr_w << 2;
			copy_rect(c->adr + y0 * dp + (x0 << 2), dp,
				  (const u8 *)bb + y0 * bb_pitch + (x0 << 2), bb_pitch, w, h);
			return 1;
		}
	}
	z[0] = z[1] = z[2] = z[3] = 0;
	return 0;
}

/* WS_OP_WIN_THUMB : a id, b largeur | hauteur << 32, c décalage -> 0 ; la vignette (pas :
 * largeur * 4) au décalage c du tampon de l'appelant, son contenu au-delà de la vignette est
 * perdu (il sert de brouillon) ; msg c = taille du tampon, d = son numéro. Le contenu (et la
 * surface attachée, comme WIN_READ partie 3) est recopié au début du tampon, puis réduit sur
 * place par moyenne de chaque bloc : le commutateur du mode poche (src/sys/switcher). */
long req_win_thumb(void)
{
	long err;
	struct snap_cli *c = snap_client(M32(msg, WM_PID), &err);
	if (!c)
		return err;
	u64 b = M64(msg, WM_B);
	u64 tw = (u32)b, th = b >> 32; /* largeur, hauteur voulues */
	u64 off = M64(msg, WM_C);
	M64(msg, WM_D) = c->num; /* réponse : le tampon, sa taille */
	u64 size = (u64)(scr_w * scr_h) << 2;
	M64(msg, WM_C) = size;
	M32(msg, WM_LEN) = 0;
	struct win *w = win_by_id(M64(msg, WM_A), -1);
	if (!w)
		return E_BADF;
	u64 cw = (u64)w->w, ch = (u64)w->h; /* contenu borné à l'écran */
	if (cw > (u64)scr_w)
		cw = (u64)scr_w;
	if (ch > (u64)scr_h)
		ch = (u64)scr_h;
	if (!tw || !th || tw > 512 || th > 512) /* 1..512, pas plus que le contenu */
		return E_INVAL;
	if (tw > cw)
		tw = cw;
	if (th > ch)
		th = ch;
	if (off + ((tw * th) << 2) > size || (off & 3)) /* la vignette tient au décalage ? */
		return E_INVAL;
	/* contenu -> début du tampon (pas l * 4) */
	copy_rect(c->adr, (long)cw << 2, (const u8 *)w->surf, w->w << 2, (long)cw, (long)ch);
	surf_read_over(w, (u32 *)c->adr, 0, 0, (long)cw, (long)ch); /* sa surface par-dessus */
	/* réduction : chaque pixel de la vignette = moyenne de son bloc (sur place : un pixel
	 * écrit est toujours avant ce qui reste à lire) */
	u32 *px = (u32 *)c->adr;
	for (u64 ty = 0; ty < th; ty++) {
		u64 sy0 = ty * ch / th, sy1 = (ty + 1) * ch / th;
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (u64 tx = 0; tx < tw; tx++) {
			u64 sx0 = tx * cw / tw, sx1 = (tx + 1) * cw / tw;
			if (sx1 <= sx0)
				sx1 = sx0 + 1;
			u64 sr = 0, sg = 0, sb = 0; /* sommes : rouge, vert, bleu */
			for (u64 y = sy0; y < sy1; y++) {
				const u32 *p = px + y * cw + sx0;
				for (u64 x = 0; x < sx1 - sx0; x++) {
					u32 v = p[x];
					sr += (v >> 16) & 0xFF;
					sg += (v >> 8) & 0xFF;
					sb += v & 0xFF;
				}
			}
			u64 n = (sy1 - sy0) * (sx1 - sx0); /* pixels du bloc */
			px[ty * tw + tx] = (u32)((sr / n) << 16 | (sg / n) << 8 | sb / n);
		}
	}
	if (off) { /* au décalage demandé : recopiée depuis la fin (les deux se chevauchent) */
		u32 *d = (u32 *)(c->adr + off);
		for (u64 i = tw * th; i-- > 0;)
			d[i] = px[i];
	}
	return 0;
}

/* pid -> son entrée (tampon créé au premier appel) ; 0 et *err < 0 si erreur */
struct snap_cli *snap_client(u64 pid, long *err)
{
	struct snap_cli *free = 0; /* place libre */
	for (int i = 0; i < SNAP_CLI; i++) {
		struct snap_cli *c = &snap_cli[i];
		if (c->pid == pid)
			return c;
		if (!c->pid && !free)
			free = c;
	}
	if (!free) {
		*err = E_NOMEM;
		return 0;
	}
	long b = korin_sys1(SYS_BUF_CREATE, (scr_w * scr_h) << 2); /* de la taille de l'écran */
	if (b < 0) {
		*err = b;
		return 0;
	}
	long a = korin_sys1(SYS_BUF_MAP, b);
	if (a >= 0) {
		free->adr = (u8 *)a;
		long r = korin_sys2(SYS_BUF_GRANT, b, pid);
		if (r >= 0) {
			free->num = (u64)b;
			free->pid = pid;
			return free;
		}
		a = r;
	}
	korin_sys1(SYS_BUF_FREE, b);
	*err = a;
	return 0;
}

/* pid terminé : son tampon est rendu */
void snap_exit(long pid)
{
	for (int i = 0; i < SNAP_CLI; i++) {
		struct snap_cli *c = &snap_cli[i];
		if (c->pid == (u64)pid && c->pid) {
			c->pid = 0;
			korin_sys1(SYS_BUF_FREE, c->num);
		}
	}
}

/* l'écran a changé de taille : tampons rendus (refaits au prochain appel, à la nouvelle
 * taille, numéro dans la réponse), tout est à recopier */
void snap_reset(void)
{
	for (int i = 0; i < SNAP_CLI; i++) {
		struct snap_cli *c = &snap_cli[i];
		if (c->pid) {
			c->pid = 0;
			korin_sys1(SYS_BUF_FREE, c->num);
		}
	}
	snap_gen += SNAP_HIST; /* (les zones gardées sont périmées) */
}
