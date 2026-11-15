/*
 * applets/dotnet.cpp -- le volet Assemblys .NET du panneau de configuration : les DLL de /devtools/dotnet, une case à
 * cocher par DLL (sa taille, son nom). Cochée, elle est gardée en mémoire et ses pages partagées entre les programmes
 * (docs/DOTNET.md). Appliquer écrit /etc/dllpreload.txt et lance dllpreload, qui accorde la mémoire au fichier et
 * le refera à chaque démarrage.
 *
 * Le volet n'est dans la liste que si /devtools/dotnet/dllpreload est là : il vient avec le paquet dotnet (sans lui, ni
 * runtime ni DLL ; Applet.needs, main.cpp).
 *
 * Port en C++ de applets/dotnet.inc (Korin ; Onyx n'avait pas .NET).
 */
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../control.h"

enum
{
	DN_MAX = 200,				/* assemblys */
	DN_NAME = 64,
	DN_LABEL = 80,
	DN_FILE = DN_MAX * DN_NAME,
};

static const char dn_s_note[] = "Coch\xC3\xA9" "es : gard\xC3\xA9" "es en m\xC3\xA9moire.";
static const char dn_s_done[] = "Chargement en cours.";
static const char dn_s_absent[] = ".NET n'est pas sur cette carte.";
#define DN_P_FILE	"/etc/dllpreload.txt"
#define DN_P_DIR	"/devtools/dotnet"
#define DN_P_TOOL	"/devtools/dotnet/dllpreload"

static long dn_n;
static char dn_names[DN_MAX][DN_NAME];
static char dn_labels[DN_MAX][DN_LABEL];
static char dn_file[DN_FILE];

/* une entrée du dossier : retenue si c'est une DLL (« .dll » aux capitales près, 5 à 63 octets) */
static void dn_add (const char *name)
{
	size_t l = strlen (name);
	if (dn_n >= DN_MAX || l >= DN_NAME || l < 5 || strcasecmp (name + l - 4, ".dll"))
		return;
	strcpy (dn_names[dn_n++], name);
}

/* dn_names par ordre alphabétique (insertion ; octets sans signe) */
static void dn_sort (void)
{
	char t[DN_NAME];
	for (long i = 1; i < dn_n; i++)
		for (long j = i; j > 0 && strcmp (dn_names[j - 1], dn_names[j]) > 0; j--)
		{
			memcpy (t, dn_names[j - 1], DN_NAME);
			memcpy (dn_names[j - 1], dn_names[j], DN_NAME);
			memcpy (dn_names[j], t, DN_NAME);
		}
}

/* i : son libellé, sa taille puis le nom sans .dll (« 412 Ko  System.Linq ») */
static void dn_label (long i)
{
	char path[128];				/* (12 + 63 octets au plus) */
	stpcpy (stpcpy (path, DN_P_DIR "/"), dn_names[i]);
	struct stat st;
	unsigned long size = stat (path, &st) == 0 ? (unsigned long) st.st_size : 0;
	const char *unit = " Mo  ";
	if (size >= 0x100000)
		size >>= 20;
	else
	{
		size = (size + 1023) >> 10;
		unit = " Ko  ";
	}
	char *o = dn_labels[i];
	int any = 0;				/* en décimal, sans zéros devant (4 chiffres, comme l'asm) */
	for (unsigned long d = 1000; d; d /= 10)
	{
		unsigned long c = size / d;
		size -= c * d;
		any |= c != 0;
		if (any || d == 1)
			*o++ = (char) ('0' + c);
	}
	*o = 0;
	o = tr_cat (o, unit);
	strcpy (o, dn_names[i]);
	o[strlen (o) - 4] = 0;
}

/* nom -> 1 s'il est une ligne de dn_file (sans blancs autour) */
static int dn_listed (const char *name)
{
	size_t l = strlen (name);
	for (const char *p = dn_file; *p;)
	{
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		const char *a = p, *b = e;
		while (a < b && (*a == ' ' || *a == '\t' || *a == '\r'))
			a++;
		while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r'))
			b--;
		if ((size_t) (b - a) == l && l && !memcmp (a, name, l))
			return 1;
		p = *e ? e + 1 : e;
	}
	return 0;
}

static void dn_open (void)
{
	ap.colw = AP_COLS * COL_W;		/* (noms longs : une seule colonne) */
	dn_n = 0;
	long n = 0;
	int fd = open (DN_P_FILE, O_RDONLY);	/* la liste */
	if (fd >= 0)
	{
		n = read (fd, dn_file, DN_FILE - 1);
		if (n < 0)
			n = 0;
		close (fd);
	}
	dn_file[n] = 0;
	DIR *d = opendir (DN_P_DIR);		/* les DLL */
	if (!d)
	{
		ap.msg = dn_s_absent;
		return;
	}
	struct dirent *e;
	while ((e = readdir (d)) != 0)
		dn_add (e->d_name);
	closedir (d);
	dn_sort ();
	for (long i = 0; i < dn_n; i++)
	{
		dn_label (i);
		applet_row (dn_labels[i], R_CHECK, 0, dn_listed (dn_names[i]), i);
	}
	ap.msg = dn_s_note;
}

/* Appliquer : le fichier réécrit, dllpreload lancé */
static void dn_apply (void)
{
	if (!dn_n)
	{
		ap.msg = dn_s_absent;
		return;
	}
	char *o = dn_file;
	*o = 0;
	for (long i = 0; i < ap.nrows; i++)
	{
		const Row &r = ap.rows[i];
		if (r.kind == R_CHECK && r.checked)
		{
			o = stpcpy (o, dn_names[r.value]);
			o = stpcpy (o, CRLF);
		}
	}
	file_write (DN_P_FILE, dn_file);
	spawn (DN_P_TOOL, 0);			/* (l'asm passait un argument vide : aucun) */
	ap.msg = dn_s_done;
}

/* montré seulement si le paquet dotnet est installé : dllpreload est à lui (/devtools/dotnet) */
const Applet dotnet_applet = { "dotnet", ".NET", "Assemblys .NET", "dotnet", dn_open, dn_apply, DN_P_TOOL };
