/* ext4w.c : écrire sur un volume ext4 (ext4w.h). Les structures et les sommes de contrôle : la documentation du noyau
 * Linux (« ext4 Data Structures and Algorithms », metadata_csum) ; les mêmes calculs que mkext4.c, jugés par e2fsck.
 *
 * L'arbre des extents d'un fichier est lu en entier dans un tableau (ex[]), changé dans le tableau, puis réécrit
 * (ext_save) en reprenant ses blocs d'arbre : profondeur 0 (4 extents dans l'inode) ou 1 (une racine de 4 index,
 * des feuilles de (bs - 12) / 12 extents). L'allocation prend une plage contiguë dans un groupe, à partir d'un but
 * (le bloc qui suit le précédent du fichier). */
#include "ext4w.h"

void *memcpy(void *d, const void *s, __SIZE_TYPE__ n);
void *memset(void *d, int c, __SIZE_TYPE__ n);
uint32_t ext4_crc32c(uint32_t crc, const void *p, uint64_t n); /* mkext4.c */

#define MAXBS 65536
#define EXT_MAX 1400
#define TREE_MAX 8

#define IN_FILETYPE 0x2u
#define IN_RECOVER 0x4u
#define IN_EXTENTS 0x40u
#define IN_64BIT 0x80u
#define IN_FLEX_BG 0x200u
#define IN_EA_INODE 0x400u
#define IN_CSUM_SEED 0x2000u
#define IN_LARGEDIR 0x4000u
#define IN_INLINE 0x8000u
#define IN_OK (IN_FILETYPE | IN_EXTENTS | IN_64BIT | IN_FLEX_BG | IN_EA_INODE | IN_CSUM_SEED | IN_LARGEDIR | IN_INLINE)
#define RO_SPARSE 0x1u
#define RO_LARGE_FILE 0x2u
#define RO_HUGE_FILE 0x8u
#define RO_GDT_CSUM 0x10u
#define RO_DIR_NLINK 0x20u
#define RO_EXTRA_ISIZE 0x40u
#define RO_CSUM 0x400u
#define RO_OK (RO_SPARSE | RO_LARGE_FILE | RO_HUGE_FILE | RO_GDT_CSUM | RO_DIR_NLINK | RO_EXTRA_ISIZE | RO_CSUM)
#define CO_SPARSE2 0x200u

#define BG_INODE_UNINIT 0x1u
#define BG_BLOCK_UNINIT 0x2u

#define FL_INDEX 0x1000u
#define FL_EXTENTS 0x80000u
#define FL_INLINE 0x10000000u

static struct ext4r *fs;
static ext4w_wr wr;
static uint32_t now, seed, gen_next, first_ino, rsv_gdt, compat, backup_bgs[2];
static uint64_t gdt_blocks, itable_blocks, free_inodes;
static int csum, state_dirty, ok;
static uint8_t db[MAXBS];   /* un bloc de travail (données, dossiers, feuilles) */
static uint8_t bm[MAXBS];   /* une bitmap */
static uint8_t gb[MAXBS];   /* le bloc des descripteurs, d'un inode, du superbloc */
static uint8_t ib[MAXBS];   /* (ext_load) une copie d'un bloc d'index */

static uint32_t r16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return r16(p) | r16(p + 2) << 16; }
static void w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v); w16(p + 2, v >> 16); }

static long put(uint64_t b, const void *buf) { return wr(fs->ctx, b, buf); }

static long get(uint64_t b, uint8_t *dst)
{
	const uint8_t *p = fs->rd(fs->ctx, b);
	if (!p)
		return -5;
	memcpy(dst, p, fs->bs);
	return 0;
}

static uint32_t crc_u32(uint32_t c, uint32_t v)
{
	uint8_t b[4];
	w32(b, v);
	return ext4_crc32c(c, b, 4);
}

static uint32_t iseed(uint32_t ino, uint32_t gen) { return crc_u32(crc_u32(seed, ino), gen); }

/* ---- superbloc */

static long sb_write(int clean)
{
	uint64_t blk = fs->bs == 1024 ? 1 : 0, off = fs->bs == 1024 ? 0 : 1024;
	long r = get(blk, gb);
	if (r)
		return r;
	uint8_t *s = gb + off;
	w32(s + 0x0C, (uint32_t)fs->free_blocks);
	if (fs->incompat & IN_64BIT)
		w32(s + 0x158, (uint32_t)(fs->free_blocks >> 32));
	w32(s + 0x10, (uint32_t)free_inodes);
	uint32_t st = r16(s + 0x3A);
	w16(s + 0x3A, clean ? st | 1 : st & ~1u);
	w32(s + 0x30, now);
	if (csum)
		w32(s + 0x3FC, ext4_crc32c(~0u, s, 0x3FC));
	return put(blk, gb);
}

/* la première écriture de la session : le volume « pas propre » jusqu'à ext4w_sync */
static long begin(void)
{
	if (!ok)
		return -30;
	if (state_dirty)
		return 0;
	state_dirty = 1;
	return sb_write(0);
}

/* ---- groupes */

static uint64_t gstart(uint64_t g) { return fs->first_data + g * fs->bpg; }
static uint64_t glen(uint64_t g)
{
	uint64_t n = fs->blocks - gstart(g);
	return n > fs->bpg ? fs->bpg : n;
}

static uint64_t gd_blk(uint64_t g, uint32_t *off)
{
	uint64_t o = g * fs->descsz;
	*off = (uint32_t)(o % fs->bs);
	return fs->first_data + 1 + o / fs->bs;
}

static long gd_get(uint64_t g, uint8_t *d)
{
	uint32_t off;
	const uint8_t *p = fs->rd(fs->ctx, gd_blk(g, &off));
	if (!p)
		return -5;
	memcpy(d, p + off, fs->descsz);
	return 0;
}

static long gd_put(uint64_t g, uint8_t *d)
{
	if (csum) {
		w16(d + 0x1E, 0);
		uint32_t c = crc_u32(seed, (uint32_t)g);
		w16(d + 0x1E, ext4_crc32c(c, d, fs->descsz) & 0xFFFF);
	}
	uint32_t off;
	uint64_t b = gd_blk(g, &off);
	long r = get(b, gb);
	if (r)
		return r;
	memcpy(gb + off, d, fs->descsz);
	return put(b, gb);
}

static int big(void) { return fs->descsz >= 64; }
static uint64_t gd_v32(const uint8_t *d, int lo, int hi) { return r32(d + lo) | (big() ? (uint64_t)r32(d + hi) << 32 : 0); }
static uint32_t gd_v16(const uint8_t *d, int lo, int hi) { return r16(d + lo) | (big() ? r16(d + hi) << 16 : 0); }
static void gd_s16(uint8_t *d, int lo, int hi, uint32_t v)
{
	w16(d + lo, v & 0xFFFF);
	if (big())
		w16(d + hi, v >> 16);
}
#define GD_BB(d) gd_v32(d, 0x00, 0x20)
#define GD_IB(d) gd_v32(d, 0x04, 0x24)
#define GD_IT(d) gd_v32(d, 0x08, 0x28)
#define GD_FREEB(d) gd_v16(d, 0x0C, 0x2C)
#define GD_FREEI(d) gd_v16(d, 0x0E, 0x2E)
#define GD_DIRS(d) gd_v16(d, 0x10, 0x30)
#define GD_UNUSED(d) gd_v16(d, 0x1C, 0x32)

static int has_backup(uint64_t g)
{
	if (g == 0)
		return 1;
	if (compat & CO_SPARSE2)
		return g == backup_bgs[0] || g == backup_bgs[1];
	if (!(fs->ro_compat & RO_SPARSE))
		return 1;
	if (g == 1)
		return 1;
	for (uint64_t b = 3; b <= 7; b += 2) /* 3, 5, 7 et leurs puissances */
		for (uint64_t p = b; p <= g; p *= b)
			if (p == g)
				return 1;
	return 0;
}

static void bset(uint8_t *b, uint64_t i) { b[i >> 3] |= (uint8_t)(1u << (i & 7)); }
static void bclr(uint8_t *b, uint64_t i) { b[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static int btst(const uint8_t *b, uint64_t i) { return (b[i >> 3] >> (i & 7)) & 1; }

/* la bitmap des blocs du groupe (BLOCK_UNINIT : faite ici, d'après les métadonnées qui s'y trouvent) */
static long bb_load(uint64_t g, const uint8_t *d)
{
	if (!(r16(d + 0x12) & BG_BLOCK_UNINIT))
		return get(GD_BB(d), bm);
	memset(bm, 0, fs->bs);
	uint64_t s = gstart(g), n = glen(g);
	if (has_backup(g))
		for (uint64_t i = 0; i < 1 + gdt_blocks + rsv_gdt && i < n; i++)
			bset(bm, i);
	uint8_t d2[64];
	for (uint64_t h = 0; h < fs->ngroups; h++) {
		long r = gd_get(h, d2);
		if (r)
			return r;
		uint64_t m[3] = {GD_BB(d2), GD_IB(d2), GD_IT(d2)};
		for (int k = 0; k < 3; k++) {
			uint64_t len = k == 2 ? itable_blocks : 1;
			for (uint64_t x = m[k]; x < m[k] + len; x++)
				if (x >= s && x < s + n)
					bset(bm, x - s);
		}
	}
	for (uint64_t i = n; i < (uint64_t)fs->bs * 8; i++) /* au-delà du groupe : pris */
		bset(bm, i);
	return 0;
}

static long bb_store(uint64_t g, uint8_t *d)
{
	long r = put(GD_BB(d), bm);
	if (r)
		return r;
	if (csum) {
		uint32_t c = ext4_crc32c(seed, bm, fs->bpg / 8);
		w16(d + 0x18, c & 0xFFFF);
		if (fs->descsz >= 0x3A)
			w16(d + 0x38, c >> 16);
	}
	w16(d + 0x12, r16(d + 0x12) & ~BG_BLOCK_UNINIT);
	return gd_put(g, d);
}

/* une plage d'au plus want blocs libres, à partir de goal -> son début, *got ; -28 plus de place */
static long alloc_run(uint64_t goal, uint64_t want, uint64_t *got, uint64_t *start)
{
	uint8_t d[64];
	if (goal < fs->first_data || goal >= fs->blocks)
		goal = fs->first_data;
	uint64_t g0 = (goal - fs->first_data) / fs->bpg;
	for (uint64_t k = 0; k <= fs->ngroups; k++) {
		uint64_t g = (g0 + k) % fs->ngroups;
		long r = gd_get(g, d);
		if (r)
			return r;
		if (!GD_FREEB(d))
			continue;
		if ((r = bb_load(g, d)))
			return r;
		uint64_t n = glen(g), i0 = k == 0 ? goal - gstart(g) : 0, i = i0;
		while (i < n && btst(bm, i))
			i++;
		if (i >= n) {
			for (i = 0; i < i0 && btst(bm, i); i++)
				;
			if (i >= i0)
				continue;
		}
		uint64_t c = 0;
		while (i + c < n && c < want && !btst(bm, i + c)) {
			bset(bm, i + c);
			c++;
		}
		gd_s16(d, 0x0C, 0x2C, GD_FREEB(d) - (uint32_t)c);
		if ((r = bb_store(g, d)))
			return r;
		fs->free_blocks -= c;
		*got = c;
		*start = gstart(g) + i;
		return 0;
	}
	return -28;
}

static long free_run(uint64_t start, uint64_t n)
{
	uint8_t d[64];
	while (n) {
		if (start < fs->first_data || start >= fs->blocks)
			return -5;
		uint64_t g = (start - fs->first_data) / fs->bpg, i = start - gstart(g);
		uint64_t k = glen(g) - i < n ? glen(g) - i : n;
		long r = gd_get(g, d);
		if (r || (r = bb_load(g, d)))
			return r;
		uint32_t c = 0;
		for (uint64_t j = i; j < i + k; j++)
			if (btst(bm, j)) {
				bclr(bm, j);
				c++;
			}
		gd_s16(d, 0x0C, 0x2C, GD_FREEB(d) + c);
		if ((r = bb_store(g, d)))
			return r;
		fs->free_blocks += c;
		start += k;
		n -= k;
	}
	return 0;
}

/* la bitmap des inodes (INODE_UNINIT : vide) */
static long ibm_load(const uint8_t *d)
{
	if (!(r16(d + 0x12) & BG_INODE_UNINIT))
		return get(GD_IB(d), bm);
	memset(bm, 0, fs->bs);
	for (uint64_t i = fs->ipg; i < (uint64_t)fs->bs * 8; i++)
		bset(bm, i);
	return 0;
}

static long ibm_store(uint64_t g, uint8_t *d)
{
	long r = put(GD_IB(d), bm);
	if (r)
		return r;
	if (csum) {
		uint32_t c = ext4_crc32c(seed, bm, fs->ipg / 8);
		w16(d + 0x1A, c & 0xFFFF);
		if (fs->descsz >= 0x3C)
			w16(d + 0x3A, c >> 16);
	}
	w16(d + 0x12, r16(d + 0x12) & ~BG_INODE_UNINIT);
	return gd_put(g, d);
}

static long alloc_inode(uint32_t parent, int dir, uint32_t *ino)
{
	uint8_t d[64];
	uint64_t g0 = parent ? (parent - 1) / fs->ipg : 0;
	for (uint64_t k = 0; k < fs->ngroups; k++) {
		uint64_t g = (g0 + k) % fs->ngroups;
		long r = gd_get(g, d);
		if (r)
			return r;
		if (!GD_FREEI(d))
			continue;
		if ((r = ibm_load(d)))
			return r;
		uint64_t i = g == 0 ? first_ino - 1 : 0;
		while (i < fs->ipg && btst(bm, i))
			i++;
		if (i >= fs->ipg)
			continue;
		bset(bm, i);
		gd_s16(d, 0x0E, 0x2E, GD_FREEI(d) - 1);
		if (dir)
			gd_s16(d, 0x10, 0x30, GD_DIRS(d) + 1);
		uint32_t unused = GD_UNUSED(d);
		if (i + 1 > fs->ipg - unused)
			gd_s16(d, 0x1C, 0x32, fs->ipg - (uint32_t)(i + 1));
		if ((r = ibm_store(g, d)))
			return r;
		free_inodes--;
		*ino = (uint32_t)(g * fs->ipg + i + 1);
		return 0;
	}
	return -28;
}

static long free_inode(uint32_t ino, int dir)
{
	uint8_t d[64];
	uint64_t g = (ino - 1) / fs->ipg, i = (ino - 1) % fs->ipg;
	long r = gd_get(g, d);
	if (r || (r = ibm_load(d)))
		return r;
	if (!btst(bm, i))
		return 0;
	bclr(bm, i);
	gd_s16(d, 0x0E, 0x2E, GD_FREEI(d) + 1);
	if (dir && GD_DIRS(d))
		gd_s16(d, 0x10, 0x30, GD_DIRS(d) - 1);
	free_inodes++;
	return ibm_store(g, d);
}

/* ---- inodes bruts (fs->isz octets) */

static long iloc(uint32_t ino, uint64_t *blk, uint32_t *off)
{
	uint8_t d[64];
	if (!ino || ino > fs->inodes)
		return -22;
	uint64_t g = (ino - 1) / fs->ipg, idx = (ino - 1) % fs->ipg;
	long r = gd_get(g, d);
	if (r)
		return r;
	uint64_t o = idx * fs->isz;
	*blk = GD_IT(d) + o / fs->bs;
	*off = (uint32_t)(o % fs->bs);
	return 0;
}

static long iget(uint32_t ino, uint8_t *raw)
{
	uint64_t b;
	uint32_t off;
	long r = iloc(ino, &b, &off);
	if (r)
		return r;
	const uint8_t *p = fs->rd(fs->ctx, b);
	if (!p)
		return -5;
	memcpy(raw, p + off, fs->isz);
	return 0;
}

static long iput(uint32_t ino, uint8_t *raw)
{
	if (csum) {
		int hi = fs->isz > 128 && r16(raw + 0x80) >= 4;
		w16(raw + 0x7C, 0);
		if (hi)
			w16(raw + 0x82, 0);
		uint32_t c = ext4_crc32c(iseed(ino, r32(raw + 0x64)), raw, fs->isz);
		w16(raw + 0x7C, c & 0xFFFF);
		if (hi)
			w16(raw + 0x82, c >> 16);
	}
	uint64_t b;
	uint32_t off;
	long r = iloc(ino, &b, &off);
	if (r || (r = get(b, gb)))
		return r;
	memcpy(gb + off, raw, fs->isz);
	return put(b, gb);
}

static uint64_t isize(const uint8_t *raw) { return r32(raw + 4) | (uint64_t)r32(raw + 0x6C) << 32; }
static void set_isize(uint8_t *raw, uint64_t v)
{
	w32(raw + 4, (uint32_t)v);
	w32(raw + 0x6C, (uint32_t)(v >> 32));
}
static void touch(uint8_t *raw, int m)
{
	w32(raw + 0x0C, now); /* ctime */
	if (m)
		w32(raw + 0x10, now); /* mtime */
}

static void to_rinode(uint32_t ino, const uint8_t *raw, struct ext4r_inode *ri)
{
	memset(ri, 0, sizeof *ri);
	ri->ino = ino;
	ri->mode = (uint16_t)r16(raw);
	ri->size = isize(raw);
	ri->mtime = r32(raw + 0x10);
	ri->flags = r32(raw + 0x20);
	memcpy(ri->block, raw + 0x28, 60);
}

/* ---- l'arbre des extents d'un fichier, dans un tableau */

struct ext {
	uint32_t lblk, len;
	uint64_t pblk;
	int uninit;
};
static struct ext ex[EXT_MAX], ex2[EXT_MAX];
static int nex;
static uint64_t tree[TREE_MAX]; /* ses blocs d'arbre (feuilles et index) */
static int ntree;

static long node_load(const uint8_t *n, uint32_t room, int level)
{
	if (r16(n) != 0xF30A)
		return -5;
	uint32_t cnt = r16(n + 2), depth = r16(n + 6);
	if (12 + 12 * cnt > room || level > 3)
		return -5;
	const uint8_t *e = n + 12;
	if (!depth) {
		for (uint32_t i = 0; i < cnt; i++, e += 12) {
			if (nex >= EXT_MAX)
				return -27;
			uint32_t len = r16(e + 4);
			ex[nex].uninit = len > 32768;
			ex[nex].len = len > 32768 ? len - 32768 : len;
			ex[nex].lblk = r32(e);
			ex[nex].pblk = (uint64_t)r16(e + 6) << 32 | r32(e + 8);
			nex++;
		}
		return 0;
	}
	uint64_t kids[512];
	if (cnt > 512)
		return -38;
	for (uint32_t i = 0; i < cnt; i++, e += 12)
		kids[i] = r32(e + 4) | (uint64_t)r16(e + 8) << 32;
	for (uint32_t i = 0; i < cnt; i++) {
		if (ntree >= TREE_MAX)
			return -38; /* (un arbre trop grand pour être réécrit ici) */
		tree[ntree++] = kids[i];
		long r = get(kids[i], ib);
		if (r)
			return r;
		static uint8_t child[MAXBS];
		memcpy(child, ib, fs->bs);
		if ((r = node_load(child, fs->bs, level + 1)))
			return r;
	}
	return 0;
}

static long ext_load(const uint8_t *raw)
{
	nex = ntree = 0;
	uint32_t fl = r32(raw + 0x20);
	if (fl & FL_INLINE)
		return -38;
	if (!(fl & FL_EXTENTS))
		return isize(raw) == 0 && r32(raw + 0x28) == 0 ? 0 : -38; /* (blocs indirects : pas modifiable) */
	return node_load(raw + 0x28, 60, 0);
}

static uint32_t per_leaf(void) { return (fs->bs - 12) / 12; }

/* [lblk, lblk + n[ ne pointe plus sur ses anciens blocs (rendus) ; pblk : sur pblk... (0 : un trou) */
static long ext_set(uint32_t lblk, uint64_t n, uint64_t pblk)
{
	int m = 0;
	uint64_t end = (uint64_t)lblk + n;
	static uint64_t fr_s[EXT_MAX], fr_n[EXT_MAX];
	int nfr = 0;
	for (int i = 0; i < nex; i++) {
		struct ext e = ex[i];
		uint64_t ee = (uint64_t)e.lblk + e.len;
		if (ee <= lblk || e.lblk >= end) {
			if (m >= EXT_MAX)
				return -27;
			ex2[m++] = e;
			continue;
		}
		if (e.lblk < lblk) {
			if (m >= EXT_MAX)
				return -27;
			ex2[m] = e;
			ex2[m++].len = lblk - e.lblk;
		}
		uint64_t os = e.lblk > lblk ? e.lblk : lblk, oe = ee < end ? ee : end;
		if (nfr >= EXT_MAX)
			return -27;
		fr_s[nfr] = e.pblk + (os - e.lblk);
		fr_n[nfr++] = oe - os;
		if (ee > end) {
			if (m >= EXT_MAX)
				return -27;
			ex2[m].lblk = (uint32_t)end;
			ex2[m].len = (uint32_t)(ee - end);
			ex2[m].pblk = e.pblk + (end - e.lblk);
			ex2[m++].uninit = e.uninit;
		}
	}
	if (pblk) { /* à sa place (le tableau reste trié) */
		if (m >= EXT_MAX)
			return -27;
		int k = m;
		while (k > 0 && ex2[k - 1].lblk > lblk) {
			ex2[k] = ex2[k - 1];
			k--;
		}
		ex2[k].lblk = lblk;
		ex2[k].len = (uint32_t)n;
		ex2[k].pblk = pblk;
		ex2[k].uninit = 0;
		m++;
	}
	int w = 0; /* les voisines contiguës fusionnées */
	for (int i = 0; i < m; i++) {
		if (w > 0) {
			struct ext *a = &ex2[w - 1];
			struct ext b = ex2[i];
			if (a->lblk + a->len == b.lblk && a->pblk + a->len == b.pblk && a->uninit == b.uninit &&
			    a->len + b.len <= (b.uninit ? 32767u : 32768u)) {
				a->len += b.len;
				continue;
			}
		}
		ex2[w++] = ex2[i];
	}
	if ((uint32_t)w > 4 * per_leaf())
		return -27; /* (trop morcelé pour un arbre de profondeur 1) */
	for (int i = 0; i < nfr; i++) {
		long r = free_run(fr_s[i], fr_n[i]);
		if (r)
			return r;
	}
	memcpy(ex, ex2, (uint64_t)w * sizeof *ex);
	nex = w;
	return 0;
}

static void put_ext(uint8_t *e, const struct ext *x)
{
	w32(e, x->lblk);
	w16(e + 4, x->uninit ? x->len + 32768 : x->len);
	w16(e + 6, (uint32_t)(x->pblk >> 32));
	w32(e + 8, (uint32_t)x->pblk);
}

static void put_hdr(uint8_t *h, uint32_t cnt, uint32_t max, uint32_t depth)
{
	w16(h, 0xF30A);
	w16(h + 2, cnt);
	w16(h + 4, max);
	w16(h + 6, depth);
	w32(h + 8, 0);
}

/* le tableau réécrit en arbre (ses anciens blocs repris), i_blocks recalculé */
static long ext_save(uint8_t *raw, uint32_t ino)
{
	uint32_t per = per_leaf();
	uint32_t leaves = nex <= 4 ? 0 : (uint32_t)((nex + per - 1) / per);
	if (leaves > 4)
		return -27;
	uint64_t blk[4];
	int have = 0;
	for (int i = 0; i < ntree && have < (int)leaves; i++)
		blk[have++] = tree[i];
	for (int i = have; i < ntree; i++) { /* les blocs d'arbre en trop */
		long r = free_run(tree[i], 1);
		if (r)
			return r;
	}
	while (have < (int)leaves) {
		uint64_t got, s;
		long r = alloc_run(nex ? ex[0].pblk : 0, 1, &got, &s);
		if (r)
			return r;
		blk[have++] = s;
	}
	uint8_t *root = raw + 0x28;
	memset(root, 0, 60);
	uint32_t gen = r32(raw + 0x64);
	if (!leaves) {
		put_hdr(root, (uint32_t)nex, 4, 0);
		for (int i = 0; i < nex; i++)
			put_ext(root + 12 + 12 * i, &ex[i]);
	} else {
		put_hdr(root, leaves, 4, 1);
		uint32_t base = (uint32_t)(nex / leaves), extra = (uint32_t)(nex % leaves), at = 0;
		for (uint32_t L = 0; L < leaves; L++) {
			uint32_t cnt = base + (L < extra);
			memset(db, 0, fs->bs);
			put_hdr(db, cnt, per, 0);
			for (uint32_t i = 0; i < cnt; i++)
				put_ext(db + 12 + 12 * i, &ex[at + i]);
			if (csum)
				w32(db + 12 + 12 * per, ext4_crc32c(iseed(ino, gen), db, 12 + 12 * per));
			long r = put(blk[L], db);
			if (r)
				return r;
			uint8_t *x = root + 12 + 12 * L;
			w32(x, ex[at].lblk);
			w32(x + 4, (uint32_t)blk[L]);
			w16(x + 8, (uint32_t)(blk[L] >> 32));
			at += cnt;
		}
	}
	for (uint32_t i = 0; i < leaves; i++)
		tree[i] = blk[i];
	ntree = (int)leaves;
	uint64_t nb = leaves;
	for (int i = 0; i < nex; i++)
		nb += ex[i].len;
	nb *= fs->bs / 512;
	w32(raw + 0x1C, (uint32_t)nb);
	w16(raw + 0x74, (uint32_t)(nb >> 32));
	uint32_t fl = r32(raw + 0x20) | FL_EXTENTS;
	w32(raw + 0x20, fl);
	return 0;
}

/* le bloc du fichier (initialisé) pour lblk, 0 : aucun */
static uint64_t ext_find(uint64_t lblk)
{
	for (int i = 0; i < nex; i++)
		if (lblk >= ex[i].lblk && lblk < (uint64_t)ex[i].lblk + ex[i].len)
			return ex[i].uninit ? 0 : ex[i].pblk + (lblk - ex[i].lblk);
	return 0;
}

static uint64_t ext_goal(uint64_t lblk, uint32_t ino)
{
	uint64_t prev = lblk ? ext_find(lblk - 1) : 0;
	if (prev)
		return prev + 1;
	if (nex)
		return ex[nex - 1].pblk + ex[nex - 1].len;
	return gstart((ino - 1) / fs->ipg);
}

/* ---- dossiers */

static uint32_t limit(void) { return csum ? fs->bs - 12 : fs->bs; }

static uint32_t rl_dec(uint32_t v)
{
	if (fs->bs < 65536)
		return v;
	if (v == 65535 || !v)
		return 65536;
	return (v & 65532) | ((v & 3) << 16);
}

static uint32_t rl_enc(uint32_t len)
{
	if (len < 65536)
		return len;
	return 65535;
}

static void dir_tail(uint8_t *b, uint32_t ino, uint32_t gen)
{
	if (!csum)
		return;
	uint8_t *t = b + fs->bs - 12;
	w32(t, 0);
	w16(t + 4, 12);
	t[6] = 0;
	t[7] = 0xDE;
	w32(t + 8, ext4_crc32c(iseed(ino, gen), b, fs->bs - 12));
}

static int dir_writable(const uint8_t *raw)
{
	uint32_t fl = r32(raw + 0x20);
	return !(fl & (FL_INDEX | FL_INLINE)) && (fl & FL_EXTENTS);
}

/* dans le dossier : le nom (exact, sinon sans la casse ASCII) -> 1, le vrai nom (actual), *ino, *type */
static long dir_lookup(const struct ext4r_inode *ri, const char *name, uint32_t len, char *actual, uint32_t *ino,
		       uint8_t *type)
{
	for (int ci = 0; ci < 2; ci++) {
		uint64_t pos = 0;
		long r;
		while ((r = ext4r_readdir(fs, ri, &pos, actual, ino, type)) > 0) {
			uint32_t n = 0;
			while (actual[n])
				n++;
			if (n != len)
				continue;
			uint32_t k = 0;
			for (; k < len; k++) {
				char a = actual[k], b = name[k];
				if (ci) {
					if (a >= 'A' && a <= 'Z')
						a += 32;
					if (b >= 'A' && b <= 'Z')
						b += 32;
				}
				if (a != b)
					break;
			}
			if (k == len)
				return 1;
		}
		if (r < 0)
			return r;
	}
	return 0;
}

static long dir_add(uint32_t dino, uint8_t *draw, const char *name, uint32_t len, uint32_t ino, uint8_t type)
{
	if (!dir_writable(draw))
		return -38;
	struct ext4r_inode ri;
	to_rinode(dino, draw, &ri);
	uint32_t need = (8 + len + 3) & ~3u, gen = r32(draw + 0x64), lim = limit();
	uint64_t nb = isize(draw) / fs->bs;
	for (uint64_t lb = 0; lb < nb; lb++) {
		uint64_t pb, run;
		long r = ext4r_map(fs, &ri, lb, &pb, &run);
		if (r < 0)
			return r;
		if (!pb)
			continue;
		if ((r = get(pb, db)))
			return r;
		for (uint32_t off = 0; off + 8 <= lim;) {
			uint8_t *e = db + off;
			uint32_t rl = rl_dec(r16(e + 4));
			if (rl < 8 || off + rl > lim)
				break;
			uint32_t used = r32(e) ? (8 + e[6] + 3) & ~3u : 0;
			if (rl - used >= need) {
				uint8_t *n = e;
				if (used) {
					w16(e + 4, rl_enc(used));
					n = e + used;
					w16(n + 4, rl_enc(rl - used));
				}
				w32(n, ino);
				n[6] = (uint8_t)len;
				n[7] = type;
				memcpy(n + 8, name, len);
				dir_tail(db, dino, gen);
				return put(pb, db);
			}
			off += rl;
		}
	}
	long r = ext_load(draw); /* plus de place : un bloc de plus */
	if (r)
		return r;
	uint64_t got, p;
	if ((r = alloc_run(ext_goal(nb, dino), 1, &got, &p)))
		return r;
	memset(db, 0, fs->bs);
	w32(db, ino);
	w16(db + 4, rl_enc(lim));
	db[6] = (uint8_t)len;
	db[7] = type;
	memcpy(db + 8, name, len);
	dir_tail(db, dino, gen);
	if ((r = put(p, db)) || (r = ext_set((uint32_t)nb, 1, p)) || (r = ext_save(draw, dino)))
		return r;
	set_isize(draw, (nb + 1) * fs->bs);
	touch(draw, 1);
	return iput(dino, draw);
}

/* l'entrée de ce nom exact quitte le dossier */
static long dir_del(uint32_t dino, uint8_t *draw, const char *name, uint32_t len)
{
	if (!dir_writable(draw))
		return -38;
	struct ext4r_inode ri;
	to_rinode(dino, draw, &ri);
	uint32_t gen = r32(draw + 0x64), lim = limit();
	uint64_t nb = isize(draw) / fs->bs;
	for (uint64_t lb = 0; lb < nb; lb++) {
		uint64_t pb, run;
		long r = ext4r_map(fs, &ri, lb, &pb, &run);
		if (r < 0)
			return r;
		if (!pb)
			continue;
		if ((r = get(pb, db)))
			return r;
		uint8_t *prev = 0;
		for (uint32_t off = 0; off + 8 <= lim;) {
			uint8_t *e = db + off;
			uint32_t rl = rl_dec(r16(e + 4));
			if (rl < 8 || off + rl > lim)
				break;
			uint32_t k = 0;
			if (r32(e) && e[6] == len)
				while (k < len && e[8 + k] == (uint8_t)name[k])
					k++;
			if (r32(e) && e[6] == len && k == len) {
				if (prev)
					w16(prev + 4, rl_enc(rl_dec(r16(prev + 4)) + rl));
				else
					w32(e, 0);
				dir_tail(db, dino, gen);
				return put(pb, db);
			}
			prev = e;
			off += rl;
		}
	}
	return -2;
}

static long dir_set_dotdot(uint32_t dino, uint32_t parent)
{
	uint8_t raw[1024];
	long r = iget(dino, raw);
	if (r)
		return r;
	struct ext4r_inode ri;
	to_rinode(dino, raw, &ri);
	uint64_t pb, run;
	if ((r = ext4r_map(fs, &ri, 0, &pb, &run)) < 0)
		return r;
	if (!pb || (r = get(pb, db)))
		return r ? r : -5;
	uint32_t rl = rl_dec(r16(db + 4));
	if (rl + 12 > fs->bs || db[rl + 6] != 2 || db[rl + 8] != '.' || db[rl + 9] != '.')
		return -5;
	w32(db + rl, parent);
	dir_tail(db, dino, r32(raw + 0x64));
	return put(pb, db);
}

static int dir_empty(uint32_t dino)
{
	struct ext4r_inode ri;
	if (ext4r_inode(fs, dino, &ri))
		return 0;
	uint64_t pos = 0;
	char name[256];
	uint32_t ino;
	uint8_t t;
	return ext4r_readdir(fs, &ri, &pos, name, &ino, &t) == 0;
}

/* ---- chemins */

static char pbuf[1024];

/* « a/b/c » -> pbuf « a/b », *name « c » (len) ; -22 vide, -36 trop long */
static long split(const char *path, const char **name, uint32_t *len)
{
	uint64_t n = 0;
	while (path[n])
		n++;
	while (n && path[n - 1] == '/')
		n--;
	if (!n || n >= sizeof pbuf)
		return -22;
	uint64_t s = n;
	while (s && path[s - 1] != '/')
		s--;
	if (n - s > 255)
		return -36;
	memcpy(pbuf, path, s);
	pbuf[s] = 0;
	*name = path + s;
	*len = (uint32_t)(n - s);
	if ((*len == 1 && path[s] == '.') || (*len == 2 && path[s] == '.' && path[s + 1] == '.'))
		return -22;
	return 0;
}

static void new_inode(uint8_t *raw, int dir)
{
	memset(raw, 0, fs->isz);
	w16(raw, dir ? 0x41ED : 0x81A4);
	w32(raw + 0x08, now);
	w32(raw + 0x0C, now);
	w32(raw + 0x10, now);
	w16(raw + 0x1A, dir ? 2 : 1);
	w32(raw + 0x20, FL_EXTENTS);
	put_hdr(raw + 0x28, 0, 4, 0);
	if (!gen_next)
		gen_next = now | 1;
	w32(raw + 0x64, gen_next++);
	if (fs->isz > 128) {
		w16(raw + 0x80, 32);
		w32(raw + 0x90, now);
	}
}

/* ---- l'interface */

long ext4w_init(struct ext4r *f, ext4w_wr w)
{
	fs = f;
	wr = w;
	ok = 0;
	state_dirty = 0;
	uint64_t blk = fs->bs == 1024 ? 1 : 0, off = fs->bs == 1024 ? 0 : 1024;
	const uint8_t *p = fs->rd(fs->ctx, blk);
	if (!p)
		return -5;
	const uint8_t *s = p + off;
	compat = r32(s + 0x5C);
	if ((fs->incompat & ~IN_OK) || (fs->incompat & IN_RECOVER) || !(fs->incompat & IN_EXTENTS))
		return -30;
	if ((fs->ro_compat & ~RO_OK) || ((fs->ro_compat & RO_GDT_CSUM) && !(fs->ro_compat & RO_CSUM)))
		return -30;
	if (fs->bs > MAXBS || fs->descsz > 64 || fs->isz > 1024)
		return -30;
	csum = (fs->ro_compat & RO_CSUM) != 0;
	seed = (fs->incompat & IN_CSUM_SEED) ? r32(s + 0x270) : ext4_crc32c(~0u, s + 0x68, 16);
	free_inodes = r32(s + 0x10);
	first_ino = r32(s + 0x4C) ? r32(s + 0x54) : 11;
	rsv_gdt = r16(s + 0xCE);
	backup_bgs[0] = r32(s + 0x24C);
	backup_bgs[1] = r32(s + 0x250);
	gdt_blocks = (fs->ngroups * fs->descsz + fs->bs - 1) / fs->bs;
	itable_blocks = ((uint64_t)fs->ipg * fs->isz + fs->bs - 1) / fs->bs;
	ok = 1;
	return 0;
}

void ext4w_time(uint32_t t)
{
	now = t;
}

long ext4w_create(const char *path, int dir, struct ext4r_inode *out)
{
	const char *name;
	uint32_t len, ino;
	long r = begin();
	if (r || (r = split(path, &name, &len)))
		return r;
	struct ext4r_inode pri;
	if ((r = ext4r_lookup(fs, pbuf, &pri)))
		return r;
	if (!EXT4R_ISDIR(pri.mode))
		return -20;
	static char actual[256];
	uint8_t t;
	if ((r = dir_lookup(&pri, name, len, actual, &ino, &t)))
		return r < 0 ? r : -17;
	static uint8_t praw[1024], nraw[1024];
	if ((r = iget(pri.ino, praw)))
		return r;
	if (!dir_writable(praw))
		return -38;
	if ((r = alloc_inode(pri.ino, dir, &ino)))
		return r;
	new_inode(nraw, dir);
	if (dir) { /* un bloc : « . » et « .. » */
		uint64_t got, p;
		if ((r = alloc_run(gstart((ino - 1) / fs->ipg), 1, &got, &p)))
			return r;
		memset(db, 0, fs->bs);
		w32(db, ino);
		w16(db + 4, 12);
		db[6] = 1;
		db[7] = 2;
		db[8] = '.';
		w32(db + 12, pri.ino);
		w16(db + 16, rl_enc(limit() - 12));
		db[18] = 2;
		db[19] = 2;
		db[20] = db[21] = '.';
		dir_tail(db, ino, r32(nraw + 0x64));
		if ((r = put(p, db)))
			return r;
		nex = ntree = 0;
		if ((r = ext_set(0, 1, p)) || (r = ext_save(nraw, ino)))
			return r;
		set_isize(nraw, fs->bs);
	}
	if ((r = iput(ino, nraw)))
		return r;
	if ((r = dir_add(pri.ino, praw, name, len, ino, dir ? 2 : 1)))
		return r;
	if ((r = iget(pri.ino, praw)))
		return r;
	if (dir && r16(praw + 0x1A) < 65000)
		w16(praw + 0x1A, r16(praw + 0x1A) + 1);
	touch(praw, 1);
	if ((r = iput(pri.ino, praw)) || (r = sb_write(0)))
		return r;
	return out ? ext4r_inode(fs, ino, out) : 0;
}

long ext4w_write(struct ext4r_inode *in, uint64_t pos, const void *buf, uint64_t n)
{
	static uint8_t raw[1024];
	long r = begin();
	if (r || (r = iget(in->ino, raw)))
		return r;
	if (EXT4R_ISDIR(r16(raw)))
		return -21;
	if ((r = ext_load(raw)))
		return r;
	const uint8_t *src = buf;
	uint64_t done = 0, bs = fs->bs;
	if ((pos + n + bs - 1) / bs > 0xFFFFFFFFull)
		return -27;
	while (done < n) {
		uint64_t lb = (pos + done) / bs, off = (pos + done) % bs, k = bs - off;
		if (k > n - done)
			k = n - done;
		uint64_t pb = ext_find(lb);
		if (pb) { /* un bloc déjà là */
			if (k < bs && (r = get(pb, db)))
				return r;
			memcpy(db + off, src + done, k);
			if ((r = put(pb, db)))
				return r;
			done += k;
			continue;
		}
		uint64_t last = (pos + n - 1) / bs, m = 1; /* les blocs sans place qui suivent */
		while (lb + m <= last && m < 32768 && !ext_find(lb + m))
			m++;
		uint64_t got, start;
		if ((r = alloc_run(ext_goal(lb, in->ino), m, &got, &start)))
			return r;
		for (uint64_t j = 0; j < got; j++) {
			uint64_t o = j ? 0 : off, c = bs - o;
			if (c > n - done)
				c = n - done;
			memset(db, 0, bs);
			memcpy(db + o, src + done, c);
			if ((r = put(start + j, db)))
				return r;
			done += c;
		}
		if ((r = ext_set((uint32_t)lb, got, start)))
			return r;
	}
	if ((r = ext_save(raw, in->ino)))
		return r;
	if (pos + n > isize(raw))
		set_isize(raw, pos + n);
	touch(raw, 1);
	if ((r = iput(in->ino, raw)) || (r = sb_write(0)) || (r = ext4r_inode(fs, in->ino, in)))
		return r;
	return (long)n;
}

/* les blocs du fichier à partir du bloc logique first sont rendus */
static long cut_from(uint8_t *raw, uint32_t ino, uint64_t first)
{
	long r = ext_load(raw);
	if (r)
		return r;
	uint64_t end = nex ? (uint64_t)ex[nex - 1].lblk + ex[nex - 1].len : 0;
	if (end > first && (r = ext_set((uint32_t)first, end - first, 0)))
		return r;
	return ext_save(raw, ino);
}

long ext4w_truncate(struct ext4r_inode *in, uint64_t size)
{
	static uint8_t raw[1024];
	long r = begin();
	if (r || (r = iget(in->ino, raw)))
		return r;
	if (EXT4R_ISDIR(r16(raw)))
		return -21;
	uint64_t bs = fs->bs;
	if (size < isize(raw)) {
		if ((r = cut_from(raw, in->ino, (size + bs - 1) / bs)))
			return r;
		uint64_t pb = size % bs ? ext_find(size / bs) : 0;
		if (pb) { /* la fin du dernier bloc : des zéros (une extension la montrerait) */
			if ((r = get(pb, db)))
				return r;
			memset(db + size % bs, 0, bs - size % bs);
			if ((r = put(pb, db)))
				return r;
		}
	} else if ((r = ext_load(raw))) {
		return r;
	}
	set_isize(raw, size);
	touch(raw, 1);
	if ((r = iput(in->ino, raw)) || (r = sb_write(0)))
		return r;
	return ext4r_inode(fs, in->ino, in);
}

/* l'entrée (nom exact) de dossier pino quitte son dossier ; son inode perd un lien (rendu au dernier) */
static long remove_entry(uint32_t pino, const char *name, uint32_t len, uint32_t ino)
{
	static uint8_t raw[1024], praw[1024];
	long r = iget(ino, raw);
	if (r || (r = iget(pino, praw)))
		return r;
	int dir = EXT4R_ISDIR(r16(raw));
	if (dir && !dir_empty(ino))
		return -39;
	if ((r = dir_del(pino, praw, name, len)))
		return r;
	uint32_t links = r16(raw + 0x1A);
	if (dir || links <= 1) {
		if ((r = cut_from(raw, ino, 0)))
			return r;
		w16(raw + 0x1A, 0);
		w32(raw + 0x14, now); /* dtime */
		touch(raw, 0);
		if ((r = iput(ino, raw)) || (r = free_inode(ino, dir)))
			return r;
	} else {
		w16(raw + 0x1A, links - 1);
		touch(raw, 0);
		if ((r = iput(ino, raw)))
			return r;
	}
	if ((r = iget(pino, praw)))
		return r;
	if (dir && r16(praw + 0x1A) > 2)
		w16(praw + 0x1A, r16(praw + 0x1A) - 1);
	touch(praw, 1);
	return iput(pino, praw);
}

long ext4w_unlink(const char *path)
{
	const char *name;
	uint32_t len, ino;
	long r = begin();
	if (r || (r = split(path, &name, &len)))
		return r;
	struct ext4r_inode pri;
	if ((r = ext4r_lookup(fs, pbuf, &pri)))
		return r;
	if (!EXT4R_ISDIR(pri.mode))
		return -20;
	static char actual[256];
	uint8_t t;
	if ((r = dir_lookup(&pri, name, len, actual, &ino, &t)) <= 0)
		return r ? r : -2;
	uint32_t al = 0;
	while (actual[al])
		al++;
	if ((r = remove_entry(pri.ino, actual, al, ino)))
		return r;
	return sb_write(0);
}

long ext4w_rename(const char *from, const char *to)
{
	static char fp[1024], fname[256], tname_act[256];
	const char *name;
	uint32_t len, sino, tino;
	uint8_t st, tt;
	long r = begin();
	if (r || (r = split(from, &name, &len)))
		return r;
	memcpy(fp, pbuf, sizeof fp);
	struct ext4r_inode fpi, tpi, si;
	if ((r = ext4r_lookup(fs, fp, &fpi)))
		return r;
	if (!EXT4R_ISDIR(fpi.mode))
		return -20;
	if ((r = dir_lookup(&fpi, name, len, fname, &sino, &st)) <= 0)
		return r ? r : -2;
	if ((r = ext4r_inode(fs, sino, &si)))
		return r;
	int sdir = EXT4R_ISDIR(si.mode);
	const char *tn;
	uint32_t tl;
	if ((r = split(to, &tn, &tl)) || (r = ext4r_lookup(fs, pbuf, &tpi)))
		return r;
	if (!EXT4R_ISDIR(tpi.mode))
		return -20;
	if (sdir) { /* pas dans lui-même */
		uint32_t x = tpi.ino;
		for (int guard = 0; guard < 4096 && x != EXT4R_ROOT; guard++) {
			if (x == sino)
				return -22;
			struct ext4r_inode xi;
			uint32_t up;
			if (ext4r_inode(fs, x, &xi) || ext4r_find(fs, &xi, "..", 2, &up) <= 0)
				return -5;
			x = up;
		}
		if (sino == EXT4R_ROOT)
			return -22;
	}
	r = dir_lookup(&tpi, tn, tl, tname_act, &tino, &tt);
	if (r < 0)
		return r;
	if (r) { /* l'arrivée existe */
		if (tino == sino) {
			if (tpi.ino == fpi.ino) { /* le même, autre casse : juste le nom */
				uint32_t fl = 0;
				while (fname[fl])
					fl++;
				static uint8_t praw[1024];
				if ((r = iget(fpi.ino, praw)) || (r = dir_del(fpi.ino, praw, fname, fl)) ||
				    (r = iget(fpi.ino, praw)) || (r = dir_add(fpi.ino, praw, tn, tl, sino, sdir ? 2 : 1)))
					return r;
				return sb_write(0);
			}
			return 0;
		}
		struct ext4r_inode ti;
		if ((r = ext4r_inode(fs, tino, &ti)))
			return r;
		if (EXT4R_ISDIR(ti.mode) && !sdir)
			return -21;
		if (!EXT4R_ISDIR(ti.mode) && sdir)
			return -20;
		uint32_t al = 0;
		while (tname_act[al])
			al++;
		if ((r = remove_entry(tpi.ino, tname_act, al, tino)))
			return r;
	}
	static uint8_t traw[1024], fraw[1024], sraw[1024];
	if ((r = iget(tpi.ino, traw)) || (r = dir_add(tpi.ino, traw, tn, tl, sino, sdir ? 2 : 1)))
		return r;
	uint32_t fl = 0;
	while (fname[fl])
		fl++;
	if ((r = iget(fpi.ino, fraw)) || (r = dir_del(fpi.ino, fraw, fname, fl)))
		return r;
	if (sdir && fpi.ino != tpi.ino) {
		if ((r = dir_set_dotdot(sino, tpi.ino)))
			return r;
		if ((r = iget(fpi.ino, fraw)))
			return r;
		if (r16(fraw + 0x1A) > 2)
			w16(fraw + 0x1A, r16(fraw + 0x1A) - 1);
		touch(fraw, 1);
		if ((r = iput(fpi.ino, fraw)) || (r = iget(tpi.ino, traw)))
			return r;
		if (r16(traw + 0x1A) < 65000)
			w16(traw + 0x1A, r16(traw + 0x1A) + 1);
		touch(traw, 1);
		if ((r = iput(tpi.ino, traw)))
			return r;
	} else {
		if ((r = iget(fpi.ino, fraw)))
			return r;
		touch(fraw, 1);
		if ((r = iput(fpi.ino, fraw)))
			return r;
		if (fpi.ino != tpi.ino) {
			if ((r = iget(tpi.ino, traw)))
				return r;
			touch(traw, 1);
			if ((r = iput(tpi.ino, traw)))
				return r;
		}
	}
	if ((r = iget(sino, sraw)))
		return r;
	touch(sraw, 0);
	if ((r = iput(sino, sraw)))
		return r;
	return sb_write(0);
}

long ext4w_utime(const char *path, uint32_t t)
{
	static uint8_t raw[1024];
	struct ext4r_inode ri;
	long r = begin();
	if (r || (r = ext4r_lookup(fs, path, &ri)) || (r = iget(ri.ino, raw)))
		return r;
	w32(raw + 0x10, t);
	w32(raw + 0x0C, now);
	if ((r = iput(ri.ino, raw)))
		return r;
	return sb_write(0);
}

long ext4w_sync(void)
{
	if (!ok || !state_dirty)
		return 0;
	state_dirty = 0;
	return sb_write(1);
}
