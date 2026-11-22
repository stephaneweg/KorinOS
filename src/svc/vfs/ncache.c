/* ncache.c : le cache des noms de vfs (22/11/2026, docs/agents/FICHIERS-PERF.md, point 1).
 *
 * Pour chaque chemin déjà demandé à un service de fichiers : présent, ou absent (avec l'erreur
 * rendue : E_NOENT, E_NOTDIR). Un open en lecture ou un stat d'un chemin connu absent est
 * répondu ici, sans aller au service (le cas des -I d'un compilateur : chaque #include essayé
 * dans des dizaines de dossiers) ; un open d'un chemin connu présent est transmis au service
 * comme avant (SVC_FORWARD) ; un open d'un chemin inconnu est fait par un appel, pour en
 * apprendre le résultat.
 *  * Clé : le canal du service et le chemin envoyé au service (rootpath compris : /boot et / d'une
 *    même partition partagent leurs entrées) ; en minuscules pour fatfs (FAT32 et exFAT
 *    comparent sans la casse), tel quel ailleurs.
 *  * Seulement les services que vfs est seul à changer : fatfs, ext4fs, ramfs (pas gdrivefs ni
 *    smbfs : d'autres machines y écrivent).
 *  * Invalidation par vfs lui-même, qui voit passer toute création, effacement, mkdir, renommage
 *    et montage : l'entrée du chemin créé ou effacé est retirée ; un renommage ou un montage
 *    vide tout (génération). Un résultat appris pendant qu'un changement passait (verrou relâché
 *    pendant l'appel) n'est pas gardé (compteur de changements).
 *  * NC_SETS ensembles de NC_WAYS entrées, remplacement de la plus ancienne de l'ensemble ; un
 *    chemin de plus de NC_PATH - 1 octets n'est pas gardé. Sous le verrou global de vfs. */
#include "vfs.h"

#define NC_SETS 2048
#define NC_WAYS 4
#define NC_PATH 200
#define NC_PRESENT 1

struct nc_entry {
	u64 hash;   /* 0 : libre */
	u32 gen;
	u32 stamp;  /* ordre d'arrivée (remplacement) */
	long chan;
	long state; /* NC_PRESENT, ou l'erreur (E_NOENT, E_NOTDIR) */
	u16 len;
	char path[NC_PATH];
};

static struct nc_entry nc[NC_SETS][NC_WAYS];
static u32 nc_gen = 1;  /* une entrée d'une autre génération est vide */
static u32 nc_stamp;
static u64 nc_changes;  /* tout changement vu (création, effacement, renommage, montage) */
static u64 nc_hits, nc_neg_hits, nc_learned;

/* le volume est-il de ceux que vfs est seul à changer ? 2 : sans la casse (fatfs) */
static int nc_kind(const struct volume *v)
{
	if (!strcmp(v->fs, "fatfs"))
		return 2;
	if (!strcmp(v->fs, "ext4fs") || !strcmp(v->fs, "ramfs"))
		return 1;
	return 0;
}

/* volume, chemin pour le service (avec rootpath), sortie -> longueur (0 : pas gardé) et le hachage */
static u64 nc_key(const struct volume *v, const char *svcpath, char *out, u64 *hash)
{
	int kind = nc_kind(v);
	if (!kind)
		return 0;
	u64 h = 1469598103934665603ULL ^ (u64)v->chan;
	u64 n = 0;
	for (const char *s = svcpath; *s; s++) {
		if (n >= NC_PATH - 1)
			return 0;
		char c = *s;
		if (kind == 2 && c >= 'A' && c <= 'Z')
			c += 32;
		out[n++] = c;
		h = (h ^ (u8)c) * 1099511628211ULL;
	}
	out[n] = 0;
	*hash = h | 1;
	return n;
}

static struct nc_entry *nc_find(long chan, const char *k, u64 n, u64 h)
{
	struct nc_entry *set = nc[h % NC_SETS];
	for (int i = 0; i < NC_WAYS; i++) {
		struct nc_entry *e = &set[i];
		if (e->hash == h && e->gen == nc_gen && e->chan == chan && e->len == n && !memcmp(e->path, k, n))
			return e;
	}
	return 0;
}

/* volume, chemin du service -> NC_PRESENT, l'erreur gardée (< 0), ou 0 : inconnu */
long nc_lookup(const struct volume *v, const char *svcpath)
{
	char k[NC_PATH];
	u64 h, n = nc_key(v, svcpath, k, &h);
	if (!n)
		return 0;
	struct nc_entry *e = nc_find(v->chan, k, n, h);
	if (!e)
		return 0;
	if (e->state < 0)
		nc_neg_hits++;
	else
		nc_hits++;
	return e->state;
}

/* le compteur des changements (avant un appel : nc_learn ne garde un résultat que s'il n'a pas bougé) */
u64 nc_seq(void)
{
	return nc_changes;
}

/* résultat d'un open en lecture ou d'un stat : r >= 0 présent ; E_NOENT, E_NOTDIR absent */
void nc_learn(const struct volume *v, const char *svcpath, long r, u64 seq)
{
	if (seq != nc_changes)
		return;
	long state;
	if (r >= 0)
		state = NC_PRESENT;
	else if (r == E_NOENT || r == E_NOTDIR)
		state = r;
	else
		return;
	char k[NC_PATH];
	u64 h, n = nc_key(v, svcpath, k, &h);
	if (!n)
		return;
	struct nc_entry *e = nc_find(v->chan, k, n, h);
	if (!e) { /* une place : libre, d'une autre génération, ou la plus ancienne */
		struct nc_entry *set = nc[h % NC_SETS];
		e = &set[0];
		for (int i = 0; i < NC_WAYS; i++) {
			struct nc_entry *x = &set[i];
			if (!x->hash || x->gen != nc_gen) {
				e = x;
				break;
			}
			if ((u32)(nc_stamp - x->stamp) > (u32)(nc_stamp - e->stamp))
				e = x;
		}
		e->hash = h;
		e->gen = nc_gen;
		e->chan = v->chan;
		e->len = (u16)n;
		memcpy(e->path, k, n + 1);
	}
	e->stamp = ++nc_stamp;
	e->state = state;
	nc_learned++;
}

/* un chemin qui va être créé ou effacé : son entrée retirée */
void nc_forget(const struct volume *v, const char *svcpath)
{
	nc_changes++;
	char k[NC_PATH];
	u64 h, n = nc_key(v, svcpath, k, &h);
	if (!n)
		return;
	struct nc_entry *e = nc_find(v->chan, k, n, h);
	if (e)
		e->hash = 0;
}

/* tout est oublié (renommage, montage, démontage) */
void nc_flush(void)
{
	nc_changes++;
	if (!++nc_gen) { /* (le tour du compteur : tout effacé) */
		memset(nc, 0, sizeof nc);
		nc_gen = 1;
	}
}

/* compteurs (journal) */
void nc_stats(u64 *hits, u64 *neg, u64 *learned)
{
	*hits = nc_hits;
	*neg = nc_neg_hits;
	*learned = nc_learned;
}
