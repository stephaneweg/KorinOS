/* pids.c : une fiche par process connu (dossier courant, entrées / sorties
 * standard), traduction de pids.inc : par blocs de PR_PER fiches ajoutés au
 * besoin et jamais déplacés (une fiche garde son adresse) ; pas de plafond.
 * Fiche créée à la naissance (ou au premier besoin), rendue à l'avis de fin. */
#include "vfs.h"

#define PR_PER 32 /* fiches par bloc */

struct pb {
	struct pb *next;
	u64 pad;
	struct pr recs[PR_PER];
};

static struct pb *pr_blocks;

struct pr *pid_find(long pid)
{
	if (!pid)
		return 0;
	for (struct pb *b = pr_blocks; b; b = b->next)
		for (int i = 0; i < PR_PER; i++)
			if (b->recs[i].pid == (u64)pid)
				return &b->recs[i];
	return 0;
}

/* -> sa fiche, créée vide s'il n'en a pas (0 : pid 0, ou plus de mémoire) */
struct pr *pid_get(long pid)
{
	struct pr *r = pid_find(pid);
	if (r || !pid)
		return r;
	for (struct pb *b = pr_blocks; b && !r; b = b->next) /* une fiche libre */
		for (int i = 0; i < PR_PER; i++)
			if (!b->recs[i].pid) {
				r = &b->recs[i];
				break;
			}
	if (!r) { /* un bloc de plus, en tête (malloc d'AppKit) */
		struct pb *b = ak_malloc(sizeof *b);
		if (!b)
			return 0;
		memset(b, 0, sizeof *b);
		b->next = pr_blocks;
		pr_blocks = b;
		r = &b->recs[0];
	}
	memset(r, 0, sizeof *r); /* (fiche à zéro : sans dossier, sans entrées / sorties) */
	r->pid = pid;
	return r;
}

void pid_forget(long pid)
{
	struct pr *r = pid_find(pid);
	if (r)
		r->pid = 0;
}

/* poignée : retirée des entrées / sorties standard de tous les process
 * (comme l'asm : les fiches libres comprises) */
void pid_stdio_drop(u64 h)
{
	for (struct pb *b = pr_blocks; b; b = b->next)
		for (int i = 0; i < PR_PER; i++)
			for (int k = 0; k < 3; k++)
				if (b->recs[i].stdio[k] == h)
					b->recs[i].stdio[k] = 0;
}
