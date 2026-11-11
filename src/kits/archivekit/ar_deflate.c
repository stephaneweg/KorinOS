/* ar_deflate.c - la compression deflate (RFC 1951) d'ArchiveKit (d'après
 * ar_deflate.inc : mêmes choix, donc mêmes octets).
 *
 * Un flux : df_begin, df_feed (autant de fois qu'il faut), df_finish ; ce qui
 * sort va au puits. La mémoire entre dans une fenêtre de 32 Ko d'historique +
 * DF_CHUNK ; quand elle est pleine, df_run la comprime, puis df_slide ramène
 * l'historique au début.
 *
 * Recherche des correspondances : chaînes de hachage (3 octets), à positions
 * absolues sur 32 bits (base + décalage) : rien à remettre à zéro entre deux
 * entrées d'une archive ni après un glissement, une position trop vieille se
 * voit à sa distance, et un retour à zéro du compteur est sans danger (le
 * candidat est vérifié sur les octets). Paresseuse à partir du niveau 4,
 * gourmande en dessous, mêmes réglages que zlib (good, lazy, nice, chain).
 *
 * Blocs : au plus DF_TOKENS jetons (ou une fenêtre) ; pour chacun on calcule le
 * coût en Huffman dynamique, en codes fixes et en bloc stocké, et on prend le
 * moins cher. Longueurs de codes : Moffat-Katajainen (Huffman minimal, sur
 * place), puis limite à 15 (ou 7) bits par correction de l'inégalité de Kraft. */
#include "ar.h"

#define DF_LOOK 262      /* ce qu'il faut devant soi pour chercher (hors de la fin) */
#define DF_MAXDIST 32767 /* (prev est un anneau de 32768 entrées) */

const u8 df_clorder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* good, lazy, nice, chain (par niveau ; 0 : stocké) */
static const u16 df_config[10][4] = {
	{0, 0, 0, 0},       {4, 4, 8, 4},       {4, 5, 16, 8},     {4, 6, 32, 32},     {4, 4, 16, 16},
	{8, 16, 32, 32},    {8, 16, 128, 128},  {8, 32, 128, 256}, {32, 128, 258, 1024}, {32, 258, 258, 4096},
};

static void df_codes(const u8 *lens, u64 n, u32 *table);
static void df_block(struct defl *s, u64 end, u64 final);

/* les tables des codes fixes */
static void df_fixed(struct defl *s)
{
	for (u64 i = 0; i < 288; i++)
		s->llen[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
	df_codes(s->llen, 288, s->fltab);
	for (u64 i = 0; i < 32; i++)
		s->dlen[i] = 5;
	df_codes(s->dlen, 32, s->fdtab);
}

/* puits -> état (0 : plus de mémoire) */
struct defl *df_new(struct sink *k)
{
	struct defl *s = ar_alloc(sizeof(struct defl));
	if (!s)
		return 0;
	s->sink = k;
	s->base = 1;
	df_fixed(s);
	return s;
}

/* un nouveau flux (l'état peut resservir) */
void df_begin(struct defl *s, u64 level, u64 wrap)
{
	s->base += s->end + 1; /* (les vieilles positions tombent hors de portée) */
	s->level = level;
	s->wrap = wrap;
	s->good = df_config[level][0];
	s->lazy = df_config[level][1];
	s->nice = df_config[level][2];
	s->chainn = df_config[level][3];
	s->pos = 0;
	s->end = 0;
	s->blk = 0;
	s->floor = 0;
	s->ntok = 0;
	s->bb = 0;
	s->bc = 0;
	s->op = 0;
	s->crc = 0;
	s->adler = 1;
	s->total = 0;
	s->err = 0;
}

/* --- sortie ------------------------------------------------------------------- */

/* passe le tampon de sortie au puits (une erreur est gardée, la suite est jetée) */
static void df_oflush(struct defl *s)
{
	u64 n = s->op;
	if (!n)
		return;
	s->op = 0;
	if (s->err)
		return;
	s->err = (u64)sink_write(s->sink, s->obuf, n);
}

/* les octets entiers du tampon de bits vers la sortie */
static void df_obytes(struct defl *s)
{
	if (s->op >= DF_OSIZE)
		df_oflush(s);
	u64 op = s->op, bb = s->bb, bc = s->bc;
	while (bc >= 8) {
		s->obuf[op++] = (u8)bb;
		bb >>= 8;
		bc -= 8;
	}
	s->op = op;
	s->bb = bb;
	s->bc = bc;
}

/* valeur, bits (<= 32) */
static void df_bits(struct defl *s, u64 v, u64 n)
{
	s->bb |= v << (s->bc & 63);
	s->bc += n;
	df_obytes(s);
}

/* complète l'octet en cours */
static void df_align(struct defl *s)
{
	s->bc = (s->bc + 7) & ~(u64)7;
	df_obytes(s);
}

/* début, fin (décalages), 1 : dernier : des blocs stockés */
static void df_stored(struct defl *s, u64 a, u64 e, u64 final)
{
	do {
		u64 n = e - a;
		if (n > 0xFFFF)
			n = 0xFFFF;
		df_bits(s, (a + n == e) & final, 3); /* (type 00) */
		df_align(s);
		df_bits(s, n | (u64)(~(u32)n & 0xFFFF) << 16, 32);
		df_oflush(s); /* le tampon est libre */
		if (!s->err)
			s->err = (u64)sink_write(s->sink, s->win + a, n);
		a += n;
	} while (a < e);
}

/* --- arbres de Huffman ------------------------------------------------------------ */

/* fréquences (u32), symboles, longueur maximale, longueurs (u8) */
static void df_tree(struct defl *s, const u32 *freq, u64 nsym, u64 maxl, u8 *lens)
{
	u64 i = 0;
	do
		lens[i] = 0;
	while (++i < nsym);
	u64 *srt = s->sort;
	u64 m = 0; /* symboles utiles */
	i = 0;
	do {
		u64 f = freq[i];
		if (f) {
			u64 key = f << 16 | i;
			u64 j = m; /* insertion dans la liste triée */
			while (j) {
				u64 p = srt[j - 1];
				if (p <= key)
					break;
				srt[j] = p;
				j--;
			}
			srt[j] = key;
			m++;
		}
	} while (++i < nsym);
	if (!m)
		return;
	if (m == 1) {
		lens[srt[0] & 0xFFFF] = 1;
		return;
	}
	/* Moffat-Katajainen : A[0..m[ trié -> A[i] = longueur du code i */
	u32 *A = s->work;
	i = 0;
	do
		A[i] = (u32)(srt[i] >> 16);
	while (++i < m);
	A[0] += A[1];
	u64 root = 0, leaf = 2, next = 1;
	while (next < m - 1) {
		/* premier fils */
		if (leaf >= m || A[root] < A[leaf]) {
			A[next] = A[root];
			A[root] = (u32)next;
			root++;
		} else {
			A[next] = A[leaf];
			leaf++;
		}
		/* second fils */
		if (leaf >= m || (root < next && A[root] < A[leaf])) {
			A[next] += A[root];
			A[root] = (u32)next;
			root++;
		} else {
			A[next] += A[leaf];
			leaf++;
		}
		next++;
	}
	u64 k = m - 2;
	A[k] = 0;
	/* profondeur des noeuds */
	while (k) {
		k--;
		A[k] = A[A[k]] + 1;
	}
	{
		u64 avbl = 1, used = 0, dpth = 0;
		long r = (long)m - 2;
		u64 nx = m - 1;
		while (avbl) {
			while (r >= 0 && A[r] == dpth) {
				used++;
				r--;
			}
			while (avbl > used) {
				A[nx] = (u32)dpth;
				nx--;
				avbl--;
			}
			avbl = used << 1;
			dpth++;
			used = 0;
		}
	}
	/* histogramme, plafonné à la longueur maximale */
	u32 *cnt = s->cnt;
	for (i = 0; i < 34; i++)
		cnt[i] = 0;
	i = 0;
	do {
		u64 d = A[i];
		if (d > maxl)
			d = maxl;
		cnt[d]++;
	} while (++i < m);
	/* Kraft : somme de cnt[i] << (max - i) */
	u64 kr = 0;
	for (i = 1; i <= maxl; i++)
		kr += (u64)cnt[i] << (maxl - i);
	u64 lim = (u64)1 << maxl;
	while (kr > lim) {
		cnt[maxl]--;
		for (u64 j = maxl - 1; j; j--)
			if (cnt[j]) {
				cnt[j]--;
				cnt[j + 1] += 2;
				break;
			}
		kr--;
	}
	/* les moins fréquents reçoivent les codes les plus longs */
	u64 at = 0;
	for (u64 l = maxl; l; l--)
		for (u32 c = cnt[l]; c; c--)
			lens[srt[at++] & 0xFFFF] = (u8)l;
}

/* longueurs (u8), symboles, table (u32 : code inversé | longueur << 16) */
static void df_codes(const u8 *lens, u64 n, u32 *table)
{
	u16 count[16] = {0}, next[16] = {0};
	u64 i = 0;
	do
		count[lens[i]]++;
	while (++i < n);
	count[0] = 0;
	u32 code = 0;
	for (i = 1; i < 16; i++) {
		code = (code + count[i - 1]) << 1;
		next[i] = (u16)code;
	}
	i = 0;
	do {
		u32 l = lens[i], v = 0;
		if (l) {
			u32 c = next[l];
			next[l] = (u16)(c + 1);
			v = ar_rev32(c) >> (32 - l) | l << 16;
		}
		table[i] = v;
	} while (++i < n);
}

/* somme des fréquences par les longueurs */
static u64 df_cost(const u32 *freq, const u8 *lens, u64 n)
{
	u64 c = 0;
	for (u64 i = 0; i < n; i++)
		c += (u64)freq[i] * lens[i];
	return c;
}

/* symbole et bits supplémentaires d'une longueur : v = longueur - 3 -> symbole -
 * 257, bits en plus, leur valeur */
static inline void df_lensym(u32 v, u32 *sym, u32 *eb, u32 *ev)
{
	*eb = 0;
	*ev = 0;
	*sym = v;
	if (v < 8)
		return;
	if (v == 255) {
		*sym = 28;
		return;
	}
	u32 n = 31 - (u32)__builtin_clz(v);
	u32 x = n - 2;
	*sym = ((n - 1) << 2) + ((v >> x) & 3);
	*eb = x;
	*ev = v & ((1u << x) - 1);
}

/* symbole d'une distance -> symbole, bits en plus, valeur */
static inline void df_distsym(u32 d, u32 *sym, u32 *eb, u32 *ev)
{
	u32 w = d - 1;
	*eb = 0;
	*ev = 0;
	*sym = w;
	if (w < 4)
		return;
	u32 n = 31 - (u32)__builtin_clz(w);
	u32 x = n - 1;
	*sym = ((w >> x) & 1) + (n << 1);
	*eb = x;
	*ev = w & ((1u << x) - 1);
}

/* hlit, hdist : les longueurs mises bout à bout, codées par les symboles 16,
 * 17, 18 -> s->rle, s->bfreq ; jetons, bits en plus */
static u64 df_rle(struct defl *s, u64 hlit, u64 hdist, u64 *extra)
{
	u8 *q = s->seq;
	u64 n = 0, i;
	do
		q[n] = s->llen[n];
	while (++n < hlit);
	i = 0;
	do
		q[n++] = s->dlen[i];
	while (++i < hdist);
	u16 *o = s->rle;
	u32 *bf = s->bfreq;
	u64 nt = 0, eb = 0;
	i = 0;
	while (i < n) {
		u32 v = q[i];
		u64 j = i;
		do
			j++;
		while (j < n && q[j] == v);
		u64 run = j - i; /* longueur de la série */
		i += run;
		if (!v) {
			while (run >= 11) {
				u64 c = run > 138 ? 138 : run;
				o[nt++] = (u16)(((c - 11) << 8) + 18);
				bf[18]++;
				eb += 7;
				run -= c;
			}
			if (run >= 3) {
				o[nt++] = (u16)(((run - 3) << 8) + 17);
				bf[17]++;
				eb += 3;
				continue;
			}
		} else {
			/* un de la longueur, puis des répétitions */
			o[nt++] = (u16)v;
			bf[v]++;
			run--;
			while (run >= 3) {
				u64 c = run > 6 ? 6 : run;
				o[nt++] = (u16)(((c - 3) << 8) + 16);
				bf[16]++;
				eb += 2;
				run -= c;
			}
		}
		while (run) {
			o[nt++] = (u16)v;
			bf[v]++;
			run--;
		}
	}
	*extra = eb;
	return nt;
}

/* codes de littéraux et longueurs, codes de distance : les jetons, puis la fin
 * de bloc */
static void df_tokens(struct defl *s, const u32 *lt, const u32 *dt)
{
	const u32 *t = s->tok, *te = s->tok + s->ntok;
	u64 bb = s->bb, bc = s->bc;
	u8 *ob = s->obuf, *lim = s->obuf + DF_OSIZE, *o = s->obuf + s->op;
	while (t < te) {
		u32 w = *t++;
		u32 dist = w >> 8, v = w & 0xFF;
		if (!dist) {
			u32 c = lt[v];
			bb |= (u64)(c & 0xFFFF) << bc;
			bc += c >> 16;
		} else {
			u32 sym, eb, ev;
			df_lensym(v, &sym, &eb, &ev);
			u32 c = lt[sym + 257];
			bb |= (u64)(c & 0xFFFF) << bc;
			bc += c >> 16;
			if (eb) {
				bb |= (u64)ev << bc;
				bc += eb;
			}
			df_distsym(dist, &sym, &eb, &ev);
			c = dt[sym];
			bb |= (u64)(c & 0xFFFF) << bc;
			bc += c >> 16;
			if (eb) {
				bb |= (u64)ev << bc;
				bc += eb;
			}
		}
		__builtin_memcpy(o, &bb, 8);
		u64 nb = bc >> 3;
		o += nb;
		bc -= nb << 3;
		bb >>= nb << 3;
		if (o >= lim) {
			s->op = (u64)(o - ob);
			s->bb = bb;
			s->bc = bc;
			df_oflush(s);
			o = ob + s->op;
			bb = s->bb;
			bc = s->bc;
		}
	}
	u32 c = lt[256];
	bb |= (u64)(c & 0xFFFF) << bc;
	bc += c >> 16;
	s->op = (u64)(o - ob);
	s->bb = bb;
	s->bc = bc;
	df_obytes(s);
}

/* fin du bloc (décalage), 1 : dernier ; s->ntok jetons -> écrit le bloc,
 * s->blk = fin du bloc */
static void df_block(struct defl *s, u64 end, u64 final)
{
	s->bend = end;
	/* fréquences */
	memset(s->lfreq, 0, sizeof s->lfreq);
	memset(s->dfreq, 0, sizeof s->dfreq);
	memset(s->bfreq, 0, sizeof s->bfreq);
	s->lfreq[256] = 1;
	u64 extra = 0; /* bits en plus */
	for (u64 i = 0; i < s->ntok; i++) {
		u32 w = s->tok[i];
		u32 dist = w >> 8, v = w & 0xFF;
		if (!dist) {
			s->lfreq[v]++;
			continue;
		}
		u32 sym, eb, ev;
		df_lensym(v, &sym, &eb, &ev);
		extra += eb;
		s->lfreq[sym + 257]++;
		df_distsym(dist, &sym, &eb, &ev);
		extra += eb;
		s->dfreq[sym]++;
	}
	/* arbres */
	df_tree(s, s->lfreq, 286, 15, s->llen);
	df_tree(s, s->dfreq, 30, 15, s->dlen);
	/* HLIT, HDIST */
	u64 hlit = 286;
	while (hlit > 257 && !s->llen[hlit - 1])
		hlit--;
	u64 hdist = 30;
	while (hdist > 1 && !s->dlen[hdist - 1])
		hdist--;
	u64 rx;
	u64 nrle = df_rle(s, hlit, hdist, &rx);
	df_tree(s, s->bfreq, 19, 7, s->blen);
	/* HCLEN */
	u64 hclen = 19;
	while (hclen > 4 && !s->blen[df_clorder[hclen - 1]])
		hclen--;
	/* coût dynamique */
	u64 dyn = hclen * 3 + 14 + extra + rx;
	dyn += df_cost(s->bfreq, s->blen, 19);
	dyn += df_cost(s->lfreq, s->llen, 286);
	dyn += df_cost(s->dfreq, s->dlen, 30);
	/* coût fixe (les longueurs fixes sont dans la table) */
	u64 fix = extra;
	for (u64 i = 0; i < 286; i++)
		fix += (u64)s->lfreq[i] * (s->fltab[i] >> 16);
	for (u64 i = 0; i < 30; i++)
		fix += (u64)s->dfreq[i] * 5;
	/* coût stocké (en bits) : octets + 5 par tranche de 65535 */
	u64 bytes = end - s->blk;
	u64 sto = ((bytes / 0xFFFF + 1) * 5 + bytes) << 3;
	u64 best = dyn < fix ? dyn : fix;
	if (sto <= best) {
		df_stored(s, s->blk, end, final);
	} else if (dyn >= fix) {
		df_bits(s, 1 << 1 | final, 3);
		df_tokens(s, s->fltab, s->fdtab);
	} else {
		df_bits(s, 2 << 1 | final, 3);
		df_bits(s, (hlit - 257) | (hdist - 1) << 5 | (hclen - 4) << 10, 14);
		for (u64 i = 0; i < hclen; i++)
			df_bits(s, s->blen[df_clorder[i]], 3);
		/* les longueurs, en jetons RLE */
		df_codes(s->blen, 19, s->btab);
		for (u64 i = 0; i < nrle; i++) {
			u32 r = s->rle[i];
			u32 sym = r & 0xFF;
			u32 c = s->btab[sym];
			df_bits(s, c & 0xFFFF, c >> 16);
			if (sym >= 16)
				df_bits(s, r >> 8, sym == 16 ? 2 : sym == 17 ? 3 : 7);
		}
		df_codes(s->llen, 286, s->ltab);
		df_codes(s->dlen, 30, s->dtab);
		df_tokens(s, s->ltab, s->dtab);
	}
	s->blk = s->bend;
	s->ntok = 0;
}

/* --- recherche ------------------------------------------------------------------- */

/* insère la position -> candidat précédent (absolu) */
static inline u32 df_insert(struct defl *s, u32 base, u64 pos)
{
	const u8 *p = s->win + pos;
	u32 v = (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16;
	u32 h = (v * 0x1E35A7BDu) >> (32 - DF_HBITS);
	u32 a = base + (u32)pos;
	u32 old = s->head[h];
	s->head[h] = a;
	s->prev[a & 0x7FFF] = old;
	return old;
}

/* candidat, meilleure longueur connue, essais -> longueur, *dist (0 : rien de
 * mieux). Lit jusqu'à 8 octets au-delà de la fin (la fenêtre a sa marge),
 * coupe ensuite. */
static u64 df_match(struct defl *s, u32 cand, u64 best, u64 chain, u64 pos, u32 base, u64 *distp)
{
	u64 maxl = s->end - pos; /* octets devant soi */
	if (maxl > 258)
		maxl = 258;
	u64 dist = 0;
	if (best < maxl) {
		u64 maxd = pos - s->floor;
		if (maxd > DF_MAXDIST)
			maxd = DF_MAXDIST;
		u64 nice = s->nice;
		if (nice > maxl)
			nice = maxl;
		const u8 *here = s->win + pos;
		u32 apos = base + (u32)pos;
		for (;;) {
			u32 d = apos - cand;
			if ((u64)(u32)(d - 1) >= maxd)
				break; /* (0 ou trop loin) */
			const u8 *c = here - d;
			/* les deux octets qui feraient mieux */
			if (here[best - 1] == c[best - 1] && here[best] == c[best]) {
				u64 l = 0;
				for (;;) {
					u64 x, y;
					__builtin_memcpy(&x, here + l, 8);
					__builtin_memcpy(&y, c + l, 8);
					x ^= y;
					if (x) {
						l += (u64)__builtin_ctzll(x) >> 3;
						if (l > maxl)
							l = maxl;
						break;
					}
					l += 8;
					if (l >= maxl) {
						l = maxl;
						break;
					}
				}
				if (l > best) {
					best = l;
					dist = d;
					if (best >= nice)
						break;
				}
			}
			if (--chain == 0)
				break;
			cand = s->prev[cand & 0x7FFF];
		}
	}
	*distp = dist;
	return best;
}

/* 1 : c'est la fin du flux -> 0, erreur. Comprime jusqu'à s->end (moins
 * DF_LOOK si le flux continue) ; ferme le bloc en cours. */
static long df_run(struct defl *s, u64 final)
{
	s->final = final;
	const u8 *win = s->win;
	u64 pos = s->pos;
	u64 lim = s->end;
	if (!final)
		lim = lim < DF_LOOK ? 0 : lim - DF_LOOK;
	if (!s->level) {
		s->pos = lim;
		if (s->blk != lim || final) {
			df_stored(s, s->blk, lim, final);
			s->blk = s->pos;
		}
		return (long)s->err;
	}
	u32 *tok = s->tok;
	u64 nt = 0;
	u32 base = (u32)s->base;
	u64 plen = 2;  /* longueur précédente */
	u64 pdist = 0; /* distance précédente */
	u64 l, d, e, m, at;
	u32 cand;

/* bloc plein : le fermer ; tout ce qui précède at est dans les jetons */
#define TOKAT(x)                                       \
	do {                                           \
		if (nt >= DF_TOKENS) {                 \
			s->ntok = nt;                  \
			df_block(s, (x), 0);           \
			nt = 0;                        \
		}                                      \
	} while (0)

	if (s->level < 4)
		goto greedy;

	/* --- paresseuse : une correspondance attend de voir si la suivante fait mieux */
none: /* aucun octet en attente */
	if (pos >= lim)
		goto close;
	if (pos + 2 >= s->end) {
		plen = 2;
		pos++;
		goto wait;
	}
	cand = df_insert(s, base, pos);
	plen = df_match(s, cand, 2, s->chainn, pos, base, &pdist);
	if (plen == 3 && pdist > 4096)
		plen = 2;
	pos++;
wait: /* l'octet pos-1 attend */
	if (pos >= lim)
		goto lastlit;
	if (pos + 2 >= s->end) {
		l = 2;
		d = 0;
		goto w3;
	}
	cand = df_insert(s, base, pos);
	if (plen >= s->lazy) {
		l = 2;
		d = 0;
		goto w3;
	}
	{
		u64 ch = s->chainn;
		if (plen >= s->good)
			ch >>= 2;
		l = df_match(s, cand, plen, ch, pos, base, &d);
	}
	if (!d || (l == 3 && d > 4096)) {
		l = 2;
		d = 0;
	}
w3:
	if (plen < 3 || l > plen) {
		/* un littéral part, l'octet courant attend */
		plen = l;
		pdist = d;
		tok[nt++] = win[pos - 1];
		pos++;
		at = pos - 1; /* (consommé : jusqu'à l'octet en attente) */
		TOKAT(at);
		goto wait;
	}
	/* la correspondance précédente est gardée : elle commence en pos-1 */
	tok[nt++] = (u32)((plen - 3) | pdist << 8);
	e = pos - 1 + plen; /* fin de la correspondance */
	m = s->end < 2 ? 0 : s->end - 2; /* dernier + 1 qu'on peut hacher */
	if (e < m)
		m = e;
	pos++;
	while (pos < m) {
		df_insert(s, base, pos);
		pos++;
	}
	pos = e;
	plen = 2;
	TOKAT(pos);
	goto none;
lastlit:
	tok[nt++] = win[pos - 1];
	goto close;

	/* --- gourmande (niveaux 1 à 3) */
greedy:
	if (pos >= lim)
		goto close;
	if (pos + 2 >= s->end)
		goto glit;
	cand = df_insert(s, base, pos);
	l = df_match(s, cand, 2, s->chainn, pos, base, &d);
	if (!d || (l == 3 && d > 4096))
		goto glit;
	tok[nt++] = (u32)((l - 3) | d << 8);
	e = pos + l; /* fin */
	if (l <= s->lazy) { /* (longueur maximale insérée) */
		m = s->end < 2 ? 0 : s->end - 2;
		if (e < m)
			m = e;
		pos++;
		while (pos < m) {
			df_insert(s, base, pos);
			pos++;
		}
	}
	pos = e;
	TOKAT(pos);
	goto greedy;
glit:
	tok[nt++] = win[pos];
	pos++;
	TOKAT(pos);
	goto greedy;

#undef TOKAT
	/* fin de la tranche : le bloc en cours se ferme */
close:
	s->pos = pos;
	s->ntok = nt;
	if (final || nt)
		df_block(s, pos, final);
	return (long)s->err;
}

/* garde l'historique, ramène le reste au début (tout ce qui précède s->pos est
 * déjà dans un bloc fermé) */
static void df_slide(struct defl *s)
{
	if (s->pos <= DF_HISTORY)
		return;
	u64 sh = s->pos - DF_HISTORY; /* de combien on glisse */
	s->end -= sh;
	s->pos -= sh;
	s->blk = s->pos;
	s->base += sh;
	s->floor = s->floor < sh ? 0 : s->floor - sh;
	ar_memcpy(s->win, s->win + sh, s->end);
}

/* données, octets -> 0, erreur */
long df_feed(struct defl *s, const u8 *data, u64 n)
{
	if (!n)
		return 0;
	s->total += n;
	s->crc = crc32_upd((u32)s->crc, data, n);
	if (s->wrap == AR_ZLIB)
		s->adler = adler32_upd((u32)s->adler, data, n);
	while (n) {
		u64 c = DF_WINCAP - s->end;
		if (c > n)
			c = n;
		ar_memcpy(s->win + s->end, data, c);
		data += c;
		n -= c;
		s->end += c;
		if (s->end < DF_WINCAP)
			continue;
		long r = df_run(s, 0);
		if (r)
			return r;
		df_slide(s);
	}
	return 0;
}

/* -> 0, erreur : tout est comprimé et passé au puits, dernier bloc marqué ; le
 * flux s'arrête sur un octet entier */
long df_finish(struct defl *s)
{
	long r = df_run(s, 1);
	if (r)
		return r;
	df_align(s);
	df_oflush(s);
	return (long)s->err;
}
