//
// printd -- le service d'impression (service « print », docs/IMPRESSION.md ; port de user/Apps/printd d'Onyx) : la file des
// travaux que les applications ont enregistres (SD0:/var/spool/print : <id>.opj les pages, <id>.job le ticket), imprimes
// l'un apres l'autre ; les imprimantes (SD0:/etc/printers.ini : ajoutees, retirees, interrogees) ; les polices.
//
// Un travail, selon le genre de son imprimante :
//   pdf  les pages rejouees en PDF (pdfsink.h), enregistrees ou l'utilisateur l'a dit ;
//   ipp  une imprimante reseau (IPP Everywhere / AirPrint, ipp.h) : les pages rendues a sa resolution (raster.h) et
//        envoyees en PWG Raster a mesure qu'elles sont faites -- ou en PDF a une imprimante qui le prend --, puis
//        l'imprimante interrogee jusqu'a ce qu'elle ait fini.
// L'utilisateur est prevenu quand un travail est imprime, ou pourquoi il ne l'a pas ete (notifyd).
//
// Deux fils (differe d'Onyx, un seul fil entre deux lignes de pixels) : celui du service repond aux requetes, l'autre fait
// les travaux ; un seul travail rendu a la fois.
//
// Pas de fenetre. Lance par SD0:/etc/session (les travaux restes dans la file sont imprimes), et par le kit s'il est absent.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (the notice: ipp.h).
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <archivekit_abi.h>
#include <netkit_abi.h>
#include "printd.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "job.h"
#include "raster.h"
#include "pdfsink.h"
#include "ippnet.h"

using namespace pprt;

#define WM_A		8
#define WM_B		16
#define WM_C		24
#define WM_D		32
#define WM_DATA		48

static inline long sys3 (long nr, long a, long b, long c)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x8 __asm__ ("x8") = nr;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8) : "memory", "cc");
	return x0;
}
enum { SYS_SVC_REGISTER = 0x30, SYS_SVC_OPEN = 0x31, SYS_SVC_CALL = 0x32, SYS_SVC_NEXT = 0x33, SYS_SVC_REPLY = 0x34 };

struct Job
{
	unsigned id; int state, pages, page;
	char printer[64], title[80], message[96], media[64], output[256];
	int copies, mono, quality;
	int remote;				// son numero a l'imprimante (pendant qu'elle imprime)
	char uri[160];
	unsigned polled; int polls;
	bool cancel;
};
enum { MAXJOB = 32 };
static Job g_job[MAXJOB]; static int g_njob;
static Job *g_cur;				// le travail en cours de rendu, d'envoi
static bool g_forget;				// les travaux finis quittent la liste (fait entre deux travaux)
static unsigned g_nextid;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;

static void spool (char *out, int cap, unsigned id, const char *ext) { snprintf (out, cap, PRINT_SPOOL "/%u%s", id, ext); }
static void spool_drop (unsigned id)
{
	char p[160];
	spool (p, sizeof p, id, ".opj"); remove (p);
	spool (p, sizeof p, id, ".job"); remove (p);
}
static Job *job_by_id (unsigned id) { for (int i = 0; i < g_njob; i++) if (g_job[i].id == id) return &g_job[i]; return 0; }
static bool finished (const Job &j) { return j.state >= PJ_DONE; }

static void notify (const char *title, const char *text)
{
	long ch = sys3 (SYS_SVC_OPEN, (long) "notify", 0, 0);
	if (ch < 0) return;
	char m[512]; memset (m, 0, sizeof m);
	*(long *) m = 1;
	int n = snprintf (m + 48, 460, "%s", title);
	if (n > 0 && n < 440) snprintf (m + 48 + n + 1, (size_t) (460 - n - 1), "%s", text);
	sys3 (SYS_SVC_CALL, ch, (long) m, 0);
}

// un travail d'apres son ticket -> en file (verrou tenu)
static bool enqueue (unsigned id)
{
	if (job_by_id (id)) return true;
	char p[160]; spool (p, sizeof p, id, ".job");
	char *t = pio_load (p, 0);
	if (!t) return false;
	if (g_njob == MAXJOB)
	{	// la liste est pleine : le plus ancien fini s'en va (pas pendant un travail : il tient par sa place)
		int k = -1;
		if (g_cur) { delete[] t; return false; }
		for (int i = 0; i < g_njob && k < 0; i++) if (finished (g_job[i])) k = i;
		if (k < 0) { delete[] t; return false; }
		for (int i = k; i + 1 < g_njob; i++) g_job[i] = g_job[i + 1];
		g_njob--;
	}
	Job &j = g_job[g_njob]; memset (&j, 0, sizeof j);
	j.id = id; j.copies = 1; j.quality = 4;
	Ini ini (t);
	while (ini.next ())
	{
		const char *k = ini.key, *v = ini.val;
		if (seq (k, "printer")) scpy (j.printer, sizeof j.printer, v);
		else if (seq (k, "title")) scpy (j.title, sizeof j.title, v);
		else if (seq (k, "media")) scpy (j.media, sizeof j.media, v);
		else if (seq (k, "output")) scpy (j.output, sizeof j.output, v);
		else if (seq (k, "copies")) j.copies = sint (v);
		else if (seq (k, "mono")) j.mono = sint (v);
		else if (seq (k, "quality")) j.quality = sint (v);
		else if (seq (k, "pages")) j.pages = sint (v);
	}
	delete[] t;
	j.state = PJ_QUEUED;
	g_njob++;
	return true;
}

// ---- les polices de la carte : famille et style de chaque fichier, lus dans ses tables -------------------------------
struct FontFile { char path[112], family[48]; int style; char *data; unsigned len; Ttf *ttf; };
enum { MAXFILES = 64 };
static FontFile s_file[MAXFILES]; static int s_nfile = -1;

static unsigned be16 (const unsigned char *p) { return (unsigned) p[0] << 8 | p[1]; }
static unsigned be32 (const unsigned char *p) { return (unsigned) p[0] << 24 | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3]; }
static bool ieq (const char *a, const char *b)
{
	for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return false; }
	return *a == *b;
}
static void scan_file (const char *path)
{
	if (s_nfile >= MAXFILES) return;
	FILE *h = fopen (path, "rb");
	if (!h) return;
	static unsigned char d[12 + 64 * 16];
	FontFile &f = s_file[s_nfile];
	f.family[0] = 0; f.style = 0; f.data = 0; f.len = 0; f.ttf = 0;
	if (fread (d, 1, 12, h) == 12 && (be32 (d) == 0x00010000 || be32 (d) == 0x74727565))
	{
		unsigned nt = be16 (d + 4); if (nt > 64) nt = 64;
		if (fread (d + 12, 1, nt * 16, h) == nt * 16)
			for (unsigned i = 0; i < nt; i++)
			{
				const unsigned char *e = d + 12 + i * 16; unsigned off = be32 (e + 8), len = be32 (e + 12);
				if (!memcmp (e, "head", 4))
				{
					unsigned char m[2];
					fseek (h, (long) off + 44, SEEK_SET);
					if (fread (m, 1, 2, h) == 2) f.style = (m[1] & 1 ? 1 : 0) | (m[1] & 2 ? 2 : 0);
				}
				else if (!memcmp (e, "name", 4) && len >= 6)
				{
					if (len > 32768) len = 32768;
					unsigned char *nm = new unsigned char[len];
					fseek (h, (long) off, SEEK_SET);
					if (fread (nm, 1, len, h) == len)
					{
						unsigned cnt = be16 (nm + 2), so = be16 (nm + 4); int best = 0;
						for (unsigned k = 0; k < cnt && 6 + (k + 1) * 12 <= len; k++)
						{
							const unsigned char *r = nm + 6 + k * 12;
							unsigned plat = be16 (r), id = be16 (r + 6), l = be16 (r + 8), o = so + be16 (r + 10);
							if (id != 1 || o + l > len) continue;
							int rank = plat == 3 ? 2 : 1;		// (noms de Windows : UTF-16 ; du Mac : octets)
							if (rank <= best) continue;
							int n = 0;
							if (plat == 3) { for (unsigned c = 0; c + 1 < l && n < 47; c += 2) f.family[n++] = (char) (nm[o + c] ? '?' : nm[o + c + 1]); }
							else for (unsigned c = 0; c < l && n < 47; c++) f.family[n++] = (char) nm[o + c];
							f.family[n] = 0; best = rank;
						}
					}
					delete[] nm;
				}
			}
	}
	fclose (h);
	if (f.family[0]) { scpy (f.path, sizeof f.path, path); s_nfile++; }
}
static void scan_fonts ()
{
	if (s_nfile >= 0) return;
	s_nfile = 0;
	static const char *const DIRS[2] = { "SD0:/fonts", "SD0:/res/fonts" };
	for (int k = 0; k < 2; k++)
	{
		DIR *d = opendir (DIRS[k]);
		if (!d) continue;
		struct dirent *e;
		while ((e = readdir (d)))
		{
			int n = slen (e->d_name);
			if (n < 5 || e->d_name[n - 4] != '.' || (e->d_name[n - 3] | 32) != 't' || (e->d_name[n - 2] | 32) != 't' || (e->d_name[n - 1] | 32) != 'f') continue;
			char p[160]; scpy (p, sizeof p, DIRS[k]); scat (p, sizeof p, "/"); scat (p, sizeof p, e->d_name);
			scan_file (p);
		}
		closedir (d);
	}
}
static int find_font (const char *family, int style, unsigned *synth)
{
	scan_fonts ();
	for (int pass = 0; pass < 2; pass++)
	{
		int any = -1, plain = -1;
		for (int i = 0; i < s_nfile; i++)
		{
			if (!ieq (s_file[i].family, family)) continue;
			if (s_file[i].style == style) { *synth = 0; return i; }
			if (s_file[i].style == 0) plain = i;
			else if ((s_file[i].style & ~style) == 0) any = i;
		}
		// la famille sans ce style : le plus proche, le reste fabrique (epaissi, penche)
		if (any >= 0) { *synth = (unsigned) (style & ~s_file[any].style); return any; }
		if (plain >= 0) { *synth = (unsigned) style; return plain; }
		family = "DejaVu Sans";
	}
	return -1;
}
static Ttf *font_ttf (int key)
{
	if (key < 0 || key >= s_nfile) return 0;
	FontFile &f = s_file[key];
	if (f.ttf) return f.ttf;
	if (!f.data) f.data = pio_load (f.path, &f.len);
	if (!f.data) return 0;
	Ttf *t = new Ttf;
	if (!t->open ((const unsigned char *) f.data, f.len)) { delete t; return 0; }
	f.ttf = t;
	return t;
}
// (job.h) une police de la carte, gardee en memoire
char *font_path_data (const char *path, unsigned *len)
{
	for (int i = 0; i < s_nfile; i++)
		if (seq (s_file[i].path, path))
		{
			pthread_mutex_lock (&g_mu);
			Ttf *t = font_ttf (i);
			pthread_mutex_unlock (&g_mu);
			if (!t) return 0;
			*len = s_file[i].len;
			return s_file[i].data;
		}
	return 0;
}
static long measure (int key, long size, const char *text)
{
	Ttf *t = font_ttf (key);
	if (!t) return 0;
	const unsigned char *s = (const unsigned char *) text;
	long units = 0;
	while (*s)
	{
		unsigned c = *s++;
		if (c >= 0xC0) { int e = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1; c &= 0x3Fu >> e; while (e-- && (*s & 0xC0) == 0x80) c = c << 6 | (*s++ & 0x3F); }
		units += t->advance (t->gid (c));
	}
	return units * size / t->upem;
}

// ---- l'imprimante, le reglage ----------------------------------------------------------------------------------------
static void clean_name (char *n)
{
	for (char *p = n; *p; p++) if (*p == '[' || *p == ']' || *p == '=' || *p == ',' || (unsigned char) *p < 32) *p = ' ';
	int l = slen (n); while (l > 0 && n[l - 1] == ' ') n[--l] = 0;
}
// une imprimante interrogee -> son entree
static bool probe (const char *address, const char *name, Printer &p, char *why, int cap, ipp::Caps *out = 0)
{
	ipp::Caps *c = new ipp::Caps;
	bool ok = ipp::get_caps (&ipp::KAPI_NET, address, *c, why, cap);
	if (ok && !c->pwg && !c->pdf) { scpy (why, cap, TR ("cette imprimante ne prend ni PWG Raster ni PDF (ce n'est pas une imprimante IPP Everywhere / AirPrint)")); ok = false; }
	if (ok)
	{
		ipp::Uri u; ipp::parse_uri (address, u);
		memset (&p, 0, sizeof p);
		scpy (p.name, sizeof p.name, name && name[0] ? name : c->name[0] ? c->name : c->model); clean_name (p.name);
		scpy (p.kind, sizeof p.kind, "ipp"); scpy (p.uri, sizeof p.uri, u.ipp); scpy (p.model, sizeof p.model, c->model);
		scpy (p.format, sizeof p.format, c->pwg ? "image/pwg-raster" : "application/pdf");
		p.color = c->color; p.dpi = c->dpi ? c->dpi : 300; p.copies = c->copies > 0 ? c->copies : 1; p.quality = c->quality;
		scpy (p.media, sizeof p.media, c->media[0] ? c->media : "iso_a4_210x297mm,na_letter_8.5x11in");
		scpy (p.media_default, sizeof p.media_default, c->media_default[0] ? c->media_default : "iso_a4_210x297mm");
		for (int i = 0; i < 4; i++) p.margin[i] = c->margin[i];
		if (out) *out = *c;
	}
	delete c;
	return ok;
}
// Le reseau local cherche, comme AirPrint (mDNS / DNS-SD) : une question -- qui offre « _ipp._tcp.local » ? --
// envoyee a 224.0.0.251:5353 depuis un port ordinaire : chaque imprimante repond a ce port (RFC 6762, requete
// « one-shot »). Ceux qui repondent sont ensuite interroges en IPP.
static PdFound g_found[16]; static int g_nfound;
static void scan ()
{
	unsigned char who[16][4]; int nwho = 0;
	g_nfound = 0;
	int u = socket (AF_INET, SOCK_DGRAM, 0);
	if (u >= 0)
	{
		static const unsigned char Q[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
			4, '_', 'i', 'p', 'p', 4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0, 0, 12, 0x80, 1 };	// PTR, IN, reponse directe
		struct sockaddr_in to; memset (&to, 0, sizeof to);
		to.sin_family = AF_INET; to.sin_port = htons (5353); to.sin_addr.s_addr = htonl (0xE00000FB);
		struct timeval tv = { 0, 250000 };
		setsockopt (u, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		for (int round = 0; round < 2; round++)
		{
			if (sendto (u, Q, sizeof Q, 0, (struct sockaddr *) &to, sizeof to) != (long) sizeof Q) break;
			for (int k = 0; k < 6; k++)					// (une seconde et demie de reponses)
			{
				static unsigned char ans[1500]; struct sockaddr_in from; socklen_t fl = sizeof from;
				long r = recvfrom (u, ans, sizeof ans, 0, (struct sockaddr *) &from, &fl);
				if (r < 12 || !(ans[2] & 0x80)) continue;		// (pas une reponse)
				bool have = false;
				for (int i = 0; i < nwho; i++) if (!memcmp (who[i], &from.sin_addr, 4)) have = true;
				if (!have && nwho < 16) memcpy (who[nwho++], &from.sin_addr, 4);
			}
		}
		close (u);
	}
	ipp::Caps *cp = new ipp::Caps;
	for (int i = 0; i < nwho && g_nfound < 16; i++)
	{
		char addr[32], why[96]; snprintf (addr, sizeof addr, "%d.%d.%d.%d", who[i][0], who[i][1], who[i][2], who[i][3]);
		if (!ipp::get_caps (&ipp::KAPI_NET, addr, *cp, why, sizeof why)) continue;
		PdFound &f = g_found[g_nfound++]; memset (&f, 0, sizeof f);
		scpy (f.address, sizeof f.address, addr); scpy (f.name, sizeof f.name, cp->name[0] ? cp->name : cp->model);
		scpy (f.model, sizeof f.model, cp->model); f.usable = cp->pwg || cp->pdf;
	}
	delete cp;
}
static inline int mpt_of (float pt) { return (int) (pt * 1000 + 0.5f); }

// PD_SETUP : le reglage d'apres l'imprimante -> 1, 0 : papier inconnu
static long setup (char *msg)
{
	long ori = *(long *) (msg + WM_A), copies = *(long *) (msg + WM_B), quality = *(long *) (msg + WM_C), mono = *(long *) (msg + WM_D);
	char *d = msg + WM_DATA;
	List *l = new List; load (*l);
	const Printer *pp = d[0] ? find (*l, d) : find (*l, l->def);
	if (!pp) pp = &l->p[0];
	const Printer &p = *pp;
	char media[64]; scpy (media, sizeof media, d + 64);
	char m[64]; bool have = false;
	for (int i = 0; media[0] && list_item (p.media, i, m, sizeof m); i++) if (seq (m, media)) have = true;
	char use[64]; scpy (use, sizeof use, have ? media : p.media_default[0] ? p.media_default : "iso_a4_210x297mm");
	float w = 595.28f, h = 841.89f;
	media_size (use, &w, &h);
	const float k = 72.0f / 2540.0f;			// (1/100 mm -> points)
	float ml = p.margin[0] * k, mt = p.margin[1] * k, mr = p.margin[2] * k, mb = p.margin[3] * k;
	int out[6];
	if (ori == 1)
	{	// la page couchee sur le papier : son bord gauche est le haut du papier (raster.h la tourne ainsi)
		out[0] = mpt_of (h); out[1] = mpt_of (w); out[2] = mpt_of (mt); out[3] = mpt_of (mr); out[4] = mpt_of (mb); out[5] = mpt_of (ml);
	}
	else { out[0] = mpt_of (w); out[1] = mpt_of (h); out[2] = mpt_of (ml); out[3] = mpt_of (mt); out[4] = mpt_of (mr); out[5] = mpt_of (mb); }
	if (copies < 1) copies = 1;
	if (copies > p.copies) copies = p.copies < 1 ? 1 : p.copies;
	if (!p.color) mono = 1;
	if (quality < 0 || quality > 31 || !(p.quality & (1u << quality))) quality = p.quality & (1u << 4) ? 4 : p.quality & (1u << 5) ? 5 : 3;
	memset (d, 0, 128 + 24);
	scpy (d, 64, p.name); scpy (d + 64, 64, use); memcpy (d + 128, out, sizeof out);
	*(long *) (msg + WM_A) = copies; *(long *) (msg + WM_B) = quality; *(long *) (msg + WM_C) = mono;
	delete l;
	return !media[0] || have;
}

// ---- les requetes -----------------------------------------------------------------------------------------------------
static long handle (long op, char *msg)
{
	char *d = msg + WM_DATA;
	long a = *(long *) (msg + WM_A);
	msg[511] = 0;
	switch (op)
	{
	case PD_NEW:
	{
		pthread_mutex_lock (&g_mu);
		unsigned id = ++g_nextid;
		pthread_mutex_unlock (&g_mu);
		spool (d, 160, id, ".opj");
		return id;
	}
	case PD_SUBMIT:
	{
		unsigned id = (unsigned) a;
		long c = *(long *) (msg + WM_C);
		Buf b;
		put_kv (b, "printer", d); put_kv (b, "title", d + 64); put_kv (b, "media", d + 144); put_kv (b, "output", d + 208);
		put_ki (b, "copies", *(long *) (msg + WM_B)); put_ki (b, "mono", c & 255); put_ki (b, "quality", (c >> 8) & 255);
		put_ki (b, "pages", *(long *) (msg + WM_D));
		char p[160]; spool (p, sizeof p, id, ".job");
		if (!pio_save (p, b.b, b.n)) { spool_drop (id); return 0; }
		pthread_mutex_lock (&g_mu);
		bool ok = enqueue (id);
		pthread_cond_signal (&g_wake);
		pthread_mutex_unlock (&g_mu);
		if (!ok) spool_drop (id);
		return ok;
	}
	case PD_ABORT: spool_drop ((unsigned) a); return 0;
	case PD_SETUP: return setup (msg);
	case PD_PRINTER:
	{
		List *l = new List; load (*l);
		long r = 0;
		if (a >= 0 && a < l->n)
		{
			const Printer &p = l->p[a]; PdPrinter &o = *(PdPrinter *) d;
			memset (&o, 0, sizeof o);
			scpy (o.name, sizeof o.name, p.name); scpy (o.kind, sizeof o.kind, p.kind); scpy (o.uri, sizeof o.uri, p.uri); scpy (o.model, sizeof o.model, p.model);
			o.color = p.color; o.copies = p.copies; o.dpi = p.dpi; o.quality = p.quality; o.is_default = seq (p.name, l->def);
			scpy (o.media_default, sizeof o.media_default, p.media_default);
			r = 1;
		}
		delete l;
		return r;
	}
	case PD_MEDIA:
	{
		List *l = new List; load (*l);
		const Printer *p = find (*l, d);
		char m[64]; long ok = p && list_item (p->media, (int) a, m, sizeof m);
		if (ok) { memset (d, 0, 104); scpy (d, 64, m); media_label (m, d + 64, 40); }
		delete l;
		return ok;
	}
	case PD_ADD:
	{
		char name[64], uri[160];
		scpy (name, sizeof name, d); scpy (uri, sizeof uri, d + 64);
		List *l = new List; load (*l);
		Printer p; char why[160] = "";
		bool ok = probe (uri, name, p, why, sizeof why);
		if (ok)
		{
			int at = -1;
			for (int i = 1; i < l->n; i++) if (seq (l->p[i].name, p.name)) at = i;
			if (at < 0 && l->n >= MAXPRINTERS) { ok = false; scpy (why, sizeof why, TR ("la liste des imprimantes est pleine")); }
			else
			{
				if (at < 0) at = l->n++;
				l->p[at] = p;
				if (l->n == 2) scpy (l->def, sizeof l->def, p.name);		// la premiere vraie imprimante : par defaut
				ok = save (*l);
				if (!ok) scpy (why, sizeof why, "SD0:/etc/printers.ini ne peut etre ecrit");
				else scpy (why, sizeof why, p.name);
			}
		}
		scpy (d, 160, why);
		delete l;
		return ok;
	}
	case PD_REMOVE: case PD_DEFAULT:
	{
		List *l = new List; load (*l);
		int at = -1;
		for (int i = 0; i < l->n; i++) if (seq (l->p[i].name, d)) at = i;
		bool ok = at >= 0;
		if (ok && op == PD_DEFAULT) scpy (l->def, sizeof l->def, d);
		else if (ok && at > 0)
		{
			for (int i = at; i + 1 < l->n; i++) l->p[i] = l->p[i + 1];
			l->n--;
			if (seq (l->def, d)) scpy (l->def, sizeof l->def, l->n > 1 ? l->p[1].name : PRINT_PDF_NAME);
		}
		else ok = false;						// (l'imprimante PDF reste)
		if (ok) ok = save (*l);
		delete l;
		return ok;
	}
	case PD_REFRESH:
	{
		List *l = new List; load (*l);
		char text[160] = ""; bool ok = false;
		int at = -1;
		for (int i = 0; i < l->n; i++) if (seq (l->p[i].name, d)) at = i;
		if (at == 0) { ok = true; scpy (text, sizeof text, TR ("Prête")); }
		else if (at > 0)
		{
			Printer p; ipp::Caps *c = new ipp::Caps;
			ok = probe (l->p[at].uri, l->p[at].name, p, text, sizeof text, c);
			if (ok)
			{
				l->p[at] = p; save (*l);
				bool trouble = c->state_reasons[0] && !seq (c->state_reasons, "none");
				snprintf (text, sizeof text, "%s", TR (c->state == 5 ? "À l'arrêt" : c->state == 4 ? "Imprime" : "Prête"));
				if (trouble) { scat (text, sizeof text, " ("); scat (text, sizeof text, c->state_reasons); scat (text, sizeof text, ")"); }
				for (int i = 0; i < c->ninks; i++)
				{
					char t[64]; snprintf (t, sizeof t, " - %s %d %%", c->ink_name[i], c->ink[i]);
					scat (text, sizeof text, t);
				}
				ok = c->state != 5;
			}
			delete c;
		}
		else scpy (text, sizeof text, TR ("imprimante inconnue"));
		scpy (d, 160, text);
		delete l;
		return ok;
	}
	case PD_SCAN: scan (); return g_nfound;
	case PD_FOUND:
		if (a < 0 || a >= g_nfound) return 0;
		memcpy (d, &g_found[a], sizeof (PdFound));
		return 1;
	case PD_JOB:
	{
		long r = 0;
		pthread_mutex_lock (&g_mu);
		if (a >= 0 && a < g_njob)
		{
			const Job &j = g_job[a]; PdJob &o = *(PdJob *) d;
			memset (&o, 0, sizeof o);
			o.id = j.id; o.state = j.state; o.pages = j.pages; o.page = j.page;
			scpy (o.printer, sizeof o.printer, j.printer); scpy (o.title, sizeof o.title, j.title); scpy (o.message, sizeof o.message, j.message);
			r = 1;
		}
		pthread_mutex_unlock (&g_mu);
		return r;
	}
	case PD_CANCEL:
	{
		pthread_mutex_lock (&g_mu);
		Job *j = job_by_id ((unsigned) a);
		if (j && !finished (*j))
		{
			if (j == g_cur) j->cancel = true;				// (le fil des travaux le voit entre deux lignes)
			else
			{
				if (j->state == PJ_PRINTING && j->remote > 0) ipp::cancel_job (&ipp::KAPI_NET, j->uri, j->remote);
				j->state = PJ_CANCELED; scpy (j->message, sizeof j->message, TR ("Annulé")); spool_drop (j->id);
			}
		}
		pthread_mutex_unlock (&g_mu);
		return j != 0;
	}
	case PD_FORGET:
		pthread_mutex_lock (&g_mu);
		g_forget = true;
		pthread_cond_signal (&g_wake);
		pthread_mutex_unlock (&g_mu);
		return 1;
	case PD_FONT:
	{
		unsigned synth = 0;
		int key = find_font (d, (int) (a & 3), &synth);
		if (key < 0) return -1;
		*(long *) (msg + WM_A) = synth;
		scpy (d, 160, s_file[key].path);
		return key;
	}
	case PD_MEASURE: return measure ((int) a, *(long *) (msg + WM_B), d);
	case PD_METRICS:
	{
		Ttf *t = font_ttf ((int) a);
		if (!t) return 0;
		long sz = *(long *) (msg + WM_B);
		*(long *) (msg + WM_A) = t->asc * sz / t->upem; *(long *) (msg + WM_B) = -t->desc * sz / t->upem;
		*(long *) (msg + WM_C) = (t->asc - t->desc + t->gap) * sz / t->upem;
		return 1;
	}
	}
	return -22;
}

// ---- un travail imprime ---------------------------------------------------------------------------------------------
static bool stop () { return g_cur && g_cur->cancel; }

static void done (Job &j, int state, const char *message, bool tell = true)
{
	j.state = state; scpy (j.message, sizeof j.message, message);
	spool_drop (j.id);
	if (!tell) return;
	char t[200];
	if (state == PJ_DONE) snprintf (t, sizeof t, "%s : %s", j.title, message);
	else snprintf (t, sizeof t, TR ("%s n'a pas été imprimé : %s"), j.title, message);
	notify (TR (state == PJ_DONE ? "Imprimé" : "Impression"), t);
}

struct RasterOut : praster::Raster
{
	ipp::Send *send; Job *job; const char *media;
	RasterOut (float w, float h, int dpi, bool gray, ipp::Send *s, Job *j, const char *m) : Raster (w, h, dpi, gray), send (s), job (j), media (m) {}
	static bool wr (void *c, const void *b, unsigned n) { return ((ipp::Send *) c)->write (b, n); }
	bool page_ready () { job->page++; return praster::pwg_page (*this, media, job->quality, job->pages, wr, send); }
};

static void print_pdf (Job &j, const Printer &)
{
	char p[160]; spool (p, sizeof p, j.id, ".opj");
	pjob::Reader r; pjob::PdfSink *s = new pjob::PdfSink;
	if (!r.open (p)) { delete s; done (j, PJ_FAILED, TR ("ses pages manquent dans la file")); return; }
	int k;
	while ((k = r.page (*s)) == 1) { j.page++; if (j.cancel) { delete s; done (j, PJ_CANCELED, TR ("Annulé"), false); return; } }
	if (k < 0) { delete s; done (j, PJ_FAILED, TR ("le fichier du travail est abîmé")); return; }
	s->w.info (j.title, 0, 0, "Korin OS");
	unsigned len = 0; unsigned char *pdf = s->w.finish (&len);
	bool ok = pdf && pio_save (j.output, pdf, len);
	delete[] pdf; delete s;
	if (!ok) { done (j, PJ_FAILED, TR ("le fichier PDF ne peut être écrit")); return; }
	j.state = PJ_DONE; snprintf (j.message, sizeof j.message, "Enregistré : %.80s", j.output);
	spool_drop (j.id);
	char t[360]; snprintf (t, sizeof t, TR ("%s : enregistré sous %s"), j.title, j.output);
	notify (TR ("PDF enregistré"), t);
}
static void print_ipp (Job &j, const Printer &pr)
{
	char p[160]; spool (p, sizeof p, j.id, ".opj");
	pjob::Reader r;
	if (!r.open (p)) { done (j, PJ_FAILED, TR ("ses pages manquent dans la file")); return; }
	bool pdf = seq (pr.format, "application/pdf");
	const char *media = j.media[0] ? j.media : pr.media_default;
	float pw = 595.28f, ph = 841.89f; media_size (media, &pw, &ph);
	ipp::Send *send = new ipp::Send;
	ipp::JobSetup js = { j.title, "armpi", pr.format, media, j.copies, j.quality, j.mono != 0 };
	scpy (j.uri, sizeof j.uri, pr.uri);
	j.state = PJ_SENDING;
	bool ok = send->begin (&ipp::KAPI_NET, pr.uri, js);
	int k = 0;
	if (ok && pdf)
	{
		pjob::PdfSink *s = new pjob::PdfSink;
		while ((k = r.page (*s)) == 1) { j.page++; if (j.cancel) break; }
		unsigned len = 0; unsigned char *d = s->w.finish (&len);
		ok = k == 0 && !j.cancel && d && send->write (d, len);
		delete[] d; delete s;
	}
	else if (ok)
	{
		ok = send->write ("RaS2", 4);
		RasterOut *out = new RasterOut (pw, ph, pr.dpi > 0 ? pr.dpi : 300, j.mono != 0, send, &j, media);
		while (ok && (k = r.page (*out)) == 1) { if (out->failed || j.cancel) ok = false; }
		if (out->failed) ok = false;
		delete out;
	}
	if (j.cancel) { delete send; done (j, PJ_CANCELED, TR ("Annulé"), false); return; }
	if (ok && k < 0) { delete send; done (j, PJ_FAILED, TR ("le fichier du travail est abîmé")); return; }
	int remote = ok ? send->end () : -1;
	char why[96]; scpy (why, sizeof why, send->err[0] ? send->err : TR ("l'imprimante n'a pas pris le travail"));
	delete send;
	if (remote < 0) { done (j, PJ_FAILED, why); return; }
	// envoye : l'imprimante imprime ; on lui demande ou elle en est (la boucle des travaux)
	j.remote = remote; j.state = PJ_PRINTING; j.polls = 0;
	scpy (j.message, sizeof j.message, TR ("Envoyé à l'imprimante"));
	char q[160]; spool (q, sizeof q, j.id, ".opj"); remove (q);		// (les pages sont a l'imprimante ; le ticket reste jusqu'a la fin)
}
static void run (Job &j)
{
	List *l = new List; load (*l);
	const Printer *pr = find (*l, j.printer);
	j.state = PJ_PREPARING; j.page = 0;
	if (!pr) done (j, PJ_FAILED, TR ("son imprimante a été retirée"));
	else if (seq (pr->kind, "pdf")) print_pdf (j, *pr);
	else print_ipp (j, *pr);
	delete l;
}
// les travaux a leur imprimante : interroges toutes les quelques secondes
static void follow ()
{
	for (int i = 0; i < g_njob; i++)
	{
		Job &j = g_job[i];
		if (j.state != PJ_PRINTING) continue;
		char why[96];
		int st = j.remote > 0 ? ipp::job_state (&ipp::KAPI_NET, j.uri, j.remote, why, sizeof why) : ipp::JOB_COMPLETED;
		j.polls++;
		if (st == ipp::JOB_COMPLETED) { char m[96]; snprintf (m, sizeof m, TR ("imprimé sur %.64s"), j.printer); done (j, PJ_DONE, m); }
		else if (st == ipp::JOB_CANCELED) done (j, PJ_CANCELED, TR ("Annulé à l'imprimante"), false);
		else if (st == ipp::JOB_ABORTED) done (j, PJ_FAILED, why[0] ? why : TR ("l'imprimante a abandonné le travail"));
		else if (st < 0 && j.polls > 100) done (j, PJ_DONE, TR ("envoyé (l'imprimante n'en dit pas plus)"), false);
		else if (st == ipp::JOB_STOPPED || (why[0] && st >= 0)) snprintf (j.message, sizeof j.message, TR ("À l'imprimante : %.60s"), why[0] ? why : TR ("arrêtée"));
		else if (st >= 0) scpy (j.message, sizeof j.message, TR (st == ipp::JOB_PROCESSING ? "Imprime" : "Attend à l'imprimante"));
	}
}

// le fil des travaux
static void *worker (void *)
{
	unsigned idle = 0;
	pthread_mutex_lock (&g_mu);
	for (;;)
	{
		if (g_forget)
		{
			int k = 0;
			for (int i = 0; i < g_njob; i++) if (!finished (g_job[i])) g_job[k++] = g_job[i];
			g_njob = k; g_forget = false;
		}
		Job *next = 0; bool printing = false;
		for (int i = 0; i < g_njob; i++)
		{
			if (g_job[i].state == PJ_QUEUED && !next) next = &g_job[i];
			if (g_job[i].state == PJ_PRINTING) printing = true;
		}
		if (next)
		{
			g_cur = next;
			pthread_mutex_unlock (&g_mu);
			run (*next);
			pthread_mutex_lock (&g_mu);
			g_cur = 0;
			continue;
		}
		if (!printing) { pthread_cond_wait (&g_wake, &g_mu); continue; }
		pthread_mutex_unlock (&g_mu);
		usleep (100000);
		if (++idle >= 30) { idle = 0; follow (); }
		pthread_mutex_lock (&g_mu);
	}
	return 0;
}

int main (void)
{
	long svc = sys3 (SYS_SVC_REGISTER, (long) PRINTD_NAME, 0, 0);
	if (svc < 0) return 0;					// (il tourne deja)
	netkit_kit_open ();
	archivekit_kit_open ();
	mkdir ("SD0:/var", 0777); mkdir ("SD0:/var/spool", 0777); mkdir (PRINT_SPOOL, 0777);
	mkdir ("SD0:/home", 0777); mkdir ("SD0:/home/documents", 0777);
	ipp::net_stop = stop;
	scan_fonts ();
	g_nextid = (unsigned) time (0) & 0xFFFFFF;
	// les travaux restes dans la file (le Pi a ete eteint avant) ; les pages sans ticket n'ont jamais ete finies
	DIR *d = opendir (PRINT_SPOOL);
	if (d)
	{
		struct dirent *e;
		while ((e = readdir (d)))
		{
			int n = slen (e->d_name);
			if (n > 4 && !strcmp (e->d_name + n - 4, ".job")) { unsigned id = (unsigned) strtoul (e->d_name, 0, 10); enqueue (id); if (id > g_nextid) g_nextid = id; }
		}
		closedir (d);
	}
	pthread_t th;
	pthread_create (&th, 0, worker, 0);
	pthread_mutex_lock (&g_mu);
	pthread_cond_signal (&g_wake);
	pthread_mutex_unlock (&g_mu);
	static char msg[512];
	for (;;)
	{
		long rq = sys3 (SYS_SVC_NEXT, svc, (long) msg, 60000);
		if (rq < 0) return 1;
		if (!rq) continue;
		long r = handle (*(long *) msg, msg);
		*(long *) msg = r;
		sys3 (SYS_SVC_REPLY, rq, (long) msg, 0);
	}
}
