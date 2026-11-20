/* dos.c -- win16, la machine DOS (docs/etude/WIN16.md §9.2) : un programme MZ ou .COM en mode réel, dans une
 * fenêtre de Korin, comme une NTVDM.
 *
 * La mémoire du PC est le bas de l'arène de win16 (adresse linéaire = segment × 16 + décalage) : table des
 * vecteurs, zone du BIOS, données du DOS en 0070, blocs de mémoire (MCB) de 0100 à A000, vidéo, ROM. Les
 * vecteurs pointent sur des points d'arrêt en F000:vecteur ; la boucle y appelle le C (bios.c, ici). Les fichiers
 * passent par le KERNEL de win16 (kernel.c : lecteurs, poignées, recherche), les processus et la console par ici.
 * Référence : krnl386 de winevdm (dosexe.c, dosmem.c, int21.c ; LGPL-2.1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "dos.h"
#include "be.h"

int kernel_fh_dev (int h);
int kernel_fh_newdev (int dev);
int kernel_fh_valid (int h);
int kernel_fh_dup (int h, int to);
int kernel_fh_close (int h);
int32_t kernel_write (int h, const void *p, uint32_t n);
int32_t kernel_read (int h, void *p, uint32_t n);
void kernel_set_dta (uint32_t segptr);
void kernel_drives_init (void);

#define DOS_SEG		0x0070		/* les données du DOS */
#define INDOS_OFF	0x0010
#define LOL_OFF		0x0100		/* la « liste des listes » (INT 21h 52h) ; le premier MCB juste avant */
#define COUNTRY_OFF	0x0200
#define FIRST_MCB	0x0100
#define MEM_TOP		0xa000
#define NOEPI		2		/* (un gestionnaire qui a déjà réglé CS:IP et la pile : EXEC) */

int dos_hold;			/* garder la fenêtre à la fin (un .BAT : main.c) */
static int closed_by_user (void) { return dos_closed; }
static uint16_t cur_psp, root_psp;
static uint16_t last_ret;		/* le code de retour du dernier enfant (4Dh) */
static uint8_t strategy;
static uint16_t last_error;

/* ======================================================================== les blocs de mémoire (MCB) */
static uint8_t *mcb (uint16_t seg) { return lin_ptr (RM (seg, 0)); }
static uint16_t mcb_owner (uint16_t seg) { return rd16 (RM (seg, 1)); }
static uint16_t mcb_size (uint16_t seg) { return rd16 (RM (seg, 3)); }

static void mcb_set (uint16_t seg, char type, uint16_t owner, uint16_t size)
{
	uint8_t *m = mcb (seg);
	m[0] = (uint8_t) type;
	wr16 (RM (seg, 1), owner);
	wr16 (RM (seg, 3), size);
}

/* réunit les blocs libres qui se suivent */
static void mcb_merge (void)
{
	for (uint16_t s = FIRST_MCB; mcb (s)[0] == 'M';)
	{
		uint16_t n = (uint16_t) (s + 1 + mcb_size (s));
		if (!mcb_owner (s) && !mcb_owner (n) && (mcb (n)[0] == 'M' || mcb (n)[0] == 'Z'))
		{
			mcb_set (s, (char) mcb (n)[0], 0, (uint16_t) (mcb_size (s) + 1 + mcb_size (n)));
			continue;
		}
		s = n;
	}
}

/* -> le segment des données du bloc, 0 (et *largest) si pas assez */
static uint16_t mcb_alloc (uint16_t paras, uint16_t owner, uint16_t *largest)
{
	mcb_merge ();
	uint16_t best = 0, big = 0;
	for (uint16_t s = FIRST_MCB;; s = (uint16_t) (s + 1 + mcb_size (s)))
	{
		uint8_t t = mcb (s)[0];
		if (t != 'M' && t != 'Z')
			break;
		if (!mcb_owner (s))
		{
			uint16_t sz = mcb_size (s);
			if (sz > big)
				big = sz;
			if (sz >= paras)
			{
				int better = !best || ((strategy & 3) == 1 && sz < mcb_size (best)) || (strategy & 3) == 2;
				if (better)
					best = s;
				if ((strategy & 3) == 0)
					break;
			}
		}
		if (t == 'Z')
			break;
	}
	if (largest)
		*largest = big;
	if (!best)
		return 0;
	uint16_t sz = mcb_size (best);
	char t = (char) mcb (best)[0];
	if ((strategy & 3) == 2 && sz > paras)
	{
		/* (le plus haut : le bloc pris est la fin du libre) */
		mcb_set (best, 'M', 0, (uint16_t) (sz - paras - 1));
		uint16_t n = (uint16_t) (best + sz - paras);
		mcb_set (n, t, owner, paras);
		return (uint16_t) (n + 1);
	}
	if (sz > paras)
	{
		mcb_set ((uint16_t) (best + 1 + paras), t, 0, (uint16_t) (sz - paras - 1));
		t = 'M';
	}
	mcb_set (best, t, owner, sz > paras ? paras : sz);
	memset (mcb (best) + 8, 0, 8);
	return (uint16_t) (best + 1);
}

static int mcb_valid (uint16_t data_seg)
{
	uint8_t t = mcb ((uint16_t) (data_seg - 1))[0];
	return data_seg > FIRST_MCB && data_seg < MEM_TOP && (t == 'M' || t == 'Z');
}

static void mcb_free (uint16_t data_seg)
{
	wr16 (RM (data_seg - 1, 1), 0);
}

/* -> 1, ou 0 et *largest (la taille possible) */
static int mcb_resize (uint16_t data_seg, uint16_t paras, uint16_t *largest)
{
	uint16_t s = (uint16_t) (data_seg - 1), sz = mcb_size (s);
	char t = (char) mcb (s)[0];
	if (paras <= sz)
	{
		if (paras < sz)
		{
			mcb_set ((uint16_t) (s + 1 + paras), t, 0, (uint16_t) (sz - paras - 1));
			mcb_set (s, 'M', mcb_owner (s), paras);
			mcb_merge ();
		}
		return 1;
	}
	uint16_t avail = sz;
	if (t == 'M')
	{
		uint16_t n = (uint16_t) (s + 1 + sz);
		mcb_merge ();
		if (!mcb_owner (n))
			avail = (uint16_t) (sz + 1 + mcb_size (n));
		if (avail >= paras)
		{
			char nt = (char) mcb (n)[0];
			uint16_t total = avail;
			if (total > paras)
			{
				mcb_set ((uint16_t) (s + 1 + paras), nt, 0, (uint16_t) (total - paras - 1));
				mcb_set (s, 'M', mcb_owner (s), paras);
			}
			else
				mcb_set (s, nt, mcb_owner (s), paras);
			return 1;
		}
	}
	if (largest)
		*largest = avail;
	return 0;
}

static void mcb_set_name (uint16_t data_seg, const char *name)
{
	uint8_t *m = mcb ((uint16_t) (data_seg - 1)) + 8;
	memset (m, 0, 8);
	for (int i = 0; i < 8 && name[i] && name[i] != '.'; i++)
		m[i] = (uint8_t) toupper ((unsigned char) name[i]);
}

static void free_owned (uint16_t psp)
{
	for (uint16_t s = FIRST_MCB;; s = (uint16_t) (s + 1 + mcb_size (s)))
	{
		uint8_t t = mcb (s)[0];
		if (t != 'M' && t != 'Z')
			break;
		if (mcb_owner (s) == psp)
			wr16 (RM (s, 1), 0);
		if (t == 'Z')
			break;
	}
	mcb_merge ();
}

/* ======================================================================== PSP et environnement */
static void make_psp (uint16_t psp, uint16_t top, uint16_t parent, uint16_t env)
{
	uint8_t *p = lin_ptr (RM (psp, 0));
	memset (p, 0, 0x100);
	p[0] = 0xcd;
	p[1] = 0x20;
	wr16 (RM (psp, 2), top);
	p[5] = 0xcb;	/* (l'appel CP/M en PSP:5 : rien) */
	wr32 (RM (psp, 0x0a), rd32 (0x22 * 4));
	wr32 (RM (psp, 0x0e), rd32 (0x23 * 4));
	wr32 (RM (psp, 0x12), rd32 (0x24 * 4));
	wr16 (RM (psp, 0x16), parent);
	/* la table des poignées (le KERNEL les garde en vrai ; celle-ci est pour qui la lit) */
	for (int i = 0; i < 20; i++)
		p[0x18 + i] = (uint8_t) (i < 5 ? i : 0xff);
	wr16 (RM (psp, 0x2c), env);
	wr16 (RM (psp, 0x32), 20);
	wr32 (RM (psp, 0x34), (uint32_t) psp << 16 | 0x18);
	wr32 (RM (psp, 0x38), 0xffffffffu);
	p[0x40] = 5;
	p[0x50] = 0xcd;
	p[0x51] = 0x21;
	p[0x52] = 0xcb;
	memset (p + 0x5c, 0, 0x24);
	memset (p + 0x5d, ' ', 11);
	memset (p + 0x6d, ' ', 11);
	p[0x81] = 0x0d;
}

static int env_len (uint16_t env)
{
	const uint8_t *e = lin_ptr (RM (env, 0));
	int n = 0;
	while (n < 32768 && (e[n] || e[n + 1]))
		n++;
	return n + 2;
}

/* une copie de l'environnement src (0 : celui du processus courant), suivie du chemin du programme */
static uint16_t env_copy (uint16_t src, const char *progpath, uint16_t owner)
{
	if (!src)
		src = rd16 (RM (cur_psp, 0x2c));
	int n = src ? env_len (src) : 2;
	int total = n + 2 + (int) strlen (progpath) + 1;
	uint16_t seg = mcb_alloc ((uint16_t) ((total + 15) / 16), owner, 0);
	if (!seg)
		return 0;
	uint8_t *d = lin_ptr (RM (seg, 0));
	if (src)
		memcpy (d, lin_ptr (RM (src, 0)), (size_t) n);
	else
		d[0] = d[1] = 0;
	d[n] = 1;
	d[n + 1] = 0;
	strcpy ((char *) d + n + 2, progpath);
	return seg;
}

/* ======================================================================== le chargeur */
struct loaded { uint16_t psp, cs, ip, ss, sp; };

static uint8_t *read_file (const char *dospath, size_t *size)
{
	char host[700];
	if (!dos_to_host (dospath, host, sizeof host))
		return 0;
	int fd = open (host, O_RDONLY);
	if (fd < 0)
		return 0;
	struct stat st;
	if (fstat (fd, &st) || st.st_size > 16 * 1024 * 1024)
	{
		close (fd);
		return 0;
	}
	uint8_t *b = malloc ((size_t) st.st_size + 16);
	size_t got = 0;
	while (b && got < (size_t) st.st_size)
	{
		ssize_t r = read (fd, b + got, (size_t) st.st_size - got);
		if (r <= 0)
			break;
		got += (size_t) r;
	}
	close (fd);
	*size = got;
	return b;
}

static int is_mz (const uint8_t *f, size_t n) { return n >= 0x1c && ((f[0] == 'M' && f[1] == 'Z') || (f[0] == 'Z' && f[1] == 'M')); }

/* l'image d'un MZ : où elle commence dans le fichier, sa taille */
static void mz_image (const uint8_t *f, size_t n, uint32_t *start, uint32_t *size)
{
	uint32_t pages = (uint32_t) (f[4] | f[5] << 8), last = (uint32_t) (f[2] | f[3] << 8);
	uint32_t total = pages * 512 - (last ? 512 - last : 0);
	*start = (uint32_t) (f[8] | f[9] << 8) * 16;
	if (total > n)
		total = (uint32_t) n;
	*size = total > *start ? total - *start : 0;
}

static void mz_relocate (const uint8_t *f, size_t n, uint16_t load_seg, uint16_t factor)
{
	int cnt = f[6] | f[7] << 8;
	uint32_t tab = (uint32_t) (f[0x18] | f[0x19] << 8);
	for (int i = 0; i < cnt && tab + (uint32_t) i * 4 + 4 <= n; i++)
	{
		const uint8_t *r = f + tab + i * 4;
		uint16_t off = (uint16_t) (r[0] | r[1] << 8), seg = (uint16_t) (r[2] | r[3] << 8);
		uint32_t a = RM (load_seg + seg, off);
		wr16 (a, (uint16_t) (rd16 (a) + factor));
	}
}

/* charge un programme : mode 0 (pour l'exécuter) ou 3 (recouvrement : à load_seg, sans PSP) -> 0 ou une erreur DOS */
static int load (const char *dospath, int ovl, uint16_t env_src, uint16_t load_seg, uint16_t factor, struct loaded *L)
{
	size_t n = 0;
	uint8_t *f = read_file (dospath, &n);
	if (!f)
		return 2;
	int mz = is_mz (f, n);
	uint32_t start = 0, size = (uint32_t) n;
	if (mz)
		mz_image (f, n, &start, &size);
	if (ovl)
	{
		memcpy (lin_ptr (RM (load_seg, 0)), f + start, size);
		if (mz)
			mz_relocate (f, n, load_seg, factor);
		free (f);
		return 0;
	}
	uint16_t img_paras = (uint16_t) ((size + 15) / 16);
	uint16_t need, want;
	if (mz)
	{
		uint16_t minalloc = (uint16_t) (f[0x0a] | f[0x0b] << 8), maxalloc = (uint16_t) (f[0x0c] | f[0x0d] << 8);
		need = (uint16_t) (0x10 + img_paras + minalloc);
		uint32_t w = 0x10u + img_paras + maxalloc;
		want = w > 0xffff ? 0xffff : (uint16_t) w;
		if (want < need)
			want = need;
	}
	else
	{
		if (n > 0xff00)
		{
			free (f);
			return 8;
		}
		need = (uint16_t) (0x10 + img_paras + 0x10);
		want = 0xffff;
	}
	/* l'environnement, puis le plus grand bloc que veut le programme */
	uint16_t env = env_copy (env_src, dospath, 8);
	if (!env)
	{
		free (f);
		return 8;
	}
	uint16_t largest = 0;
	mcb_alloc (0xffff, 8, &largest);
	if (largest < need)
	{
		mcb_free (env);
		free (f);
		return 8;
	}
	uint16_t paras = want < largest ? want : largest;
	uint16_t psp = mcb_alloc (paras, 8, 0);
	wr16 (RM (psp - 1, 1), psp);
	wr16 (RM (env - 1, 1), psp);
	const char *b = strrchr (dospath, '\\');
	mcb_set_name (psp, b ? b + 1 : dospath);
	make_psp (psp, (uint16_t) (psp + paras), cur_psp, env);
	L->psp = psp;
	if (mz)
	{
		uint16_t ls = (uint16_t) (psp + 0x10);
		memcpy (lin_ptr (RM (ls, 0)), f + start, size);
		mz_relocate (f, n, ls, ls);
		L->ss = (uint16_t) (ls + (f[0x0e] | f[0x0f] << 8));
		L->sp = (uint16_t) (f[0x10] | f[0x11] << 8);
		L->cs = (uint16_t) (ls + (f[0x16] | f[0x17] << 8));
		L->ip = (uint16_t) (f[0x14] | f[0x15] << 8);
	}
	else
	{
		memcpy (lin_ptr (RM (psp, 0x100)), f, n);
		L->cs = L->ss = psp;
		L->ip = 0x100;
		L->sp = paras >= 0x1000 ? 0xfffe : (uint16_t) (paras * 16 - 2);
		wr16 (RM (psp, L->sp), 0);
	}
	free (f);
	return 0;
}

static void set_cmdtail (uint16_t psp, const uint8_t *tail)
{
	uint8_t *p = lin_ptr (RM (psp, 0x80));
	int n = tail[0] > 126 ? 126 : tail[0];
	p[0] = (uint8_t) n;
	memcpy (p + 1, tail + 1, (size_t) n);
	p[1 + n] = 0x0d;
}

/* lance le processus chargé : les registres du départ selon DOS 5 */
static void start (const struct loaded *L)
{
	cur_psp = L->psp;
	kernel_set_dta (MAKESEGPTR (L->psp, 0x80));
	cpu_set_sreg (S_DS, L->psp);
	cpu_set_sreg (S_ES, L->psp);
	cpu_set_sreg (S_SS, L->ss);
	cpu_set_reg (R_ESP, L->sp);
	cpu_set_reg (R_EAX, 0);
	cpu_set_reg (R_EBX, 0);
	cpu_set_reg (R_ECX, 0x00ff);
	cpu_set_reg (R_EDX, L->psp);
	cpu_set_reg (R_ESI, L->ip);
	cpu_set_reg (R_EDI, L->sp);
	cpu_set_reg (R_EBP, 0x091c);
	cpu_jump (L->cs, L->ip);
	cpu_set_flags (0x7202);
}

/* ======================================================================== EXEC et la fin des processus */
#define MAX_DEPTH 16
static struct { uint16_t psp; uint32_t regs[8]; uint16_t ds, es; } saved[MAX_DEPTH];
static int depth;

static int exec (void)
{
	uint8_t mode = rAL;
	const char *path = w16_raw (MAKESEGPTR (cpu_sreg (S_DS), rDX));
	uint32_t pb = RM (cpu_sreg (S_ES), rBX);
	char p[260];
	snprintf (p, sizeof p, "%s", path);
	for (char *q = p; *q; q++)
		*q = (char) toupper ((unsigned char) *q);
	/* le chemin complet, comme DOS le met après l'environnement */
	char full[300];
	if (p[0] && p[1] == ':')
		snprintf (full, sizeof full, "%s", p);
	else
	{
		uint8_t cur = 0;
		char dir[80] = "";
		{
			/* (le lecteur et le dossier courants, par le KERNEL) */
			uint32_t ax = cpu_reg (R_EAX), dx = cpu_reg (R_EDX), si = cpu_reg (R_ESI);
			uint16_t ds = cpu_sreg (S_DS);
			set16 (R_EAX, 0x1900);
			dos_int21 ();
			cur = rAL;
			uint32_t tmp = RM (DOS_SEG, 0x400);
			cpu_set_sreg (S_DS, DOS_SEG);
			set16 (R_ESI, 0x400);
			set16 (R_EAX, 0x4700);
			set16 (R_EDX, 0);
			dos_int21 ();
			snprintf (dir, sizeof dir, "%s", (const char *) lin_ptr (tmp));
			cpu_set_reg (R_EAX, ax);
			cpu_set_reg (R_EDX, dx);
			cpu_set_reg (R_ESI, si);
			cpu_set_sreg (S_DS, ds);
		}
		if (p[0] == '\\')
			snprintf (full, sizeof full, "%c:%s", 'A' + cur, p);
		else
			snprintf (full, sizeof full, "%c:\\%s%s%s", 'A' + cur, dir, dir[0] ? "\\" : "", p);
	}
	w16_log ("DOS : EXEC %d %s\n", mode, full);
	if (mode == 3)
	{
		int e = load (full, 1, 0, rd16 (pb), rd16 (pb + 2), 0);
		if (e)
		{
			set16 (R_EAX, (uint16_t) e);
			set_cf (1);
		}
		else
			set_cf (0);
		return DONE;
	}
	if (mode != 0 && mode != 1)
	{
		set16 (R_EAX, 1);
		set_cf (1);
		return DONE;
	}
	if (depth >= MAX_DEPTH)
	{
		set16 (R_EAX, 8);
		set_cf (1);
		return DONE;
	}
	struct loaded L;
	int e = load (full, 0, rd16 (pb), 0, 0, &L);
	if (e)
	{
		set16 (R_EAX, (uint16_t) e);
		set_cf (1);
		return DONE;
	}
	uint32_t tail = rd32 (pb + 2), fcb1 = rd32 (pb + 6), fcb2 = rd32 (pb + 10);
	if (tail)
		set_cmdtail (L.psp, lin_ptr (far_lin (tail)));
	if (fcb1)
		memcpy (lin_ptr (RM (L.psp, 0x5c)), lin_ptr (far_lin (fcb1)), 16);
	if (fcb2)
		memcpy (lin_ptr (RM (L.psp, 0x6c)), lin_ptr (far_lin (fcb2)), 16);
	/* le parent : sa pile (le cadre de son INT 21h) dans son PSP, son adresse de retour dans celui de l'enfant */
	uint16_t sp = (uint16_t) cpu_reg (R_ESP), ss = cpu_sreg (S_SS);
	uint32_t frame = RM (ss, sp);
	wr16 (RM (cur_psp, 0x2e), sp);
	wr16 (RM (cur_psp, 0x30), ss);
	wr16 (RM (L.psp, 0x0a), rd16 (frame));
	wr16 (RM (L.psp, 0x0c), rd16 (frame + 2));
	wr32 (0x22 * 4, rd32 (RM (L.psp, 0x0a)));
	saved[depth].psp = cur_psp;
	for (int r = 0; r < 8; r++)
		saved[depth].regs[r] = cpu_reg (r);
	saved[depth].ds = cpu_sreg (S_DS);
	saved[depth].es = cpu_sreg (S_ES);
	depth++;
	if (mode == 1)
	{
		/* (charger sans lancer : SS:SP et CS:IP dans le bloc ; le PSP courant devient l'enfant) */
		wr16 (pb + 0x0e, (uint16_t) (L.sp - 2));
		wr16 (pb + 0x10, L.ss);
		wr16 (pb + 0x12, L.ip);
		wr16 (pb + 0x14, L.cs);
		wr16 (RM (L.ss, L.sp - 2), 0);
		cur_psp = L.psp;
		kernel_set_dta (MAKESEGPTR (L.psp, 0x80));
		set_cf (0);
		return DONE;
	}
	start (&L);
	return NOEPI;
}

/* la fin du processus courant (type : 0 normale, 1 Ctrl-C, 2 erreur critique, 3 résident) */
static int terminate (uint8_t code, int type, uint16_t keep_paras)
{
	uint16_t psp = cur_psp, parent = rd16 (RM (psp, 0x16));
	last_ret = (uint16_t) (type << 8 | code);
	w16_log ("DOS : fin du processus %04x, code %d (type %d)\n", psp, code, type);
	if (parent == root_psp || !depth || parent == psp)
	{
		w16_exit = 1;
		w16_exit_code = code;
		return NOEPI;
	}
	/* les vecteurs 22h-24h reviennent, la mémoire est rendue (sauf celle d'un résident) */
	wr32 (0x22 * 4, rd32 (RM (psp, 0x0a)));
	wr32 (0x23 * 4, rd32 (RM (psp, 0x0e)));
	wr32 (0x24 * 4, rd32 (RM (psp, 0x12)));
	if (type == 3)
		mcb_resize (psp, keep_paras < 6 ? 6 : keep_paras, 0);
	else
		free_owned (psp);
	depth--;
	cur_psp = saved[depth].psp;
	kernel_set_dta (MAKESEGPTR (cur_psp, 0x80));
	for (int r = 0; r < 8; r++)
		cpu_set_reg (r, saved[depth].regs[r]);
	cpu_set_sreg (S_DS, saved[depth].ds);
	cpu_set_sreg (S_ES, saved[depth].es);
	cpu_set_sreg (S_SS, rd16 (RM (cur_psp, 0x30)));
	cpu_set_reg (R_ESP, rd16 (RM (cur_psp, 0x2e)));
	/* le retour dans le parent : l'adresse de fin (PSP:0A de l'enfant) dans le cadre de son INT 21h */
	uint32_t frame = RM (cpu_sreg (S_SS), (uint16_t) cpu_reg (R_ESP));
	wr32 (frame, rd32 (0x22 * 4));
	set_cf (0);
	return DONE;
}

/* ======================================================================== la console */
static uint16_t pending_scan;		/* (une touche étendue : le code vient au prochain appel) */

static int con_getkey (uint8_t *ch)
{
	if (pending_scan)
	{
		*ch = (uint8_t) pending_scan;
		pending_scan = 0;
		return 1;
	}
	uint16_t k;
	if (!kbd_get (1, &k))
		return 0;
	uint8_t a = (uint8_t) k;
	if (a == 0 || a == 0xe0)
	{
		a = 0;
		pending_scan = (uint16_t) (k >> 8);
		if (!pending_scan)
			pending_scan = 0x100;
	}
	*ch = a;
	return 1;
}

static int con_ready (void)
{
	uint16_t k;
	return pending_scan || kbd_get (0, &k);
}

static int con_col;

static void out_char (uint8_t c)
{
	if (kernel_fh_dev (1) != 1)
	{
		if (kernel_fh_dev (1) == 0)
			kernel_write (1, &c, 1);
		return;
	}
	if (c == 9)
	{
		do
			vga_tty (' ');
		while (lin_ptr (0x450 + lin_ptr (0x462)[0] * 2)[0] & 7);
		return;
	}
	vga_tty (c);
	(void) con_col;
}

/* une ligne tapée (0Ah, et 3Fh sur la console) : rappelée à chaque touche jusqu'à Entrée */
static char line[256];
static int line_len, line_active;

static int line_input (int max)
{
	if (!line_active)
	{
		line_active = 1;
		line_len = 0;
	}
	uint8_t c;
	while (con_getkey (&c))
	{
		if (c == 0)
		{
			uint8_t s;
			con_getkey (&s);	/* (une touche étendue : ignorée) */
			continue;
		}
		if (c == 0x0d)
		{
			line[line_len] = 0x0d;
			vga_tty (0x0d);
			line_active = 0;
			return DONE;
		}
		if (c == 8)
		{
			if (line_len)
			{
				line_len--;
				vga_tty (8);
				vga_tty (' ');
				vga_tty (8);
			}
			continue;
		}
		if (c == 0x1b)
		{
			vga_tty ('\\');
			vga_tty (0x0d);
			vga_tty (0x0a);
			line_len = 0;
			continue;
		}
		if (line_len < max - 1 && line_len < 254)
		{
			line[line_len++] = (char) c;
			vga_tty (c);
		}
	}
	return WAIT;
}

/* ce qui reste de la ligne lue par 3Fh sur la console */
static char con_rest[260];
static int con_rest_n, con_rest_pos;

static int con_read (uint32_t buf, int n)
{
	if (con_rest_pos >= con_rest_n)
	{
		if (line_input (128) == WAIT)
			return -1;
		memcpy (con_rest, line, (size_t) line_len);
		con_rest[line_len] = 0x0d;
		con_rest[line_len + 1] = 0x0a;
		con_rest_n = line_len + 2;
		con_rest_pos = 0;
		vga_tty (0x0a);
	}
	int k = con_rest_n - con_rest_pos < n ? con_rest_n - con_rest_pos : n;
	memcpy (lin_ptr (buf), con_rest + con_rest_pos, (size_t) k);
	con_rest_pos += k;
	return k;
}

/* ======================================================================== INT 21h */
static void dos_error (uint16_t e)
{
	last_error = e;
	set16 (R_EAX, e);
	set_cf (1);
}

static int is_sep (int c) { return c == ':' || c == '.' || c == ';' || c == ',' || c == '=' || c == '+' || c == ' ' || c == '\t'; }

/* 29h : un nom de fichier dans une FCB */
static void parse_fcb (void)
{
	uint32_t s = RM (cpu_sreg (S_DS), rSI);
	uint8_t *fcb = lin_ptr (RM (cpu_sreg (S_ES), rDI));
	uint8_t fl = rAL, res = 0;
	const uint8_t *p = lin_ptr (s);
	int i = 0;
	if (fl & 1)
		while (p[i] == ' ' || p[i] == '\t' || (is_sep (p[i]) && p[i] != '.' && p[i] != 0x0d))
			i++;
	else
		while (p[i] == ' ' || p[i] == '\t')
			i++;
	if (p[i] && p[i + 1] == ':' && isalpha (p[i]))
	{
		fcb[0] = (uint8_t) (toupper (p[i]) - 'A' + 1);
		i += 2;
	}
	else if (!(fl & 2))
		fcb[0] = 0;
	uint8_t name[11];
	memset (name, ' ', 11);
	int got_name = 0, got_ext = 0;
	for (int k = 0; k < 8 && p[i] > ' ' && !is_sep (p[i]) && p[i] != '\\' && p[i] != '/'; i++)
	{
		got_name = 1;
		if (p[i] == '*')
		{
			while (k < 8)
				name[k++] = '?';
			res = 1;
			continue;
		}
		if (p[i] == '?')
			res = 1;
		name[k++] = (uint8_t) toupper (p[i]);
	}
	while (p[i] > ' ' && !is_sep (p[i]) && p[i] != '\\' && p[i] != '/')
		i++;
	if (p[i] == '.')
	{
		i++;
		for (int k = 8; k < 11 && p[i] > ' ' && !is_sep (p[i]) && p[i] != '\\' && p[i] != '/'; i++)
		{
			got_ext = 1;
			if (p[i] == '*')
			{
				while (k < 11)
					name[k++] = '?';
				res = 1;
				continue;
			}
			if (p[i] == '?')
				res = 1;
			name[k++] = (uint8_t) toupper (p[i]);
		}
		while (p[i] > ' ' && !is_sep (p[i]))
			i++;
	}
	if (got_name || !(fl & 4))
		memcpy (fcb + 1, name, 8);
	if (got_ext || !(fl & 8))
		memcpy (fcb + 9, name + 8, 3);
	set16 (R_ESI, (uint16_t) (rSI + i));
	set8l (R_EAX, res);
}

static int int21 (void)
{
	uint8_t c;
	set_cf (0);
	switch (rAH)
	{
	case 0x00:
		return terminate (0, 0, 0);
	case 0x01:
	case 0x07:
	case 0x08:
		if (!con_getkey (&c))
			return WAIT;
		if (rAH == 0x01)
			out_char (c);
		set8l (R_EAX, c);
		return DONE;
	case 0x02:
		out_char (rDL);
		set8l (R_EAX, rDL);
		return DONE;
	case 0x03:
	case 0x04:
	case 0x05:
		set8l (R_EAX, 0);
		return DONE;
	case 0x06:
		if (rDL != 0xff)
		{
			out_char (rDL);
			set8l (R_EAX, rDL);
			return DONE;
		}
		if (con_getkey (&c))
		{
			set8l (R_EAX, c);
			set_zf (0);
		}
		else
		{
			set8l (R_EAX, 0);
			set_zf (1);
			bios_poll (0);
		}
		return DONE;
	case 0x09:
	{
		uint32_t a = RM (cpu_sreg (S_DS), rDX);
		for (int i = 0; i < 65536 && lin_ptr (a + i)[0] != '$'; i++)
			out_char (lin_ptr (a + i)[0]);
		set8l (R_EAX, '$');
		return DONE;
	}
	case 0x0a:
	{
		uint8_t *b = lin_ptr (RM (cpu_sreg (S_DS), rDX));
		if (!b[0])
			return DONE;
		if (line_input (b[0]) == WAIT)
			return WAIT;
		memcpy (b + 2, line, (size_t) line_len + 1);
		b[1] = (uint8_t) line_len;
		return DONE;
	}
	case 0x0b:
		set8l (R_EAX, con_ready () ? 0xff : 0);
		if (!con_ready ())
			bios_poll (0);
		return DONE;
	case 0x0c:
	{
		uint16_t k;
		pending_scan = 0;
		while (kbd_get (1, &k))
			;
		uint8_t f = rAL;
		if (f == 0x01 || f == 0x06 || f == 0x07 || f == 0x08 || f == 0x0a)
		{
			set8h (R_EAX, f);
			return int21 ();
		}
		return DONE;
	}
	case 0x0d:
		return DONE;
	case 0x25:
		wr16 ((uint32_t) rAL * 4, rDX);
		wr16 ((uint32_t) rAL * 4 + 2, cpu_sreg (S_DS));
		return DONE;
	case 0x35:
		set16 (R_EBX, rd16 ((uint32_t) rAL * 4));
		cpu_set_sreg (S_ES, rd16 ((uint32_t) rAL * 4 + 2));
		return DONE;
	case 0x26:
		make_psp (rDX, rd16 (RM (cur_psp, 2)), cur_psp, rd16 (RM (cur_psp, 0x2c)));
		memcpy (lin_ptr (RM (rDX, 0x80)), lin_ptr (RM (cur_psp, 0x80)), 0x80);
		return DONE;
	case 0x29:
		parse_fcb ();
		return DONE;
	case 0x2c:	/* l'heure, au centième (les boucles d'attente des compilateurs s'en servent) */
	{
		struct timespec ts;
		clock_gettime (CLOCK_REALTIME, &ts);
		time_t t = ts.tv_sec;
		struct tm *tm = localtime (&t);
		set16 (R_ECX, (uint16_t) (tm->tm_hour << 8 | tm->tm_min));
		set16 (R_EDX, (uint16_t) (tm->tm_sec << 8 | (int) (ts.tv_nsec / 10000000)));
		bios_poll (0);
		return DONE;
	}
	case 0x2b:
	case 0x2d:
		set8l (R_EAX, 0);
		return DONE;
	case 0x30:
		set16 (R_EAX, rAL == 1 ? 0x0005 : 0x0005);
		set16 (R_EBX, rAL == 1 ? 0x0000 : 0xff00);
		set16 (R_ECX, 0);
		return DONE;
	case 0x31:
		return terminate (rAL, 3, rDX);
	case 0x33:
		switch (rAL)
		{
		case 0x00: set8l (R_EDX, 0); break;
		case 0x05: set8l (R_EDX, 3); break;
		case 0x06: set16 (R_EBX, 0x0005); set16 (R_EDX, 0x0000); break;
		default: set8l (R_EDX, 0); break;
		}
		return DONE;
	case 0x34:
		cpu_set_sreg (S_ES, DOS_SEG);
		set16 (R_EBX, INDOS_OFF);
		return DONE;
	case 0x37:
		if (rAL == 0)
			set8l (R_EDX, '/');
		set8l (R_EAX, 0);
		return DONE;
	case 0x38:
		if (rDX != 0xffff)
			memcpy (lin_ptr (RM (cpu_sreg (S_DS), rDX)), lin_ptr (RM (DOS_SEG, COUNTRY_OFF)), 34);
		set16 (R_EBX, 33);
		set16 (R_EAX, 33);
		return DONE;
	case 0x3c:
	case 0x3d:
	{
		/* les noms de périphériques */
		const char *n = w16_raw (MAKESEGPTR (cpu_sreg (S_DS), rDX));
		const char *b = strrchr (n, '\\');
		b = b ? b + 1 : (n[0] && n[1] == ':' ? n + 2 : n);
		char base[16];
		int k = 0;
		for (; b[k] && b[k] != '.' && k < 15; k++)
			base[k] = (char) toupper ((unsigned char) b[k]);
		base[k] = 0;
		int dev = !strcmp (base, "CON") ? 1 : !strcmp (base, "NUL") ? 2 : !strcmp (base, "PRN") || !strcmp (base, "AUX")
			|| !strncmp (base, "LPT", 3) || !strncmp (base, "COM", 3) ? 3 : 0;
		if (dev && (!base[3] || isdigit ((unsigned char) base[3])))
		{
			int h = kernel_fh_newdev (dev);
			if (h < 0)
				dos_error (4);
			else
				set16 (R_EAX, (uint16_t) h);
			return DONE;
		}
		break;
	}
	case 0x3e:
		if (kernel_fh_close (rBX))
			dos_error (6);
		return DONE;
	case 0x3f:
	{
		int dev = kernel_fh_dev (rBX);
		if (dev == 1)
		{
			int r = con_read (RM (cpu_sreg (S_DS), rDX), rCX);
			if (r < 0)
				return WAIT;
			set16 (R_EAX, (uint16_t) r);
			return DONE;
		}
		if (dev)
		{
			set16 (R_EAX, 0);
			return DONE;
		}
		break;
	}
	case 0x40:
	{
		int dev = kernel_fh_dev (rBX);
		if (dev == 1)
		{
			const uint8_t *p = lin_ptr (RM (cpu_sreg (S_DS), rDX));
			for (int i = 0; i < rCX; i++)
				vga_tty (p[i]);
		}
		if (dev)
		{
			set16 (R_EAX, rCX);
			return DONE;
		}
		break;
	}
	case 0x44:
	{
		int dev = kernel_fh_dev (rBX);
		if (rAL == 0x00 && dev)
		{
			set16 (R_EDX, (uint16_t) (dev == 1 ? 0x80d3 : dev == 2 ? 0x8084 : 0x80c0));
			return DONE;
		}
		if (rAL == 0x00 && kernel_fh_valid (rBX))
		{
			set16 (R_EDX, 0x0002);	/* (un fichier du disque C:) */
			return DONE;
		}
		if (rAL == 0x01)
			return DONE;
		if (rAL == 0x06)
		{
			set8l (R_EAX, dev == 1 ? (con_ready () ? 0xff : 0) : 0xff);
			return DONE;
		}
		if (rAL == 0x07)
		{
			set8l (R_EAX, 0xff);
			return DONE;
		}
		if (rAL == 0x0e)
		{
			set8l (R_EAX, 0);
			return DONE;
		}
		break;
	}
	case 0x45:
	{
		int h = kernel_fh_dup (rBX, -1);
		if (h < 0)
			dos_error (6);
		else
			set16 (R_EAX, (uint16_t) h);
		return DONE;
	}
	case 0x46:
		if (kernel_fh_dup (rBX, rCX) < 0)
			dos_error (6);
		return DONE;
	case 0x48:
	{
		uint16_t largest = 0, s = mcb_alloc (rBX, cur_psp, &largest);
		if (!s)
		{
			dos_error (8);
			set16 (R_EBX, largest);
		}
		else
			set16 (R_EAX, s);
		return DONE;
	}
	case 0x49:
		if (!mcb_valid (cpu_sreg (S_ES)))
			dos_error (9);
		else
		{
			mcb_free (cpu_sreg (S_ES));
			mcb_merge ();
		}
		return DONE;
	case 0x4a:
	{
		uint16_t largest = 0;
		if (!mcb_valid (cpu_sreg (S_ES)))
			dos_error (9);
		else if (!mcb_resize (cpu_sreg (S_ES), rBX, &largest))
		{
			dos_error (8);
			set16 (R_EBX, largest);
		}
		return DONE;
	}
	case 0x4b:
		return exec ();
	case 0x4c:
		return terminate (rAL, 0, 0);
	case 0x4d:
		set16 (R_EAX, last_ret);
		last_ret = 0;
		return DONE;
	case 0x50:
		cur_psp = rBX;
		return DONE;
	case 0x51:
	case 0x62:
		set16 (R_EBX, cur_psp);
		return DONE;
	case 0x52:
		cpu_set_sreg (S_ES, DOS_SEG);
		set16 (R_EBX, LOL_OFF);
		return DONE;
	case 0x54:
		set8l (R_EAX, 0);
		return DONE;
	case 0x55:
		make_psp (rDX, rSI, cur_psp, rd16 (RM (cur_psp, 0x2c)));
		cur_psp = rDX;
		return DONE;
	case 0x58:
		if (rAL == 0)
			set16 (R_EAX, strategy);
		else if (rAL == 1)
			strategy = rBL;
		else if (rAL == 2)
			set8l (R_EAX, 0);
		return DONE;
	case 0x59:
		set16 (R_EAX, last_error);
		set16 (R_EBX, 0x0d01);
		set8h (R_ECX, 1);
		return DONE;
	case 0x5d:
	case 0x5e:
	case 0x5f:
		dos_error (1);
		return DONE;
	case 0x60:
	{
		/* (le nom complet : seulement le lecteur ajouté) */
		const char *n = w16_raw (MAKESEGPTR (cpu_sreg (S_DS), rSI));
		char *d = (char *) lin_ptr (RM (cpu_sreg (S_ES), rDI));
		if (n[0] && n[1] == ':')
			snprintf (d, 128, "%s", n);
		else
			snprintf (d, 128, "D:\\%s", n[0] == '\\' ? n + 1 : n);
		for (char *q = d; *q; q++)
			*q = (char) toupper ((unsigned char) *q);
		return DONE;
	}
	case 0x63:
		cpu_set_sreg (S_DS, DOS_SEG);
		set16 (R_ESI, COUNTRY_OFF + 0x40);
		set8l (R_EAX, 0);
		return DONE;
	case 0x65:
		if (rAL == 0x20)
			set8l (R_EDX, (uint8_t) toupper (rDL));
		else if (rAL == 0x01)
		{
			uint8_t *d = lin_ptr (RM (cpu_sreg (S_ES), rDI));
			d[0] = 1;
			wr16 (RM (cpu_sreg (S_ES), rDI + 1), 38);
			wr16 (RM (cpu_sreg (S_ES), rDI + 3), 33);
			wr16 (RM (cpu_sreg (S_ES), rDI + 5), 437);
			memcpy (d + 7, lin_ptr (RM (DOS_SEG, COUNTRY_OFF)), 34);
			set16 (R_ECX, 41);
		}
		else
			dos_error (1);
		return DONE;
	case 0x66:
		set16 (R_EBX, 437);
		set16 (R_EDX, 437);
		return DONE;
	case 0x67:
	case 0x68:
	case 0x6a:
		return DONE;
	case 0x6c:	/* ouverture étendue (DOS 4) : par 3Dh ou 3Ch */
	{
		char host[700];
		const char *n = w16_raw (MAKESEGPTR (cpu_sreg (S_DS), rSI));
		int exists = dos_to_host (n, host, sizeof host) && access (host, F_OK) == 0;
		int act = exists ? (rDL & 0x0f) : (rDL >> 4);
		if (!act)
		{
			dos_error (exists ? 80 : 2);
			return DONE;
		}
		uint16_t dx = rDX, bx = rBX;
		set16 (R_EDX, rSI);
		set16 (R_EAX, (uint16_t) (exists && act == 1 ? 0x3d00 | (bx & 3) : 0x3c00));
		int r = int21 ();
		if (!(cpu_flags () & 1))
			set16 (R_ECX, (uint16_t) (!exists ? 2 : act == 1 ? 1 : 3));
		if (cpu_flags () & 1)
			set16 (R_EDX, dx);
		return r;
	}
	case 0x71:
		set16 (R_EAX, 0x7100);
		set_cf (1);
		return DONE;
	}
	/* le reste (fichiers, dossiers, recherche, date) : le KERNEL de win16 */
	dos_int21 ();
	if (cpu_flags () & 1)
		last_error = rAX;
	return DONE;
}

/* ======================================================================== les vecteurs du DOS */
static int int2f (void)
{
	switch (rAX)
	{
	case 0x1600:	/* Windows ? non */
	case 0x4300:	/* XMS ? pas encore */
	case 0x1a00:	/* ANSI.SYS ? non */
	case 0xae00:	/* commandes installables : non */
	case 0x1100:
	case 0xb700:
		set8l (R_EAX, 0);
		return DONE;
	case 0x1680:	/* rendre la main : volontiers */
		bios_idle (1);
		set8l (R_EAX, 0);
		return DONE;
	case 0x4a01:
	case 0x4a02:
		set16 (R_EBX, 0);
		return DONE;
	}
	return DONE;
}

int dos_trap (int vec)
{
	switch (vec)
	{
	case 0x00:
	{
		const char *m = "\r\nDivide overflow\r\n";
		while (*m)
			vga_tty ((uint8_t) *m++);
		return terminate (0, 0, 0);	/* (le cadre de l'exception est oublié avec le processus) */
	}
	case 0x06:
	{
		uint32_t f = RM (cpu_sreg (S_SS), (uint16_t) cpu_reg (R_ESP));
		w16_fatal ("instruction invalide en %04x:%04x", rd16 (f + 2), rd16 (f));
	}
	case 0x20:
		return terminate (0, 0, 0);
	case 0x21:
		return int21 ();
	case 0x22:
		return DONE;
	case 0x23:
		return terminate (0, 1, 0);
	case 0x24:
		set8l (R_EAX, 3);
		return DONE;
	case 0x25:
	case 0x26:
		set16 (R_EAX, 0x0201);
		set_cf (1);
		return DONE;
	case 0x27:
		return terminate (0, 3, (uint16_t) ((rDX + 15) / 16));
	case 0x28:
		return DONE;
	case 0x29:
		vga_tty (rAL);
		return DONE;
	case 0x2a:
		set8h (R_EAX, 0);
		return DONE;
	case 0x2e:
		set16 (R_EAX, 0);
		return DONE;
	case 0x2f:
		return int2f ();
	case 0x33:	/* la souris : à l'étape D3 */
		if (rAX == 0)
			set16 (R_EAX, 0);
		return DONE;
	}
	return -1;
}

void dos_far (int n)
{
	(void) n;
}

/* ======================================================================== la boucle */
static void epilogue (void)
{
	uint16_t ip = pop16 (), cs = pop16 (), f = pop16 ();
	f = (uint16_t) ((f & ~0x8d5u) | (cpu_flags () & 0x8d5u));
	cpu_jump (cs, ip);
	cpu_set_flags ((cpu_flags () & ~0xffffu) | f);
}

static void trap (uint32_t ip)
{
	if (ip >= FAR_BASE)
	{
		if (ip < FAR_BASE + 0x80)
			bios_far ((int) ip);
		else
			dos_far ((int) ip);
		uint16_t rip = pop16 (), rcs = pop16 ();
		cpu_jump (rcs, rip);
		return;
	}
	int vec = (int) ip;
	uint32_t ss = cpu_sreg_base (S_SS);
	uint16_t sp = (uint16_t) cpu_reg (R_ESP);
	uint16_t flags = rd16 (ss + sp + 4);
	cpu_set_flags ((cpu_flags () & ~0xffffu) | (flags & ~0x100u));
	static uint32_t last_wait;	/* (un gestionnaire rappelé pendant une attente : noté une fois) */
	uint32_t here = (uint32_t) vec << 24 ^ (uint32_t) rAX << 8 ^ sp;
	if (w16_trace && here != last_wait && vec != 0x16 && vec != 0x28 && !(vec == 0x21 && (rAH == 0x0b || rAH == 0x06 || rAH == 0x02 || rAH == 0x2c)))
		w16_log ("INT %02xh AX=%04x BX=%04x CX=%04x DX=%04x de %04x:%04x\n", vec, rAX, rBX, rCX, rDX,
			rd16 (ss + sp + 2), rd16 (ss + sp));
	int r = dos_trap (vec);
	if (r < 0)
		r = bios_int (vec);
	if (r < 0)
	{
		static uint8_t seen[256];
		if (!seen[vec])
		{
			seen[vec] = 1;
			w16_log ("DOS : INT %02xh ignorée (AX=%04x)\n", vec, rAX);
		}
		r = DONE;
	}
	if (r != WAIT)
		last_wait = 0;
	if (w16_exit || r == NOEPI)
		return;
	if (r == WAIT)
	{
		last_wait = here;
		/* (le gestionnaire sera rappelé ; les IRQ passent entre-temps) */
		cpu_set_flags (cpu_flags () | 0x200);
		bios_idle (20);
		return;
	}
	epilogue ();
}

static void run (void)
{
	unsigned n = 0;
	while (!w16_exit)
	{
		if (!(++n & 15))
		{
			if (!(n & 1023))
				bios_poll (0);
			if (cpu_flags () & 0x200)
			{
				int v = pic_pending ();
				if (v >= 0)
					cpu_interrupt (v);
			}
		}
		if (cpu_sreg (S_CS) == ROM_SEG)
		{
			uint32_t ip = cpu_eip ();
			if (ip < TRAP_END)
			{
				trap (ip);
				continue;
			}
		}
		if (w16_trace > 1)
		{
			char buf[128];
			cpu_disasm (buf, cpu_sreg (S_CS), cpu_eip ());
			fprintf (stderr, "%04x:%04x %-32s AX=%04x BX=%04x CX=%04x DX=%04x SI=%04x DI=%04x BP=%04x SP=%04x DS=%04x ES=%04x SS=%04x\n",
				cpu_sreg (S_CS), cpu_eip (), buf, rAX, rBX, rCX, rDX, rSI, rDI, rBP, (uint16_t) cpu_reg (R_ESP),
				cpu_sreg (S_DS), cpu_sreg (S_ES), cpu_sreg (S_SS));
		}
		cpu_step ();
		if (cpu_take_halt ())
		{
			/* HLT : attendre la prochaine interruption */
			while (!w16_exit && (cpu_flags () & 0x200))
			{
				int v = pic_pending ();
				if (v >= 0)
				{
					cpu_interrupt (v);
					break;
				}
				bios_idle (10);
			}
		}
	}
}

/* ======================================================================== le départ */
static void dos_data_init (void)
{
	memset (lin_ptr (RM (DOS_SEG, 0)), 0, 0x900);
	wr16 (RM (DOS_SEG, LOL_OFF - 2), FIRST_MCB);
	lin_ptr (RM (DOS_SEG, LOL_OFF + 0x20))[0] = 26;	/* (nombre de lecteurs) */
	lin_ptr (RM (DOS_SEG, LOL_OFF + 0x21))[0] = 26;	/* (LASTDRIVE) */
	/* les informations du pays (France : 33) */
	uint8_t *c = lin_ptr (RM (DOS_SEG, COUNTRY_OFF));
	wr16 (RM (DOS_SEG, COUNTRY_OFF), 1);	/* jj/mm/aa */
	memcpy (c + 2, "EUR", 4);
	c[7] = ' ';
	c[9] = ',';
	c[11] = '/';
	c[13] = ':';
	c[15] = 3;
	c[16] = 2;
	c[17] = 1;
	wr32 (RM (DOS_SEG, COUNTRY_OFF + 18), (uint32_t) ROM_SEG << 16 | (ROM_CODE + 0x70));
	c[22] = ';';
	lin_ptr (RM (ROM_SEG, ROM_CODE + 0x70))[0] = 0xcb;	/* (la routine des majuscules : RETF) */
	/* la mémoire : un seul bloc libre */
	mcb_set (FIRST_MCB, 'Z', 0, (uint16_t) (MEM_TOP - FIRST_MCB - 1));
}

int dos_main (const char *path, const char *dospath_in, const char *args)
{
	w16_realmode = 1;
	if (!cpu_init ())
	{
		fprintf (stderr, "win16 : pas assez de mémoire\n");
		return 1;
	}
	kernel_drives_init ();
	bios_init ();
	dos_data_init ();
	/* le processus racine (le « DOS ») et son environnement */
	root_psp = mcb_alloc (0x10, 8, 0);
	wr16 (RM (root_psp - 1, 1), root_psp);
	mcb_set_name (root_psp, "COMMAND");
	uint16_t env = mcb_alloc (0x20, root_psp, 0);
	{
		char *e = (char *) lin_ptr (RM (env, 0));
		int o = 0;
		o += sprintf (e + o, "COMSPEC=Z:\\COMMAND.COM") + 1;
		o += sprintf (e + o, "PATH=Z:\\;C:\\;D:\\") + 1;
		o += sprintf (e + o, "PROMPT=$P$G") + 1;
		o += sprintf (e + o, "TEMP=C:\\") + 1;
		e[o] = 0;
	}
	make_psp (root_psp, MEM_TOP, root_psp, env);
	cur_psp = root_psp;
	/* le programme : D:\NOM.EXE (D: = son dossier) */
	const char *b = strrchr (path, '/');
	b = b ? b + 1 : path;
	char dpath[300];
	snprintf (dpath, sizeof dpath, "D:\\%s", b);
	if (dospath_in)
		snprintf (dpath, sizeof dpath, "%s", dospath_in);
	for (char *q = dpath; *q; q++)
		*q = (char) toupper ((unsigned char) *q);
	char title[160];
	snprintf (title, sizeof title, "%s", dospath_in ? "DOS" : dpath + 3);
	bios_set_title (title);
	struct loaded L;
	int e = load (dpath, 0, 0, 0, 0, &L);
	if (e)
	{
		char msg[400];
		snprintf (msg, sizeof msg, "Impossible de charger %s (erreur DOS %d).", path, e);
		be_message ("DOS", msg, 0);
		fprintf (stderr, "win16 : %s\n", msg);
		return 1;
	}
	uint8_t tail[130];
	int n = args ? (int) strlen (args) : 0;
	if (n > 125)
		n = 125;
	tail[0] = (uint8_t) (n ? n + 1 : 0);
	tail[1] = ' ';
	if (n)
		memcpy (tail + 2, args, (size_t) n);
	set_cmdtail (L.psp, tail);
	/* les deux FCB, tirées des deux premiers arguments */
	{
		uint8_t *p = lin_ptr (RM (L.psp, 0));
		uint16_t ds = cpu_sreg (S_DS), es = cpu_sreg (S_ES);
		uint32_t si = cpu_reg (R_ESI), di = cpu_reg (R_EDI), ax = cpu_reg (R_EAX);
		cpu_set_sreg (S_DS, L.psp);
		cpu_set_sreg (S_ES, L.psp);
		set16 (R_ESI, 0x81);
		set16 (R_EDI, 0x5c);
		set16 (R_EAX, 0x2901);
		parse_fcb ();
		set16 (R_EDI, 0x6c);
		set16 (R_EAX, 0x2901);
		parse_fcb ();
		cpu_set_sreg (S_DS, ds);
		cpu_set_sreg (S_ES, es);
		cpu_set_reg (R_ESI, si);
		cpu_set_reg (R_EDI, di);
		cpu_set_reg (R_EAX, ax);
		(void) p;
	}
	depth = 0;
	wr32 (RM (L.psp, 0x0a), (uint32_t) ROM_SEG << 16 | 0x20);
	w16_log ("DOS : %s, PSP %04x, départ en %04x:%04x, pile %04x:%04x\n", dpath, L.psp, L.cs, L.ip, L.ss, L.sp);
	start (&L);
	bios_poll (1);
	run ();
	w16_log ("DOS : fin, code %d\n", w16_exit_code);
	int code = w16_exit_code;
	if (dos_hold && !closed_by_user ())
	{
		/* (un .BAT lancé depuis Fichiers : sa sortie reste lisible jusqu'à la fermeture de la fenêtre) */
		bios_poll (1);
		char t[200];
		snprintf (t, sizeof t, "%s (terminé)", title);
		bios_set_title (t);
		w16_exit = 0;
		while (!w16_exit)
			bios_idle (100);
	}
	if (bios_win)
		be_window_close (bios_win);
	return code;
}
