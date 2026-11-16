/* readtest : ext4r sous Linux (src/lib/ext4/test/run_read.sh) : readtest IMAGE [CHEMIN...]
 * Sans chemin : tout l'arbre, une ligne par entrée (« F chemin taille crc32 », « D chemin », « L chemin cible »),
 * triée par le script. Avec des chemins : ext4r_lookup de chacun (« chemin -> F taille crc32 » ou l'erreur). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ext4r.h"

static FILE *f;
static struct ext4r fs;

#define SLOTS 64
static uint8_t *cbuf[SLOTS];
static uint64_t ctag[SLOTS];
static uint32_t cbs;

static const uint8_t *rd(void *ctx, uint64_t block)
{
	(void)ctx;
	uint32_t bs = fs.bs ? fs.bs : 1024;
	if (bs != cbs) { /* (taille de bloc connue après le superbloc : cache vidé) */
		for (int i = 0; i < SLOTS; i++) {
			free(cbuf[i]);
			cbuf[i] = malloc(bs);
			ctag[i] = 0;
		}
		cbs = bs;
	}
	int s = (int)(block % SLOTS);
	if (ctag[s] == block + 1)
		return cbuf[s];
	if (fseeko(f, (off_t)(block * bs), SEEK_SET) || fread(cbuf[s], 1, bs, f) != bs)
		return 0;
	ctag[s] = block + 1;
	return cbuf[s];
}

static uint32_t crc_tab[256];
static uint32_t crc32(uint32_t c, const uint8_t *p, size_t n)
{
	if (!crc_tab[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t x = i;
			for (int k = 0; k < 8; k++)
				x = (x >> 1) ^ (0xEDB88320u & -(x & 1));
			crc_tab[i] = x;
		}
	c = ~c;
	while (n--)
		c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return ~c;
}

static long file_crc(const struct ext4r_inode *in, uint32_t *crc)
{
	static uint8_t b[100000]; /* (pas une puissance de 2 : des lectures à cheval sur les blocs) */
	uint64_t pos = 0;
	*crc = 0;
	while (pos < in->size) {
		long r = ext4r_read(&fs, in, pos, b, sizeof b);
		if (r <= 0)
			return r < 0 ? r : -5;
		*crc = crc32(*crc, b, (size_t)r);
		pos += (uint64_t)r;
	}
	return 0;
}

static void walk(const struct ext4r_inode *dir, const char *path)
{
	uint64_t pos = 0;
	char name[256];
	uint32_t ino;
	uint8_t type;
	long r;
	while ((r = ext4r_readdir(&fs, dir, &pos, name, &ino, &type)) > 0) {
		if (!path[0] && !strcmp(name, "lost+found"))
			continue;
		char p[4096];
		snprintf(p, sizeof p, "%s%s%s", path, path[0] ? "/" : "", name);
		struct ext4r_inode c;
		if (ext4r_inode(&fs, ino, &c)) {
			printf("? %s inode %u\n", p, ino);
			continue;
		}
		if (EXT4R_ISDIR(c.mode)) {
			printf("D %s\n", p);
			walk(&c, p);
		} else if (EXT4R_ISLNK(c.mode)) {
			char t[1024] = {0};
			if (c.size < 60 && !(c.flags & 0x10080000))
				memcpy(t, c.block, c.size);
			else
				ext4r_read(&fs, &c, 0, t, c.size < sizeof t - 1 ? c.size : sizeof t - 1);
			printf("L %s %s\n", p, t);
		} else {
			uint32_t crc;
			long e = file_crc(&c, &crc);
			if (e)
				printf("? %s lecture %ld\n", p, e);
			else
				printf("F %s %llu %08x\n", p, (unsigned long long)c.size, crc);
		}
	}
	if (r < 0)
		printf("? %s readdir %ld\n", path, r);
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	long r = ext4r_mount(&fs, rd, 0);
	if (r) {
		printf("ext4r_mount : %ld\n", r);
		return 1;
	}
	fprintf(stderr, "bloc %u, %llu blocs, %llu groupes, inode %u, desc %u, incompat %x%s, « %s »\n", fs.bs,
		(unsigned long long)fs.blocks, (unsigned long long)fs.ngroups, fs.isz, fs.descsz, fs.incompat,
		(fs.flags & EXT4R_DIRTY) ? " (journal à rejouer)" : "", fs.label);
	if (argc == 2) {
		struct ext4r_inode root;
		if (ext4r_inode(&fs, EXT4R_ROOT, &root))
			return 1;
		walk(&root, "");
		return 0;
	}
	for (int i = 2; i < argc; i++) {
		struct ext4r_inode in;
		r = ext4r_lookup(&fs, argv[i], &in);
		if (r) {
			printf("%s -> erreur %ld\n", argv[i], r);
			continue;
		}
		if (EXT4R_ISDIR(in.mode)) {
			printf("%s -> D\n", argv[i]);
			continue;
		}
		uint32_t crc;
		r = file_crc(&in, &crc);
		printf("%s -> F %llu %08x\n", argv[i], (unsigned long long)in.size, crc);
	}
	return 0;
}
