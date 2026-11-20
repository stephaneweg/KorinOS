/* lz.c -- win16 : les fichiers compressés des disquettes de Microsoft (SZDD, « COMPRESS -r » : FICHIER.EX_) et ce
 * qui s'en sert pour installer un programme : LZEXPAND (LZOpenFile, LZInit, LZCopy, LZRead...), VER (VerFindFile,
 * VerInstallFile), et KERNEL.WinExec / LoadModule (un autre programme : un autre win16, par posix_spawn).
 *
 * Le format SZDD : « SZDD\x88\xf0\x27\x33 », 'A', le dernier caractère du nom, la taille (32 bits), puis du LZSS :
 * un octet de drapeaux pour 8 éléments (1 : un octet tel quel ; 0 : deux octets, position sur 12 bits et longueur
 * - 3 sur 4 bits, dans une fenêtre de 4096 octets remplie d'espaces, qui commence en 4096 - 16). Même algorithme que
 * tools/win16/expand.py et que lzexpand de Wine (LGPL-2.1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/stat.h>
#include "w16.h"

int kernel_open_host (const char *host, int flags);
int kernel_fh_fd (int h);
int kernel_fh_close (int h);
extern char **environ;
char w16_self[512];		/* le programme win16 lui-même (main.c), pour WinExec */

static const uint8_t szdd_magic[8] = { 'S', 'Z', 'D', 'D', 0x88, 0xf0, 0x27, 0x33 };

/* tout le fichier ouvert par fd, depuis le début */
static uint8_t *slurp_fd (int fd, size_t *n)
{
	struct stat st;
	if (fstat (fd, &st) || lseek (fd, 0, SEEK_SET) < 0)
		return 0;
	uint8_t *b = malloc ((size_t) st.st_size + 1);
	size_t got = 0;
	while (b && got < (size_t) st.st_size)
	{
		ssize_t r = read (fd, b + got, (size_t) st.st_size - got);
		if (r <= 0)
			break;
		got += (size_t) r;
	}
	*n = got;
	return b;
}

static int is_szdd (const uint8_t *b, size_t n) { return n >= 14 && !memcmp (b, szdd_magic, 8) && b[8] == 'A'; }

/* décompresse (ou recopie) -> un nouveau tampon */
static uint8_t *expand (const uint8_t *in, size_t n, size_t *outn)
{
	if (!is_szdd (in, n))
	{
		uint8_t *o = malloc (n + 1);
		if (o)
			memcpy (o, in, n);
		*outn = n;
		return o;
	}
	uint32_t size = (uint32_t) (in[10] | in[11] << 8 | in[12] << 16 | (uint32_t) in[13] << 24);
	uint8_t *o = malloc ((size_t) size + 1), win[4096];
	if (!o)
		return 0;
	memset (win, ' ', sizeof win);
	size_t ip = 14, op = 0;
	unsigned pos = 4096 - 16;
	while (ip < n && op < size)
	{
		uint8_t flags = in[ip++];
		for (int bit = 0; bit < 8 && ip < n && op < size; bit++)
		{
			if (flags & (1 << bit))
			{
				uint8_t c = in[ip++];
				o[op++] = c;
				win[pos] = c;
				pos = (pos + 1) & 4095;
			}
			else
			{
				if (ip + 1 >= n)
					break;
				unsigned a = in[ip], b = in[ip + 1];
				ip += 2;
				unsigned mpos = a | (b & 0xf0) << 4, len = (b & 0x0f) + 3;
				for (unsigned k = 0; k < len && op < size; k++)
				{
					uint8_t c = win[(mpos + k) & 4095];
					o[op++] = c;
					win[pos] = c;
					pos = (pos + 1) & 4095;
				}
			}
		}
	}
	*outn = op;
	return o;
}

static int write_host (const char *path, const uint8_t *b, size_t n)
{
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return 0;
	size_t done = 0;
	while (done < n)
	{
		ssize_t r = write (fd, b + done, n - done);
		if (r <= 0)
			break;
		done += (size_t) r;
	}
	close (fd);
	return done == n;
}

/* une copie de l'hôte vers l'hôte, décompressée si besoin -> 1 */
static int expand_file (const char *src, const char *dst)
{
	int fd = open (src, O_RDONLY);
	if (fd < 0)
		return 0;
	size_t n = 0, on = 0;
	uint8_t *in = slurp_fd (fd, &n);
	close (fd);
	if (!in)
		return 0;
	uint8_t *out = expand (in, n, &on);
	free (in);
	int ok = out && write_host (dst, out, on);
	free (out);
	w16_log ("   décompression %s -> %s : %s (%zu octets)\n", src, dst, ok ? "faite" : "échec", on);
	return ok;
}

/* « dossier » + « \ » + « nom » (DOS) */
static void dos_join (char *out, size_t cap, const char *dir, const char *name)
{
	size_t l = strlen (dir);
	snprintf (out, cap, "%s%s%s", dir, l && dir[l - 1] != '\\' && dir[l - 1] != ':' ? "\\" : "", name);
}

/* ======================================================================== LZEXPAND */
static int temp_n;

/* LZInit : une poignée sur le contenu décompressé (un fichier temporaire), ou la même si le fichier ne l'est pas */
void api_LZInit16 (struct w16call *c)
{
	int h = (int) (c->a[0] & 0xffff), fd = kernel_fh_fd (h);
	if (fd < 0)
	{
		c->ret = (uint16_t) -1;	/* LZERROR_BADINHANDLE */
		return;
	}
	size_t n = 0;
	uint8_t *in = slurp_fd (fd, &n);
	if (!in)
	{
		c->ret = (uint16_t) -2;
		return;
	}
	if (!is_szdd (in, n))
	{
		free (in);
		lseek (fd, 0, SEEK_SET);
		c->ret = (uint32_t) h;
		return;
	}
	size_t on = 0;
	uint8_t *out = expand (in, n, &on);
	free (in);
	char tmp[256];
	snprintf (tmp, sizeof tmp, "/tmp/win16-lz-%d-%d", (int) getpid (), temp_n++);
	int ok = out && write_host (tmp, out, on);
	free (out);
	int nh = ok ? kernel_open_host (tmp, O_RDONLY) : -1;
	unlink (tmp);	/* (ouvert : il reste lisible jusqu'à sa fermeture) */
	if (nh < 0)
	{
		c->ret = (uint16_t) -2;
		return;
	}
	kernel_fh_close (h);
	c->ret = (uint32_t) nh;
}

/* LZOpenFile (nom, OFSTRUCT, mode) : ouvre, remplit OFSTRUCT, puis LZInit en lecture */
void api_LZOpenFile16 (struct w16call *c)
{
	const char *name = w16_raw (c->a[0]);
	uint16_t mode = (uint16_t) c->a[2];
	char host[700];
	if (!dos_to_host (name, host, sizeof host))
	{
		c->ret = (uint16_t) -1;
		return;
	}
	uint8_t *of = far_ptr (c->a[1]);
	if (of)
	{
		memset (of, 0, 136);
		of[0] = 136;
		snprintf ((char *) of + 8, 128, "%s", name);
	}
	int fl = (mode & 0x1000) ? O_RDWR | O_CREAT | O_TRUNC : (mode & 3) == 1 ? O_WRONLY : (mode & 3) == 2 ? O_RDWR : O_RDONLY;
	if (mode & 0x4000)	/* OF_EXIST */
	{
		c->ret = access (host, F_OK) ? (uint16_t) -1 : 1;
		return;
	}
	int h = kernel_open_host (host, fl);
	if (h < 0)
	{
		c->ret = (uint16_t) -1;
		return;
	}
	if (fl != O_RDONLY)
	{
		c->ret = (uint32_t) h;
		return;
	}
	struct w16call d;
	memset (&d, 0, sizeof d);
	d.a[0] = (uint32_t) h;
	api_LZInit16 (&d);
	c->ret = d.ret;
}

static int32_t copy_handles (int src, int dst)
{
	int sfd = kernel_fh_fd (src), dfd = kernel_fh_fd (dst);
	if (sfd < 0)
		return -1;
	if (dfd < 0)
		return -2;
	size_t n = 0, on = 0;
	uint8_t *in = slurp_fd (sfd, &n);
	if (!in)
		return -3;
	uint8_t *out = expand (in, n, &on);
	free (in);
	if (!out)
		return -5;
	size_t done = 0;
	while (done < on)
	{
		ssize_t r = write (dfd, out + done, on - done);
		if (r <= 0)
			break;
		done += (size_t) r;
	}
	free (out);
	return done == on ? (int32_t) on : -4;
}

void api_LZCopy16 (struct w16call *c) { c->ret = (uint32_t) copy_handles ((int) (c->a[0] & 0xffff), (int) (c->a[1] & 0xffff)); }
void api_CopyLZFile16 (struct w16call *c) { api_LZCopy16 (c); }

void api_LZRead16 (struct w16call *c)
{
	int fd = kernel_fh_fd ((int) (c->a[0] & 0xffff));
	if (fd < 0)
	{
		c->ret = (uint16_t) -1;
		return;
	}
	ssize_t r = read (fd, far_ptr (c->a[1]), c->a[2] & 0xffff);
	c->ret = r < 0 ? (uint16_t) -3 : (uint32_t) r;
}

void api_LZSeek16 (struct w16call *c)
{
	int fd = kernel_fh_fd ((int) (c->a[0] & 0xffff));
	off_t r = fd < 0 ? -1 : lseek (fd, (off_t) (int32_t) c->a[1], (int) (c->a[2] & 3));
	c->ret = (uint32_t) (r < 0 ? -1 : r);
}

void api_LZClose16 (struct w16call *c) { kernel_fh_close ((int) (c->a[0] & 0xffff)); c->ret = 0; }
void api_LZStart16 (struct w16call *c) { c->ret = 1; }
void api_LZDone16 (struct w16call *c) { c->ret = 0; }

/* GetExpandedName : le dernier caractère d'origine (« _ » remplacé), d'après l'en-tête SZDD */
void api_GetExpandedName16 (struct w16call *c)
{
	const char *name = w16_raw (c->a[0]);
	char *out = far_ptr (c->a[1]);
	if (!out)
	{
		c->ret = (uint16_t) -1;
		return;
	}
	strcpy (out, name);
	char host[700];
	uint8_t h[14];
	int fd = dos_to_host (name, host, sizeof host) ? open (host, O_RDONLY) : -1;
	if (fd >= 0)
	{
		if (read (fd, h, 14) == 14 && is_szdd (h, 14) && h[9])
		{
			size_t l = strlen (out);
			if (l && out[l - 1] == '_')
				out[l - 1] = (char) (islower ((unsigned char) out[l - 2]) ? tolower (h[9]) : h[9]);
		}
		close (fd);
	}
	c->ret = 1;
}

/* ======================================================================== VER */
static void put_str (uint32_t buf, uint32_t lenp, const char *s)
{
	uint16_t cap = lenp ? rd16 (far_lin (lenp)) : 0;
	char *d = far_ptr (buf);
	if (d && cap)
		snprintf (d, cap, "%s", s);
	if (lenp)
		wr16 (far_lin (lenp), (uint16_t) (strlen (s) + 1));
}

/* ---- les informations de version (ressource VERSIONINFO, type 16) d'un fichier NE, compressé ou non (VER.DLL
 * passe par LZEXPAND) ; le bloc 16 bits : cbBlock, cbValue, clé terminée par 0 (alignée sur 4), valeur (alignée sur
 * 4), enfants. D'après ver.c de Wine (LGPL-2.1). ---- */
static uint8_t *version_block (const char *dosname, uint32_t *len)
{
	char host[700];
	*len = 0;
	if (!dos_to_host (dosname, host, sizeof host))
		return 0;
	int fd = open (host, O_RDONLY);
	if (fd < 0)
		return 0;
	size_t n;
	uint8_t *b = slurp_fd (fd, &n);
	close (fd);
	if (b && is_szdd (b, n))
	{
		size_t m;
		uint8_t *x = expand (b, n, &m);
		free (b);
		b = x;
		n = m;
	}
	uint8_t *res = 0;
	if (b && n >= 0x40 && b[0] == 'M' && b[1] == 'Z')
	{
		uint32_t neo = (uint32_t) b[0x3c] | (uint32_t) b[0x3d] << 8 | (uint32_t) b[0x3e] << 16 | (uint32_t) b[0x3f] << 24;
		if (neo + 0x40 <= n && b[neo] == 'N' && b[neo + 1] == 'E')
		{
			uint32_t rt = neo + (uint32_t) (b[neo + 0x24] | b[neo + 0x25] << 8);
			uint32_t rn = neo + (uint32_t) (b[neo + 0x26] | b[neo + 0x27] << 8);
			if (rt < rn && rt + 4 <= n)
			{
				int shift = b[rt] | b[rt + 1] << 8;
				uint32_t q = rt + 2;
				while (q + 8 <= n && (b[q] | b[q + 1] << 8))
				{
					uint16_t type = (uint16_t) (b[q] | b[q + 1] << 8), cnt = (uint16_t) (b[q + 2] | b[q + 3] << 8);
					if (type == 0x8010 && cnt && q + 8 + 12 <= n)
					{
						uint32_t off = (uint32_t) (b[q + 8] | b[q + 9] << 8) << shift;
						uint32_t sz = (uint32_t) (b[q + 10] | b[q + 11] << 8) << shift;
						if (off < n && sz)
						{
							if (off + sz > n)
								sz = (uint32_t) (n - off);
							uint32_t real = (uint32_t) (b[off] | b[off + 1] << 8);	/* (cbBlock : sans le bourrage) */
							if (real && real <= sz)
								sz = real;
							res = malloc (sz);
							if (res)
							{
								memcpy (res, b + off, sz);
								*len = sz;
							}
						}
						break;
					}
					q += 8 + 12u * cnt;
				}
			}
		}
	}
	free (b);
	return res;
}

/* GetFileVersionInfoSize (nom, &poignée) -> taille du bloc, 0 sans VERSIONINFO */
void api_GetFileVersionInfoSize16 (struct w16call *c)
{
	uint32_t len;
	uint8_t *v = version_block (w16_raw (c->a[0]), &len);
	free (v);
	if (c->a[1])
		wr32 (far_lin (c->a[1]), 0);
	c->ret = len;
}

/* GetFileVersionInfo (nom, poignée, taille, tampon) -> 1, ou 0 */
void api_GetFileVersionInfo16 (struct w16call *c)
{
	uint32_t len;
	uint8_t *v = version_block (w16_raw (c->a[0]), &len);
	c->ret = 0;
	if (v && c->a[3])
	{
		uint32_t n = len < c->a[2] ? len : c->a[2];
		memcpy (far_ptr (c->a[3]), v, n);
		c->ret = 1;
	}
	free (v);
}

/* VerQueryValue (bloc, « \StringFileInfo\040904E4\FileVersion », &pointeur, &taille) : descend dans l'arbre */
void api_VerQueryValue16 (struct w16call *c)
{
	uint32_t base = c->a[0];
	const char *path = w16_raw (c->a[1]);
	uint32_t blk = far_lin (base);
	uint16_t off = 0;
	c->ret = 0;
	for (;;)
	{
		while (*path == '\\')
			path++;
		if (!*path)
			break;
		const char *e = strchr (path, '\\');
		size_t kl = e ? (size_t) (e - path) : strlen (path);
		/* les enfants de ce bloc : après la clé et la valeur */
		uint16_t cb = rd16 (blk + off), cv = rd16 (blk + off + 2);
		const char *key = (const char *) lin_ptr (blk + off + 4);
		uint16_t child = (uint16_t) ((off + 4 + strlen (key) + 1 + 3) & ~3u);
		child = (uint16_t) ((child + cv + 3) & ~3u);
		uint16_t end = (uint16_t) (off + cb);
		int found = 0;
		while (child + 4 <= end)
		{
			uint16_t ccb = rd16 (blk + child);
			if (!ccb)
				break;
			const char *ck = (const char *) lin_ptr (blk + child + 4);
			if (strlen (ck) == kl && !strncasecmp (ck, path, kl))
			{
				off = child;
				found = 1;
				break;
			}
			child = (uint16_t) ((child + ccb + 3) & ~3u);
		}
		if (!found)
			return;
		path += kl;
	}
	const char *key = (const char *) lin_ptr (blk + off + 4);
	uint16_t val = (uint16_t) ((off + 4 + strlen (key) + 1 + 3) & ~3u);
	if (c->a[2])
		wr32 (far_lin (c->a[2]), base + val);
	if (c->a[3])
		wr16 (far_lin (c->a[3]), rd16 (blk + off + 2));
	c->ret = rd16 (blk + off + 2) != 0;
}

/* VerFindFile (flags, nom, dossier de Windows, dossier de l'app, courant, sa taille, destination, sa taille) */
void api_VerFindFile16 (struct w16call *c)
{
	const char *windir = w16_raw (c->a[2]), *appdir = w16_raw (c->a[3]);
	put_str (c->a[4], c->a[5], "");
	put_str (c->a[6], c->a[7], (c->a[0] & 1) ? windir : appdir);	/* (VFFF_ISSHAREDFILE : dans Windows) */
	c->ret = 0;
}

/* VerInstallFile (flags, source, destination, dossier source, dossier destination, courant, temporaire, sa taille) :
 * copie en décompressant -> 0, ou VIF_CANNOTREADSRC, VIF_CANNOTCREATE */
void api_VerInstallFile16 (struct w16call *c)
{
	char s[300], d[300], hs[700], hd[700];
	if (!w16_raw (c->a[1])[0])
	{
		/* (un nom vide : la ligne vide à la fin d'un SETUP.LST ; rien à faire) */
		c->ret = 0;
		return;
	}
	dos_join (s, sizeof s, w16_raw (c->a[3]), w16_raw (c->a[1]));
	dos_join (d, sizeof d, w16_raw (c->a[4]), w16_raw (c->a[2]));
	if (c->a[7])
		wr16 (far_lin (c->a[7]), 0);
	if (!dos_to_host (s, hs, sizeof hs) || access (hs, R_OK))
	{
		w16_log ("VerInstallFile : %s introuvable\n", s);
		c->ret = 0x10000;
		return;
	}
	if (!dos_to_host (d, hd, sizeof hd) || !expand_file (hs, hd))
	{
		c->ret = 0x800;
		return;
	}
	c->ret = 0;
}

/* ======================================================================== WinExec, LoadModule */
/* un programme (nom DOS, avec ou sans .EXE) -> son chemin de l'hôte, cherché comme Windows : tel quel (dossier courant),
 * le dossier du programme, C:\WINDOWS, C:\WINDOWS\SYSTEM */
static int find_program (const char *prog, char *host, size_t cap)
{
	char name[300];
	snprintf (name, sizeof name, "%s", prog);
	const char *base = strrchr (name, '\\');
	base = base ? base + 1 : (name[0] && name[1] == ':' ? name + 2 : name);
	if (!strchr (base, '.'))
		strncat (name, ".EXE", sizeof name - strlen (name) - 1);
	if (dos_to_host (name, host, cap) && !access (host, F_OK))
		return 1;
	if (base != name)
		return 0;
	static const char *dirs[] = { "D:\\", "C:\\WINDOWS\\", "C:\\WINDOWS\\SYSTEM\\" };
	for (int i = 0; i < 3; i++)
	{
		char p[340];
		snprintf (p, sizeof p, "%s%s", dirs[i], name);
		if (dos_to_host (p, host, cap) && !access (host, F_OK))
			return 1;
	}
	return 0;
}

static uint16_t win_exec (const char *cmd)
{
	char buf[512], *argv[32];
	int argc = 0;
	snprintf (buf, sizeof buf, "%s", cmd);
	char *p = buf;
	char self[512];
	snprintf (self, sizeof self, "%s", w16_self[0] && !access (w16_self, X_OK) ? w16_self : "/apps/win16.app/main");
	argv[argc++] = self;
	/* (l'enfant voit le même D: : SETUP.EXE passe « D: », le dossier de l'archive, à SETUP1.EXE) */
	char dopt[600];
	snprintf (dopt, sizeof dopt, "D=%s", task.dir);
	argv[argc++] = (char *) "--drive";
	argv[argc++] = dopt;
	char host[700];
	/* le programme, puis ses arguments (séparés par des espaces ; « ... » gardés ensemble) */
	int first = 1;
	while (*p && argc < 30)
	{
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		char *start = p;
		if (*p == '"')
		{
			start = ++p;
			while (*p && *p != '"')
				p++;
		}
		else
			while (*p && *p != ' ' && *p != '\t')
				p++;
		if (*p)
			*p++ = 0;
		if (first)
		{
			if (!find_program (start, host, sizeof host))
			{
				w16_log ("WinExec : %s introuvable\n", start);
				return 2;
			}
			argv[argc++] = host;
			first = 0;
		}
		else
			argv[argc++] = start;
	}
	if (first)
		return 2;
	argv[argc] = 0;
	pid_t pid;
	int r = posix_spawn (&pid, self, 0, 0, argv, environ);
	w16_log ("WinExec : « %s » : %s %s (%d arguments) -> %s\n", cmd, self, host, argc - 4, r ? "échec" : "lancé");
	return r ? 0 : 33;
}

void api_WinExec16 (struct w16call *c)
{
	c->ret = win_exec (w16_raw (c->a[0]));
}

/* LoadModule (nom, bloc de paramètres : environnement, ligne de commande Pascal, ...) : comme WinExec */
void api_LoadModule16 (struct w16call *c)
{
	char cmd[400];
	const char *name = w16_raw (c->a[0]);
	const uint8_t *pb = far_ptr (c->a[1]);
	const uint8_t *tail = pb ? far_ptr (rd32 (far_lin (c->a[1]) + 2)) : 0;
	int n = tail ? tail[0] : 0;
	snprintf (cmd, sizeof cmd, "%s %.*s", name, n, tail ? (const char *) tail + 1 : "");
	(void) pb;
	c->ret = win_exec (cmd);
}
