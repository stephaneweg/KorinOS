/*
 * conf.c -- webd : /etc/web/sites.ini, lu et réécrit (docs/WEB.md).
 *
 *   # un commentaire
 *   [site atelier-photo]
 *   root    = HOME:/sites/atelier-photo
 *   bind    = http://(tout nom):80      (écrit « * » : voir docs/WEB.md)
 *   bind    = https://photo.exemple.be:443
 *   index   = index.html index.htm
 *   listing = 0
 *   enabled = 1
 *
 * Une liaison : schéma (http, https), hôte (« * » : tout nom), port (80 et 443 par défaut). Le certificat d'une liaison
 * https est celui de son hôte dans /etc/web/certs (tls.c). Réécrit par « webd site ... » et l'applet : les
 * commentaires ne sont pas gardés, sauf l'en-tête.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "webd.h"

static char *trim (char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen (s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

static void site_defaults (struct site *s, const char *name)
{
	memset (s, 0, sizeof *s);
	snprintf (s->name, sizeof s->name, "%s", name);
	strcpy (s->index[0], "index.html");
	strcpy (s->index[1], "index.htm");
	s->nindex = 2;
	s->enabled = 1;
}

/* « http://(tout):80 » (« * » pour l'hôte), « https://exemple.be », « http://localhost:8081 » -> 0, ou -1 */
int bind_parse (const char *str, struct bind *b)
{
	memset (b, 0, sizeof *b);
	const char *p = str;
	if (!strncasecmp (p, "https://", 8)) {
		b->scheme = SCHEME_HTTPS;
		b->port = 443;
		p += 8;
	} else if (!strncasecmp (p, "http://", 7)) {
		b->scheme = SCHEME_HTTP;
		b->port = 80;
		p += 7;
	} else
		return -1;
	size_t n = 0;
	while (*p && *p != ':' && *p != '/' && n + 1 < sizeof b->host)
		b->host[n++] = (char) tolower ((unsigned char) *p++);
	b->host[n] = 0;
	if (!n)
		return -1;
	if (*p == ':') {
		char *end;
		long port = strtol (p + 1, &end, 10);
		if (port < 1 || port > 65535 || (*end && *end != '/'))
			return -1;
		b->port = (int) port;
	}
	return 0;
}

void bind_format (const struct bind *b, char *out, size_t n)
{
	int def = (b->scheme == SCHEME_HTTP && b->port == 80) || (b->scheme == SCHEME_HTTPS && b->port == 443);
	if (def)
		snprintf (out, n, "%s://%s", b->scheme == SCHEME_HTTPS ? "https" : "http", b->host);
	else
		snprintf (out, n, "%s://%s:%d", b->scheme == SCHEME_HTTPS ? "https" : "http", b->host, b->port);
}

static int truthy (const char *v)
{
	return !strcmp (v, "1") || !strcasecmp (v, "yes") || !strcasecmp (v, "oui") || !strcasecmp (v, "true") ||
		!strcasecmp (v, "on");
}

/* -> 0, -1 si le fichier manque (c vide) ; les lignes fautives sont dites (sauf quiet) et sautées */
int conf_load (const char *file, struct conf *c, int quiet)
{
	memset (c, 0, sizeof *c);
	FILE *f = fopen (file, "r");
	if (!f)
		return -1;
	char line[1024];
	struct site *s = NULL;
	int ln = 0;
	while (fgets (line, sizeof line, f)) {
		ln++;
		char *l = trim (line);
		if (!*l || *l == '#' || *l == ';')
			continue;
		if (*l == '[') {
			char *e = strchr (l, ']');
			if (!e) {
				if (!quiet)
					fprintf (stderr, "webd: %s:%d : « ] » manque\n", file, ln);
				s = NULL;
				continue;
			}
			*e = 0;
			char *name = trim (l + 1);
			if (!strncasecmp (name, "site ", 5) && c->nsites < MAX_SITES) {
				s = &c->sites[c->nsites++];
				site_defaults (s, trim (name + 5));
			} else
				s = NULL;	/* (d'autres sections, plus tard : [server]...) */
			continue;
		}
		char *eq = strchr (l, '=');
		if (!eq || !s)
			continue;
		*eq = 0;
		char *k = trim (l), *v = trim (eq + 1);
		if (!strcasecmp (k, "root")) {
			snprintf (s->root, sizeof s->root, "%s", v);
			size_t n = strlen (s->root);
			while (n > 1 && s->root[n - 1] == '/' && s->root[n - 2] != ':')
				s->root[--n] = 0;
		} else if (!strcasecmp (k, "bind")) {
			if (s->nbinds < MAX_BINDS && bind_parse (v, &s->binds[s->nbinds]) == 0)
				s->nbinds++;
			else if (!quiet)
				fprintf (stderr, "webd: %s:%d : liaison refusée « %s »\n", file, ln, v);
		} else if (!strcasecmp (k, "index")) {
			s->nindex = 0;
			for (char *t = strtok (v, " ,"); t && s->nindex < MAX_INDEX; t = strtok (NULL, " ,"))
				snprintf (s->index[s->nindex++], sizeof s->index[0], "%s", t);
		} else if (!strcasecmp (k, "listing"))
			s->listing = truthy (v);
		else if (!strcasecmp (k, "enabled"))
			s->enabled = truthy (v);
		else if (!strcasecmp (k, "dev"))
			s->dev = truthy (v);
	}
	fclose (f);
	return 0;
}

int conf_save (const char *file, const struct conf *c)
{
	mkdirs (CONF_DIR);
	char tmp[600];
	snprintf (tmp, sizeof tmp, "%s.new", file);
	FILE *f = fopen (tmp, "w");
	if (!f)
		return -1;
	fputs ("# Les sites du serveur web de Korin (docs/WEB.md). Réécrit par « webd site » et Réglages › Serveur web.\n"
		"# bind = http://*:80 | https://nom.exemple:443 ; le certificat d'un hôte : /etc/web/certs/<hôte>.crt et .key\n",
		f);
	for (int i = 0; i < c->nsites; i++) {
		const struct site *s = &c->sites[i];
		fprintf (f, "\n[site %s]\nroot = %s\n", s->name, s->root);
		for (int j = 0; j < s->nbinds; j++) {
			char b[200];
			bind_format (&s->binds[j], b, sizeof b);
			fprintf (f, "bind = %s\n", b);
		}
		fputs ("index =", f);
		for (int j = 0; j < s->nindex; j++)
			fprintf (f, " %s", s->index[j]);
		fprintf (f, "\nlisting = %d\nenabled = %d\n", s->listing, s->enabled);
		if (s->dev)
			fputs ("dev = 1\n", f);
	}
	if (fclose (f) != 0)
		return -1;
	remove (file);
	return rename (tmp, file);
}

struct site *conf_find (struct conf *c, const char *name)
{
	for (int i = 0; i < c->nsites; i++)
		if (!strcasecmp (c->sites[i].name, name))
			return &c->sites[i];
	return NULL;
}
