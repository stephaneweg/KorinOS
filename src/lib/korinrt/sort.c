/* sort.c : tri fusion stable (docs/BIN-C.md) ; même ordre que la fusion de
 * sort.asm (moitié gauche = n / 2, l'élément de gauche gagne à égalité). */
#include "korinrt.h"

typedef int (*cmp3)(const void *, const void *, void *);

static void merge(void **v, size_t n, void **tmp, cmp3 cmp, void *ctx)
{
	if (n < 2)
		return;
	size_t l = n / 2;
	merge(v, l, tmp, cmp, ctx);
	merge(v + l, n - l, tmp, cmp, ctx);
	for (size_t i = 0; i < l; i++)
		tmp[i] = v[i];
	size_t i = 0, j = l, k = 0;
	while (i < l) {
		if (j < n && cmp(tmp[i], v[j], ctx) > 0)
			v[k++] = v[j++];
		else
			v[k++] = tmp[i++];
	}
}

/* tableau de pointeurs : tmp de n / 2 pointeurs pris au tas (ou 0 : rien
 * trié si la mémoire manque) */
void sort_ptrs(void **v, size_t n, cmp3 cmp, void *ctx)
{
	if (n < 2)
		return;
	void **tmp = malloc((n / 2 + 1) * sizeof(void *));
	if (!tmp)
		return;
	merge(v, n, tmp, cmp, ctx);
	free(tmp);
}

struct qctx {
	int (*cmp)(const void *, const void *);
};

static int qcmp(const void *a, const void *b, void *ctx)
{
	return ((struct qctx *)ctx)->cmp(a, b);
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	if (n < 2)
		return;
	void **v = malloc(n * sizeof(void *));
	u8 *copy = malloc(n * size);
	if (!v || !copy) {
		free(v);
		free(copy);
		return;
	}
	for (size_t i = 0; i < n; i++)
		v[i] = (u8 *)base + i * size;
	struct qctx c = { cmp };
	sort_ptrs(v, n, qcmp, &c);
	for (size_t i = 0; i < n; i++)
		memcpy(copy + i * size, v[i], size);
	memcpy(base, copy, n * size);
	free(copy);
	free(v);
}
