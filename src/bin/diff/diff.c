/* diff - compare deux fichiers texte ligne à ligne : diff [-q] fichier1 fichier2 (docs/OUTILS.md),
 * comme diff.asm (d'après diff.c d'Onyx) : à la manière classique, « 3c3 », « 5a6,7 », « 8,9d7 »,
 * les lignes de fichier1 après « < », celles de fichier2 après « > », « --- » entre les deux.
 * -q (seulement en premier mot) : « Les fichiers A et B sont différents ». Code 0 identiques (octet
 * pour octet), 1 différents, 2 erreur (usage, fichier illisible ou dossier, trop de lignes).
 * Le début et la fin communs mis à part, la plus longue sous-suite commune du milieu (table de
 * 16 bits, 24 M cases au plus).
 *
 * Comme l'asm : les lignes se comparent jusqu'à leur premier octet nul ; quand un seul des deux
 * finit par une fin de ligne, « \ Pas de fin de ligne à la fin de fichierN » vient après tous les
 * morceaux (même s'il n'y en a aucun) ; l'entrée standard ne se lit pas (« - » est un nom). */
#include "korinrt.h"

#define DIFF_MAX (24 * 1024 * 1024) /* cases de la table */

static char **A, **B; /* les lignes du milieu */
static u16 *L;        /* L[i * (M + 1) + j] : sous-suite commune de A[i..], B[j..] */
static u64 N, M, W, pre;

/* chemin -> contenu (un 0 après), *n octets ; 0 : illisible ou dossier (slurp) */
static char *slurp(const char *path, u64 *n)
{
	long st[2];
	if (sys_stat(path, st) < 0 || (st[1] & ATTR_DIR))
		return 0;
	u64 size = (u64)st[0];
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	char *buf = malloc(size + 1);
	if (!buf) {
		sys_close(fd);
		return 0;
	}
	u64 got = 0;
	while (got != size) {
		long r = sys_read(fd, buf + got, size - got);
		if (r <= 0)
			break;
		got += (u64)r;
	}
	buf[got] = 0;
	sys_close(fd);
	*n = got;
	return buf;
}

/* texte (les fins de ligne deviennent des 0), octets -> lignes, *count ; *nl = 1 si le texte finit
 * par une fin de ligne (ou est vide) */
static char **lines(char *t, u64 n, u64 *count, int *nl)
{
	u64 c = 0;
	for (u64 i = 0; i < n; i++)
		if (t[i] == 10)
			c++;
	*nl = 1;
	if (n && t[n - 1] != 10) {
		*nl = 0;
		c++;
	}
	char **v = malloc((c + 1) * 8);
	u64 k = 0;
	char *s = t;
	for (u64 i = 0; i < n; i++)
		if (t[i] == 10) {
			t[i] = 0;
			v[k++] = s;
			s = t + i + 1;
		}
	if (k < c)
		v[k] = s;
	*count = c;
	return v;
}

/* « a » ou « a,b » */
static void range(u64 a, u64 b)
{
	out_putn(a);
	if (b > a) {
		out_putc(',');
		out_putn(b);
	}
}

/* un morceau : A[i0..i1[, B[j0..j1[ */
static void hunk(u64 i0, u64 j0, u64 i1, u64 j1)
{
	if (i1 == i0) {
		out_putn(pre + i0);
		out_putc('a');
		range(pre + j0 + 1, pre + j1);
	} else if (j1 == j0) {
		range(pre + i0 + 1, pre + i1);
		out_putc('d');
		out_putn(pre + j0);
	} else {
		range(pre + i0 + 1, pre + i1);
		out_putc('c');
		range(pre + j0 + 1, pre + j1);
	}
	out_nl();
	for (u64 i = i0; i < i1; i++) {
		out_puts("< ");
		out_puts(A[i]);
		out_nl();
	}
	if (i0 != i1 && j0 != j1)
		out_puts("---\n");
	for (u64 j = j0; j < j1; j++) {
		out_puts("> ");
		out_puts(B[j]);
		out_nl();
	}
}

int main(int argc, char **argv)
{
	int a = 1, quiet = 0;
	if (argc < 2)
		goto usage;
	if (str_eq(argv[1], "-q")) {
		quiet = 1;
		a = 2;
	}
	if (argc - a != 2)
		goto usage;
	u64 na, nb;
	char *ta = slurp(argv[a], &na);
	if (!ta) {
		err_file(argv[a]);
		return 2;
	}
	char *tb = slurp(argv[a + 1], &nb);
	if (!tb) {
		err_file(argv[a + 1]);
		return 2;
	}
	if (na == nb && !memcmp(ta, tb, na))
		return 0;
	if (quiet) {
		out_say("Les fichiers ");
		out_puts(argv[a]);
		out_say(" et ");
		out_puts(argv[a + 1]);
		out_say(" sont différents\n");
		return 1;
	}
	u64 n, m;
	int nla, nlb;
	A = lines(ta, na, &n, &nla);
	B = lines(tb, nb, &m, &nlb);
	u64 p = 0, s = 0;
	while (p < n && p < m && str_eq(A[p], B[p]))
		p++;
	while (s < n - p && s < m - p && str_eq(A[n - 1 - s], B[m - 1 - s]))
		s++;
	pre = p;
	A += p;
	B += p;
	N = n - p - s;
	M = m - p - s;
	if ((N + 1) * (M + 1) > DIFF_MAX)
		goto too_big;
	L = malloc((N + 1) * (M + 1) * 2);
	if (!L)
		goto too_big;
	W = M + 1;
	for (u64 i = N;; i--) {
		for (u64 j = M;; j--) {
			u64 k = i * W + j;
			u16 v;
			if (i == N || j == M)
				v = 0;
			else if (str_eq(A[i], B[j]))
				v = (u16)(L[k + W + 1] + 1);
			else {
				u16 d = L[k + W], r = L[k + 1];
				v = d >= r ? d : r;
			}
			L[k] = v;
			if (!j)
				break;
		}
		if (!i)
			break;
	}
	u64 i = 0, j = 0;
	for (;;) {
		if (i >= N && j >= M)
			break;
		if (i < N && j < M && str_eq(A[i], B[j])) {
			i++;
			j++;
			continue;
		}
		u64 i0 = i, j0 = j;
		for (;;) {
			if (i >= N && j >= M)
				break;
			if (i < N && j < M && str_eq(A[i], B[j]))
				break;
			if (j >= M)
				i++;
			else if (i >= N)
				j++;
			else if (L[i * W + j + W] >= L[i * W + j + 1])
				i++;
			else
				j++;
		}
		hunk(i0, j0, i, j);
	}
	if (nla != nlb) {
		out_puts("\\ ");
		out_say(nla ? "Pas de fin de ligne à la fin de fichier2\n"
			    : "Pas de fin de ligne à la fin de fichier1\n");
	}
	return 1;
too_big:
	err_msg("les fichiers diffèrent trop pour être comparés (trop de lignes)");
	return 2;
usage:
	err_msg("usage : diff [-q] fichier1 fichier2");
	return 2;
}
