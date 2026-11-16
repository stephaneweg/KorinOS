/* writetest : ext4w sous Linux (src/lib/ext4/test/run_write.sh) : writetest IMAGE GRAINE OPERATIONS [GROS_MO]
 * Des opérations au hasard (créer, écrire, tronquer, effacer, renommer, dossiers), suivies dans un modèle en
 * mémoire ; tout est relu par ext4r et comparé au modèle, puis ext4w_sync. GROS_MO : d'abord un gros fichier
 * (l'allocation dans les groupes suivants, BLOCK_UNINIT). Le script juge ensuite l'image par e2fsck -fn.
 * Sortie : « ok ... » ou la première différence (code 1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ext4w.h"

static FILE *f;
static struct ext4r fs;

#define SLOTS 256
static uint8_t *cbuf[SLOTS];
static uint64_t ctag[SLOTS];
static uint32_t cbs;

static void cache_size(void)
{
	uint32_t bs = fs.bs ? fs.bs : 1024;
	if (bs == cbs)
		return;
	for (int i = 0; i < SLOTS; i++) {
		free(cbuf[i]);
		cbuf[i] = malloc(bs);
		ctag[i] = 0;
	}
	cbs = bs;
}

static const uint8_t *rd(void *ctx, uint64_t block)
{
	(void)ctx;
	cache_size();
	int s = (int)(block % SLOTS);
	if (ctag[s] == block + 1)
		return cbuf[s];
	if (fseeko(f, (off_t)(block * cbs), SEEK_SET) || fread(cbuf[s], 1, cbs, f) != cbs)
		return 0;
	ctag[s] = block + 1;
	return cbuf[s];
}

static long wr(void *ctx, uint64_t block, const void *buf)
{
	(void)ctx;
	cache_size();
	int s = (int)(block % SLOTS);
	if (buf != cbuf[s] || ctag[s] != block + 1)
		memcpy(cbuf[s], buf, cbs);
	ctag[s] = block + 1;
	if (fseeko(f, (off_t)(block * cbs), SEEK_SET) || fwrite(buf, 1, cbs, f) != cbs)
		return -5;
	return 0;
}

/* ---- le modèle */
#define NMAX 400
struct ent {
	char path[200];
	int dir, used;
	uint8_t *data;
	uint64_t size;
};
static struct ent m[NMAX];
static unsigned rs;
static unsigned rnd(void)
{
	rs = rs * 1103515245u + 12345u;
	return rs >> 8;
}

static int slot(void)
{
	for (int i = 0; i < NMAX; i++)
		if (!m[i].used)
			return i;
	return -1;
}

static int pick(int dir)
{
	int c[NMAX], n = 0;
	for (int i = 0; i < NMAX; i++)
		if (m[i].used && m[i].dir == dir)
			c[n++] = i;
	return n ? c[rnd() % n] : -1;
}

static int children(const char *p)
{
	size_t n = strlen(p);
	for (int i = 0; i < NMAX; i++)
		if (m[i].used && !strncmp(m[i].path, p, n) && m[i].path[n] == '/')
			return 1;
	return 0;
}

static void parent_dir(char *out)
{
	int d = pick(1);
	if (d < 0 || rnd() % 3 == 0)
		out[0] = 0;
	else
		snprintf(out, 200, "%s/", m[d].path);
}

static int fail(const char *what, const char *p, long r)
{
	printf("ECHEC : %s %s -> %ld\n", what, p, r);
	return 1;
}

static void fill(uint8_t *d, uint64_t n)
{
	for (uint64_t i = 0; i < n; i++)
		d[i] = (uint8_t)rnd();
}

static int op_write(int i, uint64_t pos, uint64_t n)
{
	struct ext4r_inode in;
	long r = ext4r_lookup(&fs, m[i].path, &in);
	if (r)
		return fail("lookup (écrire)", m[i].path, r);
	uint8_t *buf = malloc(n ? n : 1);
	fill(buf, n);
	r = ext4w_write(&in, pos, buf, n);
	if (r == -28 || r == -27) { /* plein, trop morcelé : refusé, rien ne change dans le modèle */
		free(buf);
		return 0;
	}
	if (r != (long)n)
		return fail("écrire", m[i].path, r);
	if (pos + n > m[i].size) {
		m[i].data = realloc(m[i].data, pos + n);
		memset(m[i].data + m[i].size, 0, pos + n - m[i].size);
		m[i].size = pos + n;
	}
	memcpy(m[i].data + pos, buf, n);
	free(buf);
	return 0;
}

static int verify(void)
{
	static uint8_t b[1 << 20];
	int files = 0;
	for (int i = 0; i < NMAX; i++) {
		if (!m[i].used)
			continue;
		struct ext4r_inode in;
		long r = ext4r_lookup(&fs, m[i].path, &in);
		if (r)
			return fail("relire (lookup)", m[i].path, r);
		if (m[i].dir != EXT4R_ISDIR(in.mode))
			return fail("relire (genre)", m[i].path, 0);
		if (m[i].dir)
			continue;
		files++;
		if (in.size != m[i].size)
			return fail("relire (taille)", m[i].path, (long)in.size);
		for (uint64_t pos = 0; pos < in.size;) {
			r = ext4r_read(&fs, &in, pos, b, sizeof b);
			if (r <= 0)
				return fail("relire", m[i].path, r);
			if (memcmp(b, m[i].data + pos, (size_t)r))
				return fail("relire (contenu)", m[i].path, (long)pos);
			pos += (uint64_t)r;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 4)
		return 2;
	f = fopen(argv[1], "r+b");
	if (!f)
		return 2;
	rs = (unsigned)atoi(argv[2]);
	int ops = atoi(argv[3]), bigmb = argc > 4 ? atoi(argv[4]) : 0;
	long r = ext4r_mount(&fs, rd, 0);
	if (r)
		return fail("ext4r_mount", argv[1], r);
	r = ext4w_init(&fs, wr);
	if (r) {
		printf("lecture seule : %ld\n", r);
		return 3;
	}
	ext4w_time(1763300000);
	if (bigmb) { /* un gros fichier d'abord */
		int i = slot();
		snprintf(m[i].path, 200, "gros.bin");
		if ((r = ext4w_create(m[i].path, 0, 0)))
			return fail("créer", m[i].path, r);
		m[i].used = 1;
		for (int k = 0; k < bigmb; k++)
			if (op_write(i, (uint64_t)k << 20, 1 << 20))
				return 1;
	}
	int counts[8] = {0};
	for (int k = 0; k < ops; k++) {
		unsigned o = rnd() % 100;
		char p[200], q[200];
		if (o < 18) { /* créer un fichier */
			int i = slot();
			if (i < 0)
				continue;
			parent_dir(p);
			snprintf(m[i].path, 200, "%sf%d_%u.dat", p, k, rnd() % 1000);
			if ((r = ext4w_create(m[i].path, 0, 0)))
				return fail("créer", m[i].path, r);
			m[i].used = 1;
			m[i].dir = 0;
			m[i].size = 0;
			m[i].data = 0;
			counts[0]++;
		} else if (o < 26) { /* créer un dossier */
			int i = slot();
			if (i < 0)
				continue;
			parent_dir(p);
			snprintf(m[i].path, 200, "%sD%d", p, k);
			if (strlen(m[i].path) > 150)
				continue;
			if ((r = ext4w_create(m[i].path, 1, 0)))
				return fail("mkdir", m[i].path, r);
			m[i].used = 1;
			m[i].dir = 1;
			m[i].data = 0;
			m[i].size = 0;
			counts[1]++;
		} else if (o < 60) { /* écrire : petit, gros, au-delà de la fin (un trou) */
			int i = pick(0);
			if (i < 0)
				continue;
			uint64_t pos, n;
			unsigned kind = rnd() % 4;
			if (kind == 0) {
				pos = m[i].size;
				n = rnd() % 70000;
			} else if (kind == 1) {
				pos = m[i].size ? rnd() % m[i].size : 0;
				n = rnd() % 5000;
			} else if (kind == 2) {
				pos = m[i].size + rnd() % 300000;
				n = rnd() % 9000 + 1;
			} else {
				pos = m[i].size;
				n = rnd() % 900000;
			}
			if (op_write(i, pos, n))
				return 1;
			counts[2]++;
		} else if (o < 66) { /* tronquer */
			int i = pick(0);
			if (i < 0)
				continue;
			uint64_t ns = m[i].size ? rnd() % (m[i].size + 20000) : rnd() % 9000;
			struct ext4r_inode in;
			if ((r = ext4r_lookup(&fs, m[i].path, &in)) || (r = ext4w_truncate(&in, ns)))
				return fail("tronquer", m[i].path, r);
			m[i].data = realloc(m[i].data, ns ? ns : 1);
			if (ns > m[i].size)
				memset(m[i].data + m[i].size, 0, ns - m[i].size);
			m[i].size = ns;
			counts[3]++;
		} else if (o < 74) { /* effacer un fichier, ou un dossier vide */
			int i = pick(rnd() % 4 == 0);
			if (i < 0 || (m[i].dir && children(m[i].path)))
				continue;
			if ((r = ext4w_unlink(m[i].path)))
				return fail("effacer", m[i].path, r);
			free(m[i].data);
			m[i].data = 0;
			m[i].used = 0;
			counts[4]++;
		} else if (o < 84) { /* renommer un fichier (parfois sur un autre : remplacé) */
			int i = pick(0);
			if (i < 0)
				continue;
			int j = rnd() % 3 == 0 ? pick(0) : -1;
			if (j == i)
				continue;
			if (j >= 0) {
				snprintf(q, 200, "%s", m[j].path);
			} else {
				parent_dir(p);
				snprintf(q, 200, "%sr%d.dat", p, k);
			}
			if ((r = ext4w_rename(m[i].path, q)))
				return fail("renommer", m[i].path, r);
			if (j >= 0) {
				free(m[j].data);
				m[j].data = 0;
				m[j].used = 0;
			}
			snprintf(m[i].path, 200, "%s", q);
			counts[5]++;
		} else if (o < 88) { /* renommer un dossier (déplacé avec ce qu'il contient) */
			int i = pick(1);
			if (i < 0)
				continue;
			parent_dir(p);
			size_t n = strlen(m[i].path);
			if (p[0] && !strncmp(p, m[i].path, n) && (p[n] == '/' || !p[n]))
				continue; /* (pas dans lui-même) */
			snprintf(q, 200, "%sM%d", p, k);
			if (strlen(q) > 120)
				continue;
			if ((r = ext4w_rename(m[i].path, q)))
				return fail("renommer (dossier)", m[i].path, r);
			for (int x = 0; x < NMAX; x++)
				if (m[x].used && x != i && !strncmp(m[x].path, m[i].path, n) && m[x].path[n] == '/') {
					char t[400];
					snprintf(t, sizeof t, "%s%s", q, m[x].path + n);
					snprintf(m[x].path, 200, "%s", t);
				}
			snprintf(m[i].path, 200, "%s", q);
			counts[6]++;
		} else { /* morceler : deux fichiers écrits en alternance, par petits bouts */
			int a = pick(0), b = pick(0);
			if (a < 0 || b < 0 || a == b)
				continue;
			for (int t = 0; t < 40; t++)
				if (op_write(t & 1 ? a : b, m[t & 1 ? a : b].size, 4096 + rnd() % 3000))
					return 1;
			counts[7]++;
		}
		if (k % 50 == 49 && verify())
			return 1;
	}
	if (verify())
		return 1;
	if ((r = ext4w_sync()))
		return fail("sync", "", r);
	fclose(f);
	int n = 0;
	for (int i = 0; i < NMAX; i++)
		n += m[i].used;
	printf("ok : %d entrées ; créés %d, dossiers %d, écritures %d, troncatures %d, effacés %d, renommés %d+%d, "
	       "morcelés %d\n", n, counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7]);
	return 0;
}
