/* fat32.c : système de fichiers FAT32 (lecture, écriture, noms longs),
 * traduction de fat32.asm (réimplémentation d'après la spécification Microsoft
 * FAT, fatgen103). Noms : UTF-8 côté OS <-> UTF-16 (LFN) sur disque ;
 * comparaison insensible à la casse pour l'ASCII ; les entrées 8.3 sont
 * générées en NOM~n.EXT. Les secteurs passent par bc_get / bc_write (fatfs.c). */
#include "fatfs.h"

static u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* ---- montage */

static void fat_fsinfo_load(struct vol *v);
static void fat_agrandir(struct vol *v);

long fat_mount(struct vol *v, u64 first)
{
	v->pstart = first;
	u8 *b = bc_get(first);
	if (!b || rd16(b + 510) != 0xAA55)
		return 0;
	if ((b[11] | b[12] << 8) != 512) /* octets par secteur */
		return 0;
	u32 spc = b[13];
	if (!spc)
		return 0;
	v->spc = spc;
	v->cshift = 31 - __builtin_clz(spc) + 9;
	u64 fat = first + rd16(b + 14); /* secteurs réservés */
	v->fat = fat;
	v->nfats = b[16];
	u64 fatsz = rd32(b + 36);
	if (!fatsz) /* 0 : FAT12/16, non géré */
		return 0;
	v->fatsz = fatsz;
	v->data = v->nfats * fatsz + fat;
	v->root = rd32(b + 44);
	u64 total = rd32(b + 32);
	v->nclust = (total - (v->data - first)) / v->spc;
	v->hint = 2;
	fat_fsinfo_load(v);
	v->type = 1;
	fat_agrandir(v);
	return 1;
}

static u8 *fat_fsinfo_get(u64 sector)
{
	u8 *b = bc_get(sector);
	if (!b)
		return 0;
	if (rd32(b) != 0x41615252 || rd32(b + 484) != 0x61417272)
		return 0;
	return b;
}

/* la partition est plus grande que le système de fichiers (Setup l'a étendue) :
 * il grandit jusqu'à elle, dans la limite de sa FAT ; secteurs totaux du BPB et
 * de sa copie, clusters libres de FSInfo */
static void fat_agrandir(struct vol *v)
{
	if (lecture_seule)
		return;
	u64 part = cc_disk; /* secteurs de la partition */
	if (!part || part == (u64)-1)
		return;
	u64 d0 = v->data - v->pstart; /* début des données (relatif) */
	if (part <= d0)
		return;
	u64 n = (part - d0) / v->spc; /* clusters que la partition tient */
	u64 fatmax = (v->fatsz << 7) - 2; /* ceux que la FAT décrit */
	if (n > fatmax)
		n = fatmax;
	if (n <= v->nclust)
		return;
	u64 more = n - v->nclust;
	v->nclust = n;
	u64 total = n * v->spc + d0;
	u64 bs = v->pstart; /* le BPB, puis sa copie (secteur 6) */
	u8 *b = bc_get(bs);
	if (!b)
		return;
	wr32(b + 32, (u32)total);
	bc_write(bs);
	b = bc_get(bs + 6);
	if (b && rd16(b + 510) == 0xAA55) {
		wr32(b + 32, (u32)total);
		bc_write(bs + 6);
	}
	if (v->free == -1) /* (inconnu : fatfs les comptera) */
		return;
	v->free += more;
	v->fsi_dirty = 1;
	fat_fsinfo_flush(v);
}

static void fat_fsinfo_load(struct vol *v)
{
	v->free = -1;
	v->fsinfo = 0;
	v->fsi_dirty = 0;
	u8 *b = bc_get(v->pstart); /* secteur de démarrage : FSInfo (+48) */
	if (!b)
		return;
	u32 rel = rd16(b + 48);
	if (!rel)
		return;
	u64 sec = v->pstart + rel;
	b = fat_fsinfo_get(sec);
	if (!b)
		return;
	v->fsinfo = sec;
	u32 fr = rd32(b + 488); /* 0xFFFFFFFF : inconnu (garde -1) */
	if (fr > v->nclust)
		return;
	v->free = fr;
}

long fat_fsinfo_flush(struct vol *v)
{
	if (v->type != 1 || !v->fsi_dirty)
		return 0;
	v->fsi_dirty = 0;
	if (!v->fsinfo)
		return 0;
	u8 *b = fat_fsinfo_get(v->fsinfo);
	if (!b)
		return 0;
	wr32(b + 488, (u32)v->free);
	wr32(b + 492, (u32)v->hint);
	bc_write(v->fsinfo);
	return 0;
}

/* ---- table d'allocation */

void fat_clus_lba(struct vol *v, u64 cluster, u64 *sector, u64 *cshift)
{
	*sector = (cluster - 2) * v->spc + v->data;
	if (cshift)
		*cshift = v->cshift;
}

static u64 clus_lba(struct vol *v, u64 cluster)
{
	u64 s;
	fat_clus_lba(v, cluster, &s, 0);
	return s;
}

/* -> cluster suivant (>= FAT_EOC : fin, 0 : erreur) */
u64 fat_next(struct vol *v, u64 cluster)
{
	u8 *b = bc_get(v->fat + (cluster >> 7)); /* 128 entrées par secteur */
	if (!b)
		return 0;
	u32 n = rd32(b + (cluster & 127) * 4) & FAT_MASK;
	return n >= 2 ? n : FAT_EOC; /* libre/réservé dans une chaîne : fin */
}

/* écrit dans toutes les FAT */
static void fat_set(struct vol *v, u64 cluster, u32 val)
{
	for (u64 i = 0; i < v->nfats; i++) {
		u64 sec = v->fat + v->fatsz * i + (cluster >> 7);
		u8 *b = bc_get(sec);
		if (!b)
			return;
		u8 *e = b + (cluster & 127) * 4;
		wr32(e, (rd32(e) & 0xF0000000u) | val); /* les 4 bits hauts sont réservés */
		bc_write(sec);
	}
}

/* prev = cluster précédent (0 : début de chaîne), whole = 1 : pas de zéros (le
 * cluster va être écrit en entier) -> nouveau cluster, fin de chaîne (0 : plein) */
u64 fat_alloc_x(struct vol *v, u64 prev, long whole)
{
	u64 c = v->hint;
	u64 end = v->nclust + 2; /* dernier cluster + 1 */
	for (u64 tries = end;; c++) {
		if (!tries)
			return 0;
		tries--;
		if (c >= end)
			c = 2;
		u8 *b = bc_get(v->fat + (c >> 7));
		if (!b)
			return 0;
		if (!(rd32(b + (c & 127) * 4) & FAT_MASK))
			break;
	}
	fat_set(v, c, FAT_MASK); /* fin de chaîne */
	if (prev)
		fat_set(v, prev, (u32)c);
	if (!whole)
		blk_zero(clus_lba(v, c), v->spc); /* contenu à zéro */
	v->hint = c + 1;
	if (v->free != -1) { /* un libre de moins (s'il est connu) */
		v->free--;
		v->fsi_dirty = 1;
	}
	return c;
}

static u64 fat_alloc(struct vol *v, u64 prev)
{
	return fat_alloc_x(v, prev, 0);
}

/* libère toute la chaîne (chaque secteur de FAT écrit une fois, à la fin) ;
 * une poignée exFAT (bit 62) est ignorée */
void fat_free_chain(struct vol *v, u64 c)
{
	bc_defer_on();
	while (c >= 2 && c < FAT_EOC) {
		u64 next = fat_next(v, c);
		fat_set(v, c, 0);
		if (v->free != -1) { /* un libre de plus (s'il est connu) */
			v->free++;
			v->fsi_dirty = 1;
		}
		if (c < v->hint) /* le cluster libéré redevient candidat */
			v->hint = c;
		c = next;
	}
	bc_defer_off();
}

/* ---- répertoires */

void dir_open(struct it *it, struct vol *v, u64 start)
{
	it->vol = v;
	it->start = start;
	it->cluster = start;
	it->index = 0;
}

/* -> l'entrée de 32 o suivante (dans le cache), 0 en fin de chaîne ; *sector : son secteur */
static u8 *dir_next(struct it *it, u64 *sector)
{
	struct vol *v = it->vol;
	u64 idx = it->index;
	u64 mask = ((u64)1 << (v->cshift - 5)) - 1; /* entrées par cluster - 1 */
	if ((idx & mask) == 0 && idx) { /* changement de cluster */
		u64 c = fat_next(v, it->cluster);
		if (c >= FAT_EOC || !c)
			return 0;
		it->cluster = c;
	}
	if (it->cluster < 2)
		return 0;
	u64 sec = clus_lba(v, it->cluster) + ((idx & mask) >> 4); /* 16 entrées par secteur */
	u8 *b = bc_get(sec);
	if (!b)
		return 0;
	*sector = sec;
	it->index = idx + 1;
	return b + (idx & 15) * 32;
}

static u8 sfn_checksum(const u8 *e)
{
	u32 s = 0;
	for (int i = 0; i < 11; i++)
		s = ((((s >> 1) & 0x7F) | (s << 7)) & 0xFF) + e[i], s &= 0xFF;
	return (u8)s;
}

/* nom 8.3 -> « nom.ext » (minuscules si l'octet 12 le demande : 0x08 base, 0x10 extension) */
static void sfn_to_name(const u8 *e, char *d)
{
	u8 lc = e[12];
	for (int i = 0; i < 8; i++) {
		u8 c = e[i];
		if (c == ' ')
			break;
		if ((lc & 8) && c >= 'A' && c <= 'Z')
			c += 32;
		*d++ = (char)c;
	}
	if (e[8] != ' ') {
		*d++ = '.';
		for (int i = 8; i < 11; i++) {
			u8 c = e[i];
			if (c == ' ')
				break;
			if ((lc & 0x10) && c >= 'A' && c <= 'Z')
				c += 32;
			*d++ = (char)c;
		}
	}
	*d = 0;
}

/* destination UTF-8 (NAME_BYTES o), source UTF-16LE, unités au plus ; les paires
 * de substitution -> 4 octets ; s'arrête à 0 ou 0xFFFF */
void utf16_to_utf8(char *d, const u8 *s, u64 max)
{
	char *lim = d + NAME_BYTES - 5; /* garde la place du plus long + 0 */
	while (max && d < lim) {
		u32 u = rd16(s);
		s += 2;
		if (!u || u == 0xFFFF)
			break;
		max--;
		if (u < 0x80) {
			*d++ = (char)u;
		} else if (u < 0x800) {
			*d++ = (char)(0xC0 | u >> 6);
			*d++ = (char)(0x80 | (u & 0x3F));
		} else if ((u & 0xFC00) == 0xD800 && max && (rd16(s) & 0xFC00) == 0xDC00) {
			u32 lo = rd16(s);
			s += 2;
			max--;
			u32 cp = (((u & 0x3FF) << 10) | (lo & 0x3FF)) + 0x10000;
			*d++ = (char)(0xF0 | cp >> 18);
			*d++ = (char)(0x80 | ((cp >> 12) & 0x3F));
			*d++ = (char)(0x80 | ((cp >> 6) & 0x3F));
			*d++ = (char)(0x80 | (cp & 0x3F));
		} else {
			*d++ = (char)(0xE0 | u >> 12);
			*d++ = (char)(0x80 | ((u >> 6) & 0x3F));
			*d++ = (char)(0x80 | (u & 0x3F));
		}
	}
	*d = 0;
}

/* chaîne UTF-8 -> UTF-16 (max unités) -> unités écrites (4 octets UTF-8 : une paire) */
long utf8_to_utf16(const char *str, u16 *d, u64 max)
{
	const u8 *s = (const u8 *)str;
	u64 n = 0;
	while (n < max) {
		u32 c = *s++;
		if (!c)
			break;
		if (c & 0x80) {
			if ((c & 0xE0) == 0xC0) {
				c = (c & 0x1F) << 6 | (*s++ & 0x3F);
			} else if ((c & 0xF0) == 0xF0) {
				if (n + 1 >= max)
					break;
				c &= 7;
				for (int k = 0; k < 3; k++)
					c = c << 6 | (*s++ & 0x3F);
				c -= 0x10000;
				d[n++] = (u16)((c >> 10) + 0xD800);
				d[n++] = (u16)((c & 0x3FF) + 0xDC00);
				continue;
			} else { /* (comme l'asm : tout autre octet de tête compte pour trois) */
				c &= 0x0F;
				c = c << 6 | (*s++ & 0x3F);
				c = c << 6 | (*s++ & 0x3F);
			}
		}
		d[n++] = (u16)c;
	}
	return n;
}

/* lit l'entrée suivante (fichier ou répertoire) -> 1, 0 en fin ; saute « . »,
 * « .. », les entrées supprimées et l'étiquette ; les LFN orphelines sont ignorées */
long dir_read(struct it *it, struct de *de)
{
	u64 lfn = 0; /* somme LFN attendue (+0x100 si LFN valide), séquence en 16..23 */
	u64 sec;
	de->h.dir = it->start;
	for (;;) {
		u64 idx = it->index; /* index de l'entrée lue */
		u8 *e = dir_next(it, &sec);
		if (!e)
			return 0;
		u8 c0 = e[0];
		if (!c0) /* 0 : plus aucune entrée */
			return 0;
		if (c0 == 0xE5) {
			lfn = 0;
			continue;
		}
		u8 at = e[11];
		if (at == ATTR_LFN) {
			u32 seq;
			if (c0 & 0x40) { /* dernière (= première lue) */
				seq = c0 & 0x1F;
				if (!seq || seq > LFN_MAX) { /* entrée orpheline */
					lfn = 0;
					continue;
				}
				lfn = e[13] | 0x100 | (u64)seq << 16;
				de->h.first = idx;
				memset(de->lfn, 0, DE_LFN_SIZE);
			}
			seq = c0 & 0x1F;
			u32 want = (lfn >> 16) & 0xFF;
			if (!seq || seq != want || e[13] != (lfn & 0xFF)) { /* suite cassée */
				lfn = 0;
				continue;
			}
			want--;
			lfn = (lfn & ~(u64)0xFF0000) | (u64)want << 16;
			u8 *z = de->lfn + (seq - 1) * 26; /* 13 caractères UTF-16 par entrée */
			memcpy(z, e + 1, 10);
			memcpy(z + 10, e + 14, 12);
			memcpy(z + 22, e + 28, 4);
			if (!want) /* dernière partie reçue */
				utf16_to_utf8(de->name, de->lfn, LFN_MAX * 13);
			continue;
		}
		if ((at & ATTR_VOLUME) || c0 == '.') {
			lfn = 0;
			continue;
		}
		/* entrée 8.3 */
		de->h.index = idx;
		if (!(lfn & 0x100)) {
			de->h.first = idx;
			sfn_to_name(e, de->name);
		} else if (((lfn >> 16) & 0xFF) || sfn_checksum(e) != (lfn & 0xFF)) {
			de->h.first = idx; /* suite LFN incomplète : nom 8.3 */
			sfn_to_name(e, de->name);
		}
		de->h.attr = e[11];
		de->h.cluster = (u64)rd16(e + 20) << 16 | rd16(e + 26);
		de->h.fsize = rd32(e + 28);
		de->mtime = rd32(e + 22); /* heure, date */
		return 1;
	}
}

/* chaîne 1 (fin : 0 ou « / »), chaîne 2 -> 1 si égales (ASCII insensible à la casse) */
static long name_equal(const char *a, const char *b)
{
	for (;;) {
		u8 c1 = (u8)*a++, c2 = (u8)*b++;
		if (c1 == '/')
			c1 = 0;
		if (c1 >= 'A' && c1 <= 'Z')
			c1 += 32;
		if (c2 >= 'A' && c2 <= 'Z')
			c2 += 32;
		if (c1 != c2)
			return 0;
		if (!c1)
			return 1;
	}
}

static long dir_find(struct vol *v, u64 dir, const char *name, struct de *de)
{
	struct it it;
	dir_open(&it, v, dir);
	while (dir_read(&it, de))
		if (name_equal(name, de->name))
			return 1;
	return 0;
}

/* chemin relatif au volume (« a/b/c ») -> 1 si trouvé ; vide : la racine */
long fat_lookup(struct vol *v, const char *p, struct de *de)
{
	u64 cur = v->root;
	de->h.cluster = cur; /* racine : description synthétique */
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
		if (!(de->h.attr & ATTR_DIR)) /* il faut descendre dans un répertoire */
			return 0;
		if (!dir_find(v, cur, p, de))
			return 0;
		cur = de->h.cluster;
		if (!cur) /* « .. » vers la racine : cluster 0 */
			cur = v->root;
		for (;;) { /* composant suivant */
			char c = *p++;
			if (!c)
				return 1;
			if (c == '/')
				break;
		}
	}
}

/* -> l'entrée (dans le cache), *sector son secteur ; 0 si hors chaîne.
 * Écart voulu avec l'asm : un répertoire sous le cluster 2 (la racine
 * synthétique de fat_lookup) n'a pas d'entrée (l'asm lisait un index non
 * initialisé et pouvait écrire dans la FAT : utime de la racine) */
u8 *dir_entry_at(struct vol *v, u64 dir, u64 index, u64 *sector)
{
	if (dir < 2)
		return 0;
	u64 c = dir;
	for (u64 skip = index >> (v->cshift - 5); skip; skip--) { /* de cluster en cluster */
		c = fat_next(v, c);
		if (c >= FAT_EOC)
			return 0;
	}
	u64 idx = index & (((u64)1 << (v->cshift - 5)) - 1);
	u64 sec = clus_lba(v, c) + (idx >> 4);
	u8 *b = bc_get(sec);
	if (!b)
		return 0;
	*sector = sec;
	return b + (idx & 15) * 32;
}

/* écrit premier cluster, taille, attributs et date dans l'entrée 8.3 */
void fat_update_entry(struct vol *v, struct dehead *de)
{
	u32 t = (u32)dos_maintenant();
	u64 sec;
	u8 *e = dir_entry_at(v, de->dir, de->index, &sec);
	if (!e)
		return;
	wr16(e + 20, (u32)(de->cluster >> 16));
	wr16(e + 26, (u32)de->cluster);
	wr32(e + 28, (u32)de->fsize);
	e[11] = (u8)de->attr;
	wr16(e + 22, t); /* dernière écriture, dernier accès */
	wr16(e + 24, t >> 16);
	wr16(e + 18, t >> 16);
	bc_write(sec);
}

/* marque supprimées l'entrée 8.3 et ses entrées LFN */
void fat_erase_entry(struct vol *v, struct dehead *de)
{
	for (u64 i = de->first; i <= de->index; i++) {
		u64 sec;
		u8 *e = dir_entry_at(v, de->dir, i, &sec);
		if (!e)
			return;
		e[0] = 0xE5;
		bc_write(sec);
	}
}

/* ---- création d'une entrée */

/* caractère -> caractère 8.3 valide en majuscule, 0 à ignorer */
static u8 sfn_char(u8 c)
{
	if (c >= 'a' && c <= 'z')
		return c - 32;
	if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
		return c;
	return 0;
}

/* fabrique « BASE~n  EXT » absent du répertoire -> 1 si ok */
static long fat_make_sfn(struct vol *v, u64 dir, const char *name, u8 *out)
{
	memset(out, ' ', 11);
	const char *ext = 0; /* après le dernier « . » */
	for (const char *p = name; *p;)
		if (*p++ == '.')
			ext = p;
	const char *p = name;
	u64 n = 0;
	while (n < 6) { /* base : 6 caractères valides */
		u8 c = (u8)*p++;
		if (!c || p == ext)
			break;
		c = sfn_char(c);
		if (c)
			out[n++] = c;
	}
	if (!n) {
		out[0] = 'F';
		n = 1;
	}
	u64 tilde = n; /* position du « ~ » */
	if (ext) {
		p = ext;
		for (u64 k = 8; k < 11;) {
			u8 c = (u8)*p++;
			if (!c)
				break;
			c = sfn_char(c);
			if (c)
				out[k++] = c;
		}
	}
	/* numéro ~n (plusieurs chiffres : la base raccourcit, dix fichiers au même
	   début de nom ne doivent pas échouer) */
	for (u64 num = 1; num <= 999999; num++) {
		u64 nd = 0;
		for (u64 x = num; x; x /= 10)
			nd++;
		u64 pos = 7 - nd;
		if (tilde < pos)
			pos = tilde;
		out[pos] = '~';
		u64 x = num;
		for (u64 k = pos + nd; nd; nd--, k--, x /= 10)
			out[k] = (u8)('0' + x % 10);
		struct it it; /* collision ? */
		dir_open(&it, v, dir);
		u64 sec;
		int taken = 0;
		for (;;) {
			u8 *e = dir_next(&it, &sec);
			if (!e || !e[0])
				break;
			if (!memcmp(e, out, 11)) {
				taken = 1;
				break;
			}
		}
		if (!taken)
			return 1;
	}
	return 0;
}

/* -> index de la première entrée d'une suite libre de want entrées (le
 * répertoire est agrandi si besoin), -1 si échec */
static long dir_find_free(struct vol *v, u64 dir, u64 want)
{
	struct it it;
	dir_open(&it, v, dir);
	u64 run = 0, start = 0, sec;
	for (;;) {
		u64 idx = it.index;
		u8 *e = dir_next(&it, &sec);
		if (!e) { /* fin de chaîne : un cluster (vide) de plus */
			if (!fat_alloc(v, it.cluster))
				return -1;
			if (!run) /* (sinon la suite continue dans le nouveau cluster) */
				start = idx;
			return start;
		}
		if (e[0] && e[0] != 0xE5) {
			run = 0;
			continue;
		}
		if (!run)
			start = idx;
		if (++run >= want)
			return start;
	}
}

/* répertoire parent, nom UTF-8 (fin : 0 ou « / »), attributs, DE à remplir
 * -> 1 si créé (fichier vide, ou répertoire avec « . » et « .. ») */
long fat_create(struct vol *v, u64 parent, const char *name, u64 attr, struct de *de)
{
	u16 n16[264]; /* nom UTF-16 (528 o) */
	u8 sfn[11];
	char *o = de->name; /* composant -> DE_NAME (UTF-8) */
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
	if (!de->name[0]) /* nom vide */
		return 0;
	u64 units = utf8_to_utf16(de->name, n16, NAME_UNITS + 1);
	if (units > NAME_UNITS) /* 255 caractères au plus */
		return 0;
	u64 nlfn = (units + 12) / 13; /* entrées LFN */
	n16[units] = 0; /* fin du nom : 0 puis 0xFFFF */
	for (u64 i = units + 1; i < nlfn * 13; i++)
		n16[i] = 0xFFFF;
	if (!fat_make_sfn(v, parent, de->name, sfn)) /* nom 8.3 unique */
		return 0;
	long first = dir_find_free(v, parent, nlfn + 1);
	if (first < 0)
		return 0;
	u8 cks = sfn_checksum(sfn);
	for (u64 seq = nlfn; seq; seq--) { /* entrées LFN (ordre inverse) */
		u64 sec;
		u8 *e = dir_entry_at(v, parent, first + (nlfn - seq), &sec);
		if (!e)
			return 0;
		e[0] = (u8)(seq | (seq == nlfn ? 0x40 : 0));
		e[11] = ATTR_LFN;
		e[12] = 0;
		e[13] = cks;
		wr16(e + 26, 0);
		const u16 *ch = n16 + (seq - 1) * 13; /* 5 + 6 + 2 caractères */
		for (int i = 0; i < 13; i++) {
			int off = i < 5 ? 1 + 2 * i : i < 11 ? 14 + 2 * (i - 5) : 28 + 2 * (i - 11);
			wr16(e + off, ch[i]);
		}
		bc_write(sec);
	}
	u64 sec;
	u8 *e = dir_entry_at(v, parent, first + nlfn, &sec);
	if (!e)
		return 0;
	memset(e, 0, 32);
	memcpy(e, sfn, 11);
	e[11] = (u8)attr;
	u32 t = (u32)dos_maintenant(); /* création, accès, dernière écriture */
	wr16(e + 14, t);
	wr16(e + 22, t);
	wr16(e + 16, t >> 16);
	wr16(e + 18, t >> 16);
	wr16(e + 24, t >> 16);
	bc_write(sec);
	de->h.dir = parent; /* description de l'entrée créée */
	de->h.first = first;
	de->h.index = first + nlfn;
	de->h.cluster = 0;
	de->h.fsize = 0;
	de->h.attr = attr;
	if (!(attr & ATTR_DIR))
		return 1;
	u64 c = fat_alloc(v, 0); /* répertoire : un cluster avec « . » et « .. » */
	if (!c)
		return 0;
	de->h.cluster = c;
	fat_update_entry(v, &de->h);
	u64 s0 = clus_lba(v, c);
	u8 *b = bc_get(s0);
	if (!b)
		return 0;
	memset(b, ' ', 11); /* « . » et « .. » remplis d'espaces */
	memset(b + 32, ' ', 11);
	b[0] = '.';
	b[32] = '.';
	b[33] = '.';
	b[11] = ATTR_DIR;
	b[32 + 11] = ATTR_DIR;
	wr16(b + 20, (u32)(c >> 16)); /* « . » -> lui-même */
	wr16(b + 26, (u32)c);
	u64 pp = parent == v->root ? 0 : parent; /* « .. » -> parent (0 si racine) */
	wr16(b + 32 + 20, (u32)(pp >> 16));
	wr16(b + 32 + 26, (u32)pp);
	bc_write(s0);
	return 1;
}

/* ---- fichiers */

/* place FO_CCLUS/FO_CIDX sur le cluster de rang rank, en allouant la chaîne si
 * alloc ; whole : ce cluster sera écrit en entier, sans zéros s'il est neuf
 * -> cluster (0 si fin / disque plein) */
static u64 fo_seek_cluster(struct fo *f, u64 rank, int alloc, long whole)
{
	struct vol *v = f->it.vol;
	if (!FO_START(f)) {
		if (!alloc)
			return 0;
		u64 c = fat_alloc_x(v, 0, rank == 0 ? whole : 0); /* fichier vide : premier cluster */
		if (!c)
			return 0;
		FO_START(f) = c;
		f->dirty = 1;
		FO_CCLUS(f) = 0;
	}
	u64 c = FO_CCLUS(f), i = FO_CIDX(f);
	if (!c || i > rank) {
		c = FO_START(f);
		i = 0;
	}
	for (; i < rank; i++) {
		u64 n = fat_next(v, c);
		if (n >= FAT_EOC) {
			if (!alloc) /* fin de chaîne en lecture */
				return 0;
			n = fat_alloc_x(v, c, i + 1 == rank ? whole : 0);
			if (!n)
				return 0;
		}
		c = n; /* (comme l'asm : 0, une erreur de lecture, est suivi tel quel) */
	}
	FO_CCLUS(f) = c;
	FO_CIDX(f) = i;
	return c;
}

/* -> octets transférés (lecture bornée par la taille), E_NOSPC (écriture, disque plein) */
long fo_rw(struct fo *f, u8 *buf, u64 len, int write)
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
		u64 rank = pos >> v->cshift; /* rang du cluster */
		long whole = 0;
		u64 csize = (u64)1 << v->cshift;
		if (write && len >= csize && !(pos & (csize - 1)))
			whole = 1;
		u64 c = fo_seek_cluster(f, rank, write, whole);
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
		u64 off = pos & mask; /* dans le cluster */
		u64 sec = lba + (off >> 9);
		u64 soff = pos & 511; /* dans le secteur */
		u64 done;
		if (!soff && len >= 512) {
			/* secteurs entiers jusqu'à la fin du cluster, puis les clusters contigus
			   (une seule commande de la carte) */
			u64 left = (mask - off + 1) >> 9;
			u64 n = len >> 9 < left ? len >> 9 : left;
			while (n < len >> 9) {
				u64 nx = fat_next(v, FO_CCLUS(f));
				if (nx >= FAT_EOC) {
					if (!write)
						break;
					if ((len >> 9) - left < v->spc) /* un cluster de plus, s'il sera écrit en entier */
						break;
					nx = fat_alloc_x(v, FO_CCLUS(f), 1);
					if (!nx)
						break;
				}
				if (nx != FO_CCLUS(f) + 1)
					break;
				FO_CCLUS(f) = nx; /* (fo_seek_cluster repart d'ici) */
				FO_CIDX(f)++;
				left += (u64)1 << (v->cshift - 9);
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
		if (f->pos > f->size) { /* le fichier grandit */
			f->size = f->pos;
			f->dirty = 1;
		}
	}
	return total;
}
