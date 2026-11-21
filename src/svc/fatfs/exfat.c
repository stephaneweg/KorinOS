/* exfat.c : système de fichiers exFAT (lecture, écriture, noms longs),
 * traduction de exfat.asm (réimplémentation d'après la spécification Microsoft
 * exFAT). Même interface que fat32.c (struct vol, de, it, fo).
 *
 * Le « cluster » qu'exFAT expose (DE_CLUSTER, FO_START, V_ROOT...) est une
 * POIGNÉE 64 bits :
 *    bits 0-31  premier cluster
 *    bits 32-61 nombre de clusters (allocation contiguë NoFatChain)
 *    bit 62     toujours 1 (poignée exFAT : fat_free_chain l'ignore)
 *    bit 63     NoFatChain : clusters contigus, la FAT n'est pas utilisée
 * Une poignée nulle = aucune allocation.
 * Hypothèses : bitmap d'allocation et table de majuscules contigus, une seule
 * FAT active (la première). */
#include "fatfs.h"

#define EX_HANDLE (1ULL << 62)
#define EX_NOFAT (1ULL << 63)
#define EX_EOC 0xFFFFFFFFu
#define EX_TS 0x5D5C6000u /* horodatage fixe : 28/10/2026 12:00 */
#define EX_MAX_SET 19      /* entrées max d'un jeu (1 + 1 + 17 noms) */

static u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u64 rd64(const u8 *p) { return rd32(p) | (u64)rd32(p + 4) << 32; }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void wr64(u8 *p, u64 v) { wr32(p, (u32)v); wr32(p + 4, (u32)(v >> 32)); }

static u64 clus_lba(struct vol *v, u64 c)
{
	u64 s;
	fat_clus_lba(v, c, &s, 0);
	return s;
}

static void ex_load_upcase(struct vol *v, u64 cluster, u64 size);

/* ---- montage */

long exfat_mount(struct vol *v, u64 first)
{
	v->pstart = first;
	u8 *b = bc_get(first);
	if (!b || memcmp(b + 3, "EXFAT   ", 8))
		return 0;
	if (b[108] != 9) /* octets par secteur : 2^9 seulement */
		return 0;
	u32 sh = b[109]; /* secteurs par cluster (log2) */
	if (sh > 16)
		return 0;
	v->spc = (u64)1 << sh;
	v->cshift = sh + 9;
	v->fat = rd32(b + 80) + first; /* FatOffset */
	v->fatsz = rd32(b + 84);
	v->nfats = 1;
	v->data = rd32(b + 88) + first; /* ClusterHeapOffset */
	v->nclust = rd32(b + 92);
	u32 root = rd32(b + 96);
	if (root < 2)
		return 0;
	v->root = root | EX_HANDLE;
	v->hint = 2;
	v->bitmap = 0;
	v->upcase = 0;
	/* bitmap (0x81) et majuscules (0x82) dans la racine */
	for (u64 i = 0;; i++) {
		u64 sec;
		u8 *e = ex_entry_at(v, v->root, i, &sec);
		if (!e || !e[0])
			break;
		if (e[0] == 0x81) {
			v->bitmap = rd32(e + 20);
			v->bitmap_len = rd64(e + 24);
			v->ex_bmlba = clus_lba(v, v->bitmap);
		} else if (e[0] == 0x82) {
			ex_load_upcase(v, rd32(e + 20), rd64(e + 24));
		}
	}
	if (!v->bitmap) /* pas de bitmap : volume inutilisable */
		return 0;
	v->type = 2;
	return 1;
}

/* table compressée (0xFFFF, n : n caractères identité) dans 128 Ko ; jamais rendue */
static void ex_load_upcase(struct vol *v, u64 cluster, u64 size)
{
	u16 *t = ak_malloc(32 << 12); /* 65536 x 2 octets */
	if (!t) /* sans table : majuscules ASCII seulement */
		return;
	for (u32 i = 0; i < 0x10000; i++)
		t[i] = (u16)i;
	u64 s0 = clus_lba(v, cluster); /* (table contiguë) */
	u64 off = 0, ch = 0;
#define WORD(o) ({ u8 *wb_ = bc_get(s0 + ((o) >> 9)); wb_ ? rd16(wb_ + ((o) & 511)) : 0xFFFF; })
	while (off < size) {
		u32 w = WORD(off);
		if (w == 0xFFFF) { /* 0xFFFF, n : saut de n caractères */
			off += 2;
			if (off >= size)
				break;
			ch += WORD(off);
			off += 2;
			continue;
		}
		if (ch >= 0x10000)
			break;
		t[ch++] = (u16)w;
		off += 2;
	}
#undef WORD
	v->upcase = t;
}

static u32 ex_upcase(u32 cp, const u16 *t)
{
	if (cp >= 0x10000)
		return cp;
	if (t)
		return t[cp];
	return cp >= 'a' && cp <= 'z' ? cp - 32 : cp;
}

/* ---- entrées de répertoire */

/* poignée du répertoire, index -> l'entrée de 32 o (cache), *sector ; 0 si hors
 * de l'allocation du répertoire */
u8 *ex_entry_at(struct vol *v, u64 dir, u64 index, u64 *sector)
{
	u64 sh = v->cshift - 5; /* log2(entrées par cluster) */
	u64 rank = index >> sh;
	u64 idx = index & (((u64)1 << sh) - 1);
	u64 c = (u32)dir; /* premier cluster */
	if (c < 2)
		return 0;
	if (dir & EX_NOFAT) { /* contigu : rang < nombre de clusters */
		if (rank >= ((dir >> 32) & 0x3FFFFFFF))
			return 0;
		c += rank;
	} else {
		for (; rank; rank--) {
			c = fat_next(v, c);
			if (c >= FAT_EOC)
				return 0;
		}
	}
	u64 sec = clus_lba(v, c) + (idx >> 4); /* 16 entrées par secteur */
	u8 *b = bc_get(sec);
	if (!b)
		return 0;
	*sector = sec;
	return b + (idx & 15) * 32;
}

/* poignée du répertoire, index de l'entrée 0x85, tampon (EX_MAX_SET x 32 o)
 * -> nombre d'entrées du jeu (0 si invalide) */
static long ex_read_set(struct vol *v, u64 dir, u64 index, u8 *set)
{
	u64 want = 1;
	for (u64 n = 0; n < want; n++) {
		u64 sec;
		u8 *e = ex_entry_at(v, dir, index + n, &sec);
		if (!e)
			return 0;
		memcpy(set + n * 32, e, 32);
		if (!n) {
			if ((e[0] & 0x7F) != 0x05) /* entrée fichier (en service ou non) */
				return 0;
			want = (u64)e[1] + 1;
			if (want > EX_MAX_SET || want < 3)
				return 0;
		}
	}
	return want;
}

static void ex_set_checksum(u8 *set, u64 n)
{
	u32 s = 0;
	for (u64 i = 0; i < n * 32; i++) {
		if (i == 2 || i == 3) /* le checksum lui-même */
			continue;
		s = ((s >> 1) & 0x7FFF) | ((s & 1) ? 0x8000 : 0);
		s = (s + set[i]) & 0xFFFF;
	}
	wr16(set + 2, s);
}

/* recalcule le checksum et écrit les entrées -> 1 si succès */
static long ex_write_set(struct vol *v, u64 dir, u64 index, u8 *set, u64 n)
{
	ex_set_checksum(set, n);
	for (u64 i = 0; i < n; i++) {
		u64 sec;
		u8 *e = ex_entry_at(v, dir, index + i, &sec);
		if (!e)
			return 0;
		memcpy(e, set + i * 32, 32);
		bc_write(sec);
	}
	return 1;
}

/* premier cluster, drapeaux du flux (bit 1 NoFatChain), DataLength -> poignée (0 : aucun cluster) */
static u64 ex_make_handle(struct vol *v, u32 first, u32 flags, u64 len)
{
	if (first < 2)
		return 0;
	u64 h = first | EX_HANDLE;
	if (flags & 2) { /* contigu : nombre de clusters de l'allocation */
		u64 n = ((len + ((u64)1 << v->cshift) - 1) >> v->cshift) & 0x3FFFFFFF;
		h |= n << 32 | EX_NOFAT;
	}
	return h;
}

long exfat_dir_read(struct it *it, struct de *de)
{
	struct vol *v = it->vol;
	u8 set[EX_MAX_SET * 32];
	u8 n16[(EX_MAX_SET * 15 + 1) * 2];
	de->h.dir = it->start;
	for (;;) {
		u64 idx = it->index, sec;
		u8 *e = ex_entry_at(v, it->start, idx, &sec);
		if (!e || !e[0]) /* fin de répertoire */
			return 0;
		it->index = idx + 1;
		if (e[0] != 0x85)
			continue;
		long n = ex_read_set(v, it->start, idx, set); /* jeu complet */
		if (!n)
			continue;
		it->index = idx + n;
		if (set[32] != 0xC0) /* flux 0xC0 obligatoire */
			continue;
		de->h.index = idx;
		de->h.first = idx;
		de->h.attr = rd16(set + 4);
		de->h.fsize = rd64(set + 32 + 24); /* DataLength */
		de->mtime = rd32(set + 12); /* dernière écriture */
		de->h.cluster = ex_make_handle(v, rd32(set + 32 + 20), set[32 + 1], de->h.fsize);
		/* nom : entrées 0xC1 consécutives, 15 caractères chacune */
		u64 len = set[32 + 3], k = 0;
		for (long x = 2; x < n && set[x * 32] == 0xC1 && k < len; x++)
			for (int j = 0; j < 15 && k < len; j++, k++)
				memcpy(n16 + 2 * k, set + x * 32 + 2 + 2 * j, 2);
		n16[2 * k] = n16[2 * k + 1] = 0;
		utf16_to_utf8(de->name, n16, 255);
		return 1;
	}
}

/* chaîne UTF-8 -> point de code (« / » rendu comme 0), *next la suite ;
 * comme l'asm : un octet de tête de 4 octets se lit comme s'il en avait 3 */
static u32 ex_u8next(const u8 **pp)
{
	const u8 *p = *pp;
	u32 c = *p++;
	if (c == '/')
		c = 0;
	if (c & 0x80) {
		if ((c & 0xE0) == 0xC0) {
			c = (c & 0x1F) << 6 | (*p++ & 0x3F);
		} else {
			c &= 0x0F;
			c = c << 6 | (*p++ & 0x3F);
			c = c << 6 | (*p++ & 0x3F);
		}
	}
	*pp = p;
	return c;
}

/* nom 1 (fin 0 ou « / »), nom 2 -> 1 si égaux (casse : table de majuscules) */
static long ex_name_equal(struct vol *v, const char *a, const char *b)
{
	const u8 *x = (const u8 *)a, *y = (const u8 *)b;
	for (;;) {
		u32 c1 = ex_upcase(ex_u8next(&x), v->upcase);
		u32 c2 = ex_upcase(ex_u8next(&y), v->upcase);
		if (c1 != c2)
			return 0;
		if (!c1)
			return 1;
	}
}

long exfat_lookup(struct vol *v, const char *p, struct de *de)
{
	u64 cur = v->root;
	de->h.cluster = cur; /* racine synthétique */
	de->h.fsize = 0;
	de->mtime = 0;
	de->h.attr = ATTR_DIR;
	de->h.dir = 0;
	de->h.index = de->h.first = 0; /* (l'asm les laissait tels quels) */
	de->name[0] = 0;
	for (;;) {
		while (*p == '/')
			p++;
		if (!*p)
			return 1;
		if (!(de->h.attr & ATTR_DIR))
			return 0;
		struct it it; /* parcours du répertoire cur */
		dir_open(&it, v, cur);
		for (;;) {
			if (!exfat_dir_read(&it, de))
				return 0;
			if (ex_name_equal(v, p, de->name))
				break;
		}
		cur = de->h.cluster;
		for (;;) {
			char c = *p++;
			if (!c)
				return 1;
			if (c == '/')
				break;
		}
	}
}

/* ---- allocation : bitmap + FAT */

static void ex_bm_locate(struct vol *v, u64 c, u64 *sec, u64 *byte, u32 *bit)
{
	c -= 2;
	*bit = c & 7;
	u64 o = c >> 3;
	*sec = v->ex_bmlba + (o >> 9);
	*byte = o & 511;
}

static long ex_bm_get(struct vol *v, u64 c)
{
	u64 sec, o;
	u32 bit;
	ex_bm_locate(v, c, &sec, &o, &bit);
	u8 *b = bc_get(sec);
	if (!b) /* erreur de lecture : prudence */
		return 1;
	return (b[o] >> bit) & 1;
}

static void ex_bm_set(struct vol *v, u64 c, long val)
{
	u64 sec, o;
	u32 bit;
	ex_bm_locate(v, c, &sec, &o, &bit);
	u8 *b = bc_get(sec);
	if (!b)
		return;
	b[o] = (u8)((b[o] & ~(1u << bit)) | (val ? 1u << bit : 0));
	bc_write(sec);
}

static void ex_fat_set(struct vol *v, u64 c, u32 val)
{
	u64 sec = v->fat + (c >> 7);
	u8 *b = bc_get(sec);
	if (!b)
		return;
	wr32(b + (c & 127) * 4, val);
	bc_write(sec);
}

static void ex_zero_cluster(struct vol *v, u64 c)
{
	blk_zero(clus_lba(v, c), v->spc);
}

/* -> cluster libre, marqué alloué et mis à zéro sauf whole (0 si plein) */
static u64 ex_alloc_cluster_x(struct vol *v, long whole)
{
	u64 c = v->hint, end = v->nclust + 2;
	for (u64 tries = end - 2;; c++) {
		if (!tries)
			return 0;
		tries--;
		if (c >= end)
			c = 2;
		if (!ex_bm_get(v, c))
			break;
	}
	ex_bm_set(v, c, 1);
	if (!whole)
		ex_zero_cluster(v, c);
	v->hint = c + 1;
	return c;
}

static u64 ex_alloc_cluster(struct vol *v)
{
	return ex_alloc_cluster_x(v, 0);
}

/* libère toute l'allocation (bitmap et FAT écrits une fois par secteur, à la fin) */
void ex_free_alloc(struct vol *v, u64 h, u64 len)
{
	bc_defer_on();
	u64 c = (u32)h; /* premier cluster */
	if (c >= 2) {
		if (h & EX_NOFAT) { /* contigu */
			u64 n = (h >> 32) & 0x3FFFFFFF;
			if (!n) /* nombre inconnu : d'après la taille */
				n = (len + ((u64)1 << v->cshift) - 1) >> v->cshift;
			for (; n; n--, c++)
				ex_bm_set(v, c, 0);
		} else {
			for (u64 guard = v->nclust + 2; c >= 2 && c < FAT_EOC && guard; guard--) {
				u64 next = fat_next(v, c);
				ex_bm_set(v, c, 0);
				ex_fat_set(v, c, 0);
				c = next;
			}
		}
	}
	bc_defer_off();
}

/* ---- fichiers */

/* rang de cluster, alloc, whole -> cluster (0 si hors fichier / disque plein) ;
 * met à jour FO_START (poignée) et FO_DIRTY quand l'allocation change */
static u64 ex_cluster_at(struct fo *f, u64 rank, int alloc, long whole)
{
	struct vol *v = f->it.vol;
	u64 h = FO_START(f);
	if ((u32)h < 2) {
		if (!alloc)
			return 0;
		u64 c = ex_alloc_cluster_x(v, rank == 0 ? whole : 0); /* fichier vide : premier cluster, contigu */
		if (!c)
			return 0;
		h = c | EX_HANDLE | EX_NOFAT | (1ULL << 32);
		FO_START(f) = h;
		FO_CCLUS(f) = 0;
		f->dirty = 1;
	}
	if (h & EX_NOFAT) { /* allocation contiguë (NoFatChain) */
		for (;;) {
			u64 n = (h >> 32) & 0x3FFFFFFF, c0 = (u32)h;
			if (rank < n)
				return c0 + rank;
			if (!alloc)
				return 0;
			u64 nx = c0 + n; /* le cluster suivant est-il libre ? */
			if (nx >= v->nclust + 2 || ex_bm_get(v, nx))
				break; /* plus de place contiguë : chaîne FAT */
			ex_bm_set(v, nx, 1);
			if (rank != n || !whole)
				ex_zero_cluster(v, nx);
			h += 1ULL << 32; /* un cluster de plus */
			FO_START(f) = h;
			f->dirty = 1;
		}
		u64 n = (h >> 32) & 0x3FFFFFFF, c0 = (u32)h, i = 0;
		for (; i + 1 < n; i++)
			ex_fat_set(v, c0 + i, (u32)(c0 + i + 1));
		ex_fat_set(v, c0 + i, EX_EOC);
		h = (u32)h | EX_HANDLE; /* poignée chaînée : cluster | EX_HANDLE */
		FO_START(f) = h;
		FO_CCLUS(f) = 0;
		f->dirty = 1;
	}
	/* chaîne FAT */
	u64 c = FO_CCLUS(f), i = FO_CIDX(f);
	if (!c || i > rank) {
		c = (u32)h;
		i = 0;
	}
	for (; i < rank; i++) {
		u64 n = fat_next(v, c);
		if (n >= FAT_EOC) {
			if (!alloc) /* fin de chaîne en lecture */
				return 0;
			n = ex_alloc_cluster_x(v, i + 1 == rank ? whole : 0);
			if (!n)
				return 0;
			ex_fat_set(v, n, EX_EOC); /* nouveau = fin de chaîne */
			ex_fat_set(v, c, (u32)n); /* précédent -> nouveau */
		}
		c = n;
	}
	FO_CCLUS(f) = c;
	FO_CIDX(f) = i;
	return c;
}

long exfat_rw(struct fo *f, u8 *buf, u64 len, int write)
{
	struct vol *v = f->it.vol;
	u64 total = 0;
	if (!write) {
		u64 avail = f->size > f->pos ? f->size - f->pos : 0;
		if (len > avail)
			len = avail;
	}
	while (len) {
		u64 pos = f->pos;
		u64 rank = pos >> v->cshift;
		u64 csize = (u64)1 << v->cshift;
		long whole = write && len >= csize && !(pos & (csize - 1));
		u64 c = ex_cluster_at(f, rank, write, whole);
		if (!c) {
			/* disque plein : E_NOSPC si rien n'a été écrit, sinon une écriture courte (la suivante
			   rend E_NOSPC) ; jamais 0, que write() prendrait pour un succès */
			if (write && !total)
				return E_NOSPC;
			break;
		}
		u64 lba, cs;
		fat_clus_lba(v, c, &lba, &cs);
		u64 mask = ((u64)1 << cs) - 1;
		u64 off = pos & mask;
		u64 sec = lba + (off >> 9);
		u64 soff = pos & 511;
		u64 done;
		if (!soff && len >= 512) {
			u64 left = (mask - off + 1) >> 9; /* secteurs entiers jusqu'à la fin du cluster */
			u64 n = len >> 9 < left ? len >> 9 : left;
			while (n < len >> 9) { /* les clusters contigus avec (une seule commande) */
				u64 nrank = (pos + (left << 9)) >> v->cshift; /* le suivant */
				long al = 0; /* alloué s'il sera écrit en entier */
				if (write)
					al = (len >> 9) - left >= v->spc;
				u64 nx = ex_cluster_at(f, nrank, al, al);
				if (!nx)
					break;
				if (clus_lba(v, nx) != sec + left)
					break;
				left += v->spc;
				n = len >> 9 < left ? len >> 9 : left;
			}
			long ok = write ? blk_write(sec, n, buf) : blk_read(sec, n, buf);
			if (!ok)
				break;
			done = n << 9;
		} else {
			u8 *b = bc_get(sec);
			if (!b)
				break;
			done = 512 - soff;
			if (done > len)
				done = len;
			if (write) {
				memcpy(b + soff, buf, done);
				bc_write(sec);
			} else {
				memcpy(buf, b + soff, done);
			}
		}
		buf += done;
		len -= done;
		total += done;
		f->pos += done;
		if (f->pos > f->size) {
			f->size = f->pos;
			f->dirty = 1;
		}
	}
	return total;
}

/* écrit premier cluster (poignée), taille, attributs, date ; si l'allocation a
 * changé (troncature), l'ancienne est libérée */
void exfat_update_entry(struct vol *v, struct dehead *de)
{
	u8 set[EX_MAX_SET * 32];
	long n = ex_read_set(v, de->dir, de->index, set);
	if (!n)
		return;
	u32 old = rd32(set + 32 + 20); /* ancienne allocation */
	u64 h = de->cluster;
	if (old != (u32)h && old) {
		u64 dl = rd64(set + 32 + 24);
		ex_free_alloc(v, ex_make_handle(v, old, set[32 + 1], dl), dl);
	}
	set[32 + 1] = (u8)(1 | (h & EX_NOFAT ? 2 : 0)); /* AllocationPossible, NoFatChain */
	wr32(set + 32 + 20, (u32)h);
	if (!(de->attr & ATTR_DIR) || !rd64(set + 32 + 24)) { /* répertoire : taille = allocation */
		wr64(set + 32 + 8, de->fsize); /* (sauf une entrée neuve : rename) ValidDataLength */
		wr64(set + 32 + 24, de->fsize); /* DataLength */
	}
	wr16(set + 4, (u32)de->attr);
	u32 t = (u32)dos_maintenant();
	wr32(set + 12, t); /* dernière écriture, dernier accès */
	wr32(set + 16, t);
	set[21] = 0;
	set[23] = 0;
	ex_write_set(v, de->dir, de->index, set, n);
}

/* free = 1 : libérer aussi l'allocation */
static void ex_erase(struct vol *v, struct dehead *de, int free)
{
	u8 set[EX_MAX_SET * 32];
	long n = ex_read_set(v, de->dir, de->index, set);
	if (!n)
		return;
	if (free) {
		u64 dl = rd64(set + 32 + 24);
		ex_free_alloc(v, ex_make_handle(v, rd32(set + 32 + 20), set[32 + 1], dl), dl);
	}
	for (long i = 0; i < n; i++) { /* InUse = 0 sur chaque entrée */
		u64 sec;
		u8 *e = ex_entry_at(v, de->dir, de->index + i, &sec);
		if (!e)
			return;
		e[0] &= 0x7F;
		bc_write(sec);
	}
}

void exfat_erase(struct vol *v, struct dehead *de)
{
	ex_erase(v, de, 1);
}

/* l'entrée seulement (fichier encore ouvert : son allocation est libérée à sa
 * dernière fermeture) */
void exfat_erase_entries(struct vol *v, struct dehead *de)
{
	ex_erase(v, de, 0);
}

static u32 hash_byte(u32 h, u32 b)
{
	h = ((h >> 1) & 0x7FFF) | ((h & 1) ? 0x8000 : 0); /* ror16, puis + octet */
	return (h + b) & 0xFFFF;
}

/* poignée du parent, nom UTF-8 (fin 0 ou « / »), attributs, DE -> 1 si créé
 * (fichier vide, ou répertoire d'un cluster) */
long exfat_create(struct vol *v, u64 parent, const char *name, u64 attr, struct de *de)
{
	u8 set[EX_MAX_SET * 32];
	u16 n16[264];
	char *o = de->name; /* composant -> DE_NAME */
	for (u64 room = NAME_BYTES - 2;;) {
		char c = *name++;
		if (c == '/')
			c = 0;
		*o++ = c;
		if (!c)
			break;
		if (!--room)
			return 0; /* nom trop long */
	}
	if (!de->name[0])
		return 0;
	u64 units = utf8_to_utf16(de->name, n16, NAME_UNITS + 1);
	if (units > NAME_UNITS) /* 255 caractères au plus */
		return 0;
	u64 nn = (units + 14) / 15; /* entrées de nom */
	u64 total = nn + 2;         /* taille du jeu */
	memset(set, 0, sizeof set);
	set[0] = 0x85;
	set[1] = (u8)(nn + 1);
	wr16(set + 4, (u32)attr);
	wr32(set + 8, EX_TS);
	wr32(set + 12, EX_TS);
	wr32(set + 16, EX_TS);
	set[32] = 0xC0;
	set[33] = 1; /* AllocationPossible */
	set[35] = (u8)units;
	for (u64 i = 0; i < nn; i++) {
		u8 *e = set + (i + 2) * 32;
		e[0] = 0xC1;
		for (u64 j = 0; j < 15 && i * 15 + j < units; j++)
			wr16(e + 2 + 2 * j, n16[i * 15 + j]);
	}
	u32 hash = 0; /* NameHash sur les caractères en majuscules */
	for (u64 k = 0; k < units; k++) {
		u32 u = ex_upcase(n16[k], v->upcase);
		hash = hash_byte(hash, u & 0xFF);
		hash = hash_byte(hash, (u >> 8) & 0xFF);
	}
	wr16(set + 36, hash);
	u64 h = 0; /* poignée du nouvel élément */
	if (attr & ATTR_DIR) { /* répertoire : un cluster vide, chaîné */
		u64 c = ex_alloc_cluster(v);
		if (!c)
			return 0;
		ex_fat_set(v, c, EX_EOC);
		wr32(set + 32 + 20, (u32)c);
		u64 cs = (u64)1 << v->cshift;
		wr64(set + 32 + 8, cs);
		wr64(set + 32 + 24, cs);
		h = c | EX_HANDLE;
	}
	/* place libre : total entrées consécutives */
	u64 idx = 0, run = 0, start = 0;
	for (;;) {
		u64 sec;
		u8 *e = ex_entry_at(v, parent, idx, &sec);
		if (!e) {
			/* seul un répertoire chaîné sans taille (la racine) peut grandir ici */
			if (v->root != parent)
				goto fail_free;
			u64 last = (u32)parent; /* dernier cluster de la chaîne */
			for (;;) {
				u64 n = fat_next(v, last);
				if (n >= FAT_EOC)
					break;
				last = n;
			}
			u64 nc = ex_alloc_cluster(v);
			if (!nc)
				goto fail_free;
			ex_fat_set(v, nc, EX_EOC);
			ex_fat_set(v, last, (u32)nc); /* précédent -> nouveau */
			continue;
		}
		if (e[0] & 0x80) {
			run = 0;
			idx++;
			continue;
		}
		if (!run)
			start = idx;
		if (++run >= total)
			break;
		idx++;
	}
	u32 t = (u32)dos_maintenant(); /* création, dernière écriture, dernier accès */
	wr32(set + 8, t);
	wr32(set + 12, t);
	wr32(set + 16, t);
	if (!ex_write_set(v, parent, start, set, total))
		goto fail_free;
	de->h.dir = parent;
	de->h.index = start;
	de->h.first = start;
	de->h.cluster = h;
	de->h.fsize = rd64(set + 32 + 24);
	de->h.attr = rd16(set + 4);
	return 1;
fail_free:
	if (h)
		ex_free_alloc(v, h, 0);
	return 0;
}
