/* Archive ustar des premiers programmes (initrd.tar), et ce qui passe avant :
 * une seconde archive propre à la machine (local.tar) et des modules texte du
 * noyau (« bootpart »). Portable. */
#include <korin/initrd.h>
#include <korin/lib.h>

struct archive {
	const uint8_t *base;
	uint64_t total;
};

static struct archive initrd, local;

#define TEXTS_MAX 4
static struct {
	const char *name, *text;
} texts[TEXTS_MAX];

void initrd_init(const void *data, uint64_t size)
{
	initrd.base = data;
	initrd.total = size;
}

void initrd_init_local(const void *data, uint64_t size)
{
	local.base = data;
	local.total = size;
}

void initrd_set_text(const char *name, const char *text)
{
	for (int i = 0; i < TEXTS_MAX; i++)
		if (!texts[i].name || !strcmp(texts[i].name, name)) {
			texts[i].name = name;
			texts[i].text = text;
			return;
		}
}

static uint64_t octal(const char *s, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
		v = v * 8 + (uint64_t)(s[i] - '0');
	return v;
}

/* parcours : name sans « ./ », données, taille ; 1 pour s'arrêter */
static int walk(const struct archive *a, int (*fn)(const char *, const void *, uint64_t, void *), void *ctx)
{
	const uint8_t *base = a->base;
	uint64_t total = a->total;
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
	for (int i = 0; i < TEXTS_MAX && texts[i].name; i++)
		if (!strcmp(texts[i].name, name)) {
			*data = texts[i].text;
			*size = strlen(texts[i].text);
			return 0;
		}
	struct find_ctx f = {name, data, size};
	return walk(&local, find_cb, &f) || walk(&initrd, find_cb, &f) ? 0 : -1;
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
	for (int i = 0; i < TEXTS_MAX && texts[i].name; i++)
		kprintf("  %-16s (noyau) %s\n", texts[i].name, texts[i].text);
	if (local.base) {
		kprintf("  local.tar :\n");
		walk(&local, list_cb, 0);
	}
	if (!initrd.base) {
		kprintf("  pas d'initrd\n");
		return;
	}
	walk(&initrd, list_cb, 0);
}
