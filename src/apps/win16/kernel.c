/* kernel.c -- win16 : KERNEL (tâche, mémoire globale et locale, sélecteurs, modules, ressources, fichiers, INI)
 * et les appels DOS (INT 21h) dont les programmes Win16 se servent encore. D'après krnl386/ de winevdm (Wine,
 * LGPL-2.1+), réécrit sur POSIX et sur la mémoire émulée.
 *
 * Lecteurs : C: = $HOME/win16/c (créé au premier lancement, avec C:\WINDOWS\WIN.INI) ; D: = le dossier du
 * programme lancé, qui est aussi le dossier courant. Les noms sont cherchés sans tenir compte de la casse. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "w16.h"
#include "be.h"

/* ======================================================================== chaînes */
static const uint16_t cp1252_hi[32] = {
	0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d,
	0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d,
	0x17e, 0x178 };

int w16_to_utf8 (const uint8_t *s, int n, char *out, int cap)
{
	int o = 0;
	for (int i = 0; (n < 0 ? s[i] : i < n) && o < cap - 4; i++)
	{
		unsigned c = s[i];
		if (c >= 0x80 && c < 0xa0)
			c = cp1252_hi[c - 0x80];
		if (c < 0x80)
			out[o++] = (char) c;
		else if (c < 0x800)
		{
			out[o++] = (char) (0xc0 | c >> 6);
			out[o++] = (char) (0x80 | (c & 0x3f));
		}
		else
		{
			out[o++] = (char) (0xe0 | c >> 12);
			out[o++] = (char) (0x80 | ((c >> 6) & 0x3f));
			out[o++] = (char) (0x80 | (c & 0x3f));
		}
	}
	out[o] = 0;
	return o;
}

/* UTF-8 -> CP1252 (ce qui n'y est pas devient « ? ») */
int w16_from_utf8 (const char *s, uint8_t *out, int cap)
{
	int o = 0;
	const uint8_t *p = (const uint8_t *) s;
	while (*p && o < cap - 1)
	{
		unsigned c = *p++;
		if (c >= 0xc0)
		{
			int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
			c &= 0x3f >> n;
			while (n-- && (*p & 0xc0) == 0x80)
				c = c << 6 | (*p++ & 0x3f);
		}
		if (c >= 0x80 && c < 0x100 && !(c >= 0x80 && c < 0xa0))
			out[o++] = (uint8_t) c;
		else if (c < 0x80)
			out[o++] = (uint8_t) c;
		else
		{
			int k;
			for (k = 0; k < 32 && cp1252_hi[k] != c; k++)
				;
			out[o++] = (uint8_t) (k < 32 ? 0x80 + k : '?');
		}
	}
	out[o] = 0;
	return o;
}

const char *w16_str (uint32_t segptr)
{
	static char bufs[8][1024];
	static int k;
	char *b = bufs[k++ & 7];
	if (!segptr)
	{
		b[0] = 0;
		return b;
	}
	if (!SELECTOROF (segptr))
	{
		snprintf (b, 1024, "#%u", OFFSETOF (segptr));	/* (un atome ou un numéro de ressource) */
		return b;
	}
	w16_to_utf8 (far_ptr (segptr), -1, b, 1024);
	return b;
}

/* une chaîne du programme, telle quelle (CP1252), copiée dans un tampon tournant ; "" si nulle */
const char *w16_raw (uint32_t segptr)
{
	static char bufs[8][1024];
	static int k;
	char *b = bufs[k++ & 7];
	b[0] = 0;
	if (segptr && SELECTOROF (segptr))
	{
		const char *s = far_ptr (segptr);
		int i;
		for (i = 0; i < 1023 && s[i]; i++)
			b[i] = s[i];
		b[i] = 0;
	}
	return b;
}

/* copie une chaîne C dans le tampon du programme (au plus n octets avec le NUL) -> longueur copiée */
static int put_str (uint32_t dst, const char *s, int n)
{
	if (!dst || n <= 0)
		return 0;
	int l = (int) strlen (s);
	if (l > n - 1)
		l = n - 1;
	memcpy (far_ptr (dst), s, (size_t) l);
	((char *) far_ptr (dst))[l] = 0;
	return l;
}

/* ======================================================================== lecteurs et chemins */
static char drive_c[512];
static char cur_dir[256] = "";		/* le dossier courant, sous D: (sans « \ » au début) */
static int cur_drive = 3;			/* 1 A:, 3 C:, 4 D: */

/* les lecteurs (1 A: … 26 Z:) : un dossier de Korin chacun (« » : la racine « / ») ou rien.
 * Par défaut C: = HOME:/win16/c, D: = le dossier du programme, E: = tout l'arbre de Korin ; HOME:/win16/drives.ini
 * (section [drives], « C=/chemin », vide pour retirer) et l'option --drive C=/chemin les changent : des réglages
 * de chaque utilisateur. */
static char *drives[27];
static char *drive_arg[27];

void kernel_drive_option (const char *spec)
{
	if (!spec[0] || spec[1] != '=')
		return;
	int d = toupper ((unsigned char) spec[0]) - 'A' + 1;
	if (d < 1 || d > 26)
		return;
	free (drive_arg[d]);
	drive_arg[d] = strdup (spec + 2);
}

static const char *drive_root (int d)
{
	return d >= 1 && d <= 26 ? drives[d] : 0;
}

/* un chemin de Korin -> le nom DOS que le programme verra : le lecteur dont le dossier le contient (le plus long) */
int host_to_dos (const char *path, char *out, size_t cap)
{
	int best = 0;
	size_t bl = 0;
	for (int d = 1; d <= 26; d++)
	{
		if (!drives[d])
			continue;
		size_t l = strlen (drives[d]);
		if ((!l || (!strncmp (path, drives[d], l) && (path[l] == '/' || !path[l]))) && (!best || l > bl))
		{
			best = d;
			bl = l;
		}
	}
	if (!best)
	{
		snprintf (out, cap, "%s", path);
		return 0;
	}
	const char *rest = path + bl;
	snprintf (out, cap, "%c:%s%s", 'A' + best - 1, *rest == '/' ? "" : "\\", rest);
	for (char *p = out; *p; p++)
		if (*p == '/')
			*p = '\\';
	return 1;
}

static void drive_set (int d, const char *path)
{
	free (drives[d]);
	drives[d] = 0;
	if (!path)
		return;
	char p[600];
	snprintf (p, sizeof p, "%s", path);
	size_t l = strlen (p);
	while (l && p[l - 1] == '/')
		p[--l] = 0;	/* (« / » devient « », la racine) */
	drives[d] = strdup (p);
}

static void drives_init (const char *home)
{
	drive_set (3, drive_c);
	drive_set (4, task.dir);
	drive_set (5, "/");
	if (w16_dosdir[0])	/* Z: : les fichiers DOS de win16 (COMMAND.COM de FreeCOM) */
		drive_set (26, w16_dosdir);
	char ini[600];
	snprintf (ini, sizeof ini, "%s/win16/drives.ini", home);
	FILE *f = fopen (ini, "r");
	if (f)
	{
		char line[700];
		int in = 0;
		while (fgets (line, sizeof line, f))
		{
			char *s = line;
			while (*s == ' ' || *s == '\t')
				s++;
			s[strcspn (s, "\r\n")] = 0;
			if (*s == '[')
			{
				in = !strncasecmp (s, "[drives]", 8);
				continue;
			}
			if (!in || !s[0] || s[0] == ';' || s[1] != '=')
				continue;
			int d = toupper ((unsigned char) s[0]) - 'A' + 1;
			if (d >= 1 && d <= 26)
				drive_set (d, s[2] ? s + 2 : 0);
		}
		fclose (f);
	}
	for (int d = 1; d <= 26; d++)
		if (drive_arg[d])
			drive_set (d, drive_arg[d][0] ? drive_arg[d] : 0);
	if (drives[3])
	{
		snprintf (drive_c, sizeof drive_c, "%s", drives[3]);
		mkdir (drive_c, 0755);
	}
}

/* un nom DOS -> chemin de l'hôte ; les composants existants sont pris avec leur casse réelle */
int dos_to_host (const char *dos, char *out, size_t cap)
{
	int drive = cur_drive;
	const char *p = dos;
	char rel[600];
	if (p[0] && p[1] == ':')
	{
		drive = toupper ((unsigned char) p[0]) - 'A' + 1;
		p += 2;
	}
	const char *root = drive_root (drive);
	if (!root)
		return 0;
	if (*p == '\\' || *p == '/')
		snprintf (rel, sizeof rel, "%s", p + 1);
	else if (drive == cur_drive && cur_dir[0])
		snprintf (rel, sizeof rel, "%s\\%s", cur_dir, p);
	else
		snprintf (rel, sizeof rel, "%s", p);
	snprintf (out, cap, "%s", root);
	char *tok = rel;
	while (tok && *tok)
	{
		char *sep = strpbrk (tok, "\\/");
		if (sep)
			*sep = 0;
		if (!strcmp (tok, "..") )
		{
			char *s = strrchr (out, '/');
			if (s && (size_t) (s - out) > strlen (root))
				*s = 0;
		}
		else if (*tok && strcmp (tok, "."))
		{
			char found[700];
			if (ne_find_file (out, tok, found, sizeof found))
				snprintf (out, cap, "%s", found);
			else
			{
				size_t l = strlen (out);
				snprintf (out + l, cap - l, "/%s", tok);
			}
		}
		tok = sep ? sep + 1 : 0;
	}
	return 1;
}

/* ======================================================================== fichiers (handles DOS) */
#define MAX_FH 64
static int fh[MAX_FH] = { -1, -1, -1, -3, -3 };	/* descripteur POSIX + 1 (0 : libre) ; < 0 : un périphérique (-1 CON, -2 NUL, -3 AUX/PRN) */

static int fh_new (int fd)
{
	for (int i = 0; i < MAX_FH; i++)	/* (la plus basse libre, comme DOS : FreeCOM redirige ainsi) */
		if (!fh[i])
		{
			fh[i] = fd + 1;
			return i;
		}
	close (fd);
	return -1;
}

static int fh_fd (int h)
{
	return h >= 0 && h < MAX_FH && fh[h] ? fh[h] - 1 : -1;
}

static int dos_open (const char *dos, int mode, int create)
{
	char path[700];
	if (!dos_to_host (dos, path, sizeof path))
		return -1;
	int fl = (mode & 3) == 0 ? O_RDONLY : (mode & 3) == 1 ? O_WRONLY : O_RDWR;
	if (create)
		fl = O_RDWR | O_CREAT | O_TRUNC;
	int fd = open (path, fl, 0644);
	w16_log ("   fichier %s (%s) : %s\n", dos, path, fd >= 0 ? "ouvert" : strerror (errno));
	return fd < 0 ? -1 : fh_new (fd);
}

void api__lclose16 (struct w16call *c)
{
	int h = (int) c->a[0], fd = fh_fd (h);
	c->ret = fd >= 0 && !close (fd) ? 0 : 0xffff;
	if (fd >= 0)
		fh[h] = 0;
}

void api__lopen16 (struct w16call *c)
{
	int h = dos_open (w16_raw (c->a[0]), (int) c->a[1], 0);
	c->ret = h < 0 ? 0xffff : (uint32_t) h;
}

void api__lcreat16 (struct w16call *c)
{
	int h = dos_open (w16_raw (c->a[0]), 2, 1);
	c->ret = h < 0 ? 0xffff : (uint32_t) h;
}

static int32_t do_read (int h, uint32_t buf, uint32_t n)
{
	int fd = fh_fd (h);
	if (fd < 0)
		return -1;
	uint32_t lin = far_lin (buf), done = 0;
	while (done < n)
	{
		/* (un tampon « huge » : la mémoire est continue derrière les sélecteurs qui se suivent) */
		ssize_t r = read (fd, lin_ptr (lin + done), n - done);
		if (r <= 0)
			break;
		done += (uint32_t) r;
	}
	return (int32_t) done;
}

static int32_t do_write (int h, uint32_t buf, uint32_t n)
{
	int fd = fh_fd (h);
	if ((h == 1 || h == 2) && fd < 0)
	{
		fwrite (far_ptr (buf), 1, n, stderr);
		return (int32_t) n;
	}
	if (fd < 0)
		return -1;
	if (!n)	/* (écrire 0 octet : tronquer ici) */
		return ftruncate (fd, lseek (fd, 0, SEEK_CUR)) ? -1 : 0;
	ssize_t r = write (fd, lin_ptr (far_lin (buf)), n);
	return (int32_t) r;
}

/* ---- pour la machine DOS (dos.c) : les poignées de périphériques (CON, NUL, AUX, PRN) et la redirection ---- */
/* le périphérique derrière une poignée : 1 CON, 2 NUL, 3 AUX/PRN ; 0 : un fichier ou rien */
int kernel_fh_dev (int h)
{
	if (h < 0 || h >= MAX_FH)
		return 0;
	return fh[h] < 0 ? -fh[h] : 0;
}

int kernel_fh_newdev (int dev)
{
	for (int i = 0; i < MAX_FH; i++)
		if (!fh[i])
		{
			fh[i] = -dev;
			return i;
		}
	return -1;
}

int kernel_fh_valid (int h)
{
	return kernel_fh_dev (h) || fh_fd (h) >= 0;
}

/* dupliquer h (dans to, ou la première libre si to < 0) -> la nouvelle poignée, -1 */
int kernel_fh_dup (int h, int to)
{
	int dev = kernel_fh_dev (h), fd = fh_fd (h);
	if (!dev && fd < 0)
		return -1;
	if (to >= 0)
	{
		if (to >= MAX_FH)
			return -1;
		if (fh[to] > 0)
			close (fh[to] - 1);
		fh[to] = dev ? -dev : dup (fd) + 1;
		return to;
	}
	if (dev)
		return kernel_fh_newdev (dev);
	return fh_new (dup (fd));
}

int kernel_fh_close (int h)
{
	if (h < 0 || h >= MAX_FH || !fh[h])
		return -1;
	if (fh[h] > 0)
		close (fh[h] - 1);
	fh[h] = 0;
	return 0;
}

/* pour lz.c : une poignée DOS sur un fichier de l'hôte ; le descripteur POSIX d'une poignée */
int kernel_open_host (const char *host, int flags)
{
	int fd = open (host, flags, 0644);
	return fd < 0 ? -1 : fh_new (fd);
}

int kernel_fh_fd (int h) { return fh_fd (h); }

int32_t kernel_write (int h, const void *p, uint32_t n)
{
	int fd = fh_fd (h);
	return fd < 0 ? -1 : (int32_t) write (fd, p, n);
}

int32_t kernel_read (int h, void *p, uint32_t n)
{
	int fd = fh_fd (h);
	return fd < 0 ? -1 : (int32_t) read (fd, p, n);
}

void kernel_set_dta (uint32_t segptr);

void api_WIN16_lread (struct w16call *c) { int32_t r = do_read ((int) c->a[0], c->a[1], c->a[2]); c->ret = (uint16_t) r; }
void api_WIN16_hread (struct w16call *c) { c->ret = (uint32_t) do_read ((int) c->a[0], c->a[1], c->a[2]); }
void api__lwrite16 (struct w16call *c) { int32_t r = do_write ((int) c->a[0], c->a[1], c->a[2]); c->ret = (uint16_t) r; }
void api__hwrite16 (struct w16call *c) { c->ret = (uint32_t) do_write ((int) c->a[0], c->a[1], c->a[2]); }

void api__llseek16 (struct w16call *c)
{
	int fd = fh_fd ((int) c->a[0]);
	off_t r = fd < 0 ? -1 : lseek (fd, (int32_t) c->a[1], (int) c->a[2]);
	c->ret = (uint32_t) (int32_t) r;
}

/* OpenFile : OFSTRUCT (cBytes, fFixedDisk, nErrCode, reserved[4], szPathName[128]) */
void api_WIN16_OpenFile16 (struct w16call *c)
{
	const char *name = w16_raw (c->a[0]);
	uint32_t of = c->a[1];
	int mode = (int) c->a[2];
	char dos[300];
	snprintf (dos, sizeof dos, "%s", name);
	int builtin = 0;
	if (!strchr (dos, '\\') && !strchr (dos, ':'))
	{
		/* (un nom seul : le dossier courant, celui du programme, C:\WINDOWS, puis C:\WINDOWS\SYSTEM) */
		char path[700];
		const char *tries[4] = { "", "D:\\", "C:\\WINDOWS\\", "C:\\WINDOWS\\SYSTEM\\" };
		int found = 0;
		for (int i = 0; i < 4 && !found; i++)
		{
			char t[300];
			snprintf (t, sizeof t, "%s%s", tries[i], name);
			if (dos_to_host (t, path, sizeof path) && !access (path, F_OK))
			{
				snprintf (dos, sizeof dos, "%s", t);
				found = 1;
			}
		}
		/* une DLL intégrée (DDEML.DLL…) n'a pas de fichier : sous Windows, elle serait dans C:\WINDOWS\SYSTEM
		   (le moteur de VB3 vérifie par OF_EXIST que ddeml.dll existe avant de s'en servir) */
		const char *dot = strrchr (name, '.');
		if (!found && dot && (!strcasecmp (dot, ".dll") || !strcasecmp (dot, ".drv") || !strcasecmp (dot, ".exe")))
		{
			char base[80];
			snprintf (base, sizeof base, "%.*s", (int) (dot - name), name);
			if (api_find_module (base) >= 0)
			{
				snprintf (dos, sizeof dos, "C:\\WINDOWS\\SYSTEM\\%s", name);
				builtin = 1;
			}
		}
	}
	uint8_t *o = far_ptr (of);
	if (o)
	{
		memset (o, 0, 136);
		o[0] = 136;
		o[1] = 1;
		snprintf ((char *) o + 8, 128, "%s", dos);
		for (char *p = (char *) o + 8; *p; p++)
			*p = (char) toupper ((unsigned char) *p);
	}
	char path[700];
	if (mode & 0x0200)	/* OF_DELETE */
	{
		c->ret = dos_to_host (dos, path, sizeof path) && !unlink (path) ? 1 : 0xffff;
		return;
	}
	if (mode & 0x4000)	/* OF_EXIST */
	{
		c->ret = builtin || (dos_to_host (dos, path, sizeof path) && !access (path, F_OK)) ? 1 : 0xffff;
		if (o && c->ret == 0xffff)
			o[2] = 2;
		return;
	}
	if (mode & 0x0100)	/* OF_PARSE */
	{
		c->ret = 0;
		return;
	}
	int h = dos_open (dos, mode & 3, (mode & 0x1000) != 0);	/* OF_CREATE */
	if (h < 0)
	{
		if (o)
			o[2] = 2;	/* fichier introuvable */
		c->ret = 0xffff;
		return;
	}
	c->ret = (uint32_t) h;
}

void api_GetWindowsDirectory16 (struct w16call *c) { c->ret = (uint32_t) put_str (c->a[0], "C:\\WINDOWS", (int) c->a[1]); }
void api_GetSystemDirectory16 (struct w16call *c) { c->ret = (uint32_t) put_str (c->a[0], "C:\\WINDOWS\\SYSTEM", (int) c->a[1]); }
void api_GetDriveType16 (struct w16call *c) { c->ret = c->a[0] < 26 && drive_root ((int) c->a[0] + 1) ? 3 : 0; }	/* DRIVE_FIXED */
void api_SetHandleCount16 (struct w16call *c) { c->ret = c->a[0] < MAX_FH ? c->a[0] : MAX_FH; }

void api_GetTempFileName16 (struct w16call *c)
{
	static unsigned n = 1;
	char s[80];
	unsigned u = c->a[2] ? c->a[2] : n++;
	snprintf (s, sizeof s, "C:\\WINDOWS\\TEMP\\%.3s%04X.TMP", w16_raw (c->a[1]), u & 0xffff);
	put_str (c->a[3], s, 144);
	c->ret = u;
}

/* ======================================================================== INI */
static void ini_path (const char *file, char *out, size_t cap)
{
	char dos[300];
	if (!file || !*file)
		file = "WIN.INI";
	if (!strchr (file, '\\') && !strchr (file, ':'))
		snprintf (dos, sizeof dos, "C:\\WINDOWS\\%s", file);
	else
		snprintf (dos, sizeof dos, "%s", file);
	dos_to_host (dos, out, cap);
}

static char *slurp (const char *path, size_t *len)
{
	FILE *f = fopen (path, "rb");
	if (!f)
	{
		*len = 0;
		return calloc (1, 1);
	}
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	char *b = calloc (1, (size_t) n + 2);
	*len = fread (b, 1, (size_t) n, f);
	fclose (f);
	return b;
}

static void trim (char *s)
{
	char *e = s + strlen (s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	char *b = s;
	while (*b == ' ' || *b == '\t')
		b++;
	memmove (s, b, strlen (b) + 1);
}

/* lit une valeur ; section 0 : la liste des sections ; key 0 : la liste des clés (chaînes doublement nulles) */
static int ini_get (const char *file, const char *section, const char *key, const char *def, char *out, int cap)
{
	char path[700];
	size_t len;
	ini_path (file, path, sizeof path);
	char *b = slurp (path, &len);
	int o = 0, insec = 0, found = 0;
	char line[1024];
	char *p = b;
	while (*p && !found)
	{
		char *nl = strchr (p, '\n');
		size_t l = nl ? (size_t) (nl - p) : strlen (p);
		snprintf (line, sizeof line, "%.*s", (int) (l < sizeof line - 1 ? l : sizeof line - 1), p);
		p = nl ? nl + 1 : p + l;
		trim (line);
		if (line[0] == '[')
		{
			char *e = strchr (line, ']');
			if (e)
				*e = 0;
			if (!section)
			{
				int n = (int) strlen (line + 1);
				if (o + n + 2 < cap)
				{
					memcpy (out + o, line + 1, (size_t) n + 1);
					o += n + 1;
				}
				continue;
			}
			insec = !strcasecmp (line + 1, section);
			continue;
		}
		if (!section || !insec || !line[0] || line[0] == ';')
			continue;
		char *eq = strchr (line, '=');
		if (!eq)
			continue;
		*eq = 0;
		trim (line);
		char *val = eq + 1;
		trim (val);
		if (!key)
		{
			int n = (int) strlen (line);
			if (o + n + 2 < cap)
			{
				memcpy (out + o, line, (size_t) n + 1);
				o += n + 1;
			}
			continue;
		}
		if (!strcasecmp (line, key))
		{
			int n = (int) strlen (val);
			if (n > 1 && (val[0] == '"' || val[0] == '\'') && val[n - 1] == val[0])
			{
				val[n - 1] = 0;
				val++;
			}
			snprintf (out, (size_t) cap, "%s", val);
			o = (int) strlen (out);
			found = 1;
		}
	}
	free (b);
	if (!section || !key)
	{
		if (o < cap)
			out[o] = 0;
		return o;
	}
	if (!found)
	{
		snprintf (out, (size_t) cap, "%s", def ? def : "");
		o = (int) strlen (out);
	}
	return o;
}

static int ini_put (const char *file, const char *section, const char *key, const char *val)
{
	char path[700];
	size_t len;
	ini_path (file, path, sizeof path);
	char *b = slurp (path, &len);
	size_t cap = len + 4096 + (key ? strlen (key) : 0) + (val ? strlen (val) : 0);
	char *out = calloc (1, cap);
	size_t o = 0;
	int insec = 0, done = 0, seen = 0;
	char line[1024], raw[1100];
	char *p = b;
	while (*p)
	{
		char *nl = strchr (p, '\n');
		size_t l = nl ? (size_t) (nl - p) : strlen (p);
		snprintf (raw, sizeof raw, "%.*s", (int) (l < sizeof raw - 1 ? l : sizeof raw - 1), p);
		p = nl ? nl + 1 : p + l;
		snprintf (line, sizeof line, "%s", raw);
		trim (line);
		if (line[0] == '[')
		{
			if (insec && !done && key && val)
			{
				o += (size_t) snprintf (out + o, cap - o, "%s=%s\r\n", key, val);
				done = 1;
			}
			char name[1024];
			snprintf (name, sizeof name, "%s", line + 1);
			char *e = strchr (name, ']');
			if (e)
				*e = 0;
			insec = !strcasecmp (name, section);
			seen |= insec;
			if (insec && !key)
				continue;	/* (la section est retirée) */
		}
		else if (insec)
		{
			if (!key)
				continue;
			char *eq = strchr (line, '=');
			if (eq)
			{
				*eq = 0;
				trim (line);
				if (!strcasecmp (line, key))
				{
					if (val && !done)
						o += (size_t) snprintf (out + o, cap - o, "%s=%s\r\n", key, val);
					done = 1;
					continue;
				}
			}
		}
		if (o + strlen (raw) + 3 < cap)
		{
			trim (raw);
			o += (size_t) snprintf (out + o, cap - o, "%s\r\n", raw);
		}
	}
	if (key && val && !done)
	{
		if (!seen)
			o += (size_t) snprintf (out + o, cap - o, "[%s]\r\n", section);
		else if (insec)
			;
		o += (size_t) snprintf (out + o, cap - o, "%s=%s\r\n", key, val);
		if (seen && !insec)
		{
			/* (la section existe plus haut : la refaire proprement serait mieux ; elle est ajoutée à la fin) */
		}
	}
	FILE *f = fopen (path, "wb");
	int ok = f && fwrite (out, 1, o, f) == o;
	if (f)
		fclose (f);
	free (b);
	free (out);
	return ok;
}

void api_GetPrivateProfileString16 (struct w16call *c)
{
	char out[4096];
	int n = (int) c->a[4];
	int l = ini_get (w16_raw (c->a[5]), c->a[0] ? w16_raw (c->a[0]) : 0, c->a[1] ? w16_raw (c->a[1]) : 0,
		w16_raw (c->a[2]), out, n < (int) sizeof out ? n : (int) sizeof out);
	if (n > 0)
	{
		if (!c->a[0] || !c->a[1])
		{
			memcpy (far_ptr (c->a[3]), out, (size_t) (l + 1 < n ? l + 1 : n));
			if (l + 1 < n)
				((char *) far_ptr (c->a[3]))[l + 1] = 0;
		}
		else
			put_str (c->a[3], out, n);
	}
	c->ret = (uint32_t) (l < n ? l : n - 1);
}

void api_GetProfileString16 (struct w16call *c)
{
	struct w16call d = *c;
	d.a[5] = 0;
	api_GetPrivateProfileString16 (&d);
	c->ret = d.ret;
}

void api_GetPrivateProfileInt16 (struct w16call *c)
{
	char out[64];
	ini_get (w16_raw (c->a[3]), w16_raw (c->a[0]), w16_raw (c->a[1]), "", out, sizeof out);
	c->ret = out[0] ? (uint16_t) atoi (out) : (uint16_t) c->a[2];
}

void api_GetProfileInt16 (struct w16call *c)
{
	char out[64];
	ini_get (0, w16_raw (c->a[0]), w16_raw (c->a[1]), "", out, sizeof out);
	c->ret = out[0] ? (uint16_t) atoi (out) : (uint16_t) c->a[2];
}

void api_WritePrivateProfileString16 (struct w16call *c)
{
	c->ret = (uint32_t) ini_put (w16_raw (c->a[3]), w16_raw (c->a[0]), c->a[1] ? w16_raw (c->a[1]) : 0,
		c->a[2] ? w16_raw (c->a[2]) : 0);
}

void api_WriteProfileString16 (struct w16call *c)
{
	c->ret = (uint32_t) ini_put (0, w16_raw (c->a[0]), c->a[1] ? w16_raw (c->a[1]) : 0, c->a[2] ? w16_raw (c->a[2]) : 0);
}

/* ======================================================================== la tâche */
static uint16_t builtin_hmod[MAX_BUILTIN];

/* les lecteurs (C:, D:, E:, drives.ini) et le dossier courant D:\ ; aussi pour la machine DOS (dos.c) */
void kernel_drives_init (void)
{
	/* C: et son WINDOWS */
	const char *home = getenv ("HOME");
	snprintf (drive_c, sizeof drive_c, "%s/win16/c", home && *home ? home : "/home");
	char p[700];
	snprintf (p, sizeof p, "%s/win16", home && *home ? home : "/home");
	mkdir (p, 0755);
	mkdir (drive_c, 0755);
	drives_init (home && *home ? home : "/home");
	snprintf (p, sizeof p, "%s/WINDOWS", drive_c);
	mkdir (p, 0755);
	snprintf (p, sizeof p, "%s/WINDOWS/SYSTEM", drive_c);
	mkdir (p, 0755);
	snprintf (p, sizeof p, "%s/WINDOWS/TEMP", drive_c);
	mkdir (p, 0755);
	snprintf (p, sizeof p, "%s/WINDOWS/WIN.INI", drive_c);
	if (access (p, F_OK))
	{
		FILE *f = fopen (p, "wb");
		if (f)
		{
			fputs ("[windows]\r\nDevice=\r\n\r\n[intl]\r\niCountry=33\r\nsDecimal=,\r\nsThousand= \r\nsShortDate=dd/MM/yyyy\r\n"
				"\r\n[fonts]\r\n", f);
			fclose (f);
		}
	}
	cur_drive = 4;
	cur_dir[0] = 0;
}

void kernel_init (void)
{
	kernel_drives_init ();
	/* le PSP et l'environnement, comme sous DOS : la ligne de commande en 0x80, le chemin après l'environnement */
	task.env = global_alloc (512, 0);
	uint8_t *e = lin_ptr (sel_base (task.env));
	int o = sprintf ((char *) e, "PATH=C:\\WINDOWS") + 1;
	o += sprintf ((char *) e + o, "WINDIR=C:\\WINDOWS") + 1;
	o += sprintf ((char *) e + o, "TEMP=C:\\WINDOWS\\TEMP") + 1;
	e[o++] = 0;
	e[o++] = 1;
	e[o++] = 0;
	strcpy ((char *) e + o, task.dospath);
	task.psp = global_alloc (256, 0);
	uint8_t *ps = lin_ptr (sel_base (task.psp));
	ps[0] = 0xcd;
	ps[1] = 0x20;
	wr16 (sel_base (task.psp) + 0x2c, task.env);
	for (int m = 0; m < MAX_BUILTIN && w16_modules[m].name; m++)
		builtin_hmod[m] = global_alloc (16, 0);
}

void kernel_set_cmdline (const char *args)
{
	uint8_t *p = lin_ptr (sel_base (task.psp));
	int n = args ? (int) strlen (args) : 0;
	if (n > 125)
		n = 125;
	p[0x80] = (uint8_t) (n + (n ? 1 : 0));
	int o = 0x81;
	if (n)
		p[o++] = ' ';
	memcpy (p + o, args, (size_t) n);
	/* (comme Windows, d'après winevdm : 0Dh seulement quand la ligne est vide ; sinon la chaîne finit par 0, car
	   ES:BX d'InitTask est le lpCmdLine de WinMain et le Command$ de Visual Basic garderait le 0Dh) */
	p[o + n] = n ? 0 : 0x0d;
}

/* InitTask (KERNEL.91, registres) : INSTANCEDATA au début de DS, tas local, registres de retour, un mot 0 empilé */
void api_InitTask16 (struct w16call *c)
{
	uint32_t ds = cpu_sreg_base (S_DS);
	uint16_t sp = (uint16_t) cpu_reg (R_ESP);
	uint16_t stackmin = (uint16_t) (sp + 4);
	uint16_t bx = (uint16_t) cpu_reg (R_EBX);
	uint16_t stacktop = (uint16_t) ((stackmin > bx ? stackmin - bx : 0) + 150);
	wr16 (ds + 0x0a, stacktop);
	wr16 (ds + 0x0c, stackmin);
	wr16 (ds + 0x0e, stackmin);
	uint16_t cx = (uint16_t) cpu_reg (R_ECX);
	if (cx && task.main->autodata)
	{
		/* DGROUP = données, tas local, pile (ne.c) : le tas est juste sous la pile */
		extern void local_init (uint16_t sel, uint16_t start, uint16_t end);
		uint32_t size = task.main->sizes[task.main->autodata];
		uint32_t end = size > bx ? size - bx : size, start = end > cx ? end - cx : 16;
		local_init (cpu_sreg (S_DS), (uint16_t) start, (uint16_t) (end > 0xffff ? 0xffff : end));
	}
	uint8_t *psp = lin_ptr (sel_base (task.psp));
	int bxo = 0x80;
	if (psp[0x80])
	{
		bxo = 0x81;
		while (psp[bxo] == ' ' || psp[bxo] == '\t')
			bxo++;
	}
	cpu_set_reg (R_EAX, 1);
	cpu_set_reg (R_EBX, (uint32_t) bxo);
	cpu_set_reg (R_ECX, stacktop);
	cpu_set_reg (R_EDX, task.cmdshow);
	cpu_set_reg (R_ESI, 0);
	cpu_set_reg (R_EDI, task.hinstance);
	cpu_set_sreg (S_ES, task.psp);
	c->push_words = 1;	/* (un mot 0 empilé au retour : le « BP » de Windows) */
}

void api_WaitEvent16 (struct w16call *c) { c->ret = 0; }
void api_Yield16 (struct w16call *c) { (void) c; }
void api_GetVersion16 (struct w16call *c) { c->ret = 0x05000a03; }	/* Windows 3.10 sur MS-DOS 5.0 */
void api_GetWinFlags16 (struct w16call *c) { c->ret = 0x0413 | 0x0400; }	/* WF_PMODE, WF_ENHANCED, WF_CPU486, WF_80x87 */
void api_SetErrorMode16 (struct w16call *c) { static uint16_t m; c->ret = m; m = (uint16_t) c->a[0]; }
void api_WIN16_GetCurrentTask (struct w16call *c) { c->ret = task.psp; }
void api_GetCurrentPDB16 (struct w16call *c) { c->ret = task.psp; }
void api_GetNumTasks16 (struct w16call *c) { c->ret = 1; }
void api_IsTask16 (struct w16call *c) { c->ret = c->a[0] == task.psp; }
void api_GetTickCount (struct w16call *c) { c->ret = be_ticks (); }
void api_GetDOSEnvironment16 (struct w16call *c) { (void) c; c->ret = MAKESEGPTR (task.env, 0); }
void api_GetLastError16 (struct w16call *c) { c->ret = 0; }
void api_SetLastError16 (struct w16call *c) { (void) c; }

void api_FatalAppExit16 (struct w16call *c)
{
	be_message (task.modname, w16_str (c->a[1]), 0x10);
	exit (1);
}

void api_OutputDebugString16 (struct w16call *c)
{
	(void) c;
	/* (fonction « register » : la chaîne est sur la pile, après l'adresse de retour) */
	uint32_t sp = cpu_sreg_base (S_SS) + (uint16_t) cpu_reg (R_ESP);
	w16_log ("débogage : %s\n", w16_str (rd32 (sp + 4)));
}

void api_DebugBreak16 (struct w16call *c) { (void) c; }

/* ---- modules ---- */
static void module_dospath (struct ne_module *m, char *out, size_t cap)
{
	const char *b = strrchr (m->path, '/');
	b = b ? b + 1 : m->path;
	snprintf (out, cap, "D:\\%s", b);
	for (char *p = out; *p; p++)
		*p = (char) toupper ((unsigned char) *p);
}

void api_GetModuleFileName16 (struct w16call *c)
{
	struct ne_module *m = ne_module_by_handle ((uint16_t) c->a[0]);
	char path[300];
	if (!m)
	{
		c->ret = 0;
		return;
	}
	if (m == task.main)
		snprintf (path, sizeof path, "%s", task.dospath);
	else
		module_dospath (m, path, sizeof path);
	c->ret = (uint32_t) put_str (c->a[1], path, (int16_t) c->a[2]);
}

void api_WIN16_GetModuleHandle (struct w16call *c)
{
	uint32_t n = c->a[0];
	if (!SELECTOROF (n))
	{
		struct ne_module *m = ne_module_by_handle (OFFSETOF (n));
		c->ret = m ? (uint32_t) m->hmodule | (uint32_t) m->hinstance << 16 : 0;
		return;
	}
	char name[64];
	snprintf (name, sizeof name, "%s", w16_raw (n));
	char *b = strrchr (name, '\\');
	char *s = b ? b + 1 : name;
	char *dot = strchr (s, '.');
	if (dot)
		*dot = 0;
	int bi = api_find_module (s);
	if (bi >= 0)
	{
		c->ret = builtin_hmod[bi];
		return;
	}
	struct ne_module *m = ne_find_module (s);
	c->ret = m ? m->hmodule : 0;
}

void api_GetModuleUsage16 (struct w16call *c)
{
	struct ne_module *m = ne_module_by_handle ((uint16_t) c->a[0]);
	c->ret = m ? (uint32_t) m->refs : 0;
}

void api_WIN16_GetExePtr (struct w16call *c)
{
	struct ne_module *m = ne_module_by_handle ((uint16_t) c->a[0]);
	c->ret = m ? m->hmodule : 0;
}

void api_LoadLibrary16 (struct w16call *c)
{
	const char *n = w16_raw (c->a[0]);
	char s[80];
	snprintf (s, sizeof s, "%s", n);
	char *b = strrchr (s, '\\');
	char *dot = strchr (b ? b + 1 : s, '.');
	if (dot)
		*dot = 0;
	int bi = api_find_module (b ? b + 1 : s);
	if (bi >= 0)
	{
		c->ret = builtin_hmod[bi];
		return;
	}
	struct ne_module *m = ne_load_library (n);
	w16_log ("   LoadLibrary (%s) : %s\n", n, m ? m->name : "introuvable");
	c->ret = m ? m->hinstance : 2;	/* (2 : fichier introuvable) */
}

void api_WIN16_FreeLibrary16 (struct w16call *c)
{
	struct ne_module *m = ne_module_by_handle ((uint16_t) c->a[0]);
	if (m && m->refs > 0)
		m->refs--;
}

void api_WIN16_GetProcAddress16 (struct w16call *c)
{
	uint16_t h = (uint16_t) c->a[0];
	uint32_t n = c->a[1];
	c->ret = 0;
	for (int bi = 0; bi < MAX_BUILTIN && w16_modules[bi].name; bi++)
		if (builtin_hmod[bi] == h)
		{
			int ord = SELECTOROF (n) ? api_find_name (bi, w16_raw (n)) : OFFSETOF (n);
			if (ord > 0 && api_lookup (bi, ord))
				c->ret = api_entry (bi, ord);
			return;
		}
	struct ne_module *m = ne_module_by_handle (h);
	if (!m)
		return;
	int ord = SELECTOROF (n) ? ne_find_name (m, w16_raw (n)) : OFFSETOF (n);
	c->ret = ord > 0 ? ne_entry (m, ord) : 0;
	w16_log ("   GetProcAddress (%s, %s) = %08x\n", m->name, SELECTOROF (n) ? w16_raw (n) : "#", c->ret);
}

void api_MakeProcInstance16 (struct w16call *c) { c->ret = c->a[0]; }
void api_FreeProcInstance16 (struct w16call *c) { (void) c; }
void api_GetCodeHandle16 (struct w16call *c) { c->ret = SELECTOROF (c->a[0]); }

void api_GetInstanceData16 (struct w16call *c)
{
	(void) c;
	c->ret = 0;
}

/* ---- mémoire globale ---- */
void api_WIN16_GlobalAlloc16 (struct w16call *c)
{
	uint32_t size = c->a[1];
	uint16_t sel = global_alloc (size, 0);
	c->ret = sel;
	c->retset = 1;
	if (sel)
		c->ret = sel;
}

void api_WIN16_GlobalReAlloc16 (struct w16call *c)
{
	uint16_t h = (uint16_t) c->a[0];
	uint32_t size = c->a[1];
	uint16_t flags = (uint16_t) c->a[2];
	if (flags & 0x80)	/* GMEM_MODIFY */
	{
		c->ret = h;
		return;
	}
	if (!h)
	{
		c->ret = global_alloc (size, 0);
		return;
	}
	if (global_realloc (h | 7, size))
	{
		c->ret = h;
		return;
	}
	if (flags & 0x02)	/* GMEM_MOVEABLE : ailleurs */
	{
		uint16_t n = global_alloc (size, 0);
		if (n)
		{
			uint32_t old = global_size (h | 7);
			memcpy (lin_ptr (sel_base (n)), lin_ptr (sel_base (h | 7)), old < size ? old : size);
			global_free (h | 7);
		}
		c->ret = n;
		return;
	}
	c->ret = 0;
}

void api_WIN16_GlobalFree16 (struct w16call *c) { global_free ((uint16_t) (c->a[0] | 7)); c->ret = 0; }
void api_WIN16_GlobalLock16 (struct w16call *c) { uint16_t s = (uint16_t) (c->a[0] | 7); c->ret = c->a[0] && sel_valid (s) ? MAKESEGPTR (s, 0) : 0; }
void api_WIN16_GlobalUnlock16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_WIN16_GlobalSize16 (struct w16call *c) { c->ret = global_size ((uint16_t) (c->a[0] | 7)); }
void api_GlobalHandle16 (struct w16call *c) { uint16_t s = (uint16_t) (c->a[0] | 7); c->ret = sel_valid (s) ? MAKESEGPTR (s, s) : 0; }
void api_WIN16_GlobalFlags16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_GlobalCompact16 (struct w16call *c) { (void) c; c->ret = 0x400000; }
void api_GetFreeSpace16 (struct w16call *c) { (void) c; c->ret = 0x800000; }
void api_LockSegment16 (struct w16call *c) { c->ret = c->a[0]; }
void api_UnlockSegment16 (struct w16call *c) { c->ret = c->a[0]; }
void api_GlobalFix16 (struct w16call *c) { c->ret = c->a[0]; }
void api_GlobalUnfix16 (struct w16call *c) { c->ret = c->a[0]; }
void api_GlobalPageLock16 (struct w16call *c) { c->ret = 1; (void) c; }
void api_GlobalPageUnlock16 (struct w16call *c) { c->ret = 0; (void) c; }
void api_GlobalLRUNewest16 (struct w16call *c) { c->ret = c->a[0]; }
void api_GlobalLRUOldest16 (struct w16call *c) { c->ret = c->a[0]; }

/* ---- sélecteurs ---- */
void api_AllocCStoDSAlias16 (struct w16call *c) { c->ret = sel_valid ((uint16_t) c->a[0]) ? sel_alias ((uint16_t) c->a[0], 0) : 0; }
void api_AllocDStoCSAlias16 (struct w16call *c) { c->ret = sel_valid ((uint16_t) c->a[0]) ? sel_alias ((uint16_t) c->a[0], 1) : 0; }
void api_WIN16_FreeSelector16 (struct w16call *c) { sel_free ((uint16_t) c->a[0]); c->ret = 0; }

void api_AllocSelector16 (struct w16call *c)
{
	uint16_t s = (uint16_t) c->a[0];
	c->ret = sel_valid (s) ? sel_alias (s, sel_is_code (s)) : sel_alloc (0, 1, 0);
}

void api_PrestoChangoSelector16 (struct w16call *c)
{
	uint16_t src = (uint16_t) c->a[0], dst = (uint16_t) c->a[1];
	if (sel_valid (src) && sel_valid (dst))
		sel_set (dst, sel_base (src), sel_limit (src) + 1, !sel_is_code (src));
	c->ret = dst;
}

void api_GetSelectorBase (struct w16call *c) { c->ret = sel_base ((uint16_t) c->a[0]); }
void api_GetSelectorLimit16 (struct w16call *c) { c->ret = sel_limit ((uint16_t) c->a[0]); }

void api_SetSelectorBase (struct w16call *c)
{
	uint16_t s = (uint16_t) c->a[0];
	sel_set (s, c->a[1], sel_limit (s) + 1, sel_is_code (s));
	cpu_reload_segments ();
	c->ret = s;
}

void api_SetSelectorLimit16 (struct w16call *c)
{
	uint16_t s = (uint16_t) c->a[0];
	sel_set (s, sel_base (s), c->a[1] + 1, sel_is_code (s));
	cpu_reload_segments ();
	c->ret = s;
}

/* ---- pointeurs et chaînes ---- */
static int ptr_ok (uint32_t p, uint32_t n)
{
	uint16_t s = SELECTOROF (p);
	if (!sel_valid (s))
		return 0;
	return !n || (uint32_t) OFFSETOF (p) + n - 1 <= sel_limit (s) || n > 0x10000;
}

void api_IsBadReadPtr16 (struct w16call *c) { c->ret = !ptr_ok (c->a[0], c->a[1]); }
void api_IsBadWritePtr16 (struct w16call *c) { c->ret = !ptr_ok (c->a[0], c->a[1]); }
void api_IsBadHugeReadPtr16 (struct w16call *c) { c->ret = !ptr_ok (c->a[0], 1); }
void api_IsBadHugeWritePtr16 (struct w16call *c) { c->ret = !ptr_ok (c->a[0], 1); }
void api_IsBadCodePtr16 (struct w16call *c) { c->ret = !ptr_ok (c->a[0], 1); }
void api_IsBadStringPtr16 (struct w16call *c) { c->ret = !ptr_ok (c->a[0], 1); }

void api_hmemcpy16 (struct w16call *c)
{
	uint32_t dst = far_lin (c->a[0]), src = far_lin (c->a[1]), n = c->a[2];
	if (n && n < W16_MEM_SIZE)
		memmove (lin_ptr (dst), lin_ptr (src), n);
}

void api_lstrcpy16 (struct w16call *c)
{
	const char *s = far_ptr (c->a[1]);
	memmove (far_ptr (c->a[0]), s, strlen (s) + 1);
	c->ret = c->a[0];
}

void api_lstrcpyn16 (struct w16call *c)
{
	const char *s = far_ptr (c->a[1]);
	int n = (int) c->a[2];
	if (n > 0)
	{
		int l = (int) strlen (s);
		if (l > n - 1)
			l = n - 1;
		memmove (far_ptr (c->a[0]), s, (size_t) l);
		((char *) far_ptr (c->a[0]))[l] = 0;
	}
	c->ret = c->a[0];
}

void api_lstrcat16 (struct w16call *c)
{
	char *d = far_ptr (c->a[0]);
	const char *s = far_ptr (c->a[1]);
	memmove (d + strlen (d), s, strlen (s) + 1);
	c->ret = c->a[0];
}

void api_lstrlen16 (struct w16call *c) { c->ret = c->a[0] ? (uint32_t) strlen (far_ptr (c->a[0])) : 0; }
void api_lstrcmpA (struct w16call *c) { c->ret = (uint16_t) (int16_t) strcmp (w16_raw (c->a[0]), w16_raw (c->a[1])); }
void api_lstrcmpiA (struct w16call *c) { c->ret = (uint16_t) (int16_t) strcasecmp (w16_raw (c->a[0]), w16_raw (c->a[1])); }

/* ---- tas local (LocalAlloc) : les blocs sont tenus ici, pas dans le segment ---- */
#define MAX_LHEAP 16
#define MAX_LBLK 2048
struct lblk { uint16_t off, size, used; };
struct lheap { uint16_t sel, start, end; int n; struct lblk b[MAX_LBLK]; };
static struct lheap *lheaps[MAX_LHEAP];

void local_init (uint16_t sel, uint16_t start, uint16_t end)
{
	struct lheap *h = 0;
	for (int i = 0; i < MAX_LHEAP && !h; i++)
		if (lheaps[i] && (lheaps[i]->sel | 7) == (sel | 7))
			h = lheaps[i];
	for (int i = 0; i < MAX_LHEAP && !h; i++)
		if (!lheaps[i])
			h = lheaps[i] = calloc (1, sizeof *h);
	if (!h)
		return;
	h->sel = sel | 7;
	h->start = (uint16_t) ((start + 15) & ~15);
	h->end = end;
	h->n = 1;
	h->b[0].off = h->start;
	h->b[0].size = (uint16_t) (end > h->start ? end - h->start : 0);
	h->b[0].used = 0;
	w16_log ("   tas local %04x : %04x..%04x\n", sel, h->start, end);
}

static struct lheap *lheap_of (uint16_t sel)
{
	for (int i = 0; i < MAX_LHEAP; i++)
		if (lheaps[i] && lheaps[i]->sel == (sel | 7))
			return lheaps[i];
	return 0;
}

static uint16_t local_alloc (uint16_t sel, uint16_t flags, uint16_t size)
{
	struct lheap *h = lheap_of (sel);
	if (!h)
	{
		/* (pas de LocalInit : un tas d'office à la fin du segment) */
		uint32_t lim = sel_limit (sel) + 1;
		local_init (sel, (uint16_t) (lim > 0x2000 ? lim - 0x2000 : lim / 2), (uint16_t) (lim > 0xffff ? 0xffff : lim));
		h = lheap_of (sel);
		if (!h)
			return 0;
	}
	size = (uint16_t) ((size + 3) & ~3);
	if (!size)
		size = 4;
	for (int i = 0; i < h->n; i++)
	{
		if (h->b[i].used || h->b[i].size < size)
			continue;
		if (h->b[i].size > size + 4 && h->n < MAX_LBLK)
		{
			memmove (&h->b[i + 2], &h->b[i + 1], (size_t) (h->n - i - 1) * sizeof h->b[0]);
			h->n++;
			h->b[i + 1].off = (uint16_t) (h->b[i].off + size);
			h->b[i + 1].size = (uint16_t) (h->b[i].size - size);
			h->b[i + 1].used = 0;
			h->b[i].size = size;
		}
		h->b[i].used = 1;
		if (flags & 0x40)	/* LMEM_ZEROINIT */
			memset (lin_ptr (sel_base (sel) + h->b[i].off), 0, size);
		return h->b[i].off;
	}
	/* plein : comme Windows, le segment grandit (jusqu'à 64 Ko) quand le tas en occupe la fin (DGROUP d'une DLL) */
	uint32_t lim = sel_limit (sel) + 1;
	if (h->end >= lim - 16 && lim < 0x10000 && h->n < MAX_LBLK)
	{
		uint32_t want = lim + size + 0x800;
		want = want > 0x10000 ? 0x10000 : (want + 0xff) & ~0xffu;
		if (want > lim && global_realloc (sel, want))
		{
			cpu_reload_segments ();
			uint16_t old_end = h->end, new_end = (uint16_t) (want > 0xffff ? 0xffff : want);
			if (h->n && !h->b[h->n - 1].used && h->b[h->n - 1].off + h->b[h->n - 1].size == old_end)
				h->b[h->n - 1].size = (uint16_t) (h->b[h->n - 1].size + (new_end - old_end));
			else
			{
				h->b[h->n].off = old_end;
				h->b[h->n].size = (uint16_t) (new_end - old_end);
				h->b[h->n].used = 0;
				h->n++;
			}
			h->end = new_end;
			w16_log ("   tas local %04x agrandi : %04x..%04x\n", sel, h->start, new_end);
			return local_alloc (sel, flags, size);
		}
	}
	return 0;
}

static struct lblk *local_find (uint16_t sel, uint16_t off, struct lheap **hp, int *ip)
{
	struct lheap *h = lheap_of (sel);
	if (!h)
		return 0;
	for (int i = 0; i < h->n; i++)
		if (h->b[i].off == off && h->b[i].used)
		{
			if (hp) *hp = h;
			if (ip) *ip = i;
			return &h->b[i];
		}
	return 0;
}

static void local_free (uint16_t sel, uint16_t off)
{
	struct lheap *h;
	int i;
	struct lblk *b = local_find (sel, off, &h, &i);
	if (!b)
		return;
	b->used = 0;
	if (i + 1 < h->n && !h->b[i + 1].used)
	{
		h->b[i].size = (uint16_t) (h->b[i].size + h->b[i + 1].size);
		memmove (&h->b[i + 1], &h->b[i + 2], (size_t) (h->n - i - 2) * sizeof h->b[0]);
		h->n--;
	}
	if (i > 0 && !h->b[i - 1].used)
	{
		h->b[i - 1].size = (uint16_t) (h->b[i - 1].size + h->b[i].size);
		memmove (&h->b[i], &h->b[i + 1], (size_t) (h->n - i - 1) * sizeof h->b[0]);
		h->n--;
	}
}

void api_LocalInit16 (struct w16call *c)
{
	uint16_t sel = c->a[0] ? (uint16_t) c->a[0] : cpu_sreg (S_DS);
	uint16_t start = (uint16_t) c->a[1], end = (uint16_t) c->a[2];
	if (!start)
	{
		uint32_t lim = sel_limit (sel) + 1;
		start = (uint16_t) (lim > end ? lim - end : 16);
		end = (uint16_t) (lim > 0xffff ? 0xffff : lim);
	}
	local_init (sel, start, end);
	c->ret = 1;
}

/* les blocs déplaçables (LMEM_MOVEABLE) : comme Windows, une poignée est l'adresse d'une case de 4 octets du tas
 * (l'adresse des données, des drapeaux, le compte des verrous), avec h & 3 == 2 ; un bloc fixe est son adresse (h & 3
 * == 0). Des programmes lisent la case eux-mêmes (le moteur de Visual Basic 3). */
static int lh_is (uint16_t h) { return (h & 3) == 2; }
static uint16_t lh_data (uint16_t sel, uint16_t h) { return lh_is (h) ? rd16 (sel_base (sel) + h) : h; }

static uint16_t local_alloc_h (uint16_t sel, uint16_t flags, uint16_t size)
{
	if (!(flags & 0x02))
		return local_alloc (sel, flags, size);
	uint16_t e = local_alloc (sel, 0, 8);
	if (!e)
		return 0;
	uint16_t d = size ? local_alloc (sel, flags, size) : 0;
	if (size && !d)
	{
		local_free (sel, e);
		return 0;
	}
	uint32_t at = sel_base (sel) + e + 2;	/* (après local_alloc : le segment a pu grandir et bouger) */
	wr16 (at, d);
	lin_ptr (at)[2] = d ? 0 : 0x40;	/* (LMEM_DISCARDED pour un bloc de taille 0) */
	lin_ptr (at)[3] = 0;
	return (uint16_t) (e + 2);
}

static void local_free_h (uint16_t sel, uint16_t h)
{
	if (lh_is (h))
	{
		uint16_t d = rd16 (sel_base (sel) + h);
		if (d)
			local_free (sel, d);
		local_free (sel, (uint16_t) (h - 2));
	}
	else
		local_free (sel, h);
}

void api_LocalAlloc16 (struct w16call *c) { c->ret = local_alloc_h (cpu_sreg (S_DS), (uint16_t) c->a[0], (uint16_t) c->a[1]); }
void api_LocalFree16 (struct w16call *c) { if (c->a[0]) local_free_h (cpu_sreg (S_DS), (uint16_t) c->a[0]); c->ret = 0; }

void api_LocalLock16 (struct w16call *c)
{
	uint16_t sel = cpu_sreg (S_DS), h = (uint16_t) c->a[0], d = h ? lh_data (sel, h) : 0;
	if (h && lh_is (h) && lin_ptr (sel_base (sel) + h)[3] < 0xff)
		lin_ptr (sel_base (sel) + h)[3]++;
	c->ret = d ? MAKESEGPTR (sel, d) : 0;
}

void api_LocalUnlock16 (struct w16call *c)
{
	uint16_t sel = cpu_sreg (S_DS), h = (uint16_t) c->a[0];
	c->ret = 0;
	if (h && lh_is (h))
	{
		uint8_t *k = lin_ptr (sel_base (sel) + h) + 3;
		if (*k)
			(*k)--;
		c->ret = *k;
	}
}

void api_LocalSize16 (struct w16call *c)
{
	uint16_t sel = cpu_sreg (S_DS), h = (uint16_t) c->a[0];
	struct lblk *b = h ? local_find (sel, lh_data (sel, h), 0, 0) : 0;
	c->ret = b ? b->size : 0;
}

void api_LocalFlags16 (struct w16call *c)
{
	uint16_t sel = cpu_sreg (S_DS), h = (uint16_t) c->a[0];
	c->ret = lh_is (h) ? (uint32_t) (lin_ptr (sel_base (sel) + h)[3] | (lin_ptr (sel_base (sel) + h)[2] & 0x40) << 8) : 0;
}

/* LocalHandle (adresse) -> la poignée dont la case la montre, sinon l'adresse elle-même */
void api_LocalHandle16 (struct w16call *c)
{
	uint16_t sel = cpu_sreg (S_DS), p = (uint16_t) c->a[0];
	struct lheap *h = lheap_of (sel);
	c->ret = p;
	for (int i = 0; h && i < h->n; i++)
		if (h->b[i].used && h->b[i].size == 8 && rd16 (sel_base (sel) + h->b[i].off + 2) == p)
		{
			c->ret = (uint32_t) (h->b[i].off + 2);
			return;
		}
}

void api_LocalReAlloc16 (struct w16call *c)
{
	uint16_t sel = cpu_sreg (S_DS), h = (uint16_t) c->a[0], size = (uint16_t) c->a[1], flags = (uint16_t) c->a[2];
	if (flags & 0x80)	/* LMEM_MODIFY : seulement les drapeaux */
	{
		c->ret = h;
		return;
	}
	uint16_t off = lh_data (sel, h);
	struct lblk *b = off ? local_find (sel, off, 0, 0) : 0;
	uint16_t n;
	if (!b)
	{
		if (!lh_is (h) || off)
		{
			c->ret = 0;
			return;
		}
		n = size ? local_alloc (sel, flags, size) : 0;	/* (un bloc jeté qui revient) */
	}
	else if (size <= b->size)
		n = off;
	else
	{
		uint16_t old = b->size;
		n = local_alloc (sel, flags, size);
		if (n)
		{
			memmove (lin_ptr (sel_base (sel) + n), lin_ptr (sel_base (sel) + off), old);
			local_free (sel, off);
		}
	}
	if (!n && size)
	{
		c->ret = 0;
		return;
	}
	if (lh_is (h))
	{
		wr16 (sel_base (sel) + h, n);
		lin_ptr (sel_base (sel) + h)[2] = n ? 0 : 0x40;
		c->ret = h;
	}
	else
		c->ret = n;
}

static uint16_t largest_free (uint16_t sel, int sum)
{
	struct lheap *h = lheap_of (sel);
	uint32_t best = 0;
	for (int i = 0; h && i < h->n; i++)
		if (!h->b[i].used)
			best = sum ? best + h->b[i].size : (h->b[i].size > best ? h->b[i].size : best);
	return (uint16_t) (best > 0xffff ? 0xffff : best);
}

void api_LocalCompact16 (struct w16call *c) { c->ret = largest_free (cpu_sreg (S_DS), 0); }
void api_LocalCountFree16 (struct w16call *c) { c->ret = largest_free (cpu_sreg (S_DS), 1); }
void api_LocalHeapSize16 (struct w16call *c)
{
	struct lheap *h = lheap_of (cpu_sreg (S_DS));
	c->ret = h ? (uint32_t) (h->end - h->start) : 0;
}
void api_LocalShrink16 (struct w16call *c) { struct lheap *h = lheap_of ((uint16_t) c->a[0] ? (uint16_t) c->a[0] : cpu_sreg (S_DS)); c->ret = h ? (uint32_t) (h->end - h->start) : 0; }

/* ---- ressources ---- */
#define MAX_RSRC 1024
static struct { struct ne_module *m; uint32_t off, size; uint16_t mem; } rsrcs[MAX_RSRC];

void api_FindResource16 (struct w16call *c)
{
	struct ne_module *m = ne_module_by_handle ((uint16_t) c->a[0]);
	uint32_t off, size;
	c->ret = 0;
	if (!m || !ne_resource_find_ex (m, c->a[2], c->a[1], &off, &size))
	{
		w16_log ("   ressource %s de type %s introuvable\n", w16_str (c->a[1]), w16_str (c->a[2]));
		return;
	}
	for (int i = 1; i < MAX_RSRC; i++)
		if ((rsrcs[i].m == m && rsrcs[i].off == off) || !rsrcs[i].m)
		{
			rsrcs[i].m = m;
			rsrcs[i].off = off;
			rsrcs[i].size = size;
			c->ret = (uint32_t) (0x8000 | i);
			return;
		}
}

static int rsrc_index (uint16_t h)
{
	int i = h & 0x7fff;
	return (h & 0x8000) && i > 0 && i < MAX_RSRC && rsrcs[i].m ? i : 0;
}

/* charge une ressource en mémoire globale -> sélecteur (gardé : un second LoadResource rend le même) */
uint16_t kernel_load_resource (uint16_t hrsrc)
{
	int i = rsrc_index (hrsrc);
	if (!i)
		return 0;
	if (rsrcs[i].mem && sel_valid (rsrcs[i].mem))
		return rsrcs[i].mem;
	uint16_t s = global_alloc (rsrcs[i].size, 0);
	if (!s)
		return 0;
	uint32_t n = rsrcs[i].size;
	if (rsrcs[i].off + n > rsrcs[i].m->size)
		n = (uint32_t) rsrcs[i].m->size - rsrcs[i].off;
	memcpy (lin_ptr (sel_base (s)), rsrcs[i].m->file + rsrcs[i].off, n);
	rsrcs[i].mem = s;
	return s;
}

/* une ressource du module (numéro ou nom) -> pointeur hôte sur ses données, taille ; 0 si absente */
const uint8_t *kernel_resource (uint16_t hinst, uint32_t type, uint32_t name, uint32_t *size)
{
	struct ne_module *m = ne_module_by_handle (hinst);
	uint32_t off;
	if (!m || !ne_resource_find_ex (m, type, name, &off, size))
		return 0;
	return m->file + off;
}

void api_LoadResource16 (struct w16call *c) { c->ret = kernel_load_resource ((uint16_t) c->a[1]); }
void api_WIN16_LockResource16 (struct w16call *c) { c->ret = c->a[0] ? MAKESEGPTR (c->a[0] | 7, 0) : 0; }

void api_FreeResource16 (struct w16call *c)
{
	for (int i = 1; i < MAX_RSRC; i++)
		if (rsrcs[i].mem && (rsrcs[i].mem | 7) == (c->a[0] | 7))
		{
			global_free (rsrcs[i].mem);
			rsrcs[i].mem = 0;
		}
	c->ret = 0;
}

void api_SizeofResource16 (struct w16call *c)
{
	int i = rsrc_index ((uint16_t) c->a[1]);
	c->ret = i ? rsrcs[i].size : 0;
}

void api_AccessResource16 (struct w16call *c)
{
	int i = rsrc_index ((uint16_t) c->a[1]);
	c->ret = 0xffff;
	if (!i)
		return;
	int fd = open (rsrcs[i].m->path, O_RDONLY);
	if (fd < 0)
		return;
	lseek (fd, rsrcs[i].off, SEEK_SET);
	int h = fh_new (fd);
	c->ret = h < 0 ? 0xffff : (uint32_t) h;
}

/* ======================================================================== INT 21h */
static void set_cf (int on)
{
	cpu_set_flags (on ? cpu_flags () | 1 : cpu_flags () & ~1u);
}

#define AX ((uint16_t) cpu_reg (R_EAX))
#define AH ((uint8_t) (cpu_reg (R_EAX) >> 8))
#define AL ((uint8_t) cpu_reg (R_EAX))
#define BX ((uint16_t) cpu_reg (R_EBX))
#define CX ((uint16_t) cpu_reg (R_ECX))
#define DX ((uint16_t) cpu_reg (R_EDX))
static void set16 (int r, uint16_t v) { cpu_set_reg (r, (cpu_reg (r) & 0xffff0000u) | v); }
static uint32_t ds_dx (void) { return MAKESEGPTR (cpu_sreg (S_DS), DX); }

void w16_terminate (int code);

static int dos_last_err;	/* (rendue par l'INT 21h 59h : le Dir$ de Visual Basic s'en sert) */

static void dos_err (int code)
{
	dos_last_err = code;
	set16 (R_EAX, (uint16_t) code);
	set_cf (1);
}

/* recherche de fichiers (4Eh/4Fh) : le résultat dans le DTA */
static uint32_t dta;
static char find_dir[700], find_pat[64];
static DIR *find_d;
static int find_attr;

static int wild_match (const char *pat, const char *name)
{
	/* motif DOS 8.3 : « * » et « ? », sans casse */
	for (; *pat; pat++, name++)
	{
		if (*pat == '*')
		{
			if (!pat[1])
				return 1;
			for (const char *n = name; *n; n++)
				if (wild_match (pat + 1, n))
					return 1;
			return wild_match (pat + 1, name);
		}
		if (!*name)
			return !strcmp (pat, ".*") || !strcmp (pat, "*") ;
		if (*pat != '?' && toupper ((unsigned char) *pat) != toupper ((unsigned char) *name))
			return 0;
	}
	return !*name;
}

void kernel_set_dta (uint32_t segptr) { dta = segptr; }

/* ---- les noms courts (8.3) des fichiers de Korin, comme Windows 95 : « Mon document.txt » -> MONDOC~1.TXT ;
 * le numéro suit l'ordre du dossier, il reste le même tant que le dossier ne change pas ---- */
static int ok83 (int c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c >= 128 && c != 0xff) || strchr ("!#$%&'()-@^_`{}~", c);
}

/* un nom déjà en 8.3 (aux minuscules près) */
static int is_83 (const char *n)
{
	const char *dot = strchr (n, '.');
	size_t bl = dot ? (size_t) (dot - n) : strlen (n);
	if (!bl || bl > 8 || (dot && (strchr (dot + 1, '.') || strlen (dot + 1) > 3)))
		return 0;
	for (const char *p = n; *p; p++)
		if (p != dot && !ok83 (toupper ((unsigned char) *p)))
			return 0;
	return 1;
}

/* la base (6 caractères) et l'extension (3) d'un nom long */
static void short_key (const char *n, char *base, char *ext)
{
	const char *dot = strrchr (n, '.');
	if (dot == n)
		dot = 0;
	int k = 0;
	for (const char *p = n; *p && p != dot && k < 6; p++)
	{
		int c = toupper ((unsigned char) *p);
		if (c == ' ' || c == '.')
			continue;
		base[k++] = (char) (ok83 (c) ? c : '_');
	}
	base[k] = 0;
	k = 0;
	for (const char *p = dot ? dot + 1 : ""; *p && k < 3; p++)
	{
		int c = toupper ((unsigned char) *p);
		if (c != ' ')
			ext[k++] = (char) (ok83 (c) ? c : '_');
	}
	ext[k] = 0;
}

void kernel_short_name (const char *dir, const char *name, char *out)
{
	if (is_83 (name))
	{
		int k = 0;
		for (; name[k] && k < 12; k++)
			out[k] = (char) toupper ((unsigned char) name[k]);
		out[k] = 0;
		return;
	}
	char base[8], ext[4];
	short_key (name, base, ext);
	int n = 1;
	DIR *d = opendir (dir && *dir ? dir : ".");
	if (d)
	{
		struct dirent *e;
		while ((e = readdir (d)) && strcmp (e->d_name, name))
		{
			if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, "..") || is_83 (e->d_name))
				continue;
			char b2[8], e2[4];
			short_key (e->d_name, b2, e2);
			if (!strcmp (b2, base) && !strcmp (e2, ext))
				n++;
		}
		closedir (d);
	}
	char tail[8];
	snprintf (tail, sizeof tail, "~%d", n);
	int keep = 8 - (int) strlen (tail);
	if ((int) strlen (base) > keep)
		base[keep] = 0;
	snprintf (out, 13, "%s%s%s%s", base, tail, ext[0] ? "." : "", ext);
}

/* le nom long d'un dossier dont le nom court est name -> 1 (out = dir/nom) */
int kernel_find_short (const char *dir, const char *name, char *out, size_t cap)
{
	if (!strchr (name, '~'))
		return 0;
	DIR *d = opendir (dir && *dir ? dir : ".");
	if (!d)
		return 0;
	struct dirent *e;
	int ok = 0;
	char sn[13];
	while (!ok && (e = readdir (d)))
	{
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, "..") || is_83 (e->d_name))
			continue;
		kernel_short_name (dir, e->d_name, sn);
		if (!strcasecmp (sn, name))
		{
			snprintf (out, cap, "%s/%s", dir && *dir ? dir : ".", e->d_name);
			ok = 1;
		}
	}
	closedir (d);
	return ok;
}

static int find_next (void)
{
	struct dirent *e;
	while (find_d && (e = readdir (find_d)))
	{
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, ".."))
			continue;
		char sn[13];
		kernel_short_name (find_dir, e->d_name, sn);
		if (!wild_match (find_pat, sn))
			continue;
		char p[1000];
		struct stat st;
		snprintf (p, sizeof p, "%s/%s", find_dir, e->d_name);
		if (stat (p, &st))
			continue;
		int dir = S_ISDIR (st.st_mode);
		int hidden = e->d_name[0] == '.';	/* (les fichiers cachés de Korin : cachés pour DOS aussi) */
		if ((dir && !(find_attr & 0x10)) || (hidden && !(find_attr & 0x02)))
			continue;
		uint8_t *d = lin_ptr (far_lin (dta));
		d[0x15] = (uint8_t) ((dir ? 0x10 : 0x20) | (hidden ? 0x02 : 0));
		struct tm *tm = localtime (&st.st_mtime);
		wr16 (far_lin (dta) + 0x16, (uint16_t) (tm->tm_hour << 11 | tm->tm_min << 5 | tm->tm_sec / 2));
		wr16 (far_lin (dta) + 0x18, (uint16_t) ((tm->tm_year - 80) << 9 | (tm->tm_mon + 1) << 5 | tm->tm_mday));
		wr32 (far_lin (dta) + 0x1a, (uint32_t) st.st_size);
		memcpy (d + 0x1e, sn, strlen (sn) + 1);
		return 1;
	}
	return 0;
}

void dos_int21 (void)
{
	/* (débogage : W16_DOSLOG=1 note chaque appel, avec le nom de fichier quand il y en a un) */
	static int doslog = -1;
	if (doslog < 0)
		doslog = getenv ("W16_DOSLOG") != 0;
	if (doslog)
		w16_log ("   INT 21h AX=%04x DX=%04x (%s)\n", AX, DX,
			AH == 0x39 || AH == 0x3a || AH == 0x3b || AH == 0x3c || AH == 0x3d || AH == 0x41 || AH == 0x43 || AH == 0x4e
				? w16_raw (ds_dx ()) : "");
	set_cf (0);
	switch (AH)
	{
	case 0x02:	/* écrire un caractère */
		fputc (DX & 0xff, stderr);
		break;
	case 0x09:	/* écrire une chaîne terminée par $ */
	{
		const char *s = far_ptr (ds_dx ());
		for (int i = 0; s[i] != '$' && i < 4096; i++)
			fputc (s[i], stderr);
		break;
	}
	case 0x0e:	/* lecteur courant */
		if ((DX & 0xff) < 26 && drive_root ((DX & 0xff) + 1))
			cur_drive = (DX & 0xff) + 1;
		set16 (R_EAX, (uint16_t) ((AX & 0xff00) | 26));
		break;
	case 0x19:
		set16 (R_EAX, (uint16_t) ((AX & 0xff00) | (cur_drive - 1)));
		break;
	case 0x1a:	/* adresse du DTA */
		dta = ds_dx ();
		break;
	case 0x2f:
		cpu_set_sreg (S_ES, SELECTOROF (dta));
		set16 (R_EBX, OFFSETOF (dta));
		break;
	case 0x25:	/* vecteur d'interruption : rien */
		break;
	case 0x35:
		cpu_set_sreg (S_ES, 0);
		set16 (R_EBX, 0);
		break;
	case 0x2a:	/* date */
	case 0x2c:	/* heure */
	{
		time_t t = time (0);
		struct tm *tm = localtime (&t);
		if (AH == 0x2a)
		{
			set16 (R_ECX, (uint16_t) (tm->tm_year + 1900));
			set16 (R_EDX, (uint16_t) ((tm->tm_mon + 1) << 8 | tm->tm_mday));
			set16 (R_EAX, (uint16_t) ((AX & 0xff00) | tm->tm_wday));
		}
		else
		{
			set16 (R_ECX, (uint16_t) (tm->tm_hour << 8 | tm->tm_min));
			set16 (R_EDX, (uint16_t) (tm->tm_sec << 8));
		}
		break;
	}
	case 0x30:	/* version de DOS : 5.0 */
		set16 (R_EAX, 0x0005);
		set16 (R_EBX, 0);
		set16 (R_ECX, 0);
		break;
	case 0x33:	/* Ctrl-Break */
		set16 (R_EDX, 0);
		break;
	case 0x36:	/* place libre : 100 Mo */
		set16 (R_EAX, 64);
		set16 (R_EBX, 3200);
		set16 (R_ECX, 512);
		set16 (R_EDX, 6400);
		break;
	case 0x39:	/* créer un dossier */
	case 0x3a:	/* le retirer */
	case 0x41:	/* effacer un fichier */
	{
		char p[700];
		int r = dos_to_host (w16_raw (ds_dx ()), p, sizeof p) ? (AH == 0x39 ? mkdir (p, 0755) : AH == 0x3a ? rmdir (p) : unlink (p)) : -1;
		if (r)
			dos_err (AH == 0x41 ? 2 : 3);
		break;
	}
	case 0x3b:	/* dossier courant */
	{
		const char *s = w16_raw (ds_dx ());
		char p[700];
		struct stat st;
		if (!dos_to_host (s, p, sizeof p) || stat (p, &st) || !S_ISDIR (st.st_mode))
		{
			dos_err (3);
			break;
		}
		if (s[0] && s[1] == ':')
		{
			cur_drive = toupper ((unsigned char) s[0]) - 'A' + 1;
			s += 2;
		}
		if (*s == '\\')
			snprintf (cur_dir, sizeof cur_dir, "%s", s + 1);
		else if (*s)
		{
			char t[256];
			snprintf (t, sizeof t, "%s%s%s", cur_dir, cur_dir[0] ? "\\" : "", s);
			snprintf (cur_dir, sizeof cur_dir, "%s", t);
		}
		size_t l = strlen (cur_dir);
		if (l && cur_dir[l - 1] == '\\')
			cur_dir[l - 1] = 0;
		break;
	}
	case 0x3c:	/* créer */
	case 0x3d:	/* ouvrir */
	{
		int h = dos_open (w16_raw (ds_dx ()), AL & 3, AH == 0x3c);
		if (h < 0)
			dos_err (2);
		else
			set16 (R_EAX, (uint16_t) h);
		break;
	}
	case 0x3e:	/* fermer */
	{
		int fd = fh_fd (BX);
		if (BX < 5)
			break;
		if (fd < 0)
			dos_err (6);
		else
		{
			close (fd);
			fh[BX] = 0;
		}
		break;
	}
	case 0x3f:	/* lire */
	{
		if (BX == 0 && fh_fd (0) < 0)
		{
			set16 (R_EAX, 0);
			break;
		}
		int32_t r = do_read (BX, ds_dx (), CX);
		if (r < 0)
			dos_err (6);
		else
			set16 (R_EAX, (uint16_t) r);
		break;
	}
	case 0x40:	/* écrire */
	{
		int32_t r = do_write (BX, ds_dx (), CX);
		if (r < 0)
			dos_err (6);
		else
			set16 (R_EAX, (uint16_t) r);
		break;
	}
	case 0x42:	/* se placer */
	{
		int fd = fh_fd (BX);
		off_t r = fd < 0 ? -1 : lseek (fd, (int32_t) ((uint32_t) CX << 16 | DX), AL);
		if (r < 0)
			dos_err (6);
		else
		{
			set16 (R_EAX, (uint16_t) r);
			set16 (R_EDX, (uint16_t) (r >> 16));
		}
		break;
	}
	case 0x43:	/* attributs */
	{
		char p[700];
		struct stat st;
		if (!dos_to_host (w16_raw (ds_dx ()), p, sizeof p) || stat (p, &st))
			dos_err (2);
		else if (AL == 0)
			set16 (R_ECX, S_ISDIR (st.st_mode) ? 0x10 : 0x20);
		break;
	}
	case 0x44:	/* IOCTL : les poignées 0..4 sont des périphériques */
		if (AL == 0)
			set16 (R_EDX, BX == 0 ? 0x81 : BX == 1 ? 0x82 : BX < 5 ? 0x80 : 0x02);
		else if (AL == 8)
			set16 (R_EAX, 1);	/* disque fixe */
		else if (AL == 9)
			set16 (R_EDX, 0);
		else
			dos_err (1);
		break;
	case 0x45:	/* dupliquer */
	{
		int fd = fh_fd (BX);
		int h = fd < 0 ? -1 : fh_new (dup (fd));
		if (h < 0)
			dos_err (6);
		else
			set16 (R_EAX, (uint16_t) h);
		break;
	}
	case 0x47:	/* dossier courant */
	{
		char *d = (char *) lin_ptr (cpu_sreg_base (S_DS) + (uint16_t) cpu_reg (R_ESI));
		int drv = (DX & 0xff) ? (DX & 0xff) : cur_drive;
		snprintf (d, 64, "%s", drv == cur_drive ? cur_dir : "");
		for (char *q = d; *q; q++)
			*q = (char) toupper ((unsigned char) *q);
		break;
	}
	case 0x4c:	/* fin du programme */
		w16_terminate (AL);
		break;
	case 0x4e:	/* premier fichier */
	case 0x4f:	/* suivant */
		if (AH == 0x4e)
		{
			char spec[300], p[700];
			snprintf (spec, sizeof spec, "%s", w16_raw (ds_dx ()));
			char *b = strrchr (spec, '\\');
			char *pat = b ? b + 1 : (spec[1] == ':' ? spec + 2 : spec);
			snprintf (find_pat, sizeof find_pat, "%s", pat);
			if (b)
				b[1] = 0;
			else
				*pat = 0;
			if (!dos_to_host (spec[0] ? spec : ".", p, sizeof p))
			{
				dos_err (3);
				break;
			}
			snprintf (find_dir, sizeof find_dir, "%s", p);
			if (find_d)
				closedir (find_d);
			find_d = opendir (find_dir);
			find_attr = CX;
		}
		if (AH == 0x4e && CX == 0x08)
		{
			/* (l'étiquette du volume seule : KORIN) */
			uint8_t *d = lin_ptr (far_lin (dta));
			d[0x15] = 0x08;
			wr32 (far_lin (dta) + 0x16, 0);
			wr32 (far_lin (dta) + 0x1a, 0);
			memcpy (d + 0x1e, "KORIN", 6);
			break;
		}
		/* (comme MS-DOS : 12h quand rien ne répond au motif, 3 quand le dossier n'existe pas ; le Dir$ de Visual
		   Basic rend "" sur 12h et lève l'erreur 53 sur tout autre code) */
		if (!find_next ())
			dos_err (AH == 0x4e && !find_d ? 3 : 18);
		break;
	case 0x51:
	case 0x62:	/* le PSP */
		set16 (R_EBX, task.psp);
		break;
	case 0x56:	/* renommer */
	{
		char a[700], b[700];
		uint32_t es_di = MAKESEGPTR (cpu_sreg (S_ES), (uint16_t) cpu_reg (R_EDI));
		if (!dos_to_host (w16_raw (ds_dx ()), a, sizeof a) || !dos_to_host (w16_raw (es_di), b, sizeof b) || rename (a, b))
			dos_err (5);
		break;
	}
	case 0x57:	/* date d'un fichier */
		set16 (R_ECX, 0);
		set16 (R_EDX, (uint16_t) ((26 << 9) | (1 << 5) | 1));
		break;
	case 0x59:	/* erreur étendue : le code, la classe (BH), l'action conseillée (BL), le lieu (CH) */
	{
		int e = dos_last_err;
		int notfound = e == 2 || e == 3 || e == 18;
		set16 (R_EAX, (uint16_t) e);
		set16 (R_EBX, (uint16_t) (e ? (notfound ? 8 : e == 5 ? 3 : 13) << 8 | (notfound ? 3 : 4) : 0));
		set16 (R_ECX, (uint16_t) ((e ? 2 : 0) << 8 | (CX & 0xff)));	/* (lieu : un disque) */
		break;
	}
	default:
		w16_log ("INT 21h AH=%02xh pas prise en charge\n", AH);
		dos_err (1);
		break;
	}
}

void api_DOS3Call (struct w16call *c)
{
	(void) c;
	dos_int21 ();
}
