/* shares.c : shares.ini et le compte de smbd.ini (shares.h), pour smbd et le volet de Réglages. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "shares.h"
#include "../../lib/keyvault/keyvault.h"

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

static int yes(const char *v)
{
	return !strcasecmp(v, "yes") || !strcasecmp(v, "oui") || !strcasecmp(v, "true") || !strcmp(v, "1");
}

int shares_load(const char *file, struct share_ent *out, int max)
{
	FILE *f = fopen(file, "r");
	if (!f)
		return 0;
	char line[1400];
	int n = 0;
	struct share_ent *cur = 0;
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (!*s || *s == '#' || *s == ';')
			continue;
		if (*s == '[') {
			char *e = strchr(s, ']');
			if (e)
				*e = 0;
			cur = 0;
			if (n < max) {
				cur = &out[n++];
				memset(cur, 0, sizeof *cur);
				snprintf(cur->name, sizeof cur->name, "%s", trim(s + 1));
			}
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq || !cur)
			continue;
		*eq = 0;
		char *k = trim(s), *v = trim(eq + 1);
		if (!strcasecmp(k, "path"))
			snprintf(cur->path, sizeof cur->path, "%s", v);
		else if (!strcasecmp(k, "type"))
			cur->type = !strcasecmp(v, "ftp") ? SHARE_FTP : SHARE_SMB;
		else if (!strcasecmp(k, "readonly"))
			cur->readonly = yes(v);
	}
	fclose(f);
	/* (une section sans dossier ne partage rien) */
	int k = 0;
	for (int i = 0; i < n; i++)
		if (out[i].path[0] && out[i].name[0])
			out[k++] = out[i];
	return k;
}

int shares_save(const char *file, const struct share_ent *s, int n)
{
	FILE *f = fopen(file, "w");
	if (!f)
		return -1;
	fputs("# Les dossiers partagés (docs/SMB.md) : Réglages › Partage de fichiers.\n"
	      "# [nom] du partage, path = dossier, type = smb | ftp, readonly = yes | no\n",
	      f);
	for (int i = 0; i < n; i++)
		fprintf(f, "\n[%s]\npath = %s\ntype = %s\nreadonly = %s\n", s[i].name, s[i].path,
			share_type_name(s[i].type), s[i].readonly ? "yes" : "no");
	return fclose(f) == 0 ? 0 : -1;
}

const char *share_type_name(int type)
{
	return type == SHARE_FTP ? "ftp" : "smb";
}

int share_name_ok(const char *name)
{
	size_t n = strlen(name);
	if (!n || n >= SHARE_NAME || name[0] == ' ' || name[n - 1] == ' ' || name[n - 1] == '.')
		return 0;
	if (!strcasecmp(name, "IPC$"))
		return 0;
	for (const char *p = name; *p; p++)
		if ((unsigned char)*p < 32 || strchr("\\/:*?\"<>|[]=", *p))
			return 0;
	return 1;
}

/* ---- smbd.ini, [users] */

int smbd_account_get(const char *file, char *user, int usz, char *pw, int psz)
{
	FILE *f = fopen(file, "r");
	user[0] = pw[0] = 0;
	if (!f)
		return 0;
	char line[512], sec[64] = "";
	int found = 0;
	while (!found && fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (*s == '[') {
			char *e = strchr(s, ']');
			if (e)
				*e = 0;
			snprintf(sec, sizeof sec, "%s", trim(s + 1));
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq || *s == '#' || *s == ';' || strcasecmp(sec, "users"))
			continue;
		*eq = 0;
		snprintf(user, (size_t)usz, "%s", trim(s));
		snprintf(pw, (size_t)psz, "%s", trim(eq + 1));
		found = user[0] != 0;
	}
	fclose(f);
	return found;
}

int smbd_account_set(const char *file, const char *user, const char *pw)
{
	/* le fichier relu, la section [users] remplacée (ou ajoutée) */
	FILE *f = fopen(file, "r");
	size_t cap = 8192, n = 0;
	char *buf = malloc(cap);
	if (!buf) {
		if (f)
			fclose(f);
		return -1;
	}
	buf[0] = 0;
	char line[1400];
	int in_users = 0, wrote = 0;
	while (f && fgets(line, sizeof line, f)) {
		char t[1400];
		snprintf(t, sizeof t, "%s", line);
		char *s = trim(t);
		if (*s == '[') {
			char *e = strchr(s, ']');
			if (e)
				*e = 0;
			in_users = !strcasecmp(trim(s + 1), "users");
			if (in_users) { /* la section, réécrite ici */
				n += (size_t)snprintf(buf + n, cap - n, "[users]\n%s = %s\n", user, pw);
				wrote = 1;
				continue;
			}
		}
		if (in_users && *s && *s != '#' && *s != ';')
			continue; /* (les anciens comptes) */
		size_t k = strlen(line);
		if (n + k + 256 >= cap) {
			char *nb = realloc(buf, cap * 2);
			if (!nb)
				break;
			buf = nb;
			cap *= 2;
		}
		memcpy(buf + n, line, k + 1);
		n += k;
	}
	if (f)
		fclose(f);
	if (!wrote) {
		if (n && buf[n - 1] != '\n')
			buf[n++] = '\n';
		if (!n)
			n += (size_t)snprintf(buf, cap, "# smbd (docs/SMB.md) : Réglages › Partage de fichiers\n[smbd]\nname = KORIN\n");
		n += (size_t)snprintf(buf + n, cap - n, "\n[users]\n%s = %s\n", user, pw);
	}
	f = fopen(file, "w");
	if (!f) {
		free(buf);
		return -1;
	}
	size_t w = fwrite(buf, 1, n, f);
	int r = fclose(f) == 0 && w == n ? 0 : -1;
	free(buf);
	return r;
}

int smbd_vault_present(void)
{
	return access("/sys/svc/keyvault", F_OK) == 0;
}

int smbd_vault_ok(void)
{
	return smbd_vault_present() && kv_request(KV_STATUS, KVF_SYSTEM, 0, 0, 0) >= 0;
}

void smbd_vault_name(char *out, size_t size, const char *user)
{
	size_t len = strlen(user);
	int raw = len <= 100; /* (« smbd/ » et 100 octets, ou « smbd/@ » et 60 en hexadécimal : moins de 128) */
	for (const char *p = user; *p && raw; p++)
		raw = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
		      strchr("._-:", *p);
	size_t o = (size_t)snprintf(out, size, "smbd/%s", raw ? "" : "@");
	for (size_t i = 0; i < len && i < (raw ? 100 : 60) && o + 3 < size; i++) {
		unsigned char c = (unsigned char)user[i];
		if (c >= 'A' && c <= 'Z') /* (les comptes de SMB ne tiennent pas compte de la casse) */
			c += 32;
		if (raw)
			out[o++] = (char)c;
		else
			o += (size_t)snprintf(out + o, 3, "%02x", c);
	}
	out[o] = 0;
}

int smbd_account_store(const char *file, const char *user, const char *pw, const char *old_user)
{
	if (!smbd_vault_ok())
		return smbd_account_set(file, user, pw);
	char name[160], t[128];
	if (!pw[0]) { /* inchangé : celui du compte d'avant, dans le coffre */
		smbd_vault_name(name, sizeof name, old_user ? old_user : user);
		long r = kv_sys_get(name, t, (long)sizeof t - 1);
		if (r <= 0)
			return -3;
		t[r] = 0;
		pw = t;
	}
	smbd_vault_name(name, sizeof name, user);
	int r;
	if (kv_sys_set(name, pw, (long)strlen(pw)) < 0)
		r = smbd_account_set(file, user, pw); /* (refusé : en clair, comme sans coffre) */
	else {
		r = smbd_account_set(file, user, "");
		if (old_user && old_user[0] && strcasecmp(old_user, user)) { /* l'ancien compte n'est plus */
			smbd_vault_name(name, sizeof name, old_user);
			kv_sys_delete(name);
		}
	}
	memset(t, 0, sizeof t);
	return r;
}
