/* hosttest : la bibliothèque GPT sous Linux, jugée par sgdisk (src/lib/gpt/test/run.sh).
 *
 *   hosttest new IMAGE         table neuve : EFI 100 Mio, Linux 200 Mio, Basic le reste
 *   hosttest add IMAGE         une partition Basic de 8 Mio dans le plus grand espace libre d'une table existante
 *   hosttest show IMAGE        la table lue
 *   hosttest breakprimary IMAGE  abîme l'en-tête principal (la lecture doit prendre la copie de secours)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gpt.h"

static FILE *f;

static long rw(void *ctx, uint64_t s, uint64_t n, void *b, int w)
{
	(void)ctx;
	if (fseek(f, (long)(s * GPT_SECTOR), SEEK_SET))
		return -5;
	size_t k = w ? fwrite(b, GPT_SECTOR, n, f) : fread(b, GPT_SECTOR, n, f);
	return k == n ? 0 : -5;
}
static long rd(void *c, uint64_t s, uint64_t n, void *b) { return rw(c, s, n, b, 0); }
static long wr(void *c, uint64_t s, uint64_t n, void *b) { return rw(c, s, n, b, 1); }

static void guid(uint8_t g[16])
{
	for (int i = 0; i < 16; i++)
		g[i] = (uint8_t)rand();
	g[7] = (uint8_t)((g[7] & 0x0F) | 0x40);
	g[8] = (uint8_t)((g[8] & 0x3F) | 0x80);
}

static void show(const struct gpt *g)
{
	char s[40], n[40];
	gpt_guid_text(g->disk_guid, s);
	printf("disque %s %llu secteurs, utile %llu..%llu%s\n", s, (unsigned long long)g->disk_sectors,
	       (unsigned long long)g->first_usable, (unsigned long long)g->last_usable, g->from_backup ? " (secours)" : "");
	for (int i = 0; i < GPT_ENTRIES; i++) {
		const struct gpt_entry *e = &g->e[i];
		if (!gpt_used(e))
			continue;
		gpt_name_get(e, n, sizeof n);
		gpt_guid_text(e->type, s);
		printf("  p%d %llu..%llu %s \"%s\"\n", i + 1, (unsigned long long)e->first, (unsigned long long)e->last, s, n);
	}
	uint64_t a, b;
	if (gpt_free_space(g, &a, &b))
		printf("  libre %llu..%llu\n", (unsigned long long)a, (unsigned long long)b);
}

int main(int argc, char **argv)
{
	static struct gpt g;
	if (argc < 3)
		return 2;
	srand(12345);
	f = fopen(argv[2], "r+b");
	if (!f)
		return 2;
	fseek(f, 0, SEEK_END);
	uint64_t sectors = (uint64_t)ftell(f) / GPT_SECTOR;
	if (!strcmp(argv[1], "new")) {
		uint8_t dg[16], p[16];
		guid(dg);
		gpt_init(&g, sectors, dg);
		uint64_t a, b;
		gpt_free_space(&g, &a, &b);
		guid(p);
		if (gpt_add(&g, GPT_T_EFI, a, a + 100 * 2048 - 1, "EFI system partition", p) != 1)
			return 3;
		gpt_free_space(&g, &a, &b);
		guid(p);
		if (gpt_add(&g, GPT_T_LINUX, a, a + 200 * 2048 - 1, "Korin home", p) != 2)
			return 3;
		gpt_free_space(&g, &a, &b);
		guid(p);
		if (gpt_add(&g, GPT_T_BASIC, a, b - 64 * 2048, "KORIN OS", p) != 3)
			return 3;
		if (gpt_add(&g, GPT_T_BASIC, a, a + 10, "chevauche", p) != -3)
			return 4; /* (le chevauchement doit être refusé) */
		if (gpt_write(&g, wr, 0))
			return 5;
	} else if (!strcmp(argv[1], "add")) {
		if (gpt_read(&g, rd, 0, sectors))
			return 6;
		uint64_t a, b;
		uint8_t p[16];
		if (!gpt_free_space(&g, &a, &b))
			return 7;
		guid(p);
		if (gpt_add(&g, GPT_T_BASIC, a, a + 8 * 2048 - 1, "ajoutee", p) < 0)
			return 8;
		if (gpt_write(&g, wr, 0))
			return 5;
	} else if (!strcmp(argv[1], "breakprimary")) {
		uint8_t z[512] = {0};
		memcpy(z, "EFI PART", 8);
		rw(0, 1, 1, z, 1); /* un en-tête aux sommes fausses */
	} else if (strcmp(argv[1], "show"))
		return 2;
	memset(&g, 0, sizeof g);
	long r = gpt_read(&g, rd, 0, sectors);
	if (r) {
		printf("lecture : %ld\n", r);
		return 9;
	}
	show(&g);
	fclose(f);
	return 0;
}
