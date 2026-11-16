/* clipd - le presse-papiers partagé (clipd d'Onyx, docs/KITS.md), lancé par la session du bureau
 * (/etc/session). Sans fenêtre. Service « clipboard » (src/sys/common/clip.h) : les 10
 * dernières copies, la plus récente en tête, chacune avec ses représentations (un format, ses
 * octets) dans la mémoire de clipd, et le curseur, l'élément que Ctrl+V colle. L'historique est
 * perdu à l'arrêt. Une copie nouvelle passe devant, le curseur y va, notifyd le dit.
 *
 * Traduit ligne à ligne de clipd.asm. */
#include "clip.h"

#define MC_WRITE 1
#define CLIP_CHUNK 0x80000 /* par SVC_MAP_CALLER (1 Mo au plus) */
#define PAGE 4096

/* une représentation (CR_*) */
struct rep {
	char fmt[CLIP_FMT];
	u8 *ptr;
	u64 n;
	u64 map; /* octets de la région (pages entières) */
};

/* une copie (CE_*) */
struct entry {
	u64 id;
	u64 nr;
	u64 hm; /* heure * 256 + minute */
	u64 bytes;
	struct rep rep[CLIP_REPS];
};

static u64 g_n;          /* copies */
static u64 g_cursor;     /* id de l'élément du curseur (0 : aucun) */
static u64 g_next = 1;   /* id suivant */
static u64 g_serial = 1; /* change à chaque modification de l'anneau */
static long g_tell;      /* une notification à envoyer */
static char tellbuf[96];
static u8 msg[WS_MSG];
static struct entry g_fresh;
static struct entry g_e[CLIP_RING + 1];

static const char s_text[] = "text";
static const char s_image[] = "image";
static const char s_files[] = "files";
static const char s_filescut[] = "files-cut";
static const char s_url[] = "url";

#define MSGQ(off) (*(u64 *)(msg + (off)))

static void changed(void)
{
	g_serial++;
}

/* id -> rang, -1 s'il n'y est pas */
static long find_id(u64 id)
{
	for (u64 i = 0; i < g_n; i++)
		if (g_e[i].id == id)
			return (long)i;
	return -1;
}

/* copie : ses régions rendues */
static void free_entry(struct entry *e)
{
	u64 n = e->nr;
	e->nr = 0;
	for (u64 i = 0; i < n; i++)
		korin_sys2(SYS_VM_UNMAP, e->rep[i].ptr, e->rep[i].map);
}

/* rang : la copie s'en va ; le curseur passe à la suivante, plus ancienne */
static void remove_at(u64 r)
{
	struct entry *e = &g_e[r];
	int was_cursor = e->id == g_cursor;
	free_entry(e);
	g_n--;
	mem_move(e, e + 1, (g_n - r) * sizeof *e);
	if (!was_cursor)
		return;
	u64 id = 0;
	if (g_n)
		id = g_e[r <= g_n - 1 ? r : g_n - 1].id;
	g_cursor = id;
}

static void clear_all(void)
{
	while (g_n)
		remove_at(g_n - 1);
	g_cursor = 0;
}

/* -> octets de tout l'anneau */
static u64 ring_bytes(void)
{
	u64 t = 0;
	for (u64 i = 0; i < g_n; i++)
		t += g_e[i].bytes;
	return t;
}

/* requête, adresse chez l'appelant, adresse ici, octets, MC_WRITE (écrit chez lui) ou 0 (lu chez
 * lui) -> 0, < 0 : erreur */
static long xfer(long req, u64 there, u8 *here, u64 n, long mode)
{
	while (n) {
		u64 k = n < CLIP_CHUNK ? n : CLIP_CHUNK;
		long m = korin_sys4(SYS_SVC_MAP_CALLER, req, there, k, mode);
		if (m < 0)
			return m;
		if (mode)
			mem_move((void *)m, here, k);
		else /* lu : ici <- chez lui */
			mem_move(here, (void *)m, k);
		korin_sys1(SYS_SVC_UNMAP_CALLER, m);
		there += k;
		here += k;
		n -= k;
	}
	return 0;
}

/* CLIP_PUT -> id, < 0 : refus */
static long op_put(long req)
{
	u64 a = MSGQ(WM_A), b = MSGQ(WM_B), c = MSGQ(WM_C), d = MSGQ(WM_D);
	if (!b)
		return E_INVAL;
	u64 total = d ? b + 8 : b; /* octets, avec l'en-tête */
	if (total > CLIP_MAXITEM)
		return E_INVAL;
	struct entry *e = &g_fresh;
	if (c) { /* une représentation de plus */
		long r = find_id(c);
		if (r < 0)
			return E_NOENT;
		e = &g_e[r];
		if (e->nr >= CLIP_REPS)
			return E_INVAL;
	} else
		memset(e, 0, sizeof *e);
	u64 map = (total + PAGE - 1) & ~(u64)(PAGE - 1);
	long p = korin_sys4(SYS_VM_MAP, 0, map, PROT_READ | PROT_WRITE, 0);
	if (p < 0)
		return E_NOMEM;
	struct rep *rp = &e->rep[e->nr];
	rp->ptr = (u8 *)p;
	rp->n = total;
	rp->map = map;
	u8 *dst = (u8 *)p; /* le contenu, après l'en-tête */
	if (d) {
		*(u64 *)p = d;
		dst += 8;
	}
	long x = xfer(req, a, dst, b, 0);
	if (x < 0) {
		korin_sys2(SYS_VM_UNMAP, rp->ptr, rp->map);
		return x;
	}
	const char *f = (const char *)msg + WM_DATA; /* le format (borné) */
	for (u64 i = 0;;) {
		char ch = f[i];
		rp->fmt[i] = ch;
		if (!ch)
			break;
		if (++i >= CLIP_FMT - 1) {
			rp->fmt[i] = 0;
			break;
		}
	}
	e->nr++;
	e->bytes += total;
	if (c) {
		changed();
		return (long)c;
	}
	u64 id = g_next++; /* la copie passe devant */
	e->id = id;
	u64 m = (u64)sys_clock(2) % 86400 / 60; /* minutes du jour (secondes locales) */
	e->hm = (m % 60) | (m / 60) << 8;
	if (g_n >= CLIP_RING)
		remove_at(g_n - 1);
	mem_move(&g_e[1], &g_e[0], g_n * sizeof(struct entry));
	mem_move(&g_e[0], e, sizeof(struct entry));
	g_n++;
	while (g_n > 1 && ring_bytes() > CLIP_MAXBYTES)
		remove_at(g_n - 1);
	g_cursor = id;
	changed();
	g_tell = 1;
	return (long)id;
}

/* rang (msg : les formats demandés) -> représentation (0 : aucune), *k rang du format, *id */
static struct rep *try_item(u64 r, u64 *k, u64 *id)
{
	struct entry *e = &g_e[r];
	const char *f = (const char *)msg + WM_DATA; /* format demandé */
	for (u64 fi = 0; *f; fi++) {
		for (u64 i = 0; i < e->nr; i++)
			if (str_eq(e->rep[i].fmt, f)) {
				*k = fi;
				*id = e->id;
				return &e->rep[i];
			}
		f += str_len(f) + 1;
	}
	return 0;
}

/* CLIP_GET -> octets écrits */
static long op_get(long req)
{
	msg[WS_MSG - 1] = 0; /* (la fin de la liste) */
	msg[WS_MSG - 2] = 0;
	u64 k = 0, id = 0;
	struct rep *rp = 0;
	long cr = find_id(g_cursor); /* le curseur d'abord */
	if (cr >= 0)
		rp = try_item((u64)cr, &k, &id);
	for (u64 i = 0; !rp && i < g_n; i++) /* puis la plus récente qui convient */
		rp = try_item(i, &k, &id);
	if (!rp) {
		MSGQ(WM_A) = 0;
		return 0;
	}
	u64 dest = MSGQ(WM_A), room = MSGQ(WM_B), skip = MSGQ(WM_C);
	u64 len = rp->n;
	MSGQ(WM_A) = len;
	MSGQ(WM_B) = k;
	MSGQ(WM_C) = id;
	if (skip > len)
		skip = len;
	u8 *src = rp->ptr + skip; /* ce qu'il reste à partir de là */
	u64 n = len - skip;
	if (n > room)
		n = room;
	if (!dest || !n)
		return 0;
	long x = xfer(req, dest, src, n, MC_WRITE);
	if (x < 0)
		return x;
	return (long)n;
}

/* copie -> représentation principale : la première qui n'est pas du texte, sinon le texte
 * (comme l'asm : la première, même sans représentation) */
static struct rep *main_rep(struct entry *e)
{
	for (u64 i = 0; i < e->nr; i++)
		if (!str_eq(e->rep[i].fmt, s_text))
			return &e->rep[i];
	return &e->rep[0];
}

/* copie -> sa représentation texte, sinon la principale */
static struct rep *text_rep(struct entry *e)
{
	for (u64 i = 0; i < e->nr; i++)
		if (str_eq(e->rep[i].fmt, s_text))
			return &e->rep[i];
	return main_rep(e);
}

/* format -> 1 s'il commence par « files » */
static int is_files(const char *f)
{
	for (const char *p = s_files; *p; p++)
		if (*f++ != *p)
			return 0;
	return 1;
}

/* représentation, destination (CI_PREVIEW_MAX + 1) : une ligne de texte */
static void line_text(struct rep *rp, char *d)
{
	const u8 *s = rp->ptr;
	u64 n = rp->n;
	while (n && *s <= ' ') { /* (les blancs de tête : passés) */
		s++;
		n--;
	}
	u64 keep = n < CI_PREVIEW_MAX ? n : CI_PREVIEW_MAX; /* octets gardés */
	u64 i;
	for (i = 0; i < keep; i++) {
		u8 c = s[i];
		d[i] = (char)(c < ' ' ? ' ' : c);
	}
	if (n > keep) { /* coupé : pas au milieu d'un caractère (comme l'asm, un caractère
			   complet en fin de coupe s'en va aussi) */
		while (i) {
			u8 c = (u8)d[i - 1];
			if ((c & 0xC0) == 0x80) {
				i--;
				continue;
			}
			if (c >= 0xC0)
				i--;
			break;
		}
	}
	d[i] = 0;
}

/* représentation (chemins séparés par \n), destination : leurs noms séparés par « , »
 * -> nombre de chemins */
static u64 line_files(struct rep *rp, char *d)
{
	const u8 *s = rp->ptr;
	u64 n = rp->n, i = 0, k = 0, count = 0;
	while (i < n) {
		u64 b = i, e = i; /* début du nom, fin de ligne */
		while (e < n) {
			u8 c = s[e];
			if (c == 10)
				break;
			e++;
			if (c == '/' || c == ':')
				b = e;
		}
		if (e != i) { /* (ligne vide) */
			count++;
			if (k != 0) {
				if (k >= CI_PREVIEW_MAX - 2)
					goto next;
				d[k++] = ',';
				d[k++] = ' ';
			}
			while (b < e && k < CI_PREVIEW_MAX)
				d[k++] = (char)s[b++];
		}
	next:
		i = e + 1;
	}
	d[k] = 0;
	return count;
}

/* CLIP_INFO */
static long op_info(void)
{
	u64 r = MSGQ(WM_A);
	if (r >= g_n)
		return 0;
	struct entry *e = &g_e[r];
	MSGQ(WM_A) = e->id;
	MSGQ(WM_B) = e->id == g_cursor;
	MSGQ(WM_C) = e->hm;
	struct rep *rp = main_rep(e);
	MSGQ(WM_D) = rp->n;
	u8 *d = msg + WM_DATA;
	*(u64 *)(d + CI_W) = 0;
	*(u64 *)(d + CI_W + 8) = 0;
	d[CI_PREVIEW] = 0;
	mem_move(d, rp->fmt, CLIP_FMT); /* le genre */
	if (str_eq(rp->fmt, s_image)) {
		if (rp->n >= 8)
			*(u64 *)(d + CI_W) = *(u64 *)rp->ptr; /* (l, h) */
	} else if (is_files(rp->fmt))
		*(u32 *)(d + CI_NFILES) = (u32)line_files(rp, (char *)d + CI_PREVIEW);
	else
		line_text(text_rep(e), (char *)d + CI_PREVIEW);
	return 1;
}

/* CLIP_DROPCUT : l'élément du curseur s'en va si c'est une coupe */
static void drop_cut(void)
{
	long r = find_id(g_cursor);
	if (r < 0)
		return;
	struct entry *e = &g_e[r];
	for (u64 i = 0; i < e->nr; i++)
		if (str_eq(e->rep[i].fmt, s_filescut)) {
			remove_at((u64)r);
			changed();
			return;
		}
}

/* représentation -> nombre de chemins */
static u64 count_paths(struct rep *rp)
{
	u64 paths = 0, chars = 0;
	for (u64 i = 0; i < rp->n; i++) {
		if (rp->ptr[i] == 10) {
			if (chars) {
				paths++;
				chars = 0;
			}
		} else
			chars++;
	}
	if (chars)
		paths++;
	return paths;
}

/* la copie de tête : une notification */
static void tell(void)
{
	struct rep *rp = main_rep(&g_e[0]);
	if (str_eq(rp->fmt, s_image)) { /* « Image copiée (l x h) » */
		tellbuf[0] = 0;
		char *e = str_cat(tellbuf, lang_tr("Image copi\xC3\xA9" "e ("));
		e = fmt_dec(*(u32 *)rp->ptr, e);
		*e = 0;
		e = str_cat(e, " x ");
		e = fmt_dec(*(u32 *)(rp->ptr + 4), e);
		*e++ = ')';
		*e = 0;
	} else if (is_files(rp->fmt)) { /* « 3 éléments copiés » */
		u64 n = count_paths(rp);
		char *e = fmt_dec(n, tellbuf);
		*e = 0;
		int cut = (int)str_eq(rp->fmt, s_filescut);
		const char *w;
		if (n == 1)
			w = cut ? lang_tr(" \xC3\xA9l\xC3\xA9ment coup\xC3\xA9")
				: lang_tr(" \xC3\xA9l\xC3\xA9ment copi\xC3\xA9");
		else
			w = cut ? lang_tr(" \xC3\xA9l\xC3\xA9ments coup\xC3\xA9s")
				: lang_tr(" \xC3\xA9l\xC3\xA9ments copi\xC3\xA9s");
		str_cat(e, w);
	} else {
		const char *t = str_eq(rp->fmt, s_url) ? lang_tr("Lien copi\xC3\xA9")
						       : lang_tr("Texte copi\xC3\xA9");
		notify_send(lang_tr("Presse-papiers"), t, 0);
		return;
	}
	notify_send(lang_tr("Presse-papiers"), tellbuf, 0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long chan = korin_sys2(SYS_SVC_REGISTER, "clipboard", 0);
	if (chan < 0) /* (un autre clipd tourne) */
		return 0;
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, chan, msg, 60000);
		if (req < 0)
			return 0;
		if (!req)
			continue;
		long r = 0;
		long rank;
		switch (MSGQ(WM_OP)) {
		case CLIP_PUT:
			r = op_put(req);
			break;
		case CLIP_GET:
			r = op_get(req);
			break;
		case CLIP_INFO:
			r = op_info();
			break;
		case CLIP_STATE:
			MSGQ(WM_A) = g_n;
			r = (long)g_serial;
			break;
		case CLIP_CURSOR:
			if (find_id(MSGQ(WM_A)) >= 0) {
				g_cursor = MSGQ(WM_A);
				changed();
			}
			break;
		case CLIP_DELETE:
			rank = find_id(MSGQ(WM_A));
			if (rank >= 0) {
				remove_at((u64)rank);
				changed();
			}
			break;
		case CLIP_CLEAR:
			clear_all();
			changed();
			break;
		case CLIP_DROPCUT:
			drop_cut();
			break;
		default:
			r = E_INVAL;
		}
		MSGQ(WM_OP) = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, msg);
		if (g_tell) { /* (l'avis, l'appelant reparti) */
			g_tell = 0;
			tell();
		}
	}
}
