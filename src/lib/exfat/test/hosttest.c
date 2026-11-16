/* hosttest : mkexfat sous Linux sur une image (src/lib/exfat/test/run.sh) : hosttest IMAGE [CLUSTER] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mkexfat.h"

static FILE *f;

static long wr(void *ctx, uint64_t sector, uint64_t count, const void *buf)
{
	(void)ctx;
	if (count > EXFAT_WR_MAX)
		return -22;
	if (fseeko(f, (off_t)(sector * EXFAT_SECTOR), SEEK_SET))
		return -5;
	return fwrite(buf, EXFAT_SECTOR, count, f) == count ? 0 : -5;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	f = fopen(argv[1], "r+b");
	if (!f)
		return 2;
	fseeko(f, 0, SEEK_END);
	struct mkexfat_opts o;
	memset(&o, 0, sizeof o);
	o.bytes = (uint64_t)ftello(f);
	o.cluster = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 0;
	o.label = "KORIN Été";
	o.serial = 0x1234ABCD;
	struct mkexfat_info info;
	long r = mkexfat(&o, wr, 0, &info);
	fclose(f);
	if (r) {
		printf("mkexfat : %ld\n", r);
		return 1;
	}
	printf("%llu secteurs, FAT %llu+%llu, tas %llu, %llu clusters de %llu\n", (unsigned long long)info.sectors,
	       (unsigned long long)info.fat_offset, (unsigned long long)info.fat_length,
	       (unsigned long long)info.heap_offset, (unsigned long long)info.clusters,
	       (unsigned long long)info.cluster_bytes);
	return 0;
}
