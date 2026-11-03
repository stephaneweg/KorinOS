//
// pkglib.h -- les paquets d'ARMPI (docs/PAQUETS.md), port de user/Libs/pkg/pkglib.h d'Onyx : ce que partagent la
// commande pkg, le gestionnaire pkgman et le service pkgd. Un seul fichier d'en-tete, une seule unite de traduction.
// C++ sur libposix, OpenSSL (TLS, SHA-256, signature ECDSA de l'index ; le seul TLS de l'OS, docs/OPENSSL.md),
// ArchiveKit (les archives ZIP).
//
// Le depot : index.txt (une section par paquet : version, taille, sha256, fichier, besoins...) et index.sig (sa
// signature ECDSA P-256 / SHA-256, DER en hexa), verifiee avec SD0:/etc/pkg/onyx.pub ; les paquets
// pkgs/<nom>-<version>.opk : des ZIP de l'arborescence de la carte plus PKG/manifest.ini. Un depot est une URL
// http(s) ou un dossier (SD0:/... : les essais, une carte sans reseau).
// La carte : SD0:/etc/pkg/pkg.ini (repo =, key =, check =) ; SD0:/var/pkg/db/<nom>.ini un paquet installe ([package],
// mode manual|auto|never ; [files] : chaque fichier et son SHA-256) ; SD0:/var/pkg/index.txt le dernier index dont
// la signature etait bonne ; SD0:/var/pkg/stage/ les paquets qui attendent le prochain demarrage (restart = 1 :
// le systeme, le firmware), mis en place par `pkg commit` (lance par init2 avant tout le reste).
//
// Rien hors de SD0: ; rien sous var/, ni etc/wifi*, ni etc/remote* ; home/ et les fichiers `config =` ne sont jamais ecrases.
//
#ifndef _pkg_pkglib_h
#define _pkg_pkglib_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include "archivekit_abi.h"
#include "netkit_abi.h"
// Le source est en francais : lang/en.txt, fusionne dans SD0:/res/lang (tools/lang/merge.py). Ni UIKit ici : lang_tr d'AppKit, par son rang.
extern "C" void *__armpi_appkit (unsigned rank);
static inline const char *TR (const char *s)
{
	static const char *(*f) (const char *);
	if (!f) f = (const char *(*) (const char *)) __armpi_appkit (42);
	return f ? f (s) : s;
}

namespace pkg {

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned char u8;

#define PKG_CONF	"SD0:/etc/pkg/pkg.ini"
#define PKG_KEY		"SD0:/etc/pkg/onyx.pub"
#define PKG_ABI		"SD0:/etc/pkg/abi"			// la somme des tables des kits (tools/pkg/mkrepo.py)
#define PKG_PRELOAD	"SD0:/etc/preload.txt"
#define PKG_REPO	"https://stephaneweg.github.io/KorinOS/packages"
#define PKG_BOARD	"armpi"
#define PKG_VAR		"SD0:/var/pkg"
#define PKG_DB		"SD0:/var/pkg/db"
#define PKG_STAGE	"SD0:/var/pkg/stage"
#define PKG_DL		"RAM0:/pkg"				// les telechargements (verifies, puis extraits)
#define PKG_DL2		"SD0:/var/pkg/dl"			// (sans RAM0:)
#define PKG_COMMIT_LOG	"SD0:/var/pkg/commit.log"

// ---- appels systeme, comme src/apps/mail/main.cpp ---------------------------------------------------------------------
static inline long sys_call (long n, long a, long b)
{
	register long x0 __asm__ ("x0") = a; register long x1 __asm__ ("x1") = b; register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x8) : "memory");
	return x0;
}
enum { SYS_KILL_ = 0x14, SYS_PROCLIST_ = 0x15, SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32, SYS_SPAWN_ = 0x8B, SYS_PRELOAD_ = 0x8D };
extern "C" long __armpi_fsys (long n, long a, long b, long c);

// ---- chaines ---------------------------------------------------------------------------------------------------------
static inline char *sdup (const char *s) { size_t n = strlen (s); char *d = (char *) malloc (n + 1); memcpy (d, s, n + 1); return d; }
static inline void cpy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline bool eq (const char *a, const char *b) { return a && b && !strcmp (a, b); }
static inline bool starts (const char *s, const char *p) { return !strncmp (s, p, strlen (p)); }

// "1.10.2" contre "1.9" : les nombres un a un (celui qui manque vaut 0) -> <0, 0, >0
static inline int vcmp (const char *a, const char *b)
{
	while (*a || *b)
	{
		long x = 0, y = 0;
		while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
		while (*b >= '0' && *b <= '9') y = y * 10 + (*b++ - '0');
		if (x != y) return x < y ? -1 : 1;
		while (*a && (*a < '0' || *a > '9')) a++;
		while (*b && (*b < '0' || *b > '9')) b++;
	}
	return 0;
}

// ---- fichiers ----------------------------------------------------------------------------------------------------------
static inline bool exists (const char *p) { struct stat st; return stat (p, &st) == 0; }
static inline bool is_dir (const char *p) { struct stat st; return stat (p, &st) == 0 && S_ISDIR (st.st_mode); }
// un fichier entier -> malloc, termine par 0 (*len : ses octets) ; 0 : absent
static inline char *read_file (const char *path, u64 *len = 0)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0) return 0;
	struct stat st;
	if (fstat (fd, &st) != 0) { close (fd); return 0; }
	u64 n = (u64) st.st_size, got = 0;
	char *b = (char *) malloc ((size_t) n + 1);
	while (b && got < n)
	{
		ssize_t r = read (fd, b + got, n - got > (1u << 20) ? (1u << 20) : (size_t) (n - got));
		if (r <= 0) break;
		got += (u64) r;
	}
	close (fd);
	if (!b || got != n) { free (b); return 0; }
	b[n] = 0;
	if (len) *len = n;
	return b;
}
static inline void mkdirs (const char *dir)
{
	char d[320]; cpy (d, dir, sizeof d);
	for (char *p = d; *p; p++)
		if (*p == '/' && p > d && p[-1] != ':') { *p = 0; mkdir (d, 0777); *p = '/'; }
	mkdir (d, 0777);
}
static inline void mkparent (const char *path)
{
	char d[320]; cpy (d, path, sizeof d);
	char *s = strrchr (d, '/');
	if (s && s > d && s[-1] != ':') { *s = 0; mkdirs (d); }
}
static inline bool write_file (const char *path, const void *b, u64 n)
{
	mkparent (path);
	char tmp[330]; snprintf (tmp, sizeof tmp, "%s.pkgtmp", path);
	int fd = open (tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) return false;
	u64 done = 0; bool ok = true;
	while (done < n)
	{
		ssize_t r = write (fd, (const char *) b + done, n - done);
		if (r <= 0) { ok = false; break; }
		done += (u64) r;
	}
	close (fd);
	if (!ok || rename (tmp, path) != 0) { unlink (tmp); return false; }
	return true;
}
// Un programme epingle (SD0:/etc/preload.txt) : vfs oublie son image quand le fichier change, elle est rechargee
static inline void repin (const char *path)
{
	char *t = read_file (PKG_PRELOAD);
	if (!t) return;
	size_t l = strlen (path);
	for (char *p = t; *p; )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char *q = p; while (q < e && (*q == ' ' || *q == '\t')) q++;
		char *r = e; while (r > q && (r[-1] == ' ' || r[-1] == '\t' || r[-1] == '\r')) r--;
		if ((size_t) (r - q) == l && !strncasecmp (q, path, l)) { __armpi_fsys (SYS_PRELOAD_, (long) path, 0, 0); break; }
		p = *e ? e + 1 : e;
	}
	free (t);
}
// from -> to, to remplace (rename de fatfs : remplace, garde les clusters)
static inline bool move (const char *from, const char *to)
{
	mkparent (to);
	if (rename (from, to) != 0) return false;
	repin (to);
	return true;
}
// un dossier et tout ce qu'il contient
static inline void remove_tree (const char *dir)
{
	DIR *d = opendir (dir);
	if (d)
	{
		for (;;)
		{
			char names[64][128]; int n = 0;
			while (n < 64)
			{
				struct dirent *e = readdir (d);
				if (!e) break;
				if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
				cpy (names[n++], e->d_name, 128);
			}
			for (int i = 0; i < n; i++)
			{
				char p[320]; snprintf (p, sizeof p, "%s/%s", dir, names[i]);
				if (is_dir (p)) remove_tree (p); else unlink (p);
			}
			if (n < 64) break;
			closedir (d); d = opendir (dir); if (!d) break;
		}
		if (d) closedir (d);
	}
	rmdir (dir);
}

// ---- SHA-256 ------------------------------------------------------------------------------------------------------------
struct Sha
{
	EVP_MD_CTX *c;
	Sha () { c = EVP_MD_CTX_new (); EVP_DigestInit_ex (c, EVP_sha256 (), 0); }
	~Sha () { EVP_MD_CTX_free (c); }
	void add (const void *b, size_t n) { EVP_DigestUpdate (c, b, n); }
	void hex (char out[65])
	{
		u8 h[32]; unsigned l = 32; EVP_DigestFinal_ex (c, h, &l);
		static const char x[] = "0123456789abcdef";
		for (int i = 0; i < 32; i++) { out[i * 2] = x[h[i] >> 4]; out[i * 2 + 1] = x[h[i] & 15]; }
		out[64] = 0;
	}
};
static inline bool sha_file (const char *path, char out[65])
{
	int fd = open (path, O_RDONLY);
	if (fd < 0) return false;
	Sha s; static __thread u8 buf[65536];
	for (;;) { ssize_t r = read (fd, buf, sizeof buf); if (r <= 0) break; s.add (buf, (size_t) r); }
	close (fd);
	s.hex (out);
	return true;
}

// La signature de l'index (DER, en hexa), verifiee avec la cle publique (PEM) -> true : bonne
static inline bool sig_ok (const char *data, size_t n, const char *sighex, const char *pem)
{
	u8 sig[160]; size_t sl = 0;
	for (const char *p = sighex; p[0] && p[1] && sl < sizeof sig; p += 2)
	{
		auto hv = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
		int a = hv (p[0]), b = hv (p[1]);
		if (a < 0 || b < 0) break;
		sig[sl++] = (u8) (a * 16 + b);
	}
	if (sl < 8) return false;
	BIO *bio = BIO_new_mem_buf (pem, -1);
	EVP_PKEY *key = bio ? PEM_read_bio_PUBKEY (bio, 0, 0, 0) : 0;
	EVP_MD_CTX *c = EVP_MD_CTX_new ();
	bool ok = key && c && EVP_DigestVerifyInit (c, 0, EVP_sha256 (), 0, key) == 1 &&
		  EVP_DigestVerify (c, sig, sl, (const u8 *) data, n) == 1;
	EVP_MD_CTX_free (c); EVP_PKEY_free (key); BIO_free (bio);
	return ok;
}

// ---- texte ini : [section] et cle = valeur ---------------------------------------------------------------------------------
struct Ini
{
	struct Kv { const char *sec, *key, *val; };
	char *buf; Kv *kv; int n, cap;
	Ini () : buf (0), kv (0), n (0), cap (0) {}
	~Ini () { clear (); }
	void clear () { free (buf); free (kv); buf = 0; kv = 0; n = cap = 0; }
	// prend le texte (malloc)
	void parse (char *text)
	{
		clear (); buf = text;
		const char *sec = "";
		for (char *p = buf; p && *p; )
		{
			char *e = p; while (*e && *e != '\n') e++;
			char *next = *e ? e + 1 : e; *e = 0;
			if (e > p && e[-1] == '\r') e[-1] = 0;
			while (*p == ' ' || *p == '\t') p++;
			if (*p == '[') { char *c = strchr (p, ']'); if (c) { *c = 0; sec = p + 1; } }
			else if (*p && *p != '#' && *p != ';')
			{
				char *q = strchr (p, '=');
				if (q)
				{
					char *k = q; *q = 0; while (k > p && (k[-1] == ' ' || k[-1] == '\t')) *--k = 0;
					char *v = q + 1; while (*v == ' ' || *v == '\t') v++;
					char *ve = v + strlen (v); while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = 0;
					if (n == cap) { cap = cap ? cap * 2 : 64; kv = (Kv *) realloc (kv, sizeof (Kv) * cap); }
					kv[n].sec = sec; kv[n].key = p; kv[n].val = v; n++;
				}
			}
			p = next;
		}
	}
	bool load (const char *path) { char *t = read_file (path); if (!t) { clear (); return false; } parse (t); return true; }
	const char *get (const char *sec, const char *key, const char *def = "") const
	{
		for (int i = 0; i < n; i++) if (eq (kv[i].sec, sec) && eq (kv[i].key, key)) return kv[i].val;
		return def;
	}
};

// ---- les besoins : "armpi >= 2026.11, abi >= 900" ---------------------------------------------------------------------------
struct Need { char name[40]; char ver[24]; };
static inline int parse_needs (const char *s, Need *out, int max)
{
	int n = 0;
	while (s && *s && n < max)
	{
		while (*s == ' ' || *s == ',') s++;
		if (!*s) break;
		Need &d = out[n]; int k = 0;
		while (*s && *s != ' ' && *s != ',' && *s != '>' && *s != '=') { if (k < 39) d.name[k++] = *s; s++; }
		d.name[k] = 0; d.ver[0] = 0;
		while (*s == ' ') s++;
		if (*s == '>' || *s == '=') { while (*s == '>' || *s == '=' || *s == ' ') s++; k = 0; while (*s && *s != ',' && *s != ' ') { if (k < 23) d.ver[k++] = *s; s++; } d.ver[k] = 0; }
		while (*s && *s != ',') s++;
		if (d.name[0]) n++;
	}
	return n;
}
// Le niveau des tables de kits du systeme en service (SD0:/etc/pkg/abi, du paquet armpi : il change au commit) :
// ce qu'etait `kapi` sur Onyx. Un paquet construit contre des tables plus grandes attend le systeme.
static inline int abi_level ()
{
	char *t = read_file (PKG_ABI);
	int v = t ? atoi (t) : 0;
	free (t);
	return v;
}

// ---- l'index du depot ---------------------------------------------------------------------------------------------------
struct Pkg
{
	const char *name, *title, *version, *category, *author, *summary, *file, *sha256, *icon, *needs, *replaces;
	u64 size, installed; bool required, restart;
};
struct Index
{
	Ini ini; Pkg *p; int n; char date[24];
	Index () : p (0), n (0) { date[0] = 0; }
	~Index () { free (p); }
	void build ()
	{
		free (p); p = 0; n = 0;
		for (int i = 0; i < ini.n; i++)
		{
			const char *s = ini.kv[i].sec;
			if (!*s || eq (s, "repo") || !eq (ini.kv[i].key, "version")) continue;
			p = (Pkg *) realloc (p, sizeof (Pkg) * (n + 1));
			Pkg &k = p[n++];
			k.name = s; k.title = ini.get (s, "title", s); k.version = ini.get (s, "version");
			k.category = ini.get (s, "category", "Autres"); k.author = ini.get (s, "author", "");
			k.summary = ini.get (s, "summary"); k.file = ini.get (s, "file"); k.sha256 = ini.get (s, "sha256");
			k.icon = ini.get (s, "icon"); k.needs = ini.get (s, "needs"); k.replaces = ini.get (s, "replaces");
			k.size = strtoull (ini.get (s, "size", "0"), 0, 10); k.installed = strtoull (ini.get (s, "installed", "0"), 0, 10);
			k.required = eq (ini.get (s, "required", "0"), "1"); k.restart = eq (ini.get (s, "restart", "0"), "1");
		}
		cpy (date, ini.get ("repo", "date"), sizeof date);
	}
	const Pkg *find (const char *name) const { for (int i = 0; i < n; i++) if (eq (p[i].name, name)) return &p[i]; return 0; }
};

// "a, b, c" contient name ? (replaces : les paquets dont un paquet prend la place, un paquet renomme)
static inline bool in_list (const char *list, const char *name)
{
	size_t l = strlen (name);
	for (const char *s = list; s && *s; )
	{
		while (*s == ' ' || *s == ',') s++;
		const char *e = s; while (*e && *e != ',' && *e != ' ') e++;
		if ((size_t) (e - s) == l && !strncmp (s, name, l)) return true;
		s = e;
	}
	return false;
}
// Fichiers de l'utilisateur, chemins relatifs a la carte : home/ (exemples seulement ecrits s'ils manquent)
static inline bool user_tree (const char *rel) { return starts (rel, "home/"); }
// Ce qu'un paquet n'a jamais le droit de toucher : l'etat de la carte, les reglages Wi-Fi
static inline bool forbidden (const char *rel)
{
	if (!*rel || rel[0] == '/' || strchr (rel, ':') || strchr (rel, '\\')) return true;
	for (const char *s = rel; s; )
	{
		if (s[0] == '.' && s[1] == '.' && (s[2] == '/' || !s[2])) return true;
		s = strchr (s, '/'); if (s) s++;
	}
	return starts (rel, "var/") || !strcasecmp (rel, "var") || starts (rel, "etc/wifi") || starts (rel, "etc/remote");
}

// ---- les paquets installes (SD0:/var/pkg/db) --------------------------------------------------------------------------------
struct Inst
{
	Ini ini; char name[40];
	bool missing = false;		// un de ses programmes manque : vu comme version 0, donc propose a nouveau
	const char *version () const { return missing ? "0" : ini.get ("package", "version", "0"); }
	void check_programs ()
	{
		for (int i = 0; i < ini.n && !missing; i++)
		{
			const char *k = ini.kv[i].key; size_t l = strlen (k);
			if (!eq (ini.kv[i].sec, "files")) continue;
			if (!(starts (k, "apps/") && l > 10 && !strcmp (k + l - 5, "/main")) && !starts (k, "bin/")) continue;
			char p[300]; snprintf (p, sizeof p, "SD0:/%s", k);
			if (!exists (p)) missing = true;
		}
	}
	const char *mode () const { return ini.get ("package", "mode", "manual"); }
	bool required () const { return eq (ini.get ("package", "required", "0"), "1"); }
	const char *needs () const { return ini.get ("package", "needs"); }
	const char *hash (const char *file) const { return ini.get ("files", file, 0); }
	bool is_config (const char *file) const
	{
		if (user_tree (file)) return true;
		const char *c = ini.get ("package", "config"); size_t l = strlen (file);
		for (const char *s = c; (s = strstr (s, file)) != 0; s += l)
			if ((s == c || s[-1] == ' ') && (s[l] == 0 || s[l] == ' ')) return true;
		return false;
	}
};
struct Db
{
	Inst **v; int n;
	Db () : v (0), n (0) {}
	~Db () { clear (); }
	void clear () { for (int i = 0; i < n; i++) delete v[i]; free (v); v = 0; n = 0; }
	void load ()
	{
		clear ();
		DIR *d = opendir (PKG_DB);
		if (!d) return;
		struct dirent *e;
		while ((e = readdir (d)) != 0)
		{
			int l = (int) strlen (e->d_name);
			if (l < 5 || strcmp (e->d_name + l - 4, ".ini")) continue;
			Inst *x = new Inst;
			char p[300]; snprintf (p, sizeof p, PKG_DB "/%s", e->d_name);
			if (!x->ini.load (p)) { delete x; continue; }
			cpy (x->name, x->ini.get ("package", "name", ""), sizeof x->name);
			if (!x->name[0]) { char t[128]; cpy (t, e->d_name, sizeof t); t[l - 4 < 127 ? l - 4 : 127] = 0; cpy (x->name, t, sizeof x->name); }
			x->check_programs ();
			v = (Inst **) realloc (v, sizeof (Inst *) * (n + 1)); v[n++] = x;
		}
		closedir (d);
		for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (v[j - 1]->name, v[j]->name) > 0; j--) { Inst *t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
	}
	Inst *find (const char *name) const { for (int i = 0; i < n; i++) if (eq (v[i]->name, name)) return v[i]; return 0; }
	// un fichier qu'un autre paquet installe possede (passe d'un paquet a l'autre : garde)
	bool owned_elsewhere (const char *file, const char *self) const
	{ for (int i = 0; i < n; i++) if (!eq (v[i]->name, self) && v[i]->ini.get ("files", file, 0)) return true; return false; }
};

// ---- ce qu'un travail dit a son appelant -----------------------------------------------------------------------------------
struct Report
{
	virtual void say (const char *s) { (void) s; }			// une ligne (pkg l'affiche)
	virtual void step (const char *what, u64 done, u64 total) { (void) what; (void) done; (void) total; }
	virtual bool cancelled () { return false; }
	virtual ~Report () {}
	void sayf (const char *fmt, ...) __attribute__ ((format (printf, 2, 3)))
	{ char t[400]; va_list a; va_start (a, fmt); vsnprintf (t, sizeof t, TR (fmt), a); va_end (a); say (t); }
};

// Les resultats d'un travail
enum { OK = 0, E_NET = 1, E_SIG = 2, E_NOTFOUND = 3, E_BADPKG = 4, E_NEEDS = 5, E_RUNNING = 6, E_REQUIRED = 7,
       E_IO = 8, E_USED = 9, E_ALREADY = 10, E_NOTINST = 11, E_CANCEL = 12 };

// ---- le telechargement : http, https, redirections, chunked ; un Sink recoit le corps ----------------------------------------
struct Sink { virtual bool put (const void *b, size_t n) = 0; virtual ~Sink () {} };
struct MemSink : Sink
{
	char *b; size_t n, cap;
	MemSink () : b (0), n (0), cap (0) {}
	~MemSink () { free (b); }
	bool put (const void *d, size_t k) override
	{
		if (n + k + 1 > cap) { cap = (n + k + 1) * 2; b = (char *) realloc (b, cap); if (!b) return false; }
		memcpy (b + n, d, k); n += k; b[n] = 0; return true;
	}
	char *take () { char *t = b; b = 0; n = cap = 0; return t; }
};
struct FileSink : Sink			// un fichier, et son SHA-256 au passage
{
	int fd; Sha sha; u64 n;
	FileSink (const char *path) : fd (open (path, O_WRONLY | O_CREAT | O_TRUNC, 0666)), n (0) {}
	~FileSink () { if (fd >= 0) close (fd); }
	bool ok () const { return fd >= 0; }
	bool put (const void *d, size_t k) override
	{
		sha.add (d, k); n += k;
		const char *p = (const char *) d;
		while (k) { ssize_t r = write (fd, p, k); if (r <= 0) return false; p += r; k -= (size_t) r; }
		return true;
	}
};

static inline unsigned clock_ms ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static SSL_CTX *tls_ctx ()
{
	static SSL_CTX *c;
	if (c) return c;
	c = SSL_CTX_new (TLS_client_method ());
	if (!c) return 0;
	SSL_CTX_set_min_proto_version (c, TLS1_2_VERSION);
	SSL_CTX_set_default_verify_paths (c);
	return c;
}
struct Conn
{
	int sock; SSL *ssl; Report &r;
	char in[16384]; int rp, wp;
	enum { TIMEOUT = 30000 };
	Conn (Report &rep) : sock (-1), ssl (0), r (rep), rp (0), wp (0) {}
	~Conn () { end (); }
	void end ()
	{
		if (ssl) { SSL_free (ssl); ssl = 0; }
		if (sock >= 0) { close (sock); sock = -1; }
	}
	void wait_io (short what) { struct pollfd p; p.fd = sock; p.events = what; p.revents = 0; poll (&p, 1, 40); }
	bool begin (const char *host, int port, bool tls)
	{
		char ps[12]; snprintf (ps, sizeof ps, "%d", port);
		struct addrinfo hints, *res = 0;
		memset (&hints, 0, sizeof hints);
		hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo (host, ps, &hints, &res) != 0 || !res) { r.sayf ("serveur %s introuvable", host); return false; }
		sock = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sock < 0 || connect (sock, res->ai_addr, res->ai_addrlen) != 0)
		{
			freeaddrinfo (res); end ();
			r.sayf ("%s ne répond pas (port %d)", host, port); return false;
		}
		freeaddrinfo (res);
		fcntl (sock, F_SETFL, fcntl (sock, F_GETFL, 0) | O_NONBLOCK);
		if (!tls) return true;
		SSL_CTX *ctx = tls_ctx ();
		ssl = ctx ? SSL_new (ctx) : 0;
		if (!ssl) { r.sayf ("TLS indisponible"); return false; }
		SSL_set_fd (ssl, sock);
		struct in_addr ip;
		if (inet_pton (AF_INET, host, &ip) == 1) X509_VERIFY_PARAM_set1_ip_asc (SSL_get0_param (ssl), host);
		else { SSL_set_tlsext_host_name (ssl, host); SSL_set1_host (ssl, host); }
		SSL_set_verify (ssl, SSL_VERIFY_PEER, 0);
		unsigned t0 = clock_ms ();
		for (;;)
		{
			if (r.cancelled ()) return false;
			ERR_clear_error ();
			int x = SSL_connect (ssl);
			if (x == 1) return true;
			int e = SSL_get_error (ssl, x);
			if (e == SSL_ERROR_WANT_READ) wait_io (POLLIN);
			else if (e == SSL_ERROR_WANT_WRITE) wait_io (POLLOUT);
			else
			{
				long v = SSL_get_verify_result (ssl);
				if (v != X509_V_OK) r.sayf ("certificat de %s refusé : %s", host, X509_verify_cert_error_string (v));
				else r.sayf ("connexion sécurisée à %s impossible", host);
				return false;
			}
			if (clock_ms () - t0 > TIMEOUT) { r.sayf ("%s : délai dépassé", host); return false; }
		}
	}
	bool send_all (const char *p, int left)
	{
		unsigned t0 = clock_ms ();
		while (left > 0)
		{
			if (r.cancelled ()) return false;
			int x;
			if (ssl)
			{
				x = SSL_write (ssl, p, left);
				if (x <= 0)
				{
					int e = SSL_get_error (ssl, x);
					if (e == SSL_ERROR_WANT_READ) wait_io (POLLIN);
					else if (e == SSL_ERROR_WANT_WRITE) wait_io (POLLOUT);
					else return false;
					x = 0;
				}
			}
			else
			{
				x = (int) send (sock, p, (size_t) left, 0);
				if (x < 0) { wait_io (POLLOUT); x = 0; }
			}
			p += x; left -= x;
			if (x) t0 = clock_ms ();
			else if (clock_ms () - t0 > TIMEOUT) return false;
		}
		return true;
	}
	// remplit in -> false : fin ou erreur
	bool fill ()
	{
		if (rp == wp) rp = wp = 0;
		if (wp == (int) sizeof in) { memmove (in, in + rp, (size_t) (wp - rp)); wp -= rp; rp = 0; }
		unsigned t0 = clock_ms ();
		for (;;)
		{
			if (r.cancelled ()) return false;
			int x; short want = POLLIN;
			if (ssl)
			{
				ERR_clear_error ();
				x = SSL_read (ssl, in + wp, (int) sizeof in - wp);
				if (x > 0) { wp += x; return true; }
				int e = SSL_get_error (ssl, x);
				if (e == SSL_ERROR_WANT_WRITE) want = POLLOUT;
				else if (e != SSL_ERROR_WANT_READ) return false;
			}
			else
			{
				x = (int) recv (sock, in + wp, sizeof in - (size_t) wp, 0);
				if (x > 0) { wp += x; return true; }
				if (x == 0) return false;
			}
			wait_io (want);
			if (clock_ms () - t0 > TIMEOUT) { r.say (TR ("délai de réception dépassé")); return false; }
		}
	}
	// une ligne, sans sa fin -> false : connexion finie
	bool line (char *out, int cap)
	{
		int n = 0;
		for (;;)
		{
			while (rp < wp)
			{
				char c = in[rp++];
				if (c == '\n') { if (n && out[n - 1] == '\r') n--; out[n] = 0; return true; }
				if (n < cap - 1) out[n++] = c;
			}
			if (!fill ()) return false;
		}
	}
	// k octets vers sink -> false : coupure
	bool body (u64 k, Sink &sink, u64 *done, u64 total, const char *what)
	{
		while (k)
		{
			if (rp == wp && !fill ()) return false;
			int n = wp - rp; if ((u64) n > k) n = (int) k;
			if (!sink.put (in + rp, (size_t) n)) return false;
			rp += n; k -= (u64) n; *done += (u64) n;
			r.step (what, *done, total);
		}
		return true;
	}
};

struct Url { char scheme[8], host[128], path[420]; int port; };
static inline bool url_parse (const char *u, Url &o)
{
	const char *p = strstr (u, "://");
	if (!p || p - u > 6) return false;
	memcpy (o.scheme, u, (size_t) (p - u)); o.scheme[p - u] = 0;
	p += 3;
	const char *e = p; while (*e && *e != '/' && *e != ':') e++;
	if (e == p || e - p >= (int) sizeof o.host) return false;
	memcpy (o.host, p, (size_t) (e - p)); o.host[e - p] = 0;
	o.port = eq (o.scheme, "https") ? 443 : 80;
	if (*e == ':') { o.port = atoi (e + 1); while (*e && *e != '/') e++; }
	cpy (o.path, *e ? e : "/", sizeof o.path);
	return true;
}
// GET url -> le corps dans sink ; suit les redirections (GitHub Pages)
static inline bool http_get (const char *first, Sink &sink, u64 want, const char *what, Report &r)
{
	char url[700]; cpy (url, first, sizeof url);
	for (int hop = 0; hop < 6; hop++)
	{
		Url u;
		if (!url_parse (url, u)) { r.sayf ("adresse illisible : %s", url); return false; }
		bool tls = eq (u.scheme, "https");
		Conn c (r);
		if (!c.begin (u.host, u.port, tls)) return false;
		char q[900];
		int n = snprintf (q, sizeof q, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Korin-pkg/1.0\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", u.path, u.host);
		if (!c.send_all (q, n)) { r.sayf ("%s : envoi impossible", u.host); return false; }
		char l[800];
		if (!c.line (l, sizeof l) || strncmp (l, "HTTP/", 5)) { r.sayf ("%s : réponse illisible", u.host); return false; }
		const char *sp = strchr (l, ' ');
		int status = sp ? atoi (sp + 1) : 0;
		long long clen = -1; bool chunked = false; char loc[700]; loc[0] = 0;
		for (;;)
		{
			if (!c.line (l, sizeof l)) { r.sayf ("%s : réponse coupée", u.host); return false; }
			if (!l[0]) break;
			if (!strncasecmp (l, "content-length:", 15)) clen = atoll (l + 15);
			else if (!strncasecmp (l, "transfer-encoding:", 18) && strstr (l + 18, "chunked")) chunked = true;
			else if (!strncasecmp (l, "location:", 9)) { const char *v = l + 9; while (*v == ' ') v++; cpy (loc, v, sizeof loc); }
		}
		if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
		{
			if (!loc[0]) { r.sayf ("%s : redirection sans adresse", u.host); return false; }
			char was[700]; cpy (was, url, sizeof was);
			if (strstr (loc, "://")) cpy (url, loc, sizeof url);
			else if (loc[0] == '/') snprintf (url, sizeof url, "%s://%s:%d%s", u.scheme, u.host, u.port, loc);
			else { char *s = strrchr (was, '/'); if (s) s[1] = 0; snprintf (url, sizeof url, "%s%s", was, loc); }
			continue;
		}
		if (status != 200) { r.sayf ("%s : HTTP %d", what, status); return false; }
		u64 done = 0, total = want ? want : clen > 0 ? (u64) clen : 0;
		r.step (what, 0, total);
		if (chunked)
		{
			for (;;)
			{
				if (!c.line (l, sizeof l)) { r.sayf ("%s : réception coupée", what); return false; }
				u64 k = strtoull (l, 0, 16);
				if (!k) return true;
				if (!c.body (k, sink, &done, total, what) || !c.line (l, sizeof l)) { r.sayf ("%s : réception coupée", what); return false; }
			}
		}
		if (clen >= 0)
		{
			if (!c.body ((u64) clen, sink, &done, total, what)) { r.sayf ("%s : réception coupée", what); return false; }
			return true;
		}
		for (;;)					// sans longueur : jusqu'a la fin de la connexion
		{
			if (c.rp == c.wp && !c.fill ()) return true;
			int k = c.wp - c.rp;
			if (!sink.put (c.in + c.rp, (size_t) k)) return false;
			c.rp += k; done += (u64) k; r.step (what, done, total);
		}
	}
	r.sayf ("%s : trop de redirections", what);
	return false;
}

// ---- ArchiveKit ------------------------------------------------------------------------------------------------------------
static inline bool archive_kit ()
{
	static int ok = -1;
	if (ok < 0) ok = archivekit_kit_open () ? 1 : 0;
	return ok == 1;
}

// ---- les process ------------------------------------------------------------------------------------------------------------
struct Proc { long pid; char name[33]; };
// La liste des process (SYS_PROCLIST : entrees de 64 octets, pid a 0, nom a 24) -> combien
static inline int proc_list (Proc *out, int cap)
{
	static __thread u8 t[128 * 64];
	long n = sys_call (SYS_PROCLIST_, (long) t, cap > 128 ? 128 : cap);
	if (n < 0) return 0;
	for (long i = 0; i < n; i++) { out[i].pid = *(long *) (t + i * 64); cpy (out[i].name, (const char *) t + i * 64 + 24, 33); }
	return (int) n;
}
static inline bool proc_running (const char *name)
{
	Proc p[128]; int n = proc_list (p, 128); long self = getpid ();
	for (int i = 0; i < n; i++) if (p[i].pid != self && eq (p[i].name, name)) return true;
	return false;
}
static inline bool proc_kill (const char *name)
{
	Proc p[128]; int n = proc_list (p, 128); bool any = false; long self = getpid ();
	for (int i = 0; i < n; i++) if (p[i].pid != self && eq (p[i].name, name)) { sys_call (SYS_KILL_, p[i].pid, 0); any = true; }
	return any;
}
// un programme lance, sans attendre (AppKit passe par init2 pour SD0:/sys) -> false : echec
static inline bool launch (const char *path, const char *arg = 0)
{
	return __armpi_fsys (SYS_SPAWN_, (long) path, (long) arg, 0) > 0;
}
// une notification (notifyd, src/inc/notify.inc) ; sans bureau : rien
static inline void notify (const char *title, const char *msg)
{
	long ch = sys_call (SYS_SVC_OPEN_, (long) "notify", 0);
	if (ch < 0) return;
	char m[512]; memset (m, 0, sizeof m);
	*(long *) m = 1;
	int n = snprintf (m + 48, 460, "%s", title);
	if (n > 0 && n < 440) snprintf (m + 48 + n + 1, (size_t) (460 - n - 1), "%s", msg);
	sys_call (SYS_SVC_CALL_, ch, (long) m);
}
// le Pi redemarre (INIT_OP_REBOOT d'init2)
static inline void reboot_now ()
{
	long ch = sys_call (SYS_SVC_OPEN_, (long) "init2", 1000);
	if (ch < 0) return;
	char m[512]; memset (m, 0, sizeof m);
	*(long *) m = 3;
	sys_call (SYS_SVC_CALL_, ch, (long) m);
	sleep (5);
}

// ---- le gestionnaire -----------------------------------------------------------------------------------------------------------
class Manager
{
public:
	char repo[300], key[200];
	Index index; bool haveIndex, verified;
	Db db;
	bool kernelChanged;				// (un commit a touche le demarrage : redemarrer)
	bool appsChanged;				// (une app installee, mise a jour, retiree : refresh_desktop)

	Manager () : haveIndex (false), verified (false), kernelChanged (false), appsChanged (false)
	{
		Ini c; c.load (PKG_CONF);
		cpy (repo, c.get ("", "repo", PKG_REPO), sizeof repo);
		cpy (key, c.get ("", "key", PKG_KEY), sizeof key);
		db.load ();
	}

	bool local_repo () const { return !starts (repo, "http://") && !starts (repo, "https://"); }
	// rel du depot -> sink
	// (une adresse complete, « https://… » : telle quelle ; les gros paquets sont dans une Release)
	bool fetch (const char *rel, Sink &sink, u64 want, Report &r)
	{
		if (strstr (rel, "://")) return http_get (rel, sink, want, rel, r);
		char url[600]; cpy (url, repo, sizeof url);
		int l = (int) strlen (url); if (l && url[l - 1] != '/') { url[l++] = '/'; url[l] = 0; }
		strncat (url, rel, sizeof url - strlen (url) - 1);
		if (!local_repo ()) return http_get (url, sink, want, rel, r);
		int fd = open (url, O_RDONLY);
		if (fd < 0) { r.sayf ("%s : absent", url); return false; }
		static __thread char buf[65536]; u64 done = 0;
		bool ok = true;
		for (;;)
		{
			if (r.cancelled ()) { ok = false; break; }
			ssize_t k = read (fd, buf, sizeof buf);
			if (k <= 0) break;
			if (!sink.put (buf, (size_t) k)) { ok = false; break; }
			done += (u64) k; r.step (rel, done, want);
		}
		close (fd);
		return ok;
	}

	// L'index du depot, sa signature verifiee (puis garde en SD0:/var/pkg/index.txt)
	int refresh (Report &r)
	{
		MemSink ti, si;
		if (!fetch ("index.txt", ti, 0, r) || !ti.n) return load_cached (r) ? OK : E_NET;
		if (!fetch ("index.sig", si, 0, r) || !si.n) { r.say (TR ("l'index n'a pas de signature : refusé")); return E_SIG; }
		char *pem = read_file (key);
		if (!pem) { r.sayf ("pas de clé publique (%s) : index refusé", key); return E_SIG; }
		bool good = sig_ok (ti.b, ti.n, si.b, pem);
		free (pem);
		if (!good) { r.say (TR ("signature de l'index fausse : refusé")); return E_SIG; }
		// l'index d'une autre carte (ses programmes ne sont pas pour ARMPI) : refuse
		{
			Ini ii; ii.parse (sdup (ti.b));
			const char *ib = ii.get ("repo", "board", "?");
			if (!eq (ib, PKG_BOARD)) { r.sayf ("ce dépôt est pour la carte « %s », pas pour %s : refusé", ib, PKG_BOARD); return E_SIG; }
		}
		write_file (PKG_VAR "/index.txt", ti.b, ti.n);
		index.ini.parse (ti.take ()); index.build (); haveIndex = verified = true;
		return OK;
	}
	bool load_cached (Report &r)
	{
		if (!index.ini.load (PKG_VAR "/index.txt")) return false;
		index.build (); haveIndex = true; verified = false;
		r.sayf ("(dépôt injoignable : son dernier index, du %s)", index.date);
		return true;
	}
	bool load_cached_quiet () { if (!index.ini.load (PKG_VAR "/index.txt")) return false; index.build (); haveIndex = true; return true; }

	// Une de ses apps tourne ? (son nom -> out) ; les programmes en memoire ne craignent rien, mais une app
	// peut reecrire des fichiers que la mise a jour change. (Le gestionnaire se met a jour lui-meme : voir install.)
	bool running (const Ini &files, const char *section, char *out, int cap)
	{
		Proc p[128]; int n = proc_list (p, 128);
		for (int i = 0; i < files.n; i++)
		{
			if (!eq (files.kv[i].sec, section) || !starts (files.kv[i].key, "apps/")) continue;
			const char *a = files.kv[i].key + 5; const char *dot = strstr (a, ".app/");
			if (!dot || strcmp (dot, ".app/main")) continue;
			char app[40]; int k = (int) (dot - a); if (k > 39) k = 39; memcpy (app, a, (size_t) k); app[k] = 0;
			for (int j = 0; j < n; j++) if (eq (p[j].name, app) && p[j].pid != getpid ()) { cpy (out, app, cap); return true; }
		}
		return false;
	}

	// Les paquets a installer pour `name` (ses besoins d'abord, sans ceux deja la) -> combien ; 0 et *err
	int resolve (const char *name, const Pkg **out, int max, int *err, Report &r, int depth = 0)
	{
		const Pkg *p = index.find (name);
		if (!p) { r.sayf ("%s : pas dans le dépôt", name); *err = E_NOTFOUND; return 0; }
		if (depth > 8) { *err = E_NEEDS; return 0; }
		Need nd[16]; int nn = parse_needs (p->needs, nd, 16), k = 0;
		for (int i = 0; i < nn; i++)
		{
			if (eq (nd[i].name, "abi"))
			{
				if (abi_level () < atoi (nd[i].ver))
				{ r.sayf ("%s demande un système plus récent (abi %s, celui-ci %d) : mettre à jour armpi puis redémarrer", name, nd[i].ver, abi_level ()); *err = E_NEEDS; return 0; }
				continue;
			}
			Inst *in = db.find (nd[i].name);
			if (in && vcmp (in->version (), nd[i].ver) >= 0) continue;
			bool planned = false;
			for (int j = 0; j < k; j++) if (eq (out[j]->name, nd[i].name)) planned = true;
			if (planned) continue;
			const Pkg *q = index.find (nd[i].name);
			if (!q || vcmp (q->version, nd[i].ver) < 0) { r.sayf ("%s demande %s %s : pas dans le dépôt", name, nd[i].name, nd[i].ver); *err = E_NEEDS; return 0; }
			int got = resolve (nd[i].name, out + k, max - k, err, r, depth + 1);
			if (!got && *err) return 0;
			k += got;
		}
		if (k < max) out[k++] = p;
		return k;
	}

	// Telecharger, verifier, installer (ou etager) un paquet
	int install (const Pkg &p, Report &r, bool force = false)
	{
		Inst *old = db.find (p.name);
		if (!p.restart && old && !force && !eq (p.name, "pkgman"))
		{
			char app[40];
			if (running (old->ini, "files", app, sizeof app)) { r.sayf ("%s tourne : le fermer, puis recommencer", app); return E_RUNNING; }
		}
		// l'archive : sa taille et son SHA-256 sont ceux de l'index signe, verifies avant toute ecriture sur la carte
		const char *dl = PKG_DL;
		mkdir (dl, 0777);
		if (!is_dir (dl)) { dl = PKG_DL2; mkdirs (dl); }
		char opk[200]; snprintf (opk, sizeof opk, "%s/%s.opk", dl, p.name);
		char h[65]; u64 got = 0;
		{
			FileSink f (opk);
			if (!f.ok ()) { r.sayf ("%s : écriture impossible de %s", p.name, opk); return E_IO; }
			bool ok = fetch (p.file, f, p.size, r);
			f.sha.hex (h); got = f.n;
			if (!ok) { unlink (opk); return r.cancelled () ? E_CANCEL : E_NET; }
		}
		if (got != p.size || !eq (h, p.sha256)) { unlink (opk); r.sayf ("%s : archive différente de celle de l'index : refusée", p.name); return E_BADPKG; }
		int rc = install_file (opk, &p, old, r);
		unlink (opk);
		if (rc == OK && p.replaces && *p.replaces) drop_replaced (p, r);
		return rc;
	}

	// Les paquets que p remplace (un paquet renomme), retires une fois p en place : leurs fichiers repris par p
	// sont gardes, leur mode passe a p
	void drop_replaced (const Pkg &p, Report &r)
	{
		char names[8][40]; int n = 0;
		for (int i = 0; i < db.n && n < 8; i++) if (in_list (p.replaces, db.v[i]->name)) cpy (names[n++], db.v[i]->name, 40);
		for (int k = 0; k < n; k++)
		{
			Inst *was = db.find (names[k]); if (!was) continue;
			char mode[16]; cpy (mode, was->mode (), sizeof mode);
			if (remove (names[k], false, r, true, true) == OK) { r.sayf ("%s remplace %s", p.name, names[k]); set_mode (p.name, mode); }
		}
	}

	// Installe le .opk de path (p : son entree de l'index, comparee a son manifeste ; 0 : un paquet local)
	int install_file (const char *path, const Pkg *p, Inst *old, Report &r)
	{
		if (!archive_kit ()) { r.say (TR ("ArchiveKit absent")); return E_IO; }
		long a = ar_open ((long) path);
		if (a <= 0) { r.sayf ("%s : archive illisible", path); return E_BADPKG; }
		long mi = ar_find (a, (long) "PKG/manifest.ini");
		long mb = mi >= 0 ? ar_load (a, mi) : 0;
		if (mb <= 0) { ar_close (a); r.sayf ("%s : pas un paquet (PKG/manifest.ini manque)", path); return E_BADPKG; }
		Ini man; man.parse (sdup ((const char *) ar_data (mb)));
		ar_free (mb);
		const char *name = man.get ("", "name"), *ver = man.get ("", "version");
		if (!*name || (p && (!eq (name, p->name) || !eq (ver, p->version))))
		{ ar_close (a); r.sayf ("%s : le manifeste ne correspond pas à l'index", path); return E_BADPKG; }
		bool stage = eq (man.get ("", "restart", "0"), "1");
		if (!old) old = db.find (name);
		long count = ar_count (a);
		// avant toute ecriture : les noms
		for (long i = 0; i < count; i++)
		{
			const char *en = (const char *) ar_name (a, i);
			if (!en || starts (en, "PKG/")) continue;
			size_t el = strlen (en);
			char rel[260]; cpy (rel, en, sizeof rel); if (el && rel[el - 1] == '/') rel[el - 1] = 0;
			if (el >= sizeof rel || forbidden (rel)) { ar_close (a); r.sayf ("%s : nom refusé dans le paquet : %s", name, en); return E_BADPKG; }
		}
		char root[200];
		if (stage) { snprintf (root, sizeof root, PKG_STAGE "/%s", name); remove_tree (root); mkdirs (root); }
		else cpy (root, "SD0:", sizeof root);
		// l'entree de la base
		size_t cap = 4096 + (size_t) count * 120; char *dbt = (char *) malloc (cap); size_t dl = 0;
		auto put = [&] (const char *s) { size_t l = strlen (s); if (dl + l + 1 >= cap) { cap = (dl + l + 1) * 2; dbt = (char *) realloc (dbt, cap); } memcpy (dbt + dl, s, l + 1); dl += l; };
		char line[512];
		put ("# installe par pkg\n[package]\n");
		static const char *const keys[] = { "name", "title", "version", "category", "author", "summary", "needs", "required", "restart", "config", 0 };
		for (int i = 0; keys[i]; i++) { snprintf (line, sizeof line, "%s = %s\n", keys[i], man.get ("", keys[i])); put (line); }
		snprintf (line, sizeof line, "mode = %s\n\n[files]\n", old ? old->mode () : "manual"); put (line);
		const char *cfg = man.get ("", "config");
		size_t mcap = 1024; char *mv = (char *) malloc (mcap); size_t ml = 0; mv[0] = 0;	// etage : [moves], [remove]
		auto putm = [&] (const char *s) { size_t l = strlen (s); if (ml + l + 1 >= mcap) { mcap = (ml + l + 1) * 2; mv = (char *) realloc (mv, mcap); } memcpy (mv + ml, s, l + 1); ml += l; };
		int files = 0, kept = 0; u64 done = 0, total = 0;
		for (long i = 0; i < count; i++) { long s = ar_entry_size (a, i); if (s > 0) total += (u64) s; }
		int rc = OK;
		for (long i = 0; i < count && rc == OK; i++)
		{
			const char *en = (const char *) ar_name (a, i);
			size_t el = en ? strlen (en) : 0;
			if (!el || en[el - 1] == '/' || starts (en, "PKG/")) continue;
			if (r.cancelled ()) { rc = E_CANCEL; break; }
			char rel[260]; cpy (rel, en, sizeof rel);
			char target[300]; cpy (target, rel, sizeof target);
			char card[300]; snprintf (card, sizeof card, "SD0:/%s", rel);
			// le fichier de l'utilisateur : remplace seulement s'il manque, ou tel que le paquet l'a laisse
			bool isCfg = user_tree (rel);
			for (const char *s = cfg; !isCfg && (s = strstr (s, rel)) != 0; s += el) if ((s == cfg || s[-1] == ' ') && (s[el] == 0 || s[el] == ' ')) isCfg = true;
			bool skip = false;
			if (isCfg && exists (card))
			{
				char now[65]; const char *was = old ? old->hash (rel) : 0;
				const char *repl = man.get ("", "replaces");		// (un paquet renomme : le hash de son predecesseur)
				for (int k = 0; !was && *repl && k < db.n; k++) if (in_list (repl, db.v[k]->name)) was = db.v[k]->hash (rel);
				if (!sha_file (card, now) || !was || !eq (now, was))
				{
					kept++;
					if (user_tree (rel)) skip = true;
					else strncat (target, ".new", sizeof target - strlen (target) - 1);
				}
			}
			char dest[320], tmp[330];
			snprintf (dest, sizeof dest, "%s/%s", root, target);
			snprintf (tmp, sizeof tmp, stage ? "%s" : "%s.pkgtmp", dest);
			char hh[65];
			if (skip)
			{
				long s = ar_load (a, i);			// (la base garde le hash du paquet)
				if (s <= 0) { rc = E_BADPKG; r.sayf ("%s : %s abîmé", name, en); break; }
				Sha sh; sh.add ((const void *) ar_data (s), (size_t) ar_bytes (s)); sh.hex (hh); ar_free (s);
			}
			else
			{
				mkparent (tmp);
				long x = ar_extract (a, i, (long) tmp);
				if (x < 0) { unlink (tmp); rc = x == -28 ? E_IO : E_BADPKG; r.sayf ("%s : %s : %s", name, en, (const char *) ar_strerror (x)); break; }
				if (!sha_file (tmp, hh)) { unlink (tmp); rc = E_IO; r.sayf ("écriture impossible : %s", tmp); break; }
				if (!stage && !move (tmp, dest)) { unlink (tmp); rc = E_IO; r.sayf ("remplacement impossible : %s", dest); break; }
			}
			snprintf (line, sizeof line, "%s = %s\n", rel, hh); put (line);
			if (stage && !skip) { snprintf (line, sizeof line, "%s = 1\n", target); if (!ml) putm ("[moves]\n"); putm (line); }
			files++; done += (u64) ar_entry_size (a, i);
			r.step (name, done, total);
		}
		ar_close (a);
		if (rc != OK) { free (dbt); free (mv); if (stage) { char st[200]; snprintf (st, sizeof st, PKG_STAGE "/%s", name); remove_tree (st); } return rc; }
		// les fichiers de l'ancienne version absents de la nouvelle s'en vont (ceux que l'utilisateur a changes restent)
		Ini now; now.parse (sdup (dbt));
		int gone = 0;
		if (old)
			for (int i = 0; i < old->ini.n; i++)
			{
				if (!eq (old->ini.kv[i].sec, "files") || now.get ("files", old->ini.kv[i].key, 0)) continue;
				if (db.owned_elsewhere (old->ini.kv[i].key, name)) continue;
				char card[300]; snprintf (card, sizeof card, "SD0:/%s", old->ini.kv[i].key);
				char hh[65];
				if (old->is_config (old->ini.kv[i].key) && sha_file (card, hh) && !eq (hh, old->ini.kv[i].val)) continue;
				if (stage) { snprintf (line, sizeof line, "%s = 1\n", old->ini.kv[i].key); if (!strstr (mv, "[remove]")) putm ("\n[remove]\n"); putm (line); }
				else { unlink (card); prune (card); }
				gone++;
			}
		char dbp[200];
		if (stage) { put ("\n"); put (mv); snprintf (dbp, sizeof dbp, PKG_STAGE "/%s.ini", name); }
		else snprintf (dbp, sizeof dbp, PKG_DB "/%s.ini", name);
		bool ok = write_file (dbp, dbt, dl);
		if (ok && !stage && strstr (dbt, "\napps/")) appsChanged = true;
		free (dbt); free (mv);
		if (!ok) { r.sayf ("écriture impossible : %s", dbp); return E_IO; }
		r.sayf ("%s %s : %d fichiers %s%s", name, ver, files, TR (stage ? "étagés : redémarrer pour finir" : "installés"),
			kept ? TR (" (vos réglages gardés, les nouveaux à côté en .new)") : "");
		db.load ();
		return OK;
	}

	// Une app ajoutee ou retiree : le panel (et la barre de menus) relus, comme relaunch du panneau de configuration
	void refresh_desktop ()
	{
		if (!appsChanged) return;
		appsChanged = false;
		static const struct { const char *name, *path; } shell[] = { { "panel", "SD0:/sys/panel" }, { "menubar", "SD0:/sys/menubar" } };
		for (auto &s : shell)
		{
			if (!proc_kill (s.name)) continue;
			for (int i = 0; i < 30 && proc_running (s.name); i++) usleep (50000);
			usleep (200000);
			launch (s.path);
		}
	}

	// les dossiers vides au-dessus d'un fichier retire
	static void prune (const char *file)
	{
		char d[320]; cpy (d, file, sizeof d);
		for (;;)
		{
			char *s = strrchr (d, '/');
			if (!s || s[-1] == ':') break;
			*s = 0;
			if (rmdir (d) != 0) break;
		}
	}

	// Retirer un paquet : ses fichiers (un reglage change reste, sauf purge)
	int remove (const char *name, bool purge, Report &r, bool force = false, bool replaced = false)
	{
		Inst *in = db.find (name);
		if (!in) { r.sayf ("%s n'est pas installé", name); return E_NOTINST; }
		if (in->required () && !force) { r.sayf ("%s fait partie du système : impossible de le retirer", name); return E_REQUIRED; }
		for (int i = 0; i < db.n && !replaced; i++)
		{
			Need nd[16]; int nn = parse_needs (db.v[i]->needs (), nd, 16);
			for (int k = 0; k < nn; k++) if (eq (nd[k].name, name)) { r.sayf ("%s en a besoin : retirer d'abord %s", db.v[i]->name, db.v[i]->name); return E_USED; }
		}
		char app[40];
		if (!force && running (in->ini, "files", app, sizeof app)) { r.sayf ("%s tourne : le fermer, puis recommencer", app); return E_RUNNING; }
		int gone = 0, kept = 0;
		for (int i = 0; i < in->ini.n; i++)
		{
			if (!eq (in->ini.kv[i].sec, "files")) continue;
			if (db.owned_elsewhere (in->ini.kv[i].key, name)) continue;
			char card[300]; snprintf (card, sizeof card, "SD0:/%s", in->ini.kv[i].key);
			char hh[65];
			if ((!purge || user_tree (in->ini.kv[i].key)) && in->is_config (in->ini.kv[i].key) && sha_file (card, hh) && !eq (hh, in->ini.kv[i].val)) { kept++; continue; }
			if (starts (in->ini.kv[i].key, "apps/")) appsChanged = true;
			if (unlink (card) == 0) gone++;
			prune (card);
		}
		char dbp[200]; snprintf (dbp, sizeof dbp, PKG_DB "/%s.ini", name);
		unlink (dbp);
		r.sayf ("%s retiré : %d fichiers%s", name, gone, kept ? TR (" (vos réglages changés gardés)") : "");
		db.load ();
		return OK;
	}

	// Ce qui ne repart qu'au prochain demarrage : ce que le firmware lit (a la racine) et init2 lui-meme
	static bool boot_file (const char *f) { return !strchr (f, '/') || eq (f, "sys/svc/init2"); }

	// Les paquets etages mis en place (au demarrage, avant tout le reste : init2)
	int commit (Report &r)
	{
		DIR *d = opendir (PKG_STAGE);
		if (!d) { r.say (TR (exists (PKG_STAGE) ? "dossier des paquets étagés illisible : laissé tel quel" : "rien d'étagé")); return OK; }
		char names[32][64]; int n = 0;
		struct dirent *e;
		while (n < 32 && (e = readdir (d)) != 0)
		{
			int l = (int) strlen (e->d_name);
			if (l > 4 && l < 64 && !strcmp (e->d_name + l - 4, ".ini")) { cpy (names[n], e->d_name, 64); names[n][l - 4] = 0; n++; }
		}
		closedir (d);
		bool left = false;
		for (int k = 0; k < n; k++)
		{
			char ip[200], dir[200]; snprintf (ip, sizeof ip, PKG_STAGE "/%s.ini", names[k]); snprintf (dir, sizeof dir, PKG_STAGE "/%s", names[k]);
			Ini st; if (!st.load (ip)) { r.sayf ("%s : fichier d'étage illisible : laissé étagé", names[k]); left = true; continue; }
			int moved = 0, missing = 0, failed = 0;
			for (int i = 0; i < st.n; i++)
			{
				const char *f = st.kv[i].key;
				if (eq (st.kv[i].sec, "moves"))
				{
					char from[320], to[320]; snprintf (from, sizeof from, "%s/%s", dir, f); snprintf (to, sizeof to, "SD0:/%s", f);
					if (forbidden (f)) { failed++; continue; }
					if (!exists (from)) { missing++; continue; }	// (deja la : un commit interrompu reprend)
					if (boot_file (f))
					{
						kernelChanged = true;
						if ((eq (f, "kernel8.img") || eq (f, "kernel_2712.img")) && exists (to))
						{ char old[320]; snprintf (old, sizeof old, "%s.old", to); rename (to, old); }
					}
					if (move (from, to)) moved++; else failed++;
				}
				else if (eq (st.kv[i].sec, "remove") && !forbidden (f) && !db.owned_elsewhere (f, names[k])) { char c[320]; snprintf (c, sizeof c, "SD0:/%s", f); unlink (c); prune (c); }
			}
			if (failed) { r.sayf ("%s : %d fichiers non déplacés : laissé étagé", names[k], failed); left = true; continue; }
			char dbp[200]; snprintf (dbp, sizeof dbp, PKG_DB "/%s.ini", names[k]);
			mkdirs (PKG_DB);
			rename (ip, dbp);
			remove_tree (dir);
			r.sayf ("%s %s : %d fichiers en place%s", names[k], st.get ("package", "version"), moved, missing ? TR (" (reprise)") : "");
		}
		if (!left) remove_tree (PKG_STAGE);
		db.load ();
		return OK;
	}
	bool staged (const char *name) { char p[200]; snprintf (p, sizeof p, PKG_STAGE "/%s.ini", name); return exists (p); }

	// manual / auto / never
	bool set_mode (const char *name, const char *mode)
	{
		if (!db.find (name)) return false;
		char p[200]; snprintf (p, sizeof p, PKG_DB "/%s.ini", name);
		u64 n; char *t = read_file (p, &n);
		if (!t) return false;
		size_t cap = (size_t) n + 64; char *o = (char *) malloc (cap); size_t ol = 0; bool done = false;
		for (char *s = t; *s; )
		{
			char *e = s; while (*e && *e != '\n') e++;
			size_t l = (size_t) (e - s);
			if (!done && l >= 4 && !strncmp (s, "mode", 4) && (s[4] == ' ' || s[4] == '='))
			{ ol += (size_t) snprintf (o + ol, cap - ol, "mode = %s\n", mode); done = true; }
			else { memcpy (o + ol, s, l); ol += l; o[ol++] = '\n'; }
			s = *e ? e + 1 : e;
		}
		free (t);
		bool ok = write_file (p, o, ol);
		free (o);
		db.load ();
		return ok;
	}
	// une mise a jour pour celui-ci ? -> l'entree de l'index
	const Pkg *update_for (const Inst &in) const
	{
		const Pkg *p = index.find (in.name);
		if (p) return vcmp (p->version, in.version ()) > 0 ? p : 0;
		for (int i = 0; i < index.n; i++) if (index.p[i].replaces && in_list (index.p[i].replaces, in.name)) return &index.p[i];
		return 0;
	}
};

} // namespace pkg

#endif
