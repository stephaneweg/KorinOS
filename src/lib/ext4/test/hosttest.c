/* hosttest : mkext4 sous Linux sur une image (src/lib/ext4/test/run.sh) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../mkext4.h"

static FILE *f;

static long wr(void *ctx, uint64_t block, uint64_t count, const void *buf)
{
	(void)ctx;
	if (fseeko(f, (off_t)(block * EXT4_BLOCK), SEEK_SET))
		return -5;
	return fwrite(buf, EXT4_BLOCK, count, f) == count ? 0 : -5;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	f = fopen(argv[1], "r+b");
	if (!f)
		return 2;
	fseeko(f, 0, SEEK_END);
	struct mkext4_opts o;
	memset(&o, 0, sizeof o);
	o.bytes = (uint64_t)ftello(f);
	o.label = "KORIN HOME";
	srand(42);
	for (int i = 0; i < 16; i++)
		o.uuid[i] = (uint8_t)rand();
	o.uuid[6] = (uint8_t)((o.uuid[6] & 0x0F) | 0x40);
	o.uuid[8] = (uint8_t)((o.uuid[8] & 0x3F) | 0x80);
	for (int i = 0; i < 4; i++)
		o.hash_seed[i] = (uint32_t)rand();
	o.now = (uint32_t)time(0);
	struct mkext4_info info;
	long r = mkext4(&o, wr, 0, &info);
	fclose(f);
	if (r) {
		printf("mkext4 : %ld\n", r);
		return 1;
	}
	printf("%llu blocs, %llu inodes, %llu groupes, journal %llu blocs, %llu libres\n", (unsigned long long)info.blocks,
	       (unsigned long long)info.inodes, (unsigned long long)info.groups, (unsigned long long)info.journal_blocks,
	       (unsigned long long)info.free_blocks);
	return 0;
}
