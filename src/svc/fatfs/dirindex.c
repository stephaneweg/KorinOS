/* dirindex.c : un index par répertoire, en mémoire (22/11/2026, docs/agents/FICHIERS-PERF.md,
 * point 2). Un répertoire est lu une fois en entier (vol_lire_rep : FAT32 ou exFAT, noms longs
 * reconstruits) ; ses noms vont dans une table de hachage. Une recherche de nom, trouvé ou non,
 * ne relit plus le répertoire : dir_find devient O(1) au lieu de (taille du dossier) x (lectures
 * d'entrées, noms longs UTF-16 -> UTF-8, comparaisons).
 *  * Clé : le premier cluster du répertoire ((u32) de la poignée exFAT, comme no_trouver).
 *  * Hachage des noms en minuscules ASCII (name_equal de FAT32 ne replie que l'ASCII). exFAT
 *    compare par sa table de majuscules : un nom demandé hors de l'ASCII y est comparé entrée
 *    par entrée (dans l'index, toujours sans lire le disque).
 *  * Tenu à jour par fatfs lui-même (un seul fatfs par volume, vfs.c) : création (di_created),
 *    entrée réécrite (di_updated), effacée (di_erased). exFAT : l'index du répertoire changé est
 *    simplement jeté (relu à la recherche suivante) ; FAT32 : mis à jour en place.
 *  * DI_MAX répertoires, DI_BYTES au plus en tout (le plus ancien d'abord rendu) ; un répertoire
 *    trop gros pour le budget n'est pas indexé (recherche linéaire, comme avant).
 *  * Fils de fatfs : sous le verrou de fatfs ; un index se construit avec le verrou relâché
 *    pendant les lectures du disque (lecteurs en parallèle) : il est « en construction », les
 *    autres fils qui le veulent attendent (ft_cond_wait) ; les écrivains, exclusifs, n'en voient
 *    jamais un en construction. */
#include "fatfs.h"

#if defined(__x86_64__)
#define DI_MAX 1024
#define DI_BYTES (96ULL << 20)
#else
#define DI_MAX 256
#define DI_BYTES (8ULL << 20)
#endif
#define DI_FREE 0
#define DI_BUILDING 1
#define DI_READY 2
#define DI_NONE 0xFFFFFFFFu
#define DI_HINTS 4096

struct di_e {
	u32 hash;
	u32 next;  /* suivante de la même case (DI_NONE : fin) */
	u32 name;  /* décalage du nom dans names */
	u32 mtime;
	struct dehead h;
};

struct dindex {
	u32 state;
	u32 key;
	u64 stamp;
	u32 n, cap;     /* entrées (effacées comprises), place */
	u32 nb;         /* cases (puissance de 2) */
	u32 live;
	u32 *buckets;
	struct di_e *e;
	char *names;
	u64 nlen, ncap;
	u64 bytes;
};

static struct dindex di[DI_MAX];
static u32 di_hint[DI_HINTS]; /* clé -> indice + 1 (un indice, vérifié) */
static u64 di_clock, di_total;
u64 di_hits, di_builds, di_drops;

static u32 di_hash(const char *s, int *ascii)
{
	u32 h = 2166136261u;
	int a = 1;
	for (;; s++) {
		u8 c = (u8)*s;
		if (!c || c == '/')
			break;
		if (c >= 'A' && c <= 'Z')
			c += 32;
		if (c >= 0x80)
			a = 0;
		h = (h ^ c) * 16777619u;
	}
	if (ascii)
		*ascii = a;
	return h | 1;
}

static void di_free(struct dindex *d)
{
	free(d->buckets);
	free(d->e);
	free(d->names);
	di_total -= d->bytes;
	memset(d, 0, sizeof *d);
}

static struct dindex *di_find(u32 key)
{
	u32 hi = (key * 2654435761u) % DI_HINTS;
	u32 x = di_hint[hi];
	if (x && di[x - 1].state != DI_FREE && di[x - 1].key == key)
		return &di[x - 1];
	for (int i = 0; i < DI_MAX; i++)
		if (di[i].state != DI_FREE && di[i].key == key) {
			di_hint[hi] = (u32)i + 1;
			return &di[i];
		}
	return 0;
}

/* le répertoire de clé key oublié (changé, exFAT ; ou rendu) */
void di_drop(u64 dir)
{
	struct dindex *d = di_find((u32)dir);
	if (d && d->state == DI_READY) {
		di_free(d);
		di_drops++;
	}
}

/* tout oublié (montage, cache vidé) */
void di_drop_all(void)
{
	for (int i = 0; i < DI_MAX; i++)
		if (di[i].state == DI_READY)
			di_free(&di[i]);
}

/* place pour bytes de plus : les plus anciens rendus -> 1, 0 si impossible */
static int di_room(u64 bytes, struct dindex *keep)
{
	while (di_total + bytes > DI_BYTES) {
		struct dindex *old = 0;
		for (int i = 0; i < DI_MAX; i++)
			if (di[i].state == DI_READY && &di[i] != keep && (!old || di[i].stamp < old->stamp))
				old = &di[i];
		if (!old)
			return 0;
		di_free(old);
	}
	return 1;
}

static int di_rehash(struct dindex *d, u32 nb)
{
	u32 *b = malloc((u64)nb * 4);
	if (!b)
		return 0;
	memset(b, 0xFF, (u64)nb * 4);
	for (u32 i = 0; i < d->n; i++) /* (dans l'ordre : la première entrée d'un nom reste devant) */
		d->e[i].next = DI_NONE;
	for (u32 i = d->n; i--;) {
		struct di_e *x = &d->e[i];
		if (!x->hash)
			continue;
		u32 k = x->hash & (nb - 1);
		x->next = b[k];
		b[k] = i;
	}
	d->bytes -= (u64)d->nb * 4;
	d->bytes += (u64)nb * 4;
	di_total += (u64)nb * 4 - (u64)d->nb * 4;
	free(d->buckets);
	d->buckets = b;
	d->nb = nb;
	return 1;
}

/* une entrée de plus (nom, tête, date) -> 1, 0 : plus de mémoire (l'index est alors à jeter) */
static int di_add(struct dindex *d, const char *name, const struct dehead *h, u32 mtime)
{
	u64 ln = 0;
	while (name[ln] && name[ln] != '/')
		ln++;
	if (d->n == d->cap) {
		u32 cap = d->cap ? d->cap * 2 : 64;
		u64 more = (u64)(cap - d->cap) * sizeof(struct di_e);
		if (!di_room(more, d))
			return 0;
		struct di_e *e = realloc(d->e, (u64)cap * sizeof(struct di_e));
		if (!e)
			return 0;
		d->e = e;
		d->cap = cap;
		d->bytes += more;
		di_total += more;
	}
	if (d->nlen + ln + 1 > d->ncap) {
		u64 cap = d->ncap ? d->ncap * 2 : 1024;
		while (cap < d->nlen + ln + 1)
			cap *= 2;
		if (!di_room(cap - d->ncap, d))
			return 0;
		char *p = realloc(d->names, cap);
		if (!p)
			return 0;
		d->bytes += cap - d->ncap;
		di_total += cap - d->ncap;
		d->names = p;
		d->ncap = cap;
	}
	if (d->live + 1 > d->nb / 2) { /* (charge d'au plus 1/2) */
		u32 nb = d->nb ? d->nb * 2 : 64;
		if (!di_room((u64)nb * 4, d) || !di_rehash(d, nb))
			return 0;
	}
	struct di_e *x = &d->e[d->n];
	x->hash = di_hash(name, 0);
	x->name = (u32)d->nlen;
	x->mtime = mtime;
	x->h = *h;
	memcpy(d->names + d->nlen, name, ln);
	d->names[d->nlen + ln] = 0;
	d->nlen += ln + 1;
	/* au bout de sa case : la première entrée d'un même nom reste devant (comme dir_find) */
	u32 k = x->hash & (d->nb - 1);
	x->next = DI_NONE;
	u32 *link = &d->buckets[k];
	while (*link != DI_NONE)
		link = &d->e[*link].next;
	*link = d->n;
	d->n++;
	d->live++;
	return 1;
}

/* (verrou de fatfs) l'index du répertoire dir, construit au besoin -> 0 : pas d'index (trop gros,
 * mémoire) : chercher sur le disque */
static struct dindex *di_get(struct vol *v, u64 dir)
{
	u32 key = (u32)dir;
	struct dindex *d;
	while ((d = di_find(key)) && d->state == DI_BUILDING)
		ft_cond_wait(); /* (un autre fil le lit) */
	if (d) {
		d->stamp = ++di_clock;
		return d;
	}
	for (int i = 0; i < DI_MAX && !d; i++)
		if (di[i].state == DI_FREE)
			d = &di[i];
	if (!d) { /* toutes prises : la plus ancienne prête */
		for (int i = 0; i < DI_MAX; i++)
			if (di[i].state == DI_READY && (!d || di[i].stamp < d->stamp))
				d = &di[i];
		if (!d)
			return 0;
		di_free(d);
	}
	d->state = DI_BUILDING;
	d->key = key;
	d->stamp = ++di_clock;
	struct de tmp;
	struct it it;
	dir_open(&it, v, dir);
	int ok = 1;
	while (ok && vol_lire_rep(&it, &tmp)) /* (verrou relâché pendant les lectures) */
		ok = di_add(d, tmp.name, &tmp.h, (u32)tmp.mtime);
	if (!ok) {
		di_free(d);
		ft_cond_broadcast();
		return 0;
	}
	d->state = DI_READY;
	di_builds++;
	ft_cond_broadcast();
	return d;
}

/* -> 1 et *de si name (fin : 0 ou « / ») est dans le répertoire dir ; 0 absent ; -1 : pas d'index */
long di_lookup(struct vol *v, u64 dir, const char *name, struct de *de)
{
	struct dindex *d = di_get(v, dir);
	if (!d)
		return -1;
	di_hits++;
	int ascii;
	u32 h = di_hash(name, &ascii);
	struct di_e *x = 0;
	if (!d->nb) /* (un répertoire vide : pas de cases) */
		return 0;
	if (v->type == 2 && !ascii) { /* exFAT, nom hors de l'ASCII : sa table de majuscules */
		for (u32 i = 0; i < d->n && !x; i++)
			if (d->e[i].hash && exfat_name_equal(v, name, d->names + d->e[i].name))
				x = &d->e[i];
	} else {
		for (u32 i = d->buckets[h & (d->nb - 1)]; i != DI_NONE; i = d->e[i].next) {
			struct di_e *y = &d->e[i];
			if (y->hash == h && fat_name_equal(name, d->names + y->name)) {
				x = y;
				break;
			}
		}
	}
	if (!x)
		return 0;
	de->h = x->h;
	de->mtime = x->mtime;
	de->pad = 0;
	u64 k = 0;
	for (const char *s = d->names + x->name; *s && k < NAME_BYTES - 1; s++)
		de->name[k++] = *s;
	de->name[k] = 0;
	return 1;
}

/* une entrée créée dans parent (nom, DE remplie par vol_creer) */
void di_created(struct vol *v, u64 parent, const char *name, const struct de *de)
{
	struct dindex *d = di_find((u32)parent);
	if (!d || d->state != DI_READY)
		return;
	if (v->type == 2) {
		di_drop(parent);
		return;
	}
	if (!di_add(d, name, &de->h, (u32)dos_maintenant()))
		di_drop(parent);
}

static struct di_e *di_entry_of(struct dindex *d, const struct dehead *h)
{
	for (u32 i = 0; i < d->n; i++)
		if (d->e[i].hash && d->e[i].h.index == h->index)
			return &d->e[i];
	return 0;
}

/* l'entrée de h (h->dir, h->index) réécrite (vol_maj : cluster, taille, attributs, date) */
void di_updated(struct vol *v, const struct dehead *h)
{
	struct dindex *d = di_find((u32)h->dir);
	if (!d || d->state != DI_READY)
		return;
	if (v->type == 2) {
		di_drop(h->dir);
		return;
	}
	struct di_e *x = di_entry_of(d, h);
	if (!x) {
		di_drop(h->dir);
		return;
	}
	x->h.cluster = h->cluster; /* (comme fat_update_entry) */
	x->h.fsize = (u32)h->fsize;
	x->h.attr = (u8)h->attr;
	x->mtime = (u32)dos_maintenant();
}

/* l'entrée de h effacée */
void di_erased(struct vol *v, const struct dehead *h)
{
	struct dindex *d = di_find((u32)h->dir);
	if (!d || d->state != DI_READY)
		return;
	if (v->type == 2) {
		di_drop(h->dir);
		return;
	}
	struct di_e *x = di_entry_of(d, h);
	if (!x) {
		di_drop(h->dir);
		return;
	}
	u32 i = (u32)(x - d->e);
	if (!d->nb)
		return;
	u32 *link = &d->buckets[x->hash & (d->nb - 1)];
	while (*link != DI_NONE && *link != i)
		link = &d->e[*link].next;
	if (*link == i)
		*link = x->next;
	x->hash = 0;
	d->live--;
}

/* (fatfs.c, op_cache) octets tenus, répertoires */
void di_stats(u64 *bytes, u64 *dirs)
{
	u64 n = 0;
	for (int i = 0; i < DI_MAX; i++)
		n += di[i].state == DI_READY;
	*bytes = di_total;
	*dirs = n;
}
