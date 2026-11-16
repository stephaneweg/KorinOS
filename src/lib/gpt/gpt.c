/* gpt.c : lire et écrire une GPT (voir gpt.h). Compilé dans korinrt (src/lib/korinrt/gpt.c l'inclut) ; un
 * programme sur libposix peut l'inclure de même. */
#include "gpt.h"

void *memcpy(void *d, const void *s, __SIZE_TYPE__ n);
void *memset(void *d, int c, __SIZE_TYPE__ n);
int memcmp(const void *a, const void *b, __SIZE_TYPE__ n);

/* EFI System C12A7328-F81F-11D2-BA4B-00A0C93EC93B, Basic data EBD0A0A2-B9E5-4433-87C0-68B6B72699C7,
 * MSR E3C9E316-0B5C-4DB8-817D-F92DF00215AE, récupération DE94BBA4-06D1-4D40-A16A-BFD50179D6AC,
 * Linux 0FC63DAF-8483-4772-8E79-3D69D8477DE4 */
const uint8_t GPT_T_EFI[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
const uint8_t GPT_T_BASIC[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
const uint8_t GPT_T_MSR[16] = {0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D, 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE};
const uint8_t GPT_T_RECOVERY[16] = {0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D, 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC};
const uint8_t GPT_T_LINUX[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};

#define ENTRY_SIZE 128
#define ENTRY_SECTORS (GPT_ENTRIES * ENTRY_SIZE / GPT_SECTOR) /* 32 */

static uint8_t sec[GPT_SECTOR];
static uint8_t ents[GPT_ENTRIES * ENTRY_SIZE];

uint32_t gpt_crc32(uint32_t crc, const void *p, uint64_t n)
{
	const uint8_t *b = p;
	crc = ~crc;
	while (n--) {
		crc ^= *b++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
	return ~crc;
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p)
{
	return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
	wr32(p, (uint32_t)v);
	wr32(p + 4, (uint32_t)(v >> 32));
}

int gpt_used(const struct gpt_entry *e)
{
	for (int i = 0; i < 16; i++)
		if (e->type[i])
			return 1;
	return 0;
}

/* un en-tête lu au secteur lba -> 0 et les entrées chargées dans g ; < 0 sinon */
static long read_at(struct gpt *g, gpt_rw rd, void *ctx, uint64_t lba)
{
	long r = rd(ctx, lba, 1, sec);
	if (r)
		return r;
	if (memcmp(sec, "EFI PART", 8))
		return -2;
	uint32_t hsize = rd32(sec + 12);
	if (hsize < 92 || hsize > GPT_SECTOR)
		return -3;
	uint32_t hcrc = rd32(sec + 16);
	wr32(sec + 16, 0);
	if (gpt_crc32(0, sec, hsize) != hcrc)
		return -3;
	uint64_t elba = rd64(sec + 72);
	uint32_t count = rd32(sec + 80), esz = rd32(sec + 84);
	if (esz != ENTRY_SIZE || count == 0 || count > GPT_ENTRIES || elba < 2 || elba >= g->disk_sectors)
		return -3;
	g->header_crc = hcrc;
	g->first_usable = rd64(sec + 40);
	g->last_usable = rd64(sec + 48);
	memcpy(g->disk_guid, sec + 56, 16);
	g->entries_crc = rd32(sec + 88);
	memset(ents, 0, sizeof ents);
	uint64_t n = (count * ENTRY_SIZE + GPT_SECTOR - 1) / GPT_SECTOR;
	r = rd(ctx, elba, n, ents);
	if (r)
		return r;
	if (gpt_crc32(0, ents, (uint64_t)count * ENTRY_SIZE) != g->entries_crc)
		return -3;
	memset(g->e, 0, sizeof g->e);
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *p = ents + i * ENTRY_SIZE;
		struct gpt_entry *e = &g->e[i];
		memcpy(e->type, p, 16);
		memcpy(e->guid, p + 16, 16);
		e->first = rd64(p + 32);
		e->last = rd64(p + 40);
		e->attrs = rd64(p + 48);
		memcpy(e->name, p + 56, 72);
	}
	return 0;
}

long gpt_read(struct gpt *g, gpt_rw rd, void *ctx, uint64_t disk_sectors)
{
	memset(g, 0, sizeof *g);
	g->disk_sectors = disk_sectors;
	long r = read_at(g, rd, ctx, 1);
	if (r == 0)
		return 0;
	long b = read_at(g, rd, ctx, disk_sectors - 1); /* la copie de secours, au dernier secteur */
	if (b == 0) {
		g->from_backup = 1;
		return 0;
	}
	return r == -2 && b == -2 ? -2 : (r < -3 ? r : -3);
}

void gpt_init(struct gpt *g, uint64_t disk_sectors, const uint8_t guid[16])
{
	memset(g, 0, sizeof *g);
	g->disk_sectors = disk_sectors;
	memcpy(g->disk_guid, guid, 16);
	g->first_usable = 2 + ENTRY_SECTORS;                  /* 34 */
	g->last_usable = disk_sectors - 2 - ENTRY_SECTORS;    /* avant les entrées et l'en-tête de secours */
}

int gpt_add(struct gpt *g, const uint8_t type[16], uint64_t first, uint64_t last, const char *name,
	    const uint8_t guid[16])
{
	if (first < g->first_usable || last > g->last_usable || first > last)
		return -2;
	int slot = -1;
	for (int i = 0; i < GPT_ENTRIES; i++) {
		const struct gpt_entry *e = &g->e[i];
		if (!gpt_used(e)) {
			if (slot < 0)
				slot = i;
			continue;
		}
		if (first <= e->last && last >= e->first)
			return -3;
	}
	if (slot < 0)
		return -1;
	struct gpt_entry *e = &g->e[slot];
	memset(e, 0, sizeof *e);
	memcpy(e->type, type, 16);
	memcpy(e->guid, guid, 16);
	e->first = first;
	e->last = last;
	gpt_name_set(e, name);
	return slot + 1;
}

void gpt_del(struct gpt *g, int num)
{
	if (num >= 1 && num <= GPT_ENTRIES)
		memset(&g->e[num - 1], 0, sizeof g->e[0]);
}

int gpt_free_space(const struct gpt *g, uint64_t *first, uint64_t *last)
{
	uint64_t best = 0, bf = 0, bl = 0;
	uint64_t pos = (g->first_usable + GPT_ALIGN - 1) / GPT_ALIGN * GPT_ALIGN; /* début candidat */
	for (;;) {
		uint64_t next = g->last_usable + 1; /* la première partition qui commence après pos */
		uint64_t after = 0;
		int hit = 0;
		for (int i = 0; i < GPT_ENTRIES; i++) {
			const struct gpt_entry *e = &g->e[i];
			if (!gpt_used(e))
				continue;
			if (e->first <= pos && e->last >= pos) { /* pos est dans une partition : après elle */
				hit = 1;
				if (e->last + 1 > after)
					after = e->last + 1;
			} else if (e->first > pos && e->first < next)
				next = e->first;
		}
		if (hit) {
			pos = (after + GPT_ALIGN - 1) / GPT_ALIGN * GPT_ALIGN;
			if (pos > g->last_usable)
				break;
			continue;
		}
		uint64_t end = next - 1;
		if (end >= pos && end - pos + 1 > best) {
			best = end - pos + 1;
			bf = pos;
			bl = end;
		}
		if (next > g->last_usable)
			break;
		pos = next;
	}
	if (best < GPT_ALIGN)
		return 0;
	*first = bf;
	*last = bl;
	return 1;
}

static void header(const struct gpt *g, uint8_t *h, uint64_t my, uint64_t alt, uint64_t elba, uint32_t ecrc)
{
	memset(h, 0, GPT_SECTOR);
	memcpy(h, "EFI PART", 8);
	wr32(h + 8, 0x00010000);
	wr32(h + 12, 92);
	wr64(h + 24, my);
	wr64(h + 32, alt);
	wr64(h + 40, g->first_usable);
	wr64(h + 48, g->last_usable);
	memcpy(h + 56, g->disk_guid, 16);
	wr64(h + 72, elba);
	wr32(h + 80, GPT_ENTRIES);
	wr32(h + 84, ENTRY_SIZE);
	wr32(h + 88, ecrc);
	wr32(h + 16, gpt_crc32(0, h, 92));
}

long gpt_write(struct gpt *g, gpt_rw wr, void *ctx)
{
	memset(ents, 0, sizeof ents);
	for (int i = 0; i < GPT_ENTRIES; i++) {
		const struct gpt_entry *e = &g->e[i];
		if (!gpt_used(e))
			continue;
		uint8_t *p = ents + i * ENTRY_SIZE;
		memcpy(p, e->type, 16);
		memcpy(p + 16, e->guid, 16);
		wr64(p + 32, e->first);
		wr64(p + 40, e->last);
		wr64(p + 48, e->attrs);
		memcpy(p + 56, e->name, 72);
	}
	g->entries_crc = gpt_crc32(0, ents, sizeof ents);
	/* le MBR protecteur : une entrée 0xEE sur tout le disque (plafonnée à 2^32 - 1) */
	memset(sec, 0, GPT_SECTOR);
	uint64_t n = g->disk_sectors - 1;
	uint8_t *pe = sec + 446;
	pe[1] = 0x00, pe[2] = 0x02, pe[3] = 0x00, pe[4] = 0xEE, pe[5] = 0xFF, pe[6] = 0xFF, pe[7] = 0xFF;
	wr32(pe + 8, 1);
	wr32(pe + 12, n > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)n);
	sec[510] = 0x55, sec[511] = 0xAA;
	long r = wr(ctx, 0, 1, sec);
	if (r)
		return r;
	uint64_t last = g->disk_sectors - 1, belba = last - ENTRY_SECTORS;
	if ((r = wr(ctx, 2, ENTRY_SECTORS, ents)))
		return r;
	if ((r = wr(ctx, belba, ENTRY_SECTORS, ents)))
		return r;
	header(g, sec, last, 1, belba, g->entries_crc); /* la copie de secours d'abord, la principale en dernier */
	if ((r = wr(ctx, last, 1, sec)))
		return r;
	header(g, sec, 1, last, 2, g->entries_crc);
	g->header_crc = rd32(sec + 16);
	return wr(ctx, 1, 1, sec);
}

static int hexv(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int gpt_guid_parse(const char *s, uint8_t out[16])
{
	uint8_t b[16];
	int n = 0;
	for (; *s && n < 32; s++) {
		if (*s == '-')
			continue;
		int v = hexv(*s);
		if (v < 0)
			return -1;
		if (n & 1)
			b[n / 2] |= (uint8_t)v;
		else
			b[n / 2] = (uint8_t)(v << 4);
		n++;
	}
	if (n != 32)
		return -1;
	static const int ord[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15}; /* (petit-boutiste) */
	for (int i = 0; i < 16; i++)
		out[i] = b[ord[i]];
	return 0;
}

void gpt_guid_text(const uint8_t g[16], char out[37])
{
	static const int ord[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
	static const char hx[] = "0123456789ABCDEF";
	int o = 0;
	for (int i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			out[o++] = '-';
		uint8_t v = g[ord[i]];
		out[o++] = hx[v >> 4];
		out[o++] = hx[v & 15];
	}
	out[o] = 0;
}

void gpt_name_set(struct gpt_entry *e, const char *s)
{
	memset(e->name, 0, sizeof e->name);
	for (int i = 0; s && s[i] && i < 36; i++)
		e->name[i] = (uint8_t)s[i];
}

void gpt_name_get(const struct gpt_entry *e, char *out, int cap)
{
	int o = 0;
	for (int i = 0; i < 36 && e->name[i] && o + 1 < cap; i++)
		out[o++] = e->name[i] < 128 ? (char)e->name[i] : '?';
	out[o] = 0;
}
