/* ext4r.c : lire un volume ext4, ext3 ou ext2 (ext4r.h). Les structures : la documentation du noyau Linux,
 * « ext4 Data Structures and Algorithms » (superbloc, descripteurs de groupe, inodes, extents, dossiers). */
#include "ext4r.h"

void *memcpy(void *d, const void *s, __SIZE_TYPE__ n);
void *memset(void *d, int c, __SIZE_TYPE__ n);

#define IN_FILETYPE 0x2
#define IN_RECOVER 0x4
#define IN_JOURNAL_DEV 0x8
#define IN_META_BG 0x10
#define IN_EXTENTS 0x40
#define IN_64BIT 0x80
#define IN_DIRDATA 0x1000
#define IN_INLINE 0x8000
#define IN_ENCRYPT 0x10000
#define IN_REFUSED (IN_JOURNAL_DEV | IN_META_BG | IN_DIRDATA | IN_ENCRYPT)

#define FL_EXTENTS 0x80000
#define FL_INLINE 0x10000000

#define LINKS_MAX 8
#define PATH_BUF 1024

static uint32_t r16(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t r32(const uint8_t *p)
{
	return r16(p) | r16(p + 2) << 16;
}

long ext4r_mount(struct ext4r *fs, ext4r_rd rd, void *ctx)
{
	memset(fs, 0, sizeof *fs);
	fs->rd = rd;
	fs->ctx = ctx;
	fs->bs = 1024; /* le superbloc : octets 1024 à 2047, le bloc 1 en blocs de 1 Kio */
	const uint8_t *s = rd(ctx, 1);
	if (!s)
		return -5;
	if (r16(s + 0x38) != 0xEF53)
		return -22;
	uint32_t log = r32(s + 0x18);
	if (log > 6)
		return -22;
	uint32_t incompat = r32(s + 0x60);
	if (incompat & IN_REFUSED)
		return -22;
	fs->inodes = r32(s);
	fs->blocks = r32(s + 0x04);
	fs->free_blocks = r32(s + 0x0C);
	fs->first_data = r32(s + 0x14);
	fs->bpg = r32(s + 0x20);
	fs->ipg = r32(s + 0x28);
	fs->isz = r32(s + 0x4C) ? r16(s + 0x58) : 128;
	fs->incompat = incompat;
	fs->ro_compat = r32(s + 0x64);
	fs->descsz = 32;
	if (incompat & IN_64BIT) {
		fs->blocks |= (uint64_t)r32(s + 0x150) << 32;
		fs->free_blocks |= (uint64_t)r32(s + 0x158) << 32;
		uint32_t d = r16(s + 0xFE);
		if (d >= 64)
			fs->descsz = d;
	}
	if (incompat & IN_RECOVER)
		fs->flags |= EXT4R_DIRTY;
	memcpy(fs->label, s + 0x78, 16);
	fs->label[16] = 0;
	if (!fs->bpg || !fs->ipg || fs->isz < 128 || fs->first_data >= fs->blocks)
		return -22;
	fs->bs = 1024u << log;
	if (fs->isz > fs->bs || (fs->isz & (fs->isz - 1)))
		return -22;
	fs->ngroups = (fs->blocks - fs->first_data + fs->bpg - 1) / fs->bpg;
	return 0;
}

long ext4r_inode(struct ext4r *fs, uint32_t ino, struct ext4r_inode *in)
{
	if (!ino || ino > fs->inodes)
		return -22;
	uint64_t g = (ino - 1) / fs->ipg, idx = (ino - 1) % fs->ipg;
	if (g >= fs->ngroups)
		return -22;
	uint64_t doff = g * fs->descsz;
	const uint8_t *d = fs->rd(fs->ctx, fs->first_data + 1 + doff / fs->bs);
	if (!d)
		return -5;
	d += doff % fs->bs;
	uint64_t table = r32(d + 8);
	if (fs->descsz >= 64)
		table |= (uint64_t)r32(d + 0x28) << 32;
	uint64_t ioff = idx * fs->isz;
	const uint8_t *p = fs->rd(fs->ctx, table + ioff / fs->bs);
	if (!p)
		return -5;
	p += ioff % fs->bs;
	in->ino = ino;
	in->mode = (uint16_t)r16(p);
	in->size = r32(p + 0x04) | (uint64_t)r32(p + 0x6C) << 32;
	in->mtime = r32(p + 0x10);
	in->flags = r32(p + 0x20);
	memcpy(in->block, p + 0x28, 60);
	in->xlen = 0;
	if (!in->mode)
		return -2; /* (inode libre) */
	if ((in->flags & FL_INLINE) && fs->isz > 128) { /* la suite : l'attribut system.data, dans l'inode */
		uint32_t extra = r16(p + 0x80);
		uint32_t h = 128 + extra; /* en-tête des attributs (0xEA020000), puis les entrées */
		if (h + 4 <= fs->isz && r32(p + h) == 0xEA020000u) {
			const uint8_t *base = p + h + 4, *end = p + fs->isz;
			for (const uint8_t *e = base; e + 16 <= end && r32(e) != 0;) {
				uint32_t nl = e[0], idx = e[1], voff = r16(e + 2), vsize = r32(e + 8);
				if (idx == 7 && nl == 4 && e + 16 + 4 <= end && e[16] == 'd' && e[17] == 'a' && e[18] == 't' &&
				    e[19] == 'a' && base + voff + vsize <= end && vsize <= sizeof in->xdata) {
					memcpy(in->xdata, base + voff, vsize);
					in->xlen = vsize;
					break;
				}
				e += (16 + nl + 3) & ~3u;
			}
		}
	}
	return 0;
}

/* blocs indirects (ext2, ext3) : le niveau level (0 direct) à partir du bloc ind */
static long map_ind(struct ext4r *fs, uint64_t ind, int level, uint64_t lblk, uint64_t *pblk)
{
	uint64_t per = fs->bs / 4;
	while (level > 0) {
		if (!ind) {
			*pblk = 0;
			return 0;
		}
		uint64_t span = 1;
		for (int i = 1; i < level; i++)
			span *= per;
		const uint8_t *b = fs->rd(fs->ctx, ind);
		if (!b)
			return -5;
		ind = r32(b + 4 * (lblk / span));
		lblk %= span;
		level--;
	}
	*pblk = ind;
	return 0;
}

long ext4r_map(struct ext4r *fs, const struct ext4r_inode *in, uint64_t lblk, uint64_t *pblk, uint64_t *run)
{
	*pblk = 0;
	*run = 1;
	if (in->flags & FL_INLINE)
		return -22;
	if (!(in->flags & FL_EXTENTS)) {
		uint64_t per = fs->bs / 4;
		if (lblk < 12) {
			*pblk = r32(in->block + 4 * lblk);
			return 0;
		}
		lblk -= 12;
		if (lblk < per)
			return map_ind(fs, r32(in->block + 48), 1, lblk, pblk);
		lblk -= per;
		if (lblk < per * per)
			return map_ind(fs, r32(in->block + 52), 2, lblk, pblk);
		lblk -= per * per;
		if (lblk < per * per * per)
			return map_ind(fs, r32(in->block + 56), 3, lblk, pblk);
		return -22;
	}
	const uint8_t *n = in->block;
	uint32_t room = 60;
	for (int guard = 0; guard < 8; guard++) {
		if (r16(n) != 0xF30A)
			return -5;
		uint32_t cnt = r16(n + 2), depth = r16(n + 6);
		if (12 + 12 * cnt > room)
			return -5;
		const uint8_t *e = n + 12;
		if (!depth) {
			for (uint32_t i = 0; i < cnt; i++, e += 12) {
				uint64_t eb = r32(e);
				uint32_t len = r16(e + 4);
				int uninit = len > 32768;
				if (uninit)
					len -= 32768;
				if (lblk < eb) { /* un trou avant cette extent */
					*run = eb - lblk;
					return 0;
				}
				if (lblk < eb + len) {
					uint64_t start = (uint64_t)r16(e + 6) << 32 | r32(e + 8);
					*run = eb + len - lblk;
					*pblk = uninit ? 0 : start + (lblk - eb); /* (non écrite : des zéros) */
					return 0;
				}
			}
			*run = 1; /* après la dernière extent : un trou (fin du fichier) */
			return 0;
		}
		if (!cnt || lblk < r32(e)) { /* avant le premier index : un trou */
			*run = cnt ? r32(e) - lblk : 1;
			return 0;
		}
		uint32_t k = 0;
		while (k + 1 < cnt && r32(e + 12 * (k + 1)) <= lblk)
			k++;
		const uint8_t *x = e + 12 * k;
		uint64_t child = r32(x + 4) | (uint64_t)r16(x + 8) << 32;
		n = fs->rd(fs->ctx, child);
		if (!n)
			return -5;
		room = fs->bs;
	}
	return -5;
}

long ext4r_read(struct ext4r *fs, const struct ext4r_inode *in, uint64_t pos, void *buf, uint64_t n)
{
	if (pos >= in->size)
		return 0;
	if (n > in->size - pos)
		n = in->size - pos;
	uint8_t *o = buf;
	if (in->flags & FL_INLINE) { /* les données dans l'inode : i_block (60 octets), puis system.data */
		for (uint64_t i = 0; i < n; i++) {
			uint64_t x = pos + i;
			o[i] = x < 60 ? in->block[x] : x - 60 < in->xlen ? in->xdata[x - 60] : 0;
		}
		return (long)n;
	}
	uint64_t done = 0;
	while (done < n) {
		uint64_t lb = (pos + done) / fs->bs, off = (pos + done) % fs->bs;
		uint64_t k = fs->bs - off;
		if (k > n - done)
			k = n - done;
		uint64_t pb, run;
		long r = ext4r_map(fs, in, lb, &pb, &run);
		if (r < 0)
			return done ? (long)done : r;
		if (!pb) {
			memset(o + done, 0, k);
		} else {
			const uint8_t *b = fs->rd(fs->ctx, pb);
			if (!b)
				return done ? (long)done : -5;
			memcpy(o + done, b + off, k);
		}
		done += k;
	}
	return (long)done;
}

static uint32_t rec_len(const struct ext4r *fs, uint32_t v)
{
	if (fs->bs < 65536)
		return v;
	if (v == 65535 || !v)
		return 65536;
	return (v & 65532) | ((v & 3) << 16);
}

/* l'entrée suivante, « . » et « .. » compris -> 1, 0 en fin, < 0 erreur ; nl = longueur du nom */
static long dir_next(struct ext4r *fs, const struct ext4r_inode *dir, uint64_t *pos, char *name, uint32_t *nl,
		     uint32_t *ino, uint8_t *type)
{
	if (dir->flags & FL_INLINE) { /* « .. » (4 octets), puis les entrées, dans les 60 octets de l'inode */
		if (*pos < 4) {
			*pos = 4;
			name[0] = name[1] = '.';
			name[2] = 0;
			*nl = 2;
			*ino = r32(dir->block);
			*type = 2;
			return 1;
		}
		for (;;) { /* i_block de 4 à 60, puis system.data (positions 60 et suivantes) */
			const uint8_t *zone = *pos < 60 ? dir->block : dir->xdata;
			uint64_t zs = *pos < 60 ? 60 : dir->xlen, zo = *pos < 60 ? *pos : *pos - 60;
			if (zo + 8 > zs) {
				if (*pos < 60 && dir->xlen) {
					*pos = 60;
					continue;
				}
				return 0;
			}
			const uint8_t *e = zone + zo;
			uint32_t rl = r16(e + 4), l = e[6];
			if (rl < 8 || zo + rl > zs || 8 + l > rl)
				return 0;
			*pos += rl;
			if (!r32(e) || !l)
				continue;
			memcpy(name, e + 8, l);
			name[l] = 0;
			*nl = l;
			*ino = r32(e);
			*type = (fs->incompat & IN_FILETYPE) ? e[7] : 0;
			return 1;
		}
	}
	while (*pos < dir->size) {
		uint64_t lb = *pos / fs->bs, off = *pos % fs->bs;
		uint64_t pb, run;
		long r = ext4r_map(fs, dir, lb, &pb, &run);
		if (r < 0)
			return r;
		if (!pb) { /* (un trou : le bloc suivant) */
			*pos = (lb + 1) * fs->bs;
			continue;
		}
		const uint8_t *b = fs->rd(fs->ctx, pb);
		if (!b)
			return -5;
		while (off + 8 <= fs->bs) {
			const uint8_t *e = b + off;
			uint32_t rl = rec_len(fs, r16(e + 4));
			uint32_t l = (fs->incompat & IN_FILETYPE) ? e[6] : r16(e + 6);
			if (rl < 8 || off + rl > fs->bs || l > 255 || 8 + l > rl)
				break; /* (abîmé : le bloc suivant) */
			off += rl;
			*pos = lb * fs->bs + off;
			if (!r32(e) || !l)
				continue;
			memcpy(name, e + 8, l);
			name[l] = 0;
			*nl = l;
			*ino = r32(e);
			*type = (fs->incompat & IN_FILETYPE) ? e[7] : 0;
			return 1;
		}
		*pos = (lb + 1) * fs->bs;
	}
	return 0;
}

long ext4r_readdir(struct ext4r *fs, const struct ext4r_inode *dir, uint64_t *pos, char *name, uint32_t *ino,
		   uint8_t *type)
{
	for (;;) {
		uint32_t nl;
		long r = dir_next(fs, dir, pos, name, &nl, ino, type);
		if (r <= 0)
			return r;
		if ((nl == 1 && name[0] == '.') || (nl == 2 && name[0] == '.' && name[1] == '.'))
			continue;
		return 1;
	}
}

static int same(const char *a, const char *b, uint32_t n, int ci)
{
	for (uint32_t i = 0; i < n; i++) {
		char x = a[i], y = b[i];
		if (ci) {
			if (x >= 'A' && x <= 'Z')
				x += 32;
			if (y >= 'A' && y <= 'Z')
				y += 32;
		}
		if (x != y)
			return 0;
	}
	return 1;
}

/* dans le dossier, le nom (len octets) -> son inode (casse exacte, sinon sans la casse ASCII), 0 : absent */
long ext4r_find(struct ext4r *fs, const struct ext4r_inode *dir, const char *s, uint32_t len, uint32_t *ino)
{
	static char name[256];
	for (int ci = 0; ci < 2; ci++) {
		uint64_t pos = 0;
		uint32_t nl, i;
		uint8_t t;
		long r;
		while ((r = dir_next(fs, dir, &pos, name, &nl, &i, &t)) > 0)
			if (nl == len && same(name, s, len, ci)) {
				*ino = i;
				return 1;
			}
		if (r < 0)
			return r;
	}
	return 0;
}

long ext4r_lookup(struct ext4r *fs, const char *path, struct ext4r_inode *in)
{
	static char buf[2][PATH_BUF];
	int cur = 0, links = 0;
	struct ext4r_inode dir;
	long r = ext4r_inode(fs, EXT4R_ROOT, &dir);
	if (r < 0)
		return r;
	const char *p = path;
	for (;;) {
		while (*p == '/')
			p++;
		if (!*p) {
			*in = dir;
			return 0;
		}
		const char *s = p;
		while (*p && *p != '/')
			p++;
		uint32_t len = (uint32_t)(p - s);
		if (len == 1 && s[0] == '.')
			continue;
		if (!EXT4R_ISDIR(dir.mode))
			return -20;
		if (len > 255)
			return -2;
		uint32_t ino;
		r = ext4r_find(fs, &dir, s, len, &ino);
		if (r < 0)
			return r;
		if (!r)
			return -2;
		struct ext4r_inode c;
		r = ext4r_inode(fs, ino, &c);
		if (r < 0)
			return r == -22 ? -5 : r;
		if (!EXT4R_ISLNK(c.mode)) {
			dir = c;
			continue;
		}
		/* un lien : sa cible, puis le reste du chemin (absolue : depuis la racine du volume) */
		if (++links > LINKS_MAX || c.size >= PATH_BUF / 2)
			return -2;
		char *o = buf[cur ^ 1];
		if (c.size < 60 && !(c.flags & (FL_EXTENTS | FL_INLINE))) {
			memcpy(o, c.block, c.size);
		} else {
			r = ext4r_read(fs, &c, 0, o, c.size);
			if (r != (long)c.size)
				return r < 0 ? r : -5;
		}
		uint64_t n = c.size;
		uint64_t rest = 0;
		while (p[rest])
			rest++;
		if (n + 1 + rest >= PATH_BUF)
			return -2;
		o[n] = '/';
		memcpy(o + n + 1, p, rest + 1);
		cur ^= 1;
		p = o;
		if (*p == '/') {
			r = ext4r_inode(fs, EXT4R_ROOT, &dir);
			if (r < 0)
				return r;
		}
	}
}
