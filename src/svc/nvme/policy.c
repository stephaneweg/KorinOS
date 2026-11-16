/* policy.c : qui a le droit d'écrire sur le disque NVMe (docs/etude/PC-PILOTES.md,
 * « NVMe », sécurité). Le disque du ZBook porte Windows : par défaut, RIEN ne
 * s'écrit. Trois verrous l'un derrière l'autre :
 *
 *  1. des règles explicites (module « nvme.txt » de l'initrd, -c FICHIER, -w) :
 *       write = CONDITIONS          la partition qui les remplit toutes s'écrit
 *       name = ALIAS, CONDITIONS    elle est aussi servie en « blk.ALIAS »
 *     conditions, séparées par des virgules : label=NOM (nom du volume FAT ou
 *     exFAT, sans casse), guid=GUID (GUID unique de la partition), type=basic,
 *     efi, msr, recovery, linux ou un GUID de type, part=N (numéro de l'entrée
 *     GPT), disk=nvme0 ; une règle qui désigne plusieurs partitions n'en ouvre
 *     aucune ;
 *  2. des refus qu'aucune règle ne lève : le disque entier, une GPT dont le CRC
 *     est faux, les types EFI, MSR, récupération de Windows, LDM, espaces de
 *     stockage, les attributs « requise par la plateforme » et « lecture seule »,
 *     un contenu NTFS, BitLocker, ReFS ou inconnu (seuls FAT12/16/32, exFAT et
 *     ext2/3/4 s'écrivent), une partition hors de la zone utile de la GPT ou qui
 *     en chevauche une autre ; -r : tout. Une exception : le type EFI s'ouvre à
 *     une règle qui donne À LA FOIS guid= et type=efi (la partition de démarrage
 *     que l'installateur a créée sur un disque entier, osinstall ; jamais celle
 *     de Windows, qu'aucune règle de l'installateur ne nomme) ;
 *  3. au plus bas, juste avant le contrôleur (policy_write_ok, appelée par
 *     nvme.c pour chaque écriture) : la plage entière doit tomber dans une
 *     partition ouverte par 1 et 2 ; sinon refus et trace, même si le service
 *     blk au-dessus s'était trompé.
 * Chaque écriture refusée rend E_PERM et laisse une ligne au journal. */
#include "nvme.h"
#include "../../lib/gpt/gpt.h"

#define RULES_MAX 16
#define RANGES_MAX 8

int policy_all_ro;
int policy_install; /* « install = yes » dans nvme.txt : la clé d'installation seulement (local.tar) */

/* le plan de l'installateur (BLK_PLAN) : ses plages et celles de la GPT, gardées à part des règles (une table
 * relue ne les efface pas) */
static struct { u64 first, count; } pranges[PLAN_RANGES + 2];
static int npranges;
static int in_plan(u64 first, u64 count);
int policy_guard_test;
const char *policy_disk = "nvme0";

struct cond {
	int has_label, has_guid, has_type, has_disk;
	long part;
	char label[16];
	u8 guid[16], type[16];
	char disk[16];
};

struct rule {
	int write;
	char alias[16];
	struct cond c;
	char text[112];
	int matches;
	int which; /* rang de la partition désignée */
};

struct range {
	u64 first, count;
};

static struct rule rules[RULES_MAX];
static int nrules;
static struct range ranges[RANGES_MAX];
static int nranges;
static u64 denied;

/* ---- GUID : texte « EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 » <-> octets du disque
 * (trois premiers champs en petit-boutiste) */

static int hexv(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int guid_parse(const char *s, u8 out[16])
{
	u8 b[16];
	int n = 0;
	if (*s == '{')
		s++;
	for (; *s && *s != '}' && n < 32; s++) {
		if (*s == '-')
			continue;
		int h = hexv(*s);
		if (h < 0)
			return -1;
		if (n & 1)
			b[n / 2] |= (u8)h;
		else
			b[n / 2] = (u8)(h << 4);
		n++;
	}
	if (n != 32)
		return -1;
	/* champs 1-3 renversés */
	out[0] = b[3], out[1] = b[2], out[2] = b[1], out[3] = b[0];
	out[4] = b[5], out[5] = b[4], out[6] = b[7], out[7] = b[6];
	memcpy(out + 8, b + 8, 8);
	return 0;
}

static void guid_text(const u8 g[16], char *o)
{
	snprintf(o, 40, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", g[3], g[2], g[1], g[0],
	         g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

struct known {
	const char *key, *guid, *text;
	int deny; /* jamais en écriture */
};

static const struct known types[] = {
	{"efi", "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "partition système EFI", 1},
	{"msr", "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "réservée Microsoft (MSR)", 1},
	{"basic", "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "données de base (Basic data)", 0},
	{"recovery", "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", "récupération de Windows", 1},
	{"ldm", "5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", "métadonnées LDM", 1},
	{"ldmdata", "AF9B60A0-1431-4F62-BC68-3311714A69AD", "données LDM", 1},
	{"spaces", "E75CAF8F-F680-4CEE-AFA3-B001E56EFC2D", "espaces de stockage", 1},
	{"linux", "0FC63DAF-8483-4772-8E79-3D69D8477DE4", "données Linux", 0},
	{0, 0, 0, 0},
};

static const struct known *type_of(const u8 g[16])
{
	for (int i = 0; types[i].key; i++) {
		u8 t[16];
		guid_parse(types[i].guid, t);
		if (!memcmp(t, g, 16))
			return &types[i];
	}
	return 0;
}

/* ---- règles */

static void upper_trim(char *d, const char *s, int max)
{
	while (*s == ' ' || *s == '\t')
		s++;
	int n = 0;
	for (; *s && n < max - 1; s++)
		d[n++] = (*s >= 'a' && *s <= 'z') ? (char)(*s - 32) : *s;
	while (n && (d[n - 1] == ' ' || d[n - 1] == '\t'))
		n--;
	d[n] = 0;
}

/* « clé=valeur » -> 0, < 0 */
static int cond_add(struct cond *c, char *item, const char *src)
{
	while (*item == ' ' || *item == '\t')
		item++;
	char *eq = strchr(item, '=');
	if (!eq) {
		nv_log("règles (%s) : « %s » sans « = » : règle ignorée\n", src, item);
		return E_INVAL;
	}
	*eq = 0;
	char key[16];
	upper_trim(key, item, sizeof key);
	char *v = eq + 1;
	while (*v == ' ' || *v == '\t')
		v++;
	char val[64];
	upper_trim(val, v, sizeof val);
	if (!strcmp(key, "LABEL")) {
		if (strlen(val) > 11 || !val[0]) {
			nv_log("règles (%s) : label « %s » : 1 à 11 caractères\n", src, val);
			return E_INVAL;
		}
		c->has_label = 1;
		strlcpy(c->label, val, sizeof c->label);
	} else if (!strcmp(key, "GUID")) {
		if (guid_parse(val, c->guid) < 0) {
			nv_log("règles (%s) : guid « %s » illisible\n", src, val);
			return E_INVAL;
		}
		c->has_guid = 1;
	} else if (!strcmp(key, "TYPE")) {
		int found = 0;
		for (int i = 0; types[i].key && !found; i++) {
			char k[16];
			upper_trim(k, types[i].key, sizeof k);
			if (!strcmp(k, val)) {
				guid_parse(types[i].guid, c->type);
				found = 1;
			}
		}
		if (!found && guid_parse(val, c->type) < 0) {
			nv_log("règles (%s) : type « %s » inconnu\n", src, val);
			return E_INVAL;
		}
		c->has_type = 1;
	} else if (!strcmp(key, "PART")) {
		c->part = (long)parse_dec(val);
		if (c->part <= 0) {
			nv_log("règles (%s) : part « %s » illisible\n", src, val);
			return E_INVAL;
		}
	} else if (!strcmp(key, "DISK")) {
		c->has_disk = 1;
		for (int i = 0; val[i]; i++) /* (les noms sont en minuscules) */
			if (val[i] >= 'A' && val[i] <= 'Z')
				val[i] = (char)(val[i] + 32);
		strlcpy(c->disk, val, sizeof c->disk);
	} else {
		nv_log("règles (%s) : condition « %s » inconnue\n", src, key);
		return E_INVAL;
	}
	return 0;
}

int policy_line(const char *line, const char *src)
{
	char b[160];
	strlcpy(b, line, sizeof b);
	char *h = strchr(b, '#');
	if (h)
		*h = 0;
	char *eq = strchr(b, '=');
	char key[16];
	if (!eq) {
		upper_trim(key, b, sizeof key);
		if (key[0])
			nv_log("règles (%s) : ligne « %s » ignorée\n", src, b);
		return key[0] ? E_INVAL : 0;
	}
	*eq = 0;
	upper_trim(key, b, sizeof key);
	if (!strcmp(key, "INSTALL")) { /* « install = yes » : la clé d'installation (local.tar), le plan de BLK_PLAN */
		char v[8];
		upper_trim(v, eq + 1, sizeof v);
		policy_install = !strcmp(v, "YES");
		if (policy_install)
			nv_log("règles (%s) : mode installation : tout en lecture seule, sauf le plan validé (BLK_PLAN)\n", src);
		return 0;
	}
	int write = !strcmp(key, "WRITE");
	if (!write && strcmp(key, "NAME")) {
		nv_log("règles (%s) : clé « %s » inconnue (write, name)\n", src, key);
		return E_INVAL;
	}
	if (nrules >= RULES_MAX) {
		nv_log("règles (%s) : plus de %d règles\n", src, RULES_MAX);
		return E_NOMEM;
	}
	struct rule *r = &rules[nrules];
	memset(r, 0, sizeof *r);
	r->write = write;
	char *s = eq + 1;
	while (*s == ' ' || *s == '\t')
		s++;
	snprintf(r->text, sizeof r->text, "%s = %s", write ? "write" : "name", s);
	int k = (int)strlen(r->text);
	while (k && (r->text[k - 1] == ' ' || r->text[k - 1] == '\r' || r->text[k - 1] == '\t'))
		r->text[--k] = 0;
	int first = 1, nconds = 0;
	while (s && *s) {
		char *comma = strchr(s, ',');
		if (comma)
			*comma = 0;
		char item[96];
		strlcpy(item, s, sizeof item);
		s = comma ? comma + 1 : 0;
		if (!write && first) { /* name = ALIAS, … */
			first = 0;
			char a[16];
			int n = 0;
			for (const char *p = item; *p && n < 15; p++)
				if (*p != ' ' && *p != '\t' && *p != '\r')
					a[n++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
			a[n] = 0;
			if (!n || strchr(a, '=')) {
				nv_log("règles (%s) : « %s » : le nom vient d'abord (name = korin, label=…)\n", src, r->text);
				return E_INVAL;
			}
			strlcpy(r->alias, a, sizeof r->alias);
			continue;
		}
		first = 0;
		int blank = 1;
		for (const char *p = item; *p; p++)
			blank &= *p == ' ' || *p == '\t' || *p == '\r';
		if (blank)
			continue;
		if (cond_add(&r->c, item, src) < 0)
			return E_INVAL;
		nconds++;
	}
	if (!nconds) {
		nv_log("règles (%s) : « %s » sans condition : ignorée\n", src, r->text);
		return E_INVAL;
	}
	nrules++;
	nv_log("règle %d (%s) : %s\n", nrules, src, r->text);
	return 0;
}

void policy_text(char *text, const char *src)
{
	char *l = text;
	while (*l) {
		char *e = l;
		while (*e && *e != '\n' && *e != '\r')
			e++;
		char c = *e;
		*e = 0;
		policy_line(l, src);
		l = c ? e + 1 : e;
	}
}

/* ---- ce que contient une partition */

enum { FS_NONE, FS_FAT12, FS_FAT16, FS_FAT32, FS_EXFAT, FS_NTFS, FS_BITLOCKER, FS_REFS, FS_UNKNOWN, FS_EXT };
static const char *fs_names[] = {"vide", "FAT12", "FAT16", "FAT32", "exFAT", "NTFS", "BitLocker", "ReFS", "inconnu",
				 "ext2/3/4"};


struct content {
	int fs;
	char label_bpb[12], label_root[12];
};

static u8 sbuf[8 * 512] __attribute__((aligned(64)));

static u32 rd32le(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static void label_copy(char *d, const u8 *s)
{
	memcpy(d, s, 11);
	d[11] = 0;
	for (int i = 10; i >= 0 && d[i] == ' '; i--)
		d[i] = 0;
	for (int i = 0; d[i]; i++)
		if (d[i] >= 'a' && d[i] <= 'z')
			d[i] = (char)(d[i] - 32);
}

/* le nom du volume dans un répertoire FAT (entrée d'attribut 0x08) */
static void fat_root_label(const struct blk_part *p, u64 sec, unsigned nsec, struct content *c)
{
	if (nsec > 8)
		nsec = 8;
	if (sec + nsec > p->count || nv_read_raw(p->first + sec, nsec, sbuf) < 0)
		return;
	for (unsigned i = 0; i < nsec * 16; i++) {
		const u8 *e = sbuf + 32 * i;
		if (!e[0])
			return;
		if (e[0] == 0xE5 || (e[11] & 0x0F) == 0x0F)
			continue;
		if ((e[11] & 0x18) == 0x08) {
			label_copy(c->label_root, e);
			return;
		}
	}
}

static void exfat_label(const struct blk_part *p, const u8 *bs, struct content *c)
{
	unsigned bps = bs[108], spc = bs[109];
	if (bps != 9 || spc > 16)
		return;
	u32 heap = rd32le(bs + 88), root = rd32le(bs + 96);
	if (root < 2)
		return;
	u64 sec = heap + ((u64)(root - 2) << spc);
	unsigned n = 1u << spc;
	if (n > 8)
		n = 8;
	if (sec + n > p->count || nv_read_raw(p->first + sec, n, sbuf) < 0)
		return;
	for (unsigned i = 0; i < n * 16; i++) {
		const u8 *e = sbuf + 32 * i;
		if (!e[0])
			return;
		if (e[0] == 0x83) { /* nom du volume : UTF-16, 11 caractères au plus */
			unsigned len = e[1] > 11 ? 11 : e[1];
			int k = 0;
			for (unsigned j = 0; j < len; j++) {
				u16 ch = (u16)(e[2 + 2 * j] | e[3 + 2 * j] << 8);
				c->label_root[k++] = ch < 0x80 ? (char)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch) : '?';
			}
			c->label_root[k] = 0;
			return;
		}
	}
}

/* le superbloc ext (octet 1024 de la partition : son 3e secteur), magique 0xEF53 en 0x38 */
static int is_ext(const struct blk_part *p)
{
	if (p->count < 4 || nv_read_raw(p->first + 2, 1, sbuf) < 0)
		return 0;
	return sbuf[0x38] == 0x53 && sbuf[0x39] == 0xEF;
}

static void probe(const struct blk_part *p, struct content *c)
{
	memset(c, 0, sizeof *c);
	if (nv_read_raw(p->first, 1, sbuf) < 0) {
		c->fs = FS_UNKNOWN;
		return;
	}
	u8 bs[512];
	memcpy(bs, sbuf, 512);
	int blank = 1;
	for (int i = 0; i < 512; i++)
		blank &= bs[i] == 0;
	if (blank) {
		c->fs = is_ext(p) ? FS_EXT : FS_NONE;
		return;
	}
	if (!memcmp(bs + 3, "NTFS    ", 8)) {
		c->fs = FS_NTFS;
		return;
	}
	if (!memcmp(bs + 3, "-FVE-FS-", 8)) {
		c->fs = FS_BITLOCKER;
		return;
	}
	if (!memcmp(bs + 3, "ReFS", 4)) {
		c->fs = FS_REFS;
		return;
	}
	if (!memcmp(bs + 3, "EXFAT   ", 8)) {
		c->fs = FS_EXFAT;
		exfat_label(p, bs, c);
		return;
	}
	unsigned bps = bs[11] | bs[12] << 8, spc = bs[13], rsv = bs[14] | bs[15] << 8, nf = bs[16];
	int fat_like = bs[510] == 0x55 && bs[511] == 0xAA && bps == 512 && spc && !(spc & (spc - 1)) && rsv && nf &&
	               nf <= 2;
	if (!fat_like) {
		c->fs = is_ext(p) ? FS_EXT : FS_UNKNOWN;
		return;
	}
	unsigned fat16sz = bs[22] | bs[23] << 8;
	if (!fat16sz && !memcmp(bs + 82, "FAT32", 5)) {
		c->fs = FS_FAT32;
		label_copy(c->label_bpb, bs + 71);
		u32 fatsz = rd32le(bs + 36), root = rd32le(bs + 44);
		if (root >= 2)
			fat_root_label(p, rsv + (u64)nf * fatsz + (u64)(root - 2) * spc, spc, c);
		return;
	}
	if (fat16sz && !memcmp(bs + 54, "FAT1", 4)) {
		c->fs = bs[58] == '2' ? FS_FAT12 : FS_FAT16;
		label_copy(c->label_bpb, bs + 43);
		unsigned rootn = bs[17] | bs[18] << 8;
		fat_root_label(p, rsv + (u64)nf * fat16sz, (rootn * 32 + 511) / 512, c);
		return;
	}
	c->fs = FS_UNKNOWN;
}

/* ---- décision */

static int matches(const struct cond *k, const struct blk_part *p, const struct content *c)
{
	if (k->has_disk && strcmp(k->disk, policy_disk))
		return 0;
	if (k->part && k->part != p->num)
		return 0;
	if (k->has_guid && (!p->gpt || memcmp(k->guid, p->uniq_guid, 16)))
		return 0;
	if (k->has_type && (!p->gpt || memcmp(k->type, p->type_guid, 16)))
		return 0;
	if (k->has_label) {
		int fat = c->fs == FS_FAT12 || c->fs == FS_FAT16 || c->fs == FS_FAT32 || c->fs == FS_EXFAT;
		if (!fat || (strcmp(k->label, c->label_bpb) && strcmp(k->label, c->label_root)))
			return 0;
	}
	return 1;
}

/* les refus que rien ne lève -> raison, ou 0 */
static const char *hard_deny(const struct blk_part *t, int n, int i, const struct blk_gpt *g,
                             const struct content *c, int efi_ok)
{
	const struct blk_part *p = &t[i];
	if (g->gpt && !g->crc_ok)
		return "CRC de la GPT faux";
	if (p->gpt) {
		const struct known *k = type_of(p->type_guid);
		static u8 efi[16];
		if (!efi[0])
			guid_parse("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", efi);
		if (k && k->deny && !(efi_ok && !memcmp(p->type_guid, efi, 16)))
			return k->text;
		if (p->attrs & 1)
			return "attribut « requise par la plateforme »";
		if (p->attrs & (1ull << 60))
			return "attribut « lecture seule »";
		if (p->first < g->first_usable || p->first + p->count - 1 > g->last_usable)
			return "hors de la zone utile de la GPT";
	} else if (p->type == 0xEF || p->type == 0x27 || p->type == 0xEE)
		return "type MBR protégé (EFI, récupération, GPT)";
	if (c->fs == FS_NTFS || c->fs == FS_BITLOCKER || c->fs == FS_REFS)
		return fs_names[c->fs];
	if (c->fs != FS_FAT12 && c->fs != FS_FAT16 && c->fs != FS_FAT32 && c->fs != FS_EXFAT && c->fs != FS_EXT)
		return "contenu ni FAT, ni exFAT, ni ext";
	if (p->first < 1 || (g->gpt && p->first <= g->entries_lba + (g->entries * g->entry_size + 511) / 512))
		return "sur la table de partitions";
	for (int j = 0; j < n; j++)
		if (j != i && t[j].first < p->first + p->count && p->first < t[j].first + t[j].count)
			return "chevauche une autre partition";
	return 0;
}

void policy_table(struct blk_part *t, int n, const struct blk_gpt *g)
{
	static struct content cs[BLK_GPT_MAX > 12 ? BLK_GPT_MAX : 12];
	if (g->gpt)
		nv_log("GPT : %u entrées de %u octets au secteur %lu, zone utile %lu à %lu, CRC %s\n", g->entries,
		       g->entry_size, (unsigned long)g->entries_lba, (unsigned long)g->first_usable,
		       (unsigned long)g->last_usable, g->crc_ok ? "juste" : "FAUX");
	else
		nv_log("pas de GPT (%d partition(s) MBR)\n", n);
	for (int i = 0; i < n; i++) {
		struct blk_part *p = &t[i];
		struct content *c = &cs[i];
		p->ro = 1;
		probe(p, c);
		char gt[40] = "", gu[40] = "", nm[40];
		int k = 0;
		for (int j = 0; j < 36 && p->name[j] && k < 39; j++)
			nm[k++] = p->name[j] < 0x80 ? (char)p->name[j] : '?';
		nm[k] = 0;
		const struct known *kt = p->gpt ? type_of(p->type_guid) : 0;
		char ty[64];
		if (p->gpt) {
			guid_text(p->type_guid, gt);
			guid_text(p->uniq_guid, gu);
			snprintf(ty, sizeof ty, "%s%s", kt ? kt->text : "type ", kt ? "" : gt);
		} else
			snprintf(ty, sizeof ty, "type MBR %02x", p->type);
		char vol[32] = "";
		const char *lb = c->label_root[0] ? c->label_root : c->label_bpb;
		if (lb[0])
			snprintf(vol, sizeof vol, ", volume « %s »", lb);
		nv_log("p%ld : secteurs %lu à %lu (%lu Mo), %s, « %s », %s%s\n", p->num, (unsigned long)p->first,
		       (unsigned long)(p->first + p->count - 1), (unsigned long)(p->count >> 11), ty, nm, fs_names[c->fs],
		       vol);
		if (p->gpt)
			nv_log("p%ld : GUID %s, attributs %016lx\n", p->num, gu, (unsigned long)p->attrs);
	}
	for (int r = 0; r < nrules; r++) {
		struct rule *u = &rules[r];
		u->matches = 0;
		for (int i = 0; i < n; i++)
			if (matches(&u->c, &t[i], &cs[i])) {
				u->matches++;
				u->which = i;
			}
		if (u->matches == 0) {
			nv_log("règle %d : aucune partition ne la remplit\n", r + 1);
			continue;
		}
		if (u->matches > 1) {
			nv_log("règle %d : %d partitions la remplissent : AMBIGU, aucune n'est ouverte\n", r + 1, u->matches);
			continue;
		}
		struct blk_part *p = &t[u->which];
		if (!u->write) {
			strlcpy(p->alias, u->alias, sizeof p->alias);
			nv_log("règle %d : p%ld aussi servie en blk.%s\n", r + 1, p->num, u->alias);
			continue;
		}
		static u8 efi_t[16];
		if (!efi_t[0])
			guid_parse("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", efi_t);
		int efi_ok = u->c.has_guid && u->c.has_type && !memcmp(u->c.type, efi_t, 16);
		const char *why = hard_deny(t, n, u->which, g, &cs[u->which], efi_ok);
		if (why) {
			nv_log("règle %d : p%ld REFUSÉE en écriture (%s) : lecture seule\n", r + 1, p->num, why);
			continue;
		}
		if (policy_all_ro) {
			nv_log("règle %d : p%ld serait permise en écriture, mais -r : lecture seule\n", r + 1, p->num);
			continue;
		}
		if (p->ro && nranges < RANGES_MAX) {
			p->ro = 0;
			ranges[nranges].first = p->first;
			ranges[nranges].count = p->count;
			nranges++;
		}
		nv_log("règle %d : p%ld en lecture et écriture\n", r + 1, p->num);
	}
	for (int i = 0; i < n && npranges; i++) /* (le plan de l'installateur : ses nouvelles partitions) */
		if (t[i].ro && in_plan(t[i].first, t[i].count)) {
			t[i].ro = 0;
			nv_log("plan : p%ld en lecture et écriture\n", t[i].num);
		}
	int open = 0;
	for (int i = 0; i < n; i++)
		open += !t[i].ro;
	nv_log("écriture permise sur %d partition(s) ; tout le reste en lecture seule\n", open);
	if (policy_guard_test) { /* (-G, contrôleur de QEMU seulement : nvme.c le vérifie) */
		nv_log("ESSAI DE LA GARDE (-G) : toutes les partitions ouvertes au service blk, la garde seule refuse\n");
		for (int i = 0; i < n; i++)
			t[i].ro = 0;
	}
	int e = policy_selftest();
	if (e) { /* (ne devrait jamais arriver : alors plus rien ne s'écrit) */
		nv_log("GARDE : %d erreur(s) à l'essai : TOUT EN LECTURE SEULE\n", e);
		policy_all_ro = 1;
		nranges = 0;
		for (int i = 0; i < n; i++)
			t[i].ro = 1;
	}
}

int policy_write_ok(u64 lba, u64 n)
{
	if (policy_all_ro || !n || lba + n < lba)
		return 0;
	for (int i = 0; i < nranges; i++)
		if (lba >= ranges[i].first && lba + n <= ranges[i].first + ranges[i].count)
			return 1;
	for (int i = 0; i < npranges; i++) /* (le plan de l'installateur) */
		if (lba >= pranges[i].first && lba + n <= pranges[i].first + pranges[i].count)
			return 1;
	return 0;
}

/* ---- le plan de l'installateur (docs/etude/INSTALLATEUR.md §8) */

static long plan_rd(void *ctx, uint64_t s, uint64_t n, void *b)
{
	(void)ctx;
	return nv_read_raw(s, n, b);
}

static int protected_type(const struct gpt_entry *e)
{
	return !memcmp(e->type, GPT_T_EFI, 16) || !memcmp(e->type, GPT_T_MSR, 16) || !memcmp(e->type, GPT_T_RECOVERY, 16);
}

long policy_plan(struct msg *m)
{
	static struct gpt g;
	u64 flags = (u64)m->a, crc = (u64)m->b, n = (u64)m->c;
	if (!policy_install) {
		nv_log("plan refusé : pas de mode installation (install = yes : la clé seulement)\n");
		return E_PERM;
	}
	if (blk_boot_part()) {
		nv_log("plan refusé : ce disque porte le système démarré (blk.boot)\n");
		return E_PERM;
	}
	if (korin_sys1(SYS_PROC_UID, (long)m->pid) != 0) {
		nv_log("plan refusé : le demandeur (pid %u) n'est pas l'uid 0 (élévation)\n", m->pid);
		return E_PERM;
	}
	if (npranges) {
		nv_log("plan refusé : un plan est déjà ouvert\n");
		return E_BUSY;
	}
	if (n == 0 || n > PLAN_RANGES || m->len < n * 16) {
		nv_log("plan refusé : %lu plage(s)\n", (unsigned long)n);
		return E_INVAL;
	}
	long r = gpt_read(&g, plan_rd, 0, nv_sectors);
	if (r == 0 && !g.from_backup) {
		if ((u32)crc != g.header_crc) {
			nv_log("plan refusé : la table a changé depuis le plan (somme %08x, le plan dit %08x)\n", g.header_crc, (u32)crc);
			return E_INVAL;
		}
	} else if (crc != 0 || !(flags & PLAN_WIPE)) {
		nv_log("plan refusé : pas de GPT lisible, et le plan n'efface pas le disque entier\n");
		return E_INVAL;
	}
	const u64 *d = (const u64 *)m->data;
	u64 last_usable = nv_sectors - 34;
	for (u64 i = 0; i < n; i++) {
		u64 a = d[2 * i], b = d[2 * i + 1];
		if (a < 34 || b > last_usable || a > b) {
			nv_log("plan refusé : plage %lu..%lu hors de la zone utile\n", (unsigned long)a, (unsigned long)b);
			return E_INVAL;
		}
		if (flags & PLAN_WIPE || r != 0)
			continue;
		for (int k = 0; k < GPT_ENTRIES; k++) { /* ni EFI, ni MSR, ni récupération : jamais, sauf disque entier */
			const struct gpt_entry *e = &g.e[k];
			if (gpt_used(e) && protected_type(e) && a <= e->last && b >= e->first) {
				nv_log("plan refusé : la plage %lu..%lu touche p%d (EFI, MSR ou récupération)\n", (unsigned long)a,
				       (unsigned long)b, k + 1);
				return E_INVAL;
			}
		}
	}
	/* accepté : la GPT (LBA 0 à 33, et les 33 derniers secteurs), puis les plages */
	pranges[0].first = 0;
	pranges[0].count = 34;
	pranges[1].first = nv_sectors - 33;
	pranges[1].count = 33;
	npranges = 2;
	for (u64 i = 0; i < n; i++) {
		pranges[npranges].first = d[2 * i];
		pranges[npranges].count = d[2 * i + 1] - d[2 * i] + 1;
		nv_log("plan : %lu..%lu ouvert en écriture\n", (unsigned long)d[2 * i], (unsigned long)d[2 * i + 1]);
		npranges++;
	}
	nv_log("plan accepté (%s) : %lu plage(s) et la GPT ; tout le reste en lecture seule\n",
	       flags & PLAN_WIPE ? "disque entier" : "à côté de l'existant", (unsigned long)n);
	return 0;
}

/* une partition entièrement dans une plage du plan : écrivable (mkfs, la copie) */
static int in_plan(u64 first, u64 count)
{
	for (int i = 2; i < npranges; i++)
		if (first >= pranges[i].first && first + count <= pranges[i].first + pranges[i].count)
			return 1;
	return 0;
}

void policy_denied(const char *dev, u64 sector, u64 count, u32 pid)
{
	denied++;
	if (denied <= 20 || denied % 1000 == 0)
		nv_log("écriture REFUSÉE : %s, secteurs %lu à %lu, pid %u (lecture seule ; %lu refus en tout)\n", dev,
		       (unsigned long)sector, (unsigned long)(sector + count - 1), pid, (unsigned long)denied);
}

/* la garde essayée sur des plages qu'elle doit refuser (et sur celles qu'elle doit
 * laisser passer) : aucune écriture n'est faite */
int policy_selftest(void)
{
	int err = 0, tries = 0;
	u64 last = nv_sectors ? nv_sectors - 1 : 0;
	const u64 must_deny[][2] = {{0, 1}, {1, 1}, {2, 32}, {last, 1}, {last - 32, 33}, {0, nv_sectors}};
	for (unsigned i = 0; i < sizeof must_deny / sizeof *must_deny; i++) {
		int inside = 0; /* (une plage tout entière dans une partition permise : pas un refus attendu) */
		for (int k = 0; k < nranges; k++)
			inside |= must_deny[i][0] >= ranges[k].first &&
			          must_deny[i][0] + must_deny[i][1] <= ranges[k].first + ranges[k].count;
		for (int k = 0; k < npranges; k++) /* (la GPT ouverte par le plan validé de l'installateur) */
			inside |= must_deny[i][0] >= pranges[k].first &&
			          must_deny[i][0] + must_deny[i][1] <= pranges[k].first + pranges[k].count;
		if (inside)
			continue;
		tries++;
		if (policy_write_ok(must_deny[i][0], must_deny[i][1])) {
			nv_log("GARDE : %lu +%lu acceptée à tort\n", (unsigned long)must_deny[i][0], (unsigned long)must_deny[i][1]);
			err++;
		}
	}
	for (int k = 0; k < nranges; k++) {
		u64 f = ranges[k].first, e = f + ranges[k].count; /* [f, e) */
		const u64 deny[][2] = {{f - 1, 1}, {f - 1, 2}, {e - 1, 2}, {e, 1}, {f - 8, ranges[k].count + 16}};
		for (unsigned i = 0; i < sizeof deny / sizeof *deny; i++) {
			int inside = 0;
			for (int j = 0; j < nranges; j++)
				inside |= j != k && deny[i][0] >= ranges[j].first &&
				          deny[i][0] + deny[i][1] <= ranges[j].first + ranges[j].count;
			if (inside)
				continue;
			tries++;
			if (policy_write_ok(deny[i][0], deny[i][1])) {
				nv_log("GARDE : %lu +%lu (bord de la partition permise) acceptée à tort\n", (unsigned long)deny[i][0],
				       (unsigned long)deny[i][1]);
				err++;
			}
		}
		tries += 3;
		if (!policy_write_ok(f, 1) || !policy_write_ok(e - 1, 1) || !policy_write_ok(f, ranges[k].count)) {
			nv_log("GARDE : la partition permise %lu +%lu est refusée\n", (unsigned long)f,
			       (unsigned long)ranges[k].count);
			err++;
		}
	}
	nv_log("garde : %d essais sans écrire, %d erreur(s)\n", tries, err);
	return err;
}
