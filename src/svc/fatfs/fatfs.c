/* fatfs (FatFS) - service de fichiers FAT32 et exFAT (docs/MICRO-NOYAU.md,
 * section 4), traduction de fatfs.asm : un volume sur un périphérique bloc
 * (blk.sd0p1, blk.sd0p2...), monté par vfs : « fatfs <volume> <périphérique>
 * [ro] » ; le format est reconnu au montage, lecture et écriture dans les deux
 * formats. Il s'enregistre sous « fs.<volume> » et répond au protocole de
 * src/inc/fs.inc.
 *  * Ses secteurs : un cache de BC_N secteurs (correspondance directe, écriture
 *    immédiate) DANS le tampon partagé avec le périphérique bloc : le service
 *    bloc y lit directement, sans copie.
 *  * Une écriture de fichier (vol_rw) : FAT et bitmap différés, écrits une fois
 *    à la fin de la requête, après les données ; un cluster neuf écrit en entier
 *    n'est pas mis à zéro ; les clusters contigus partent en une commande.
 *  * Un cache de données sous le système de fichiers (cc_*) : blocs de 64 Ko du
 *    périphérique, lus une fois, gardés en mémoire privée ; les lectures de
 *    fichier et les secteurs de bc_get y passent. Toute écriture passe par
 *    blk_request, qui en retire les blocs touchés (docs/FICHIERS.md, 4.2).
 *  * Un fichier ouvert : un objet de « ouverts » (droits, propriétaire,
 *    drapeaux) qui désigne un NŒUD de « noeuds », partagé par toutes les
 *    ouvertures du même fichier (clé : répertoire parent + index de son entrée).
 *    L'entrée du répertoire est réécrite à chaque fermeture d'un nœud modifié,
 *    et tout de suite pour O_TRUNC.
 *  * unlink d'un fichier encore ouvert : ses entrées sont effacées, ses clusters
 *    libérés à la dernière fermeture.
 *  * Propriétaires, accès et tampons partagés (ops 14-16) : fsowner.c. */
#include "fatfs.h"

#define BC_N 256      /* secteurs en cache (128 Ko) */
#define BLOC_N 128    /* secteurs par transfert direct (64 Ko : FS_READ_BUF) */
#define PARTAGE ((BC_N + BLOC_N) * 512) /* tampon partagé avec le périphérique */
#define OUV_MAX 512   /* fichiers ouverts (.NET en garde beaucoup) */
#define NO_MAX OUV_MAX
#define CC_BLOCK_SHIFT 7 /* bloc du cache : 128 secteurs (BLOC_N) */
#define CC_BLOCK_BYTES (BLOC_N * 512)
#define CC_BLOCKS_MAX 4096 /* 256 Mo */
#define CC_OTHER_MAX 1024  /* 64 Mo : les volumes autres que SD0 */
#define CC_HASH 8192
#define CC_FILE_MAX 0x8000000 /* un fichier plus gros n'y passe pas (vidéo) */
#define CC_RESERVE 16384      /* pages libres laissées aux process (64 Mo) */
#define CC_CHECK_MASK 63      /* mémoire libre revue tous les 64 défauts */
#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_READ 3
#define BLK_WRITE 4
#define BLK_READ_D 6
#define DOS_1980 315532800ULL

static long canal_blk;
static long req_cour; /* numéro de la requête en cours */
static u64 direct;    /* 1 : la destination est chez l'appelant (op_lire_d) */
static u8 *cc_base;   /* la région du cache (0 : pas de cache) */
static u64 cc_max, cc_cap, cc_limit, cc_used, cc_hand, cc_off, cc_skip;
static u64 cc_hits, cc_misses, cc_evicts;
u64 cc_disk; /* secteurs du périphérique */
u64 lecture_seule;
u64 dos_date_forcee; /* (un utime en cours) */
static u8 *cache;     /* le tampon partagé (cache des secteurs, puis zone de transfert) */
static char nom_svc[32], nom_blk[32];
static struct msg msg __attribute__((aligned(16)));
static struct msg msg_blk __attribute__((aligned(16)));
static struct de ren_src, ren_dst, ren_new;
static struct vol volume;
static u64 bc_tags[BC_N];
static u64 bc_defer, bc_any;
static u8 bc_dirty[BC_N];
static struct fo ouverts[OUV_MAX];
static struct no noeuds[NO_MAX];
static u64 cc_tags[CC_BLOCKS_MAX]; /* bloc + 1 (0 : libre) */
static u32 cc_next[CC_BLOCKS_MAX];
static u32 cc_head[CC_HASH];
static u8 cc_refs[CC_BLOCKS_MAX];

static void cc_forget(u64 sector, u64 n);
static long cc_read(u64 sector, u64 n, u8 *dst);

/* ---- dates (src/inc/dostime.inc) : date DOS << 16 | heure DOS, en heure locale
 * comme sous Windows (2 s de précision) ; secondes Unix pour les programmes */

static u64 unix_vers_dos(u64 x)
{
	if (x < DOS_1980)
		x = DOS_1980;
	u64 days = x / 86400, sod = x % 86400;
	u64 z = days + 719468; /* depuis le 1er mars de l'an 0 */
	u64 era = z / 146097, doe = z - era * 146097;
	u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	u64 y = era * 400 + yoe;
	u64 doy = doe - (yoe * 365 + (yoe >> 2) - yoe / 100);
	u64 mp = (doy * 5 + 2) / 153;
	u64 d = doy - (mp * 153 + 2) / 5 + 1;
	u64 m = mp < 10 ? mp + 3 : mp - 9;
	if (m <= 2)
		y++;
	u64 date = (y - 1980) << 9 | m << 5 | d;
	u64 h = sod / 3600, mi = (sod % 3600) / 60, s = sod % 60;
	return (h << 11 | mi << 5 | s >> 1) | date << 16;
}

static u64 dos_vers_unix(u32 w)
{
	u32 d = w >> 16;
	if (!d)
		return 0;
	u32 t = w & 0xFFFF;
	u32 y = ((d >> 9) & 0x7F) + 1980;
	u32 m = (d >> 5) & 15, dd = d & 31;
	if (m < 1) /* (0 sur un disque : janvier) */
		m = 1;
	if (dd < 1)
		dd = 1;
	if (m <= 2) {
		y--;
		m += 12;
	}
	m -= 3; /* mois, depuis mars */
	u32 era = y / 400, yoe = y - era * 400;
	u32 doy = (m * 153 + 2) / 5 + dd - 1;
	u32 doe = yoe * 365 + (yoe >> 2) - yoe / 100 + doy;
	u64 days = (u64)era * 146097 + doe - 719468;
	return days * 86400 + ((t >> 11) & 31) * 3600 + ((t >> 5) & 63) * 60 + (t & 31) * 2;
}

static u64 dos_vers_utc(u32 w)
{
	u64 u = dos_vers_unix(w);
	if (!u)
		return 0;
	return u - korin_sys1(SYS_CLOCK_GET, 1);
}

static u64 utc_vers_dos(u64 x)
{
	return unix_vers_dos(x + korin_sys1(SYS_CLOCK_GET, 1));
}

u64 dos_maintenant(void)
{
	if (dos_date_forcee)
		return dos_date_forcee;
	return unix_vers_dos(korin_sys1(SYS_CLOCK_GET, 2));
}

/* ---- périphérique bloc et cache des secteurs */

/* op, secteur, nombre, adresse -> résultat ; le seul passage des écritures */
static long blk_request(long op, u64 sector, u64 count, void *addr)
{
	if (op == BLK_WRITE)
		cc_forget(sector, count);
	msg_blk.op = op;
	msg_blk.a = sector;
	msg_blk.b = count;
	msg_blk.c = (i64)addr;
	msg_blk.len = 0;
	return korin_sys2(SYS_SVC_CALL, canal_blk, &msg_blk);
}

/* secteur -> ses 512 octets en cache, 0 si erreur */
u8 *bc_get(u64 sector)
{
	u64 slot = sector & (BC_N - 1);
	u8 *p = cache + slot * 512;
	u64 tag = bc_tags[slot];
	if (tag == sector + 1) /* étiquette = secteur + 1 (0 : vide) */
		return p;
	if (bc_dirty[slot]) { /* la place d'un secteur différé */
		bc_dirty[slot] = 0;
		if (blk_request(BLK_WRITE, tag - 1, 1, p))
			return 0;
	}
	bc_tags[slot] = 0;
	if (!cc_read(sector, 1, p) && blk_request(BLK_READ, sector, 1, p))
		return 0;
	bc_tags[slot] = sector + 1;
	return p;
}

/* secteur obtenu par bc_get et modifié en place : l'écrit -> 1 (pendant une
 * écriture de fichier, vol_rw : plus tard, une seule fois) */
long bc_write(u64 sector)
{
	u64 slot = sector & (BC_N - 1);
	if (bc_defer) {
		bc_dirty[slot] = (u8)bc_defer;
		bc_any = bc_defer;
		return 1;
	}
	return blk_request(BLK_WRITE, sector, 1, cache + slot * 512) == 0;
}

void bc_defer_on(void)
{
	bc_defer = 1;
}

/* les secteurs différés -> 1, 0 si l'un n'a pas pu être écrit */
static long bc_flush(void)
{
	long ok = 1;
	if (!bc_any)
		return 1;
	bc_any = 0;
	for (u64 s = 0; s < BC_N; s++) {
		if (!bc_dirty[s])
			continue;
		bc_dirty[s] = 0;
		if (!bc_tags[s])
			continue;
		if (blk_request(BLK_WRITE, bc_tags[s] - 1, 1, cache + s * 512))
			ok = 0;
	}
	return ok;
}

long bc_defer_off(void)
{
	bc_defer = 0;
	return bc_flush();
}

/* secteurs retirés du cache */
static void bc_oublier(u64 sector, u64 n)
{
	for (; n; n--, sector++) {
		u64 slot = sector & (BC_N - 1);
		if (bc_tags[slot] == sector + 1) {
			bc_tags[slot] = 0;
			bc_dirty[slot] = 0; /* (écrit depuis : plus rien à écrire) */
		}
	}
}

/* mis à zéro, BLOC_N secteurs par commande -> 1 */
long blk_zero(u64 sector, u64 n)
{
	u8 *zone = cache + BC_N * 512; /* zone de transfert */
	while (n) {
		u64 k = n < BLOC_N ? n : BLOC_N;
		memset(zone, 0, k << 9);
		if (blk_request(BLK_WRITE, sector, k, zone))
			return 0;
		bc_oublier(sector, k);
		sector += k;
		n -= k;
	}
	return 1;
}

/* lecture (hors cache des secteurs) ou écriture (et le cache invalidé) -> 1 */
static long blk_direct(u64 sector, u64 n, u8 *buf, int write)
{
	u8 *zone = cache + BC_N * 512; /* zone de transfert : après le cache */
	while (n) {
		u64 k = n < BLOC_N ? n : BLOC_N;
		if (write) {
			memmove(zone, buf, k << 9);
			if (blk_request(BLK_WRITE, sector, k, zone))
				return 0;
			bc_oublier(sector, k); /* secteurs écrits : hors du cache */
		} else {
			u64 got = 0;
			if (!cc_skip)
				got = cc_read(sector, n, buf);
			if (got) {
				k = got;
			} else if (direct && !((u64)buf & 3) && !blk_request(BLK_READ_D, sector, n, buf)) {
				k = n; /* dans la mémoire de l'appelant */
			} else {
				if (blk_request(BLK_READ, sector, k, zone))
					return 0;
				memmove(buf, zone, k << 9);
			}
		}
		sector += k;
		n -= k;
		buf += k << 9;
	}
	return 1;
}

long blk_read(u64 sector, u64 n, void *dst)
{
	return blk_direct(sector, n, dst, 0);
}

long blk_write(u64 sector, u64 n, const void *src)
{
	return blk_direct(sector, n, (u8 *)src, 1);
}

/* ---- cache de données : blocs de 64 Ko du périphérique (numéro = secteur /
 * 128), commun à FAT32 et exFAT. Une région à la demande de ce process ; un
 * bloc neuf est peuplé d'abord (MADV_WILLNEED : E_NOMEM plutôt qu'une faute
 * qui tuerait le service). Quand la mémoire manque, le noyau lève
 * SVC_SIG_LOWMEM (SVF_MEMORY) : chaque signal rend la moitié du cache, et il
 * ne regrandit qu'au-dessus de 2 x CC_RESERVE pages libres. Table de hachage
 * (cc_head, cc_next : indice + 1), remplacement par horloge (cc_refs). */

static long free_pages(long *pages)
{
	u64 st[8];
	long r = korin_sys2(SYS_VM_STATS, 0, st);
	*pages = st[VS_FREE_LOW / 8] + st[VS_FREE_HIGH / 8];
	return r;
}

/* la région, à sa taille la plus grande ; permis d'abord : 1/8 de la mémoire libre */
static void cc_init(void)
{
	long pages;
	if (free_pages(&pages) < 0)
		return;
	u64 n = (u64)pages >> 7; /* pages / 8, en blocs de 16 pages */
	u64 max = CC_OTHER_MAX;
	if (*(u32 *)(nom_svc + 3) == 0x00306473) /* « fs.sd0 » */
		max = CC_BLOCKS_MAX;
	cc_max = max;
	if (n > max)
		n = max;
	if (n < 64) /* trop peu de mémoire : pas de cache */
		return;
	cc_cap = n;
	cc_limit = n;
	long v = korin_sys4(SYS_VM_MAP, 0, max << 16, PROT_READ | PROT_WRITE, 0);
	if (v < 0)
		return;
	cc_base = (u8 *)v;
}

/* bloc -> son indice, -1 s'il n'est pas là */
static long cc_find(u64 block)
{
	u64 key = block + 1;
	u32 i = cc_head[block & (CC_HASH - 1)];
	while (i) {
		i--;
		if (cc_tags[i] == key)
			return i;
		i = cc_next[i];
	}
	return -1;
}

/* indice : retiré de la table */
static void cc_unlink(u64 i)
{
	u64 tag = cc_tags[i];
	if (!tag)
		return;
	cc_tags[i] = 0;
	u32 *link = &cc_head[(tag - 1) & (CC_HASH - 1)]; /* le lien qui le désigne */
	while (*link) {
		if (*link == i + 1) {
			*link = cc_next[i];
			return;
		}
		link = &cc_next[*link - 1];
	}
}

/* secteur, nombre : les blocs touchés quittent le cache */
static void cc_forget(u64 sector, u64 n)
{
	if (!n || !cc_used)
		return;
	u64 last = (sector + n - 1) >> CC_BLOCK_SHIFT;
	for (u64 b = sector >> CC_BLOCK_SHIFT; b <= last; b++) {
		long i = cc_find(b);
		if (i >= 0)
			cc_unlink(i);
	}
}

/* blocs gardés : ceux d'au-delà quittent la table, leurs pages sont rendues */
static void cc_shrink(u64 keep)
{
	u64 used = cc_used;
	if (keep >= used)
		return;
	cc_used = keep;
	cc_hand = 0;
	for (u64 i = keep; i < used; i++)
		cc_unlink(i);
	korin_sys3(SYS_VM_ADVISE, cc_base + (keep << 16), (used - keep) << 16, MADV_DONTNEED);
}

/* tout est rendu (commande fscache) */
static void cc_drop(void)
{
	cc_shrink(0);
}

/* (SVC_SIG_LOWMEM) la moitié du cache est rendue ; il ne regrandit pas tant que
 * la mémoire reste rare */
static void cc_give_back(void)
{
	u64 n = cc_used >> 1;
	if (n < 64) /* (moins de 4 Mo : ne sert plus à rien) */
		n = 0;
	cc_limit = n;
	cc_shrink(n);
}

/* (tous les 64 défauts, au repos) moins de CC_RESERVE pages libres : le cache
 * rend ; plus du double : il peut regrandir */
static void cc_pressure(void)
{
	if (!cc_base)
		return;
	long pages;
	if (free_pages(&pages) < 0)
		return;
	u64 fr = (u64)pages;
	if (fr < CC_RESERVE) {
		cc_give_back();
		return;
	}
	u64 n = fr >> 7; /* 1/8, en blocs de 16 pages */
	if (n > cc_max)
		n = cc_max;
	if (n < cc_cap)
		n = cc_cap;
	cc_cap = n;
	if (fr >= CC_RESERVE * 2)
		cc_limit = n;
}

/* -> indice d'un bloc libre (hors table), -1 si aucun */
static long cc_take(void)
{
	u64 used = cc_used;
	if (used < cc_limit) { /* un bloc neuf : peuplé tout de suite */
		if (!korin_sys3(SYS_VM_ADVISE, cc_base + (used << 16), CC_BLOCK_BYTES, MADV_WILLNEED)) {
			cc_used = used + 1;
			return used;
		}
		cc_limit = used; /* plus de mémoire : on s'en tient là */
	}
	if (!used)
		return -1;
	u64 h = cc_hand;
	for (u64 turns = used << 1; turns; turns--) { /* (deux tours au plus) */
		if (h >= used)
			h = 0;
		u64 next = h + 1;
		if (cc_tags[h] && cc_refs[h]) {
			cc_refs[h] = 0;
			h = next;
			continue;
		}
		if (cc_tags[h])
			cc_evicts++;
		cc_hand = next;
		cc_unlink(h);
		return h;
	}
	return -1;
}

/* bloc -> son adresse dans le cache (lu au besoin), 0 si pas possible */
static u8 *cc_block(u64 block)
{
	long i = cc_find(block);
	if (i >= 0) {
		cc_refs[i] = 1;
		cc_hits++;
		return cc_base + ((u64)i << 16);
	}
	if ((block + 1) << CC_BLOCK_SHIFT > cc_disk) /* (dernier bloc incomplet : hors cache) */
		return 0;
	if (!(++cc_misses & CC_CHECK_MASK))
		cc_pressure();
	i = cc_take();
	if (i < 0)
		return 0;
	u8 *zone = cache + BC_N * 512; /* zone de transfert */
	if (blk_request(BLK_READ, block << CC_BLOCK_SHIFT, BLOC_N, zone))
		return 0;
	u8 *p = cc_base + ((u64)i << 16);
	memmove(p, zone, CC_BLOCK_BYTES);
	cc_refs[i] = 1;
	cc_tags[i] = block + 1; /* dans la table */
	u64 hh = block & (CC_HASH - 1);
	cc_next[i] = cc_head[hh];
	cc_head[hh] = (u32)i + 1;
	return p;
}

/* -> secteurs lus, 0 : passer par le chemin ordinaire */
static long cc_read(u64 sector, u64 n, u8 *dst)
{
	if (!cc_base || cc_off)
		return 0;
	u64 total = 0;
	while (n) {
		u8 *b = cc_block(sector >> CC_BLOCK_SHIFT);
		if (!b)
			break;
		u64 off = sector & (BLOC_N - 1);
		u64 k = BLOC_N - off;
		if (k > n)
			k = n;
		sector += k;
		n -= k;
		total += k;
		memmove(dst, b + (off << 9), k << 9);
		dst += k << 9;
	}
	return total;
}

/* 18 cache(a : 0 lire, FS_CC_ON, FS_CC_OFF, FS_CC_DROP) -> a succès, b défauts,
 * c blocs en mémoire, d blocs remplacés */
static long op_cache(struct msg *m)
{
	if (m->a) {
		if (m->a == FS_CC_ON) {
			cc_off = 0;
		} else { /* (FS_CC_DROP, FS_CC_OFF, et toute autre valeur, comme l'asm) */
			cc_drop();
			if (m->a == FS_CC_OFF)
				cc_off = 1;
		}
	}
	m->a = cc_hits;
	m->b = cc_misses;
	m->c = cc_used;
	m->d = cc_evicts;
	return 0;
}

/* ---- le format du volume */

static long vol_monter(struct vol *v, u64 first)
{
	return fat_mount(v, first) || exfat_mount(v, first);
}

static long vol_chercher(struct vol *v, const char *path, struct de *de)
{
	return v->type == 2 ? exfat_lookup(v, path, de) : fat_lookup(v, path, de);
}

static long vol_lire_rep(struct it *it, struct de *de)
{
	return it->vol->type == 2 ? exfat_dir_read(it, de) : dir_read(it, de);
}

static long vol_creer(struct vol *v, u64 parent, const char *name, u64 attr, struct de *de)
{
	return v->type == 2 ? exfat_create(v, parent, name, attr, de) : fat_create(v, parent, name, attr, de);
}

static void vol_maj(struct vol *v, struct dehead *de)
{
	if (v->type == 2)
		exfat_update_entry(v, de);
	else
		fat_update_entry(v, de);
}

/* supprime l'entrée et libère son allocation */
static void vol_effacer(struct vol *v, struct dehead *de)
{
	if (v->type == 2) {
		exfat_erase(v, de);
		return;
	}
	fat_erase_entry(v, de);
	fat_free_chain(v, de->cluster);
}

/* supprime l'entrée seulement (fichier encore ouvert) */
static void vol_effacer_entree(struct vol *v, struct dehead *de)
{
	if (v->type == 2)
		exfat_erase_entries(v, de);
	else
		fat_erase_entry(v, de);
}

/* nœud effacé : libère ses clusters (dernière fermeture) */
static void vol_liberer(struct vol *v, struct no *n)
{
	if (v->type == 2)
		ex_free_alloc(v, n->h.cluster, n->h.fsize);
	else
		fat_free_chain(v, n->h.cluster);
}

/* (comme fo_rw) en écriture, FAT et bitmap écrits une seule fois, une fois les
 * données sur la carte */
static long vol_rw(struct fo *f, u8 *buf, u64 len, int write)
{
	cc_skip = f->size > CC_FILE_MAX; /* un très gros fichier : hors du cache */
	int ex = f->it.vol->type == 2;
	if (!write)
		return ex ? exfat_rw(f, buf, len, 0) : fo_rw(f, buf, len, 0);
	bc_defer = 1;
	long r = ex ? exfat_rw(f, buf, len, 1) : fo_rw(f, buf, len, 1);
	bc_defer = 0;
	return bc_flush() ? r : E_IO;
}

/* ---- objets fichiers */

static void fo_from_de(struct fo *f, const struct de *de)
{
	FO_START(f) = de->h.cluster;
	FO_CCLUS(f) = 0;
	FO_CIDX(f) = 0;
	f->size = de->h.fsize;
	f->pdir = de->h.dir;
	f->index = de->h.index;
	f->first = de->h.first;
	f->attr = de->h.attr;
}

/* -> objet libre (mis à zéro), 0 si aucun */
static struct fo *fo_libre(void)
{
	for (int i = 0; i < OUV_MAX; i++)
		if (!ouverts[i].type) {
			memset(&ouverts[i], 0, sizeof ouverts[i]);
			return &ouverts[i];
		}
	return 0;
}

static struct fo *fo_de(u64 num)
{
	if (num >= OUV_MAX || !ouverts[num].type)
		return 0;
	return &ouverts[num];
}

/* -> objet ouvert (numéro en a) si l'appelant y a droit ; *err : E_INVAL
 * (numéro libre) ou E_BADF (pas un de ses propriétaires) */
static struct fo *fo_acces(struct msg *m, long *err)
{
	struct fo *f = fo_de(m->a);
	if (!f) {
		*err = E_INVAL;
		return 0;
	}
	*err = fo_access(m, m->a);
	return *err ? 0 : f;
}

/* ---- nœuds : l'état partagé d'un fichier ouvert */

static struct no *no_trouver(const struct dehead *de)
{
	for (int i = 0; i < NO_MAX; i++) {
		struct no *n = &noeuds[i];
		if (n->refs && !n->efface && (u32)n->h.dir == (u32)de->dir && n->h.index == de->index)
			return n;
	}
	return 0;
}

/* DE d'un fichier -> son nœud, une ouverture de plus (créé d'après la DE s'il
 * n'était pas ouvert), 0 si la table est pleine */
static struct no *no_prendre(const struct de *de)
{
	struct no *n = no_trouver(&de->h);
	if (!n) {
		for (int i = 0; i < NO_MAX && !n; i++)
			if (!noeuds[i].refs)
				n = &noeuds[i];
		if (!n)
			return 0;
		memset(n, 0, sizeof *n);
		n->h = de->h;
	}
	n->refs++;
	return n;
}

/* objet : son état <- celui du nœud (avant vol_rw) */
static void fo_charger(struct fo *f)
{
	struct no *n = f->node;
	if (!n)
		return;
	FO_START(f) = n->h.cluster;
	f->size = n->h.fsize;
	FO_CCLUS(f) = n->cclus;
	FO_CIDX(f) = n->cidx;
	f->dirty = 0;
}

/* objet : son état -> le nœud (après vol_rw) */
static void fo_ranger(struct fo *f)
{
	struct no *n = f->node;
	if (!n)
		return;
	n->h.cluster = FO_START(f);
	n->h.fsize = f->size;
	n->cclus = FO_CCLUS(f);
	n->cidx = FO_CIDX(f);
	if (f->dirty)
		n->dirty = f->dirty;
}

/* O_TRUNC : le fichier est vide pour toutes ses ouvertures ; chaîne libérée,
 * entrée réécrite tout de suite */
static void no_tronquer(struct vol *v, struct no *n)
{
	if (n->dirty) /* disque à jour d'abord (exFAT : vol_maj libère l'allocation écrite) */
		vol_maj(v, &n->h);
	u64 c = n->h.cluster;
	n->h.cluster = 0;
	n->h.fsize = 0;
	n->cclus = 0;
	n->cidx = 0;
	fat_free_chain(v, c); /* FAT32 : la chaîne (une poignée exFAT est ignorée) */
	vol_maj(v, &n->h);
	n->dirty = 0;
}

/* objet ouvert : libéré ; son nœud : taille et cluster réécrits s'ils ont
 * changé ; effacé pendant l'ouverture : clusters libérés à la dernière fermeture */
static void fo_fermer(struct fo *f)
{
	struct no *n = f->node;
	if (n) {
		if (n->efface) {
			if (n->refs == 1)
				vol_liberer(f->it.vol, n);
		} else if (n->dirty) {
			vol_maj(f->it.vol, &n->h);
			n->dirty = 0;
		}
		n->refs--;
	}
	f->type = 0;
	f->node = 0;
}

static void fo_fermer_num(long num)
{
	struct fo *f = fo_de(num);
	if (f)
		fo_fermer(f);
}

/* 25 sync -> a (et résultat) : fichiers ouverts ; l'entrée de chaque fichier
 * ouvert modifié est réécrite (eject, mkfs) */
static long op_sync(struct msg *m)
{
	for (int i = 0; i < NO_MAX; i++) {
		struct no *n = &noeuds[i];
		if (n->refs && !n->efface && n->dirty) {
			vol_maj(&volume, &n->h);
			n->dirty = 0;
		}
	}
	long c = 0; /* fichiers ouverts (pas les répertoires) */
	for (int i = 0; i < OUV_MAX; i++)
		c += ouverts[i].type == 1;
	m->a = c;
	return c;
}

static void fin_pid(long pid)
{
	fo_own_end(pid, fo_fermer_num);
	tampons_fin(pid);
}

/* nom UTF-8 (fin 0 ou « / ») -> 1 s'il dépasse 255 caractères UTF-16 (4 octets
 * UTF-8 : deux) ou NAME_BYTES-3 octets */
static long nom_trop_long(const char *s)
{
	u64 ch = 0, by = 0;
	for (;;) {
		u8 c = (u8)s[by];
		if (!c || c == '/')
			break;
		by++;
		if ((c & 0xC0) == 0x80) /* octet de suite : pas un caractère */
			continue;
		ch++;
		if (c >= 0xF0)
			ch++;
	}
	return ch > NAME_UNITS || by > NAME_BYTES - 3;
}

/* DE lue sur le disque -> taille du fichier (celle de son nœud s'il est ouvert) */
static u64 no_taille(const struct de *de)
{
	if (de->h.attr & ATTR_DIR)
		return de->h.fsize;
	struct no *n = no_trouver(&de->h);
	return n ? n->h.fsize : de->h.fsize;
}

/* « parent/nom » : le dernier composant ; le parent cherché dans *pde -> son
 * cluster (la racine si pas de « / »), 0 si absent ou pas un répertoire */
static const char *dernier(const char *p)
{
	const char *last = p;
	for (const char *s = p; *s;)
		if (*s++ == '/')
			last = s;
	return last;
}

static long parent_de(char *path, const char *last, struct de *pde, u64 *cluster, long notdir_err)
{
	if (last == path) {
		*cluster = volume.root;
		return 0;
	}
	char *cut = (char *)last - 1;
	*cut = 0; /* « parent/nom » -> « parent » */
	long f = vol_chercher(&volume, path, pde);
	*cut = '/';
	if (!f)
		return E_NOENT;
	if (!(pde->h.attr & ATTR_DIR))
		return notdir_err;
	*cluster = pde->h.cluster;
	return 0;
}

/* ---- opérations (message -> résultat) */

static struct de de_op; /* (l'asm : sur la pile de chaque op) */

/* 1 ouvrir(a drapeaux, chemin) -> b numéro, c attributs, d taille */
static long op_ouvrir(struct msg *m)
{
	struct de *de = &de_op;
	open_extras_take(m);
	u64 fl = m->a;
	if ((fl & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND)) && lecture_seule)
		return E_PERM;
	char *path = (char *)m->data;
	if (!vol_chercher(&volume, path, de)) {
		if (!(fl & O_CREATE)) /* création : le parent doit exister */
			return E_NOENT;
		const char *last = dernier(path);
		u64 parent;
		long r = parent_de(path, last, de, &parent, E_NOTDIR);
		if (r)
			return r;
		if (nom_trop_long(last))
			return E_NAMETOOLONG;
		if (!vol_creer(&volume, parent, last, ATTR_ARCHIVE, de))
			return E_NOSPC;
	}
	struct fo *f = fo_libre();
	if (!f)
		return E_MFILE;
	f->it.vol = &volume;
	fo_from_de(f, de);
	u64 type = 1; /* fichier */
	if (f->attr & ATTR_DIR) {
		if (fl & O_WRITE) {
			f->type = 0;
			return E_ISDIR;
		}
		type = 2; /* répertoire : itérateur au début */
		FO_CCLUS(f) = FO_START(f);
	}
	f->type = type;
	f->flags = fl;
	if (type == 1) {
		struct no *n = no_prendre(de); /* fichier : son nœud (partagé) */
		if (!n) {
			f->type = 0;
			return E_MFILE;
		}
		f->node = n;
		if (fl & O_TRUNC) /* troncature : pour toutes les ouvertures */
			no_tronquer(&volume, n);
		fo_charger(f); /* taille et chaîne du nœud */
		f->posp = (fl & O_APPEND) ? (i64)f->size : 0; /* position : 0, la fin avec O_APPEND */
	}
	long owner = proprietaire(m);
	long num = f - ouverts;
	m->b = num;
	fo_own_clear(num);
	if (!fo_own_add(num, owner)) { /* table des propriétaires pleine */
		fo_fermer(f);
		return E_MFILE;
	}
	m->c = f->attr;
	m->d = f->size;
	open_extras_put(m); /* a : notre canal (vfs transmet) */
	return 0;
}

/* 2 stat(chemin) -> b taille, c attributs, d date */
static long op_stat(struct msg *m)
{
	struct de *de = &de_op;
	if (!vol_chercher(&volume, (char *)m->data, de))
		return E_NOENT;
	m->b = no_taille(de); /* ouvert : la taille de son nœud */
	m->c = de->h.attr;
	m->d = dos_vers_utc((u32)de->mtime);
	return 0;
}

/* 5 utime(b secondes UTC ; chemin) : la date de l'entrée du répertoire */
static long op_utime(struct msg *m)
{
	struct de *de = &de_op;
	if (lecture_seule)
		return E_PERM;
	if (!vol_chercher(&volume, (char *)m->data, de))
		return E_NOENT;
	dos_date_forcee = utc_vers_dos(m->b);
	vol_maj(&volume, &de->h);
	dos_date_forcee = 0;
	return 0;
}

/* 6 rename(ancien, 0, nouveau, 0) : une entrée de répertoire neuve, mêmes
 * clusters et même date, puis l'ancienne effacée ; un fichier à l'arrivée est
 * remplacé ; le nœud d'un fichier ouvert suit */
static long op_rename(struct msg *m)
{
	if (lecture_seule)
		return E_PERM;
	struct vol *v = &volume;
	char *oldp = (char *)m->data, *newp = oldp;
	while (*newp++)
		;
	if (!*oldp || !*newp)
		return E_INVAL;
	if (!vol_chercher(v, oldp, &ren_src))
		return E_NOENT;
	if (ren_src.h.attr & ATTR_DIR) { /* un répertoire : pas dans lui-même */
		u64 i = 0;
		while (oldp[i] && oldp[i] == newp[i])
			i++;
		if (!oldp[i] && newp[i] == '/')
			return E_INVAL;
	}
	const char *last = dernier(newp); /* le dernier composant, son répertoire */
	u64 dir;
	long r = parent_de(newp, last, &ren_dst, &dir, E_NOENT);
	if (r)
		return r;
	int erase_old = 1;
	if (vol_chercher(v, newp, &ren_dst)) {
		if ((u32)ren_src.h.dir == (u32)ren_dst.h.dir && ren_src.h.index == ren_dst.h.index) {
			vol_effacer_entree(v, &ren_src.h); /* la même entrée (la casse change) : effacée d'abord */
			erase_old = 0;
		} else {
			if (ren_dst.h.attr & ATTR_DIR)
				return E_EXIST;
			if (ren_src.h.attr & ATTR_DIR)
				return E_NOTDIR;
			struct no *n = no_trouver(&ren_dst.h); /* le fichier remplacé (comme unlink) */
			if (n) {
				n->efface = 1;
				vol_effacer_entree(v, &ren_dst.h);
			} else {
				vol_effacer(v, &ren_dst.h);
			}
		}
	}
	if (nom_trop_long(last))
		return E_NAMETOOLONG;
	if (!vol_creer(v, dir, last, ATTR_ARCHIVE, &ren_new))
		return E_NOSPC;
	ren_new.h.cluster = ren_src.h.cluster;
	ren_new.h.fsize = ren_src.h.fsize;
	ren_new.h.attr = ren_src.h.attr;
	dos_date_forcee = ren_src.mtime;
	vol_maj(v, &ren_new.h);
	dos_date_forcee = 0;
	struct no *n = no_trouver(&ren_src.h); /* ouvert : son nœud suit */
	if (n) {
		n->h.dir = ren_new.h.dir;
		n->h.index = ren_new.h.index;
		n->h.first = ren_new.h.first;
	}
	if (erase_old)
		vol_effacer_entree(v, &ren_src.h);
	/* FAT32 : « .. » d'un répertoire déplacé */
	if (v->type != 2 && (ren_src.h.attr & ATTR_DIR) && ren_src.h.dir != dir) {
		u64 sec;
		u8 *e = dir_entry_at(v, ren_src.h.cluster, 1, &sec);
		if (e) {
			u64 pp = dir == v->root ? 0 : dir;
			e[20] = (u8)(pp >> 16);
			e[21] = (u8)(pp >> 24);
			e[26] = (u8)pp;
			e[27] = (u8)(pp >> 8);
			bc_write(sec);
		}
	}
	return 0;
}

/* 3 mkdir(chemin) */
static long op_mkdir(struct msg *m)
{
	struct de *de = &de_op;
	if (lecture_seule)
		return E_PERM;
	char *path = (char *)m->data;
	if (vol_chercher(&volume, path, de))
		return E_EXIST;
	const char *last = dernier(path);
	u64 parent;
	long r = parent_de(path, last, de, &parent, E_NOENT);
	if (r)
		return r;
	if (nom_trop_long(last))
		return E_NAMETOOLONG;
	return vol_creer(&volume, parent, last, ATTR_DIR, de) == 1 ? 0 : E_NOSPC;
}

/* 4 unlink(chemin) : fichier, ou répertoire vide */
static long op_unlink(struct msg *m)
{
	static struct de de2;
	struct de *de = &de_op;
	if (lecture_seule)
		return E_PERM;
	char *path = (char *)m->data;
	if (!*path) /* racine du volume */
		return E_INVAL;
	if (!vol_chercher(&volume, path, de))
		return E_NOENT;
	if (de->h.attr & ATTR_DIR) { /* répertoire : doit être vide */
		struct it it;
		dir_open(&it, &volume, de->h.cluster);
		if (vol_lire_rep(&it, &de2))
			return E_NOTEMPTY;
	} else {
		struct no *n = no_trouver(&de->h); /* fichier encore ouvert ? */
		if (n) { /* son nœud perd sa clé ; ses clusters à sa dernière fermeture */
			n->efface = 1;
			vol_effacer_entree(&volume, &de->h);
			return 0;
		}
	}
	vol_effacer(&volume, &de->h);
	return 0;
}

/* FO_POS = b, ou FO_POSP (b = FS_POS_CUR) */
static void fo_pos_req(struct fo *f, struct msg *m)
{
	f->pos = m->b == FS_POS_CUR ? (u64)f->posp : (u64)m->b;
}

/* état du nœud, FO_POS = la position demandée, ou la fin avec O_APPEND */
static void fo_position(struct fo *f, struct msg *m)
{
	fo_charger(f);
	u64 p = m->b == FS_POS_CUR ? (u64)f->posp : (u64)m->b;
	if (f->flags & O_APPEND)
		p = f->size;
	f->pos = p;
}

/* d = position après l'op ; FO_POSP la suit si b = FS_POS_CUR */
static void fo_pos_apres(struct fo *f, struct msg *m)
{
	m->d = f->pos;
	if (m->b == FS_POS_CUR)
		f->posp = f->pos;
}

/* 10 lire(a numéro, b position, c octets) -> octets (données) */
static long op_lire(struct msg *m)
{
	long err;
	struct fo *f = fo_acces(m, &err);
	if (!f)
		return err;
	if (f->type != 1)
		return E_INVAL;
	fo_charger(f);
	fo_pos_req(f, m);
	u64 len = (u64)m->c > FS_DATA_MAX ? FS_DATA_MAX : (u64)m->c;
	long r = vol_rw(f, m->data, len, 0);
	m->len = (u32)r;
	fo_ranger(f);
	fo_pos_apres(f, m);
	return r;
}

/* 11 écrire(a numéro, b position, c octets, données) -> octets, b taille */
static long op_ecrire(struct msg *m)
{
	if (lecture_seule)
		return E_PERM;
	long err;
	struct fo *f = fo_acces(m, &err);
	if (!f)
		return err;
	if (f->type != 1)
		return E_INVAL;
	if (!(f->flags & O_WRITE))
		return E_PERM;
	if ((u64)m->c > FS_DATA_MAX)
		return E_INVAL;
	fo_position(f, m); /* état du nœud, position (O_APPEND) */
	long r = vol_rw(f, m->data, m->c, 1);
	fo_ranger(f);
	fo_pos_apres(f, m);
	m->b = f->size;
	return r;
}

/* 12 fermer(a numéro) -> 0 (avis du noyau : fermé pour tous ; appel d'un
 * propriétaire : il le quitte, fermé avec le dernier) */
static long op_fermer(struct msg *m)
{
	long err;
	struct fo *f = fo_acces(m, &err);
	if (!f)
		return err;
	if (!fo_own_close(m, m->a))
		return 0;
	fo_fermer(f);
	return 0;
}

/* 26 position(a numéro, b décalage, c origine) -> nouvelle position (aussi en b) */
static long op_position(struct msg *m)
{
	long err;
	struct fo *f = fo_acces(m, &err);
	if (!f)
		return err;
	if (f->type != 1)
		return E_INVAL;
	fo_charger(f); /* (taille du nœud) */
	u64 org = m->c;
	if (org > 2)
		return E_INVAL;
	i64 base = org == 1 ? f->posp : org == 2 ? (i64)f->size : 0;
	i64 p = (i64)((u64)base + (u64)m->b);
	if (p < 0)
		return E_INVAL;
	f->posp = p;
	m->b = p;
	return p;
}

/* destination (256 o), nom UTF-8 : 255 octets au plus, coupé avant un caractère entier */
static void nom_256(char *d, const char *s)
{
	u64 i = 0;
	while (s[i]) {
		if (i >= 255) {
			while ((s[i] & 0xC0) == 0x80) /* pas au milieu d'un caractère */
				i--;
			break;
		}
		d[i] = s[i];
		i++;
	}
	d[i] = 0;
}

/* 13 lire_rep(a numéro, b rang) -> 1 (données : nom[256], taille, attributs), 0 en fin */
static long op_lire_rep(struct msg *m)
{
	struct de *de = &de_op;
	long err;
	struct fo *f = fo_acces(m, &err);
	if (!f)
		return err;
	if (f->type != 2)
		return E_INVAL;
	u64 rank = m->b;
	if (f->rang > rank) { /* plus haut : depuis le début */
		dir_open(&f->it, f->it.vol, FO_START(f));
		f->rang = 0;
	}
	for (;;) {
		if (!vol_lire_rep(&f->it, de))
			return 0;
		u64 r = f->rang++;
		if (r >= rank)
			break;
	}
	nom_256((char *)m->data, de->name);
	u64 *t = (u64 *)(m->data + 256);
	t[0] = no_taille(de);
	t[1] = de->h.attr;
	m->len = 272;
	return 1;
}

/* 15 lire_t, 16 écrire_t (a numéro, b position, c octets) : au début du tampon */
static long rw_t(struct msg *m, int write)
{
	if (write && lecture_seule)
		return E_PERM;
	long err;
	struct fo *f = fo_acces(m, &err);
	if (!f)
		return err;
	if (f->type != 1)
		return E_INVAL;
	if (write && !(f->flags & O_WRITE))
		return E_PERM;
	u8 *buf = tampon_de(m); /* le tampon de l'appelant */
	if (!buf || (u64)m->c > FS_BUFFER_SIZE)
		return E_INVAL;
	fo_position(f, m); /* état du nœud, position (O_APPEND) */
	if (!write) /* lecture : la position demandée */
		fo_pos_req(f, m);
	long r = vol_rw(f, buf, m->c, write);
	fo_ranger(f);
	fo_pos_apres(f, m);
	if (write)
		m->b = f->size;
	return r;
}

static long op_lire_t(struct msg *m) { return rw_t(m, 0); }
static long op_ecrire_t(struct msg *m) { return rw_t(m, 1); }

/* 17 lire_d(a numéro, b position, c octets, d adresse) -> octets : la zone de
 * l'appelant, mappée, sert de destination (SVC_MAP_CALLER ; le noyau la rend
 * avec la réponse) ; les secteurs entiers y vont sans copie */
static long op_lire_d(struct msg *m)
{
	long err;
	struct fo *f = fo_acces(m, &err);
	if (!f)
		return err;
	if (f->type != 1)
		return E_INVAL;
	if ((u64)m->c > FS_DIRECT_MAX)
		return E_INVAL;
	long va = korin_sys4(SYS_SVC_MAP_CALLER, req_cour, m->d, m->c, MC_WRITE);
	if (va < 0)
		return va;
	fo_position(f, m);
	fo_pos_req(f, m);
	direct = 1;
	long r = vol_rw(f, (u8 *)va, m->c, 0);
	direct = 0;
	fo_ranger(f);
	fo_pos_apres(f, m);
	return r;
}

/* 19 info -> a octets du volume, b octets libres (-1 : inconnu), c octets par
 * cluster, d FS_T_* ; données : étiquette (UTF-8, vide : aucune) */
static long op_info(struct msg *m)
{
	struct vol *v = &volume;
	m->a = v->nclust << v->cshift;
	m->c = (u64)1 << v->cshift;
	m->d = v->type;
	m->data[0] = 0;
	if (v->type != FS_T_EXFAT) {
		i64 fr = v->free; /* FAT32 : FSInfo, tenu à jour */
		m->b = fr < 0 ? fr : (i64)((u64)fr << v->cshift);
		u8 *b = bc_get(v->pstart); /* étiquette du secteur de démarrage */
		if (!b)
			return 0;
		u64 len = 0; /* longueur sans les blancs de la fin */
		for (u64 i = 0; i < 11; i++) {
			m->data[i] = b[71 + i];
			if (b[71 + i] != ' ')
				len = i + 1;
		}
		m->data[len] = 0;
		/* (NO NAME : aucune ; comme l'asm, comparé après la coupe des blancs : la
		   comparaison sur 8 octets ne voit jamais « NO NAME » seul) */
		if (!memcmp(m->data, "NO NAME ", 8))
			m->data[0] = 0;
		return 0;
	}
	u64 left = v->nclust, used = 0, sec = v->ex_bmlba; /* exFAT : les bits du bitmap */
	i64 freeb = 0;
	while (left) {
		u8 *b = bc_get(sec);
		if (!b) {
			freeb = -1;
			goto label;
		}
		sec++;
		for (u64 i = 0; i < 512 && left; i++) {
			u32 x = b[i];
			if (left < 8) {
				x &= (1u << left) - 1;
				left = 8;
			}
			left -= 8;
			used += __builtin_popcount(x);
		}
	}
	freeb = (i64)((v->nclust - used) << v->cshift);
label:
	m->b = freeb;
	for (u64 i = 0;; i++) { /* étiquette : entrée 0x83 de la racine */
		u64 s;
		u8 *e = ex_entry_at(v, v->root, i, &s);
		if (!e || !e[0])
			break;
		if (e[0] != 0x83)
			continue;
		u32 n = e[1] > 11 ? 11 : e[1]; /* caractères (11 au plus), UTF-16 */
		u8 *o = m->data;
		for (u32 k = 0; k < n; k++) {
			u32 u = e[2 + 2 * k] | e[3 + 2 * k] << 8;
			if (u < 0x80) {
				*o++ = (u8)u;
			} else if (u < 0x800) {
				*o++ = (u8)(0xC0 | u >> 6);
				*o++ = (u8)(0x80 | (u & 0x3F));
			} else {
				*o++ = (u8)(0xE0 | u >> 12);
				*o++ = (u8)(0x80 | ((u >> 6) & 0x3F));
				*o++ = (u8)(0x80 | (u & 0x3F));
			}
		}
		*o = 0;
		break;
	}
	return 0;
}

static const struct {
	long op;
	long (*fn)(struct msg *);
} ops[] = {
	{FS_OPEN, op_ouvrir},      {FS_STAT, op_stat},       {FS_MKDIR, op_mkdir},
	{FS_UNLINK, op_unlink},    {FS_READ, op_lire},       {FS_WRITE, op_ecrire},
	{FS_READDIR, op_lire_rep}, {FS_BUFFER, op_tampon},   {FS_READ_BUF, op_lire_t},
	{FS_WRITE_BUF, op_ecrire_t}, {FS_UTIME, op_utime},   {FS_RENAME, op_rename},
	{FS_READ_DIRECT, op_lire_d}, {FS_CACHE, op_cache},   {FS_INFO, op_info},
	{FS_SYNC, op_sync},        {FS_SEEK, op_position},   {0, 0},
};

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *p = krt_line; /* « <volume> <périphérique> [ro] » */
	if (!p)
		SVC_QUIT();
	char *o = str_cat(nom_svc, "fs."); /* « fs.<volume> » */
	for (;;) {
		char c = *p++;
		if (c == ' ')
			break;
		if (!c)
			SVC_QUIT();
		*o++ = c;
	}
	*o = 0;
	o = nom_blk; /* le périphérique */
	char c;
	for (;;) {
		c = *p++;
		if (c == ' ' || !c)
			break;
		*o++ = c;
	}
	*o = 0;
	if (c && *p == 'r') /* « ro » */
		lecture_seule = 1;
	canal_blk = korin_sys2(SYS_SVC_OPEN, nom_blk, 3000);
	if (canal_blk < 0)
		SVC_QUIT();
	if (blk_request(BLK_INFO, 0, 0, 0) < 0) /* lecture seule ? son pid */
		SVC_QUIT();
	cc_disk = msg_blk.b; /* (secteurs : fin du cache) */
	if (msg_blk.a)
		lecture_seule = 1;
	long blk_pid = msg_blk.d;
	long num = korin_sys1(SYS_BUF_CREATE, PARTAGE); /* le tampon partagé */
	if (num < 0)
		SVC_QUIT();
	cache = (u8 *)(BUF_VA_BASE + (u64)num * BUF_SLOT);
	korin_sys2(SYS_BUF_GRANT, num, blk_pid);
	msg_blk.op = BLK_ATTACH; /* attacher */
	msg_blk.a = num;
	msg_blk.b = PARTAGE;
	if (korin_sys2(SYS_SVC_CALL, canal_blk, &msg_blk) < 0)
		SVC_QUIT();
	cc_init();
	if (!vol_monter(&volume, 0)) /* le système de fichiers */
		SVC_QUIT();
	/* prévenu des fins de process, des fork, mémoire rare */
	fs_canal = korin_sys2(SYS_SVC_REGISTER, nom_svc, SVF_ENDS | SVF_BIRTHS | SVF_MEMORY);
	if (fs_canal < 0)
		SVC_QUIT();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, fs_canal, &msg, 60000);
		if (req < 0)
			SVC_QUIT();
		if (!req) {
			cc_pressure(); /* (au repos aussi : mémoire libre) */
			continue;
		}
		u64 op = msg.op;
		if (op == SVC_OP_END) { /* fin d'un process : ses fichiers */
			fin_pid(msg.a);
			fat_fsinfo_flush(&volume);
			continue;
		}
		if (op == SVC_OP_SIGNAL) { /* le noyau manque de pages */
			cc_give_back();
			continue;
		}
		if (op == SVC_OP_FORK) { /* fork : le fils tient les fichiers du père */
			fo_own_fork(msg.a, msg.b);
			continue;
		}
		if (op == SVC_OP_BIRTH) /* (avis sans réponse : rien à faire) */
			continue;
		long r = E_INVAL;
		if (op == FS_CLOSE) {
			r = op_fermer(&msg);
			fat_fsinfo_flush(&volume); /* FSInfo réécrit si les clusters libres ont changé */
			if (!msg.pid) /* avis du noyau (pid 0) : sans réponse */
				continue;
		} else {
			for (int i = 0; ops[i].op; i++)
				if ((u64)ops[i].op == op) {
					req_cour = req;
					r = ops[i].fn(&msg);
					fat_fsinfo_flush(&volume);
					break;
				}
		}
		msg.op = r;
		korin_sys2(SYS_SVC_REPLY, req, &msg);
	}
}
