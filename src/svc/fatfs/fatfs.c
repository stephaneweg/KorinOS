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
 *  * Propriétaires, accès et tampons partagés (ops 14-16) : fsowner.c.
 *  * Plusieurs fils (22/11/2026, docs/agents/FICHIERS-PERF.md, point 4 ; FT_N, le PC) : les
 *    lectures (open sans écriture, read, stat, readdir, seek, info) sont servies en parallèle,
 *    les écritures une à la fois et seules. Un verrou (big) tient tout l'état ; un fil lecteur
 *    le relâche pendant ses lectures du périphérique (blk_request), chacun dans sa zone de
 *    transfert du tampon partagé, et ne garde aucun pointeur dans les caches à travers une
 *    lecture (bc_get lit dans sa zone, puis copie dans le cache des secteurs ; cc_block cherche
 *    à nouveau le bloc après la lecture). Un écrivain attend qu'aucun lecteur ne soit en cours
 *    et garde le verrou jusqu'au bout. Une op sur un fichier ouvert le tient (fo->busy) : deux
 *    ops sur le même fichier se suivent ; une fermeture qui doit écrire repasse en écrivain.
 *    Les avis (fin de process, fork) sont des écrivains, pris dans l'ordre (recv).
 *  * Un index par répertoire (dirindex.c) : la recherche d'un nom, trouvé ou non, ne relit
 *    plus le répertoire. */
#include "fatfs.h"

#if defined(__x86_64__)
#define BC_N 2048     /* secteurs en cache (1 Mo : un dossier de milliers de noms longs y tient ; point 3 de
			 docs/agents/FICHIERS-PERF.md) */
#else
#define BC_N 256      /* secteurs en cache (128 Ko) */
#endif
#define BLOC_N 128    /* secteurs par transfert direct (64 Ko : FS_READ_BUF) */
#if defined(__x86_64__)
#define FT_N 8        /* fils qui servent les requêtes */
#else
#define FT_N 1        /* (Pi : un seul, le noyau asm n'a pas été essayé avec plusieurs fils par service) */
#endif
#define FT_STACK 131072 /* pile d'un fil, alignée : son contexte (struct tctx) au pied */
#define PARTAGE ((BC_N + FT_N * BLOC_N) * 512) /* tampon partagé avec le périphérique : le cache des
							 secteurs, puis une zone de transfert par fil */
#define OUV_MAX 512   /* fichiers ouverts (.NET en garde beaucoup) */
#define NO_MAX OUV_MAX
#define CC_BLOCK_SHIFT 7 /* bloc du cache : 128 secteurs (BLOC_N) */
#define CC_BLOCK_BYTES (BLOC_N * 512)
#if defined(__x86_64__)
#define CC_BLOCKS_MAX 16384 /* 1 Go pour / (le PC : 1/8 de la mémoire libre au plus, rendu quand elle manque) */
#define CC_OTHER_MAX 4096   /* 256 Mo : les autres volumes */
#define CC_HASH 32768
#else
#define CC_BLOCKS_MAX 4096 /* 256 Mo (décision du 01/11/2026 pour la carte du Pi) */
#define CC_OTHER_MAX 1024  /* 64 Mo : les volumes autres que SD0 */
#define CC_HASH 8192
#endif
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
static u8 *cc_base;   /* la région du cache (0 : pas de cache) */
static u64 cc_max, cc_cap, cc_limit, cc_used, cc_hand, cc_off;
static u64 cc_hits, cc_misses, cc_evicts;
u64 cc_disk; /* secteurs du périphérique */
u64 lecture_seule;
u64 dos_date_forcee; /* (un utime en cours) */
static u8 *cache;     /* le tampon partagé (cache des secteurs, puis zone de transfert) */
static char nom_svc[32], nom_blk[32];
static struct de ren_src, ren_dst, ren_new; /* (écrivains seulement) */
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

/* ---- fils : le contexte de chacun, au pied de sa pile (FT_STACK octets, alignée) */
#define MODE_R 1 /* lecteur : le verrou relâché pendant ses lectures du périphérique */
#define MODE_W 2 /* écrivain : seul, le verrou gardé */
struct tctx {
	struct msg msg;     /* la requête en cours (et sa réponse) */
	struct msg msg_blk; /* au périphérique */
	struct de de_op;    /* (l'asm : sur la pile de chaque op) */
	u64 direct;         /* 1 : la destination est chez l'appelant (op_lire_d) */
	u64 cc_skip;        /* 1 : un très gros fichier, hors du cache */
	long req;           /* numéro de la requête en cours */
	int mode;           /* MODE_R, MODE_W, 0 : hors requête */
	int idx;            /* sa zone de transfert */
	struct fo *held;    /* le fichier ouvert tenu (fo->busy) */
	u8 sec[512] __attribute__((aligned(16)));
} __attribute__((aligned(16)));
#define tc ((struct tctx *)((u64)__builtin_frame_address(0) & ~(u64)(FT_STACK - 1)))
#define ZONE (cache + BC_N * 512 + (u64)tc->idx * BLOC_N * 512) /* sa zone de transfert */

static volatile u32 big;      /* tout l'état de fatfs */
static volatile u32 recv;     /* un seul fil prend la requête suivante (l'ordre des avis) */
static volatile u32 cond_seq; /* changements attendus (lecteurs finis, index construit, fichier rendu) */
static u32 readers;           /* (big) lecteurs en cours */
static u32 writers_waiting;   /* (big) écrivains qui attendent : pas de nouveau lecteur */

static void mutex_lock(volatile u32 *m)
{
	u32 c = 0;
	if (__atomic_compare_exchange_n(m, &c, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return;
	if (c != 2)
		c = __atomic_exchange_n(m, 2, __ATOMIC_ACQUIRE);
	while (c) {
		korin_sys3(SYS_FUTEX_WAIT, (long)m, 2, 0);
		c = __atomic_exchange_n(m, 2, __ATOMIC_ACQUIRE);
	}
}

static void mutex_unlock(volatile u32 *m)
{
	if (__atomic_fetch_sub(m, 1, __ATOMIC_RELEASE) != 1) {
		__atomic_store_n(m, 0, __ATOMIC_RELEASE);
		korin_sys2(SYS_FUTEX_WAKE, (long)m, 1);
	}
}

void ft_cond_wait(void)
{
	u32 s = __atomic_load_n(&cond_seq, __ATOMIC_ACQUIRE);
	mutex_unlock(&big);
	korin_sys3(SYS_FUTEX_WAIT, (long)&cond_seq, s, 0);
	mutex_lock(&big);
}

void ft_cond_broadcast(void)
{
	__atomic_fetch_add(&cond_seq, 1, __ATOMIC_RELEASE);
	korin_sys2(SYS_FUTEX_WAKE, (long)&cond_seq, 0x7FFFFFFF);
}

static void reader_begin(void)
{
	mutex_lock(&big);
	while (writers_waiting)
		ft_cond_wait();
	readers++;
	tc->mode = MODE_R;
}

static void writer_begin(void)
{
	mutex_lock(&big);
	writers_waiting++;
	while (readers)
		ft_cond_wait();
	writers_waiting--;
	tc->mode = MODE_W;
}

/* la fin de la requête : le fichier tenu rendu, le verrou rendu */
static void op_end(void)
{
	if (tc->held) {
		tc->held->busy = 0;
		tc->held = 0;
		ft_cond_broadcast();
	}
	if (tc->mode == MODE_R && !--readers)
		ft_cond_broadcast(); /* (un écrivain attend peut-être) */
	if (tc->mode == MODE_W && writers_waiting)
		ft_cond_broadcast();
	tc->mode = 0;
	mutex_unlock(&big);
}

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
	struct msg *mb = &tc->msg_blk;
	mb->op = op;
	mb->a = sector;
	mb->b = count;
	mb->c = (i64)addr;
	mb->len = 0;
	if (tc->mode != MODE_R || (op != BLK_READ && op != BLK_READ_D))
		return korin_sys2(SYS_SVC_CALL, canal_blk, mb);
	mutex_unlock(&big); /* (lecteur : les autres fils servent pendant la lecture) */
	long r = korin_sys2(SYS_SVC_CALL, canal_blk, mb);
	mutex_lock(&big);
	return r;
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
	/* lu dans un tampon du fil (le verrou peut être relâché pendant la lecture : un autre fil
	   peut prendre cette place entre-temps), puis copié dans le cache */
	u8 *t = tc->sec;
	if (!cc_read(sector, 1, t)) {
		u8 *zone = ZONE;
		if (blk_request(BLK_READ, sector, 1, zone))
			return 0;
		memmove(t, zone, 512);
	}
	if (bc_dirty[slot]) { /* (pris entre-temps par un secteur différé : écrit d'abord) */
		bc_dirty[slot] = 0;
		if (bc_tags[slot] && blk_request(BLK_WRITE, bc_tags[slot] - 1, 1, p))
			return 0;
	}
	memmove(p, t, 512);
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
	u8 *zone = ZONE; /* zone de transfert */
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
	u8 *zone = ZONE; /* zone de transfert : après le cache */
	while (n) {
		u64 k = n < BLOC_N ? n : BLOC_N;
		if (write) {
			memmove(zone, buf, k << 9);
			if (blk_request(BLK_WRITE, sector, k, zone))
				return 0;
			bc_oublier(sector, k); /* secteurs écrits : hors du cache */
		} else {
			u64 got = 0;
			if (!tc->cc_skip)
				got = cc_read(sector, n, buf);
			if (got) {
				k = got;
			} else if (tc->direct && !((u64)buf & 3) && !blk_request(BLK_READ_D, sector, n, buf)) {
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
	if (*(u32 *)(nom_svc + 3) == 0x746f6f72 && !nom_svc[7]) /* « fs.root » : le volume du système */
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
	u8 *zone = ZONE; /* zone de transfert du fil */
	if (blk_request(BLK_READ, block << CC_BLOCK_SHIFT, BLOC_N, zone))
		return 0;
	i = cc_find(block); /* (verrou relâché pendant la lecture : un autre fil l'a peut-être mis) */
	if (i >= 0) {
		cc_refs[i] = 1;
		return cc_base + ((u64)i << 16);
	}
	if (!cc_base || cc_off) /* (vidé ou éteint entre-temps) */
		return 0;
	i = cc_take();
	if (i < 0)
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
			di_drop_all(); /* (l'index des répertoires aussi : une mesure à froid) */
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

long vol_lire_rep(struct it *it, struct de *de)
{
	return it->vol->type == 2 ? exfat_dir_read(it, de) : dir_read(it, de);
}

/* le composant name (fin : 0 ou « / ») du répertoire dir -> 1 et *de, 0 absent : par l'index
 * (dirindex.c), sinon en lisant le répertoire (comme dir_find) */
static long vol_composant(struct vol *v, u64 dir, const char *name, struct de *de)
{
	long r = di_lookup(v, dir, name, de);
	if (r >= 0)
		return r;
	struct it it;
	dir_open(&it, v, dir);
	while (vol_lire_rep(&it, de))
		if (v->type == 2 ? exfat_name_equal(v, name, de->name) : fat_name_equal(name, de->name))
			return 1;
	return 0;
}

/* chemin relatif au volume (« a/b/c ») -> 1 si trouvé ; vide : la racine (comme fat_lookup et
 * exfat_lookup, chaque répertoire par son index) */
static long vol_chercher(struct vol *v, const char *p, struct de *de)
{
	u64 cur = v->root;
	de->h.cluster = cur; /* racine : description synthétique */
	de->h.fsize = 0;
	de->mtime = 0;
	de->h.attr = ATTR_DIR;
	de->h.dir = 0;
	de->h.index = de->h.first = 0;
	de->name[0] = 0;
	for (;;) {
		while (*p == '/')
			p++;
		if (!*p)
			return 1;
		if (!(de->h.attr & ATTR_DIR)) /* il faut descendre dans un répertoire */
			return 0;
		if (!vol_composant(v, cur, p, de))
			return 0;
		cur = de->h.cluster;
		if (!cur && v->type != 2) /* FAT32 : « .. » vers la racine, cluster 0 */
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

static long vol_creer(struct vol *v, u64 parent, const char *name, u64 attr, struct de *de)
{
	long r = v->type == 2 ? exfat_create(v, parent, name, attr, de) : fat_create(v, parent, name, attr, de);
	if (r == 1)
		di_created(v, parent, name, de);
	else
		di_drop(parent); /* (à moitié fait : relu) */
	return r;
}

static void vol_maj(struct vol *v, struct dehead *de)
{
	if (v->type == 2)
		exfat_update_entry(v, de);
	else
		fat_update_entry(v, de);
	di_updated(v, de);
}

/* supprime l'entrée et libère son allocation */
static void vol_effacer(struct vol *v, struct dehead *de)
{
	di_erased(v, de);
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
	di_erased(v, de);
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
	tc->cc_skip = f->size > CC_FILE_MAX; /* un très gros fichier : hors du cache */
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

/* (lecteur) le fichier ouvert f est tenu jusqu'à la fin de l'op : une autre op sur lui attend
 * -> 0, ou E_INVAL s'il a été fermé pendant l'attente */
static long fo_tenir(struct fo *f)
{
	if (tc->mode != MODE_R)
		return 0;
	while (f->busy) {
		ft_cond_wait();
		if (!f->type)
			return E_INVAL;
	}
	f->busy = 1;
	tc->held = f;
	return 0;
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
	if (*err)
		return 0;
	*err = fo_tenir(f); /* (fils : une op à la fois sur un fichier ouvert) */
	if (!*err && !f->type)
		*err = E_INVAL;
	if (!*err && tc->held == f) /* (attendu : fermé puis rouvert entre-temps ?) */
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

/* (fin d'un process) num, qui va être fermé : doit-il écrire, ou une op s'en sert-elle ? */
static long fin_ecrit(long num)
{
	struct fo *f = fo_de(num);
	if (!f)
		return 0;
	if (f->busy)
		return 1;
	struct no *n = f->node;
	return n && (n->efface ? n->refs == 1 : n->dirty != 0);
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

#define de_op (tc->de_op) /* (l'asm : sur la pile de chaque op) */
#define E_RETRY_W (-1000) /* (lecteur) l'op doit écrire : refaite en écrivain */


/* 1 ouvrir(a drapeaux, chemin) -> b numéro, c attributs, d taille */
static long op_ouvrir(struct msg *m)
{
	struct de *de = &de_op;
	long extras = m->c; /* (gardés ici : le verrou peut être relâché pendant la recherche) */
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
	m->c = extras;
	open_extras_take(m);
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
	if (tc->mode == MODE_R) { /* la dernière fermeture d'un fichier qui doit écrire : en écrivain */
		u32 pid = m->pid;
		long left = fo_own_count(m->a);
		int last = !pid || pid == SVC_PID_KERNEL || left - (fo_own_find(m->a, pid) != 0) <= 0;
		struct no *n = f->node;
		if (last && n && (n->efface ? n->refs == 1 : n->dirty != 0))
			return E_RETRY_W;
	}
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
	long va = korin_sys4(SYS_SVC_MAP_CALLER, tc->req, m->d, m->c, MC_WRITE);
	if (va < 0)
		return va;
	fo_position(f, m);
	fo_pos_req(f, m);
	tc->direct = 1;
	long r = vol_rw(f, (u8 *)va, m->c, 0);
	tc->direct = 0;
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

/* les ops : mode lecteur (MODE_R, en parallèle) ou écrivain (MODE_W, seul) */
static const struct {
	long op;
	long (*fn)(struct msg *);
	int mode;
} ops[] = {
	{FS_OPEN, op_ouvrir, MODE_R},      {FS_STAT, op_stat, MODE_R},        {FS_MKDIR, op_mkdir, MODE_W},
	{FS_UNLINK, op_unlink, MODE_W},    {FS_READ, op_lire, MODE_R},        {FS_WRITE, op_ecrire, MODE_W},
	{FS_READDIR, op_lire_rep, MODE_R}, {FS_BUFFER, op_tampon, MODE_R},    {FS_READ_BUF, op_lire_t, MODE_R},
	{FS_WRITE_BUF, op_ecrire_t, MODE_W}, {FS_UTIME, op_utime, MODE_W},    {FS_RENAME, op_rename, MODE_W},
	{FS_READ_DIRECT, op_lire_d, MODE_R}, {FS_CACHE, op_cache, MODE_W},    {FS_INFO, op_info, MODE_R},
	{FS_SYNC, op_sync, MODE_W},        {FS_SEEK, op_position, MODE_R},    {FS_CLOSE, op_fermer, MODE_R},
	{0, 0, 0},
};

static u8 *pile_neuve(void)
{
	u8 *p = malloc(2 * FT_STACK);
	if (!p)
		return 0;
	p = (u8 *)(((u64)p + FT_STACK - 1) & ~(u64)(FT_STACK - 1));
	memset(p, 0, sizeof(struct tctx));
	return p;
}

/* (recv tenu) la requête req de tc->msg : son mode pris, recv rendu, traitée, répondue */
static void traiter(long req)
{
	struct msg *m = &tc->msg;
	u64 op = m->op;
	if (op == SVC_OP_BIRTH) { /* (avis sans réponse, rien à faire) */
		mutex_unlock(&recv);
		return;
	}
	if (op == SVC_OP_FORK) { /* fork : le fils tient les fichiers du père (sans écriture : en
				    lecteur, mais pris avant les requêtes qui suivent, sous recv) */
		reader_begin();
		mutex_unlock(&recv);
		fo_own_fork(m->a, m->b);
		op_end();
		return;
	}
	if (op == SVC_OP_END) { /* fin d'un process : ses fichiers ; en écrivain seulement si l'un
				   d'eux doit écrire ou sert encore à une op */
		reader_begin();
		mutex_unlock(&recv);
		if (fo_own_any_last(m->a, fin_ecrit)) {
			op_end();
			writer_begin();
			fin_pid(m->a);
			fat_fsinfo_flush(&volume);
		} else {
			fin_pid(m->a);
		}
		op_end();
		return;
	}
	if (op == SVC_OP_SIGNAL) { /* le noyau manque de pages */
		writer_begin();
		mutex_unlock(&recv);
		cc_give_back();
		op_end();
		return;
	}
	int k = -1;
	for (int i = 0; ops[i].op; i++)
		if ((u64)ops[i].op == op) {
			k = i;
			break;
		}
	int mode = k < 0 ? MODE_W : ops[k].mode;
	if (op == FS_OPEN && (m->a & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND)))
		mode = MODE_W;
	struct msg save __attribute__((aligned(16)));
	if (mode == MODE_R && op == FS_CLOSE)
		memcpy(&save, m, sizeof save); /* (refaite en écrivain au besoin) */
	if (mode == MODE_R)
		reader_begin();
	else
		writer_begin();
	mutex_unlock(&recv);
	tc->req = req;
	long r = k < 0 ? E_INVAL : ops[k].fn(m);
	if (r == E_RETRY_W) { /* (une fermeture qui écrit) */
		op_end();
		memcpy(m, &save, sizeof save);
		writer_begin();
		tc->req = req;
		r = ops[k].fn(m);
	}
	if (tc->mode == MODE_W)
		fat_fsinfo_flush(&volume); /* FSInfo réécrit si les clusters libres ont changé */
	op_end();
	if (op == FS_CLOSE && !m->pid) /* avis du noyau (pid 0) : sans réponse */
		return;
	m->op = r;
	korin_sys2(SYS_SVC_REPLY, req, m);
}

/* un fil : prend la requête suivante (seul, sous recv), puis la traite */
static THREAD_ENTRY void servir(void *arg)
{
	(void)arg;
	for (;;) {
		mutex_lock(&recv);
		long req = korin_sys3(SYS_SVC_NEXT, fs_canal, &tc->msg, 60000);
		if (req < 0)
			SVC_QUIT();
		if (!req) {
			mutex_unlock(&recv);
			writer_begin();
			cc_pressure(); /* (au repos aussi : mémoire libre) */
			op_end();
			continue;
		}
		traiter(req);
	}
}

static int ft_count;

/* le premier fil : le périphérique, le volume, l'inscription, les autres fils, puis il sert */
static THREAD_ENTRY void demarrer(void *arg)
{
	(void)arg;
	tc->idx = 0;
	tc->mode = MODE_W; /* (seul pour l'instant) */
	struct msg *mb = &tc->msg_blk;
	canal_blk = korin_sys2(SYS_SVC_OPEN, nom_blk, 3000);
	if (canal_blk < 0)
		SVC_QUIT();
	if (blk_request(BLK_INFO, 0, 0, 0) < 0) /* lecture seule ? son pid */
		SVC_QUIT();
	cc_disk = mb->b; /* (secteurs : fin du cache) */
	if (mb->a)
		lecture_seule = 1;
	long blk_pid = mb->d;
	long num = korin_sys1(SYS_BUF_CREATE, PARTAGE); /* le tampon partagé */
	if (num < 0)
		SVC_QUIT();
	cache = (u8 *)(BUF_VA_BASE + (u64)num * BUF_SLOT);
	korin_sys2(SYS_BUF_GRANT, num, blk_pid);
	mb->op = BLK_ATTACH; /* attacher */
	mb->a = num;
	mb->b = PARTAGE;
	if (korin_sys2(SYS_SVC_CALL, canal_blk, mb) < 0)
		SVC_QUIT();
	cc_init();
	if (!vol_monter(&volume, 0)) /* le système de fichiers */
		SVC_QUIT();
	/* prévenu des fins de process, des fork, mémoire rare */
	fs_canal = korin_sys2(SYS_SVC_REGISTER, nom_svc, SVF_ENDS | SVF_BIRTHS | SVF_MEMORY);
	if (fs_canal < 0)
		SVC_QUIT();
	tc->mode = 0;
	for (int i = 1; i < FT_N; i++) {
		u8 *st = pile_neuve();
		if (!st)
			break;
		((struct tctx *)st)->idx = i;
		if (korin_sys3(SYS_THREAD_CREATE, servir, st + FT_STACK, 0) < 0)
			break;
		ft_count++;
	}
	servir(0);
}

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
	u8 *st = pile_neuve(); /* tout fatfs tourne sur des piles à contexte (tc) */
	if (!st)
		SVC_QUIT();
	long t = korin_sys3(SYS_THREAD_CREATE, demarrer, st + FT_STACK, 0);
	if (t < 0)
		SVC_QUIT();
	for (;;)
		if (korin_sys1(SYS_THREAD_JOIN, t) < 0)
			korin_sys1(SYS_SLEEP, 60000);
}
