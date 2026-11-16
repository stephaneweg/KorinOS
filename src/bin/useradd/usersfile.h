/* usersfile.h : lire et récrire /etc/users.ini (useradd, userdel, usermod ; docs/etude/MULTI-UTILISATEUR.md
 * §2.1). Le fichier entier en mémoire, une section par compte ; les commentaires et les autres comptes sont
 * gardés tels quels. Écrire demande l'uid 0 (sudo) : vfs applique les règles de /etc. */
#ifndef USERSFILE_H
#define USERSFILE_H

#include "korinrt.h"

#define UF_MAX 16384
#define UF_PATH "/etc/users.ini"

static char uf_buf[UF_MAX];
static u64 uf_len;

__attribute__((unused)) static void uf_out(const char *s)
{
	sys_write(2, s, (u64)str_len(s));
}

/* -> 0, ou < 0 (pas de fichier : vide) */
__attribute__((unused)) static long uf_load(void)
{
	uf_len = 0;
	uf_buf[0] = 0;
	long fd = sys_open(UF_PATH, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, uf_buf, UF_MAX - 1);
	sys_close(fd);
	if (n < 0)
		return n;
	uf_len = (u64)n;
	uf_buf[uf_len] = 0;
	return 0;
}

__attribute__((unused)) static long uf_save(void)
{
	long fd = sys_open(UF_PATH, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long r = sys_write(fd, uf_buf, uf_len);
	sys_close(fd);
	return r < 0 ? r : 0;
}

/* début de la ligne « [name] » et fin de la section (début de la suivante, ou fin du fichier) -> 1 */
__attribute__((unused)) static int uf_section(const char *name, u64 *b, u64 *e)
{
	u64 n = (u64)str_len(name);
	u64 i = 0;
	int found = 0;
	while (i < uf_len) {
		u64 l = i;
		while (i < uf_len && uf_buf[i] != '\n')
			i++;
		if (i < uf_len)
			i++;
		const char *p = uf_buf + l;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p != '[')
			continue;
		if (found) {
			*e = l;
			return 1;
		}
		if (!memcmp(p + 1, name, n) && p[1 + n] == ']') {
			found = 1;
			*b = l;
		}
	}
	if (found)
		*e = uf_len;
	return found;
}

/* remplace [b, e) par s */
__attribute__((unused)) static int uf_splice(u64 b, u64 e, const char *s)
{
	u64 n = (u64)str_len(s);
	if (uf_len - (e - b) + n >= UF_MAX)
		return 0;
	memmove(uf_buf + b + n, uf_buf + e, uf_len - e + 1);
	memcpy(uf_buf + b, s, n);
	uf_len = uf_len - (e - b) + n;
	return 1;
}

/* la valeur de « key = » dans la section, copiée dans out -> 1 */
__attribute__((unused)) static int uf_get(const char *name, const char *key, char *out, u64 cap)
{
	u64 b, e;
	if (!uf_section(name, &b, &e))
		return 0;
	u64 k = (u64)str_len(key);
	u64 i = b;
	while (i < e) {
		u64 l = i;
		while (i < e && uf_buf[i] != '\n')
			i++;
		const char *p = uf_buf + l;
		while (*p == ' ' || *p == '\t')
			p++;
		if (!memcmp(p, key, k) && (p[k] == ' ' || p[k] == '\t' || p[k] == '=')) {
			p += k;
			while (*p == ' ' || *p == '\t' || *p == '=')
				p++;
			u64 n = 0;
			while (p + n < uf_buf + i && p[n] != '\r' && n + 1 < cap) {
				out[n] = p[n];
				n++;
			}
			while (n && (out[n - 1] == ' ' || out[n - 1] == '\t'))
				n--;
			out[n] = 0;
			return 1;
		}
		if (i < e)
			i++;
	}
	return 0;
}

/* « key = value » dans la section (remplacée, ou ajoutée à la fin de la section) -> 1 */
__attribute__((unused)) static int uf_set(const char *name, const char *key, const char *value)
{
	u64 b, e;
	if (!uf_section(name, &b, &e))
		return 0;
	char line[256];
	str_cat(str_cat(str_cat(str_cat(line, key), " = "), value), "\n");
	u64 k = (u64)str_len(key);
	u64 i = b;
	u64 last = e; /* fin du dernier texte de la section (avant les lignes vides) */
	while (i < e) {
		u64 l = i;
		while (i < e && uf_buf[i] != '\n')
			i++;
		if (i < e)
			i++;
		const char *p = uf_buf + l;
		while (*p == ' ' || *p == '\t')
			p++;
		if (!memcmp(p, key, k) && (p[k] == ' ' || p[k] == '\t' || p[k] == '='))
			return uf_splice(l, i, line);
		if (*p != '\n' && *p != '\r')
			last = i;
	}
	if (last > uf_len)
		last = uf_len;
	if (last && uf_buf[last - 1] != '\n') { /* (la dernière ligne sans fin de ligne) */
		if (!uf_splice(last, last, "\n"))
			return 0;
		last++;
	}
	return uf_splice(last, last, line);
}

/* l'uid suivant (1000 au moins) */
__attribute__((unused)) static long uf_next_uid(void)
{
	long best = 999;
	for (u64 i = 0; i < uf_len;) {
		u64 l = i;
		while (i < uf_len && uf_buf[i] != '\n')
			i++;
		if (i < uf_len)
			i++;
		const char *p = uf_buf + l;
		while (*p == ' ' || *p == '\t')
			p++;
		if (!memcmp(p, "uid", 3)) {
			p += 3;
			while (*p == ' ' || *p == '\t' || *p == '=')
				p++;
			long u = (long)parse_dec(p);
			if (u > best)
				best = u;
		}
	}
	return best + 1;
}

/* un nom de compte permis : minuscules, chiffres, « - » et « _ », 1 à 31 caractères, une lettre d'abord */
__attribute__((unused)) static int uf_name_ok(const char *n)
{
	if (!(n[0] >= 'a' && n[0] <= 'z'))
		return 0;
	u64 k = 0;
	for (; n[k]; k++)
		if (!((n[k] >= 'a' && n[k] <= 'z') || (n[k] >= '0' && n[k] <= '9') || n[k] == '-' || n[k] == '_'))
			return 0;
	return k < 32;
}

__attribute__((unused)) static int uf_need_root(const char *cmd)
{
	if (korin_sys0(SYS_GETUID) > 0) {
		uf_out(cmd);
		uf_out(" : commande systeme, a lancer avec sudo\n");
		return 1;
	}
	return 0;
}

#endif
