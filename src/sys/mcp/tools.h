/* tools.h -- les outils de korin-mcp (tools.c, files.c, docs/MCP-KORIN.md) */
#ifndef KORIN_MCP_TOOLS_H
#define KORIN_MCP_TOOLS_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"

/* ce que rend un outil : du JSON (j), et peut-être une image PNG ; err non vide : l'outil a échoué (isError) */
struct result {
	struct jb j;
	uint8_t *png;
	size_t png_len;
	char err[600];
};

struct tool {
	const char *name;
	const char *title;
	const char *description;
	const char *schema;   /* inputSchema (JSON) */
	int read_only;        /* annotations : readOnlyHint (sinon destructiveHint selon destructive) */
	int destructive;
	void (*run)(const struct jv *args, struct result *r);
};

extern const struct tool tools[];
extern const int ntools;

/* une erreur rendue par l'outil (printf) */
void fail(struct result *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* outils des fichiers (files.c) */
void t_list_dir(const struct jv *a, struct result *r);
void t_read_file(const struct jv *a, struct result *r);
void t_find_files(const struct jv *a, struct result *r);
void t_volumes(const struct jv *a, struct result *r);
void t_packages(const struct jv *a, struct result *r);

/* une date (secondes UTC) en ISO 8601 : « 2026-11-22T17:05:09Z » (t : 24 octets) */
void iso_time(long long secs, char *t);
/* le chemin est-il un fichier secret que l'outil ne lit pas ? (texte de la raison, 0 : non) */
const char *secret_path(const char *abs);

#endif
