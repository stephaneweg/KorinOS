/*
 * json.h -- libacp : juste assez de JSON pour l'Agent Client Protocol (docs/PYTHON.md §5) : lire un message en arbre
 * (objets, tableaux, chaînes décodées en UTF-8, nombres gardés tels qu'écrits, vrai/faux/nul) et en écrire un dans un
 * tampon. Le lecteur reprend celui de PocketQI (src/apps/pocketqi/json.h), en C.
 */
#ifndef ACP_JSON_H
#define ACP_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ };

typedef struct json {
	int kind;
	int b;			/* JSON_BOOL */
	char *s;		/* JSON_STR : le texte (UTF-8, terminé par 0) ; JSON_NUM : le nombre écrit */
	size_t len;		/* longueur de s */
	int n, cap;		/* JSON_ARR, JSON_OBJ : éléments */
	struct json *items;
	char **keys;		/* JSON_OBJ */
} json_t;

json_t *json_parse (const char *text, size_t len);	/* NULL si mal formé */
void json_free (json_t *v);
const json_t *json_get (const json_t *v, const char *key);	/* NULL si absent ou pas un objet */
const json_t *json_at (const json_t *v, int i);
const json_t *json_path (const json_t *v, const char *path);	/* "a.b.c" */
const char *json_text (const json_t *v);	/* chaîne ou nombre ; "" sinon (jamais NULL) */
const char *json_gets (const json_t *v, const char *key);	/* json_text (json_get (v, key)) */
long long json_int (const json_t *v, long long def);
int json_len (const json_t *v);		/* éléments d'un tableau ou d'un objet */

/* tampon d'écriture */
typedef struct jbuf {
	char *p;
	size_t n, cap;
} jbuf;

void jb_init (jbuf *b);
void jb_free (jbuf *b);
void jb_putn (jbuf *b, const char *s, size_t n);
void jb_puts (jbuf *b, const char *s);
void jb_putc (jbuf *b, char c);
void jb_printf (jbuf *b, const char *fmt, ...);
void jb_str (jbuf *b, const char *s);		/* chaîne JSON échappée, guillemets compris */
void jb_strn (jbuf *b, const char *s, size_t n);
void jb_json (jbuf *b, const json_t *v);	/* réécrit une valeur lue */
const char *jb_cstr (jbuf *b);			/* terminé par 0 */

#ifdef __cplusplus
}
#endif

#endif
