/*
 * json.h -- le JSON de korin-mcp (docs/MCP-KORIN.md) : un arbre lu (une requête de JSON-RPC, quelques Ko) et un
 * tampon qui grandit pour écrire les réponses. Sans dépendance : libposix (malloc, snprintf).
 */
#ifndef KORIN_MCP_JSON_H
#define KORIN_MCP_JSON_H

#include <stddef.h>
#include <stdint.h>

/* ---- lecture ---- */
enum jtype { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };

struct jv {
	enum jtype t;
	int b;              /* J_BOOL */
	double num;         /* J_NUM */
	char *s;            /* J_STR : UTF-8, terminée par 0 ; J_OBJ : clé de cet élément dans son parent */
	size_t len;         /* J_STR : octets */
	struct jv *kid;     /* J_ARR, J_OBJ : premier élément */
	struct jv *next;    /* l'élément suivant du parent */
	char *key;          /* clé (élément d'un objet) */
	const char *raw;    /* le texte de la valeur dans l'entrée (pour renvoyer un « id » tel quel) */
	size_t rawlen;
};

/* texte -> arbre (0 : JSON faux) ; json_free le rend */
struct jv *json_parse(const char *text, size_t len);
void json_free(struct jv *v);
/* le membre d'un objet (0 : absent ou pas un objet) */
struct jv *json_get(const struct jv *obj, const char *key);
/* raccourcis : la valeur, ou def si absente ou d'un autre type */
const char *json_str(const struct jv *obj, const char *key, const char *def);
long long json_int(const struct jv *obj, const char *key, long long def);
int json_bool(const struct jv *obj, const char *key, int def);

/* ---- écriture ---- */
struct jb {
	char *p;
	size_t n, cap;
	int need_comma; /* une valeur vient d'être écrite à ce niveau */
	int oom;
};

void jb_init(struct jb *b);
void jb_free(struct jb *b);
void jb_raw(struct jb *b, const char *s, size_t n);
void jb_rawz(struct jb *b, const char *s);
/* objets et tableaux ; jb_key écrit « "clé": » (et la virgule d'avant) */
void jb_obj(struct jb *b);
void jb_obj_end(struct jb *b);
void jb_arr(struct jb *b);
void jb_arr_end(struct jb *b);
void jb_key(struct jb *b, const char *k);
/* valeurs (la virgule d'avant si besoin) */
void jb_str(struct jb *b, const char *s);
void jb_strn(struct jb *b, const char *s, size_t n);
void jb_int(struct jb *b, long long v);
void jb_uint(struct jb *b, unsigned long long v);
void jb_num(struct jb *b, double v, int decimals);
void jb_bool(struct jb *b, int v);
void jb_null(struct jb *b);
/* un texte JSON déjà fait (un « id » rendu tel quel) */
void jb_json(struct jb *b, const char *s, size_t n);
/* clé + valeur */
void jb_kstr(struct jb *b, const char *k, const char *s);
void jb_kint(struct jb *b, const char *k, long long v);
void jb_kbool(struct jb *b, const char *k, int v);
void jb_knum(struct jb *b, const char *k, double v, int decimals);
/* base64 d'octets (une chaîne JSON) */
void jb_base64(struct jb *b, const uint8_t *d, size_t n);

/* le texte est-il de l'UTF-8 valide (sans NUL) ? */
int utf8_valid(const uint8_t *s, size_t n);

#endif
