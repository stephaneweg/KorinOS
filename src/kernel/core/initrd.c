/* Archive ustar des premiers programmes (initrd.tar). Portable. */
#include <korin/initrd.h>
#include <korin/lib.h>

static const uint8_t *base;
static uint64_t total;

void initrd_init(const void *data, uint64_t size)
{
	base = data;
	total = size;
}

static uint64_t octal(const char *s, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
		v = v * 8 + (uint64_t)(s[i] - '0');
	return v;
}

/* parcours : name sans « ./ », données, taille ; 1 pour s'arrêter */
static int walk(int (*fn)(const char *, const void *, uint64_t, void *), void *ctx)
{
	for (uint64_t off = 0; base && off + 512 <= total;) {
		const char *h = (const char *)base + off;
		if (!h[0])
			break;
		uint64_t size = octal(h + 124, 12);
		char name[101];
		memcpy(name, h, 100);
		name[100] = 0;
		const char *n = name;
		if (n[0] == '.' && n[1] == '/')
			n += 2;
		char type = h[156];
		if ((type == '0' || type == 0) && off + 512 + size <= total)
			if (fn(n, base + off + 512, size, ctx))
				return 1;
		off += 512 + ALIGN_UP(size, 512);
	}
	return 0;
}

struct find_ctx {
	const char *name;
	const void **data;
	uint64_t *size;
};

static int find_cb(const char *n, const void *d, uint64_t s, void *c)
{
	struct find_ctx *f = c;
	if (strcmp(n, f->name))
		return 0;
	*f->data = d;
	*f->size = s;
	return 1;
}

int initrd_find(const char *name, const void **data, uint64_t *size)
{
	struct find_ctx f = {name, data, size};
	return walk(find_cb, &f) ? 0 : -1;
}

static int list_cb(const char *n, const void *d, uint64_t s, void *c)
{
	(void)d;
	(void)c;
	kprintf("  %-16s %lu octets\n", n, s);
	return 0;
}

void initrd_list(void)
{
	if (!base) {
		kprintf("  pas d'initrd\n");
		return;
	}
	walk(list_cb, 0);
}
