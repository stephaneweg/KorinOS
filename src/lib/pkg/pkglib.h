//
// pkglib.h -- les paquets d'ARMPI (docs/PAQUETS.md), port de user/Libs/pkg/pkglib.h d'Onyx : ce que partagent la
// commande pkg, le gestionnaire pkgman et le service pkgd. Un seul fichier d'en-tete, une seule unite de traduction.
// C++ sur libposix, OpenSSL (TLS, SHA-256, signature ECDSA de l'index ; le seul TLS de l'OS, docs/OPENSSL.md),
// ArchiveKit (les archives ZIP).
//
// Le depot : index.txt (une section par paquet : version, taille, sha256, fichier, besoins...) et index.sig (sa
// signature ECDSA P-256 / SHA-256, DER en hexa), verifiee avec /etc/pkg/onyx.pub ; les paquets
// pkgs/<nom>-<version>.opk : des ZIP de l'arborescence de la carte plus PKG/manifest.ini. Un depot est une URL
// http(s) ou un dossier (/... : les essais, une carte sans reseau).
// La carte : /etc/pkg/pkg.ini (repo =, key =, check =) ; /var/pkg/db/<nom>.ini un paquet installe ([package],
// mode manual|auto|never ; [files] : chaque fichier et son SHA-256) ; /var/pkg/index.txt le dernier index dont
// la signature etait bonne ; /var/pkg/stage/ les paquets qui attendent le prochain demarrage (restart = 1 :
// le systeme, le firmware), mis en place par `pkg commit` (lance par init2 avant tout le reste).
//
// Rien hors de / et /boot ; rien sous var/, ni etc/wifi*, ni etc/remote* ; home/ et les fichiers `config =` ne sont
// jamais ecrases.
// Les fichiers de demarrage (`boot =` du manifeste, des motifs : kernel8.img *.mod config.txt overlays/...) vont dans /boot/,
// la vraie racine de la partition de demarrage (docs/FICHIERS.md, « Le volume /boot ») ; le reste dans / (qui peut
// avoir un rootpath, /korinARM). /boot n'est monte qu'a la demande (VFS_BOOT_BEGIN : la ligne « BOOT = ..., ondemand »
// de /etc/mounts.txt), le temps d'une installation, d'un retrait ou d'un commit, puis rendu (VFS_BOOT_END). Sans
// cette ligne : la racine /, comme avant.
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
#include <armpi/syscall.h>
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
// Le source est en francais : lang/en.txt, fusionne dans /res/lang (tools/lang/merge.py). Ni UIKit ici : lang_tr d'AppKit, par son rang.
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

#define PKG_CONF	"/etc/pkg/pkg.ini"
#define PKG_KEY		"/etc/pkg/onyx.pub"
#define PKG_ABI		"/etc/pkg/abi"			// la somme des tables des kits (tools/pkg/mkrepo.py)
#define PKG_PRELOAD	"/etc/preload.txt"
// Le depot et la carte : un index par plateforme, signe par la meme cle (docs/PAQUETS.md, « Le PC ») : le Pi a la
// racine de packages/ (board = armpi, paquet systeme armpi), le PC dans packages/pc/ (board = pc, paquet systeme pc).
#if defined (__x86_64__)
#define PKG_REPO	"https://stephaneweg.github.io/KorinOS/packages/pc"
#define PKG_BOARD	"pc"
#define PKG_SYSTEM	"pc"
#else
#define PKG_REPO	"https://stephaneweg.github.io/KorinOS/packages"
#define PKG_BOARD	"armpi"
#define PKG_SYSTEM	"armpi"
#endif
#define PKG_VAR		"/var/pkg"
#define PKG_DB		"/var/pkg/db"
#define PKG_STAGE	"/var/pkg/stage"
#define PKG_AB_COPY	"/var/pkg/ab.txt"	// (l'état A/B du démarrage en cours, écrit par pkg bootok)
#define PKG_DL		"/tmp/pkg"				// les telechargements (verifies, puis extraits) hors uid 0
#define PKG_DL0		"/tmp/0/pkg"				// en uid 0 (pkgd, sudo pkg) : /tmp/0, que vfs reserve a l'uid 0
								// dans tous les modes ; /tmp/pkg, commun, laissait un compte
								// remplacer l'archive verifiee avant son extraction (K1, E51)
#define PKG_DL2		"/var/pkg/dl"			// (sans /tmp)
#define PKG_COMMIT_LOG	"/var/pkg/commit.log"

// --root : la racine du système que pkg gère (« /media/tgt » : osinstall installe dans le disque en cours d'installation ;
// 18/11/2026). Vide : « / ». Avec une racine, pkg lit /etc/pkg, la base et l'index de cette racine, y installe sans
// étager (le système qu'on installe ne tourne pas) et ne relance pas le bureau.
static char g_pkgroot[160];
static inline void pkg_set_root (const char *r)
{
	snprintf (g_pkgroot, sizeof g_pkgroot, "%s", r && strcmp (r, "/") ? r : "");
	size_t l = strlen (g_pkgroot);
	while (l && g_pkgroot[l - 1] == '/') g_pkgroot[--l] = 0;
}
// un chemin du système -> le même sous la racine (huit tampons tournants)
static inline const char *RP (const char *path)
{
	if (!g_pkgroot[0]) return path;
	static char b[8][400]; static int k;
	char *o = b[k++ & 7];
	snprintf (o, sizeof b[0], "%s%s", g_pkgroot, path);
	return o;
}

// ---- appels systeme, comme src/apps/mail/main.cpp (Pi : svc 0 ; PC : syscall) -----------------------------------------
static inline long sys_call (long n, long a, long b) { return armpi_syscall (n, a, b, 0, 0, 0, 0); }
enum { SYS_KILL_ = 0x14, SYS_PROCLIST_ = 0x15, SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32, SYS_SPAWN_ = 0x8B, SYS_PRELOAD_ = 0x8D,
       SYS_SPAWN_AS_ = 0x8F, SYS_GETUID_ = 0x90, SYS_PROC_UID_ = 0x92 };
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

// ---- les fichiers de demarrage (/boot) ------------------------------------------------------------------------------------
// un motif de `boot =` (comme matches de mkrepo.py) : "dossier/" (tout dedans), "*" et "?" (fnmatchcase : * passe les /),
// sinon le nom exact
static inline bool glob_match (const char *p, const char *s)
{
	for (; *p; p++, s++)
	{
		if (*p == '*')
		{
			while (p[1] == '*') p++;
			if (!p[1]) return true;
			for (; *s; s++) if (glob_match (p + 1, s)) return true;
			return false;
		}
		if (!*s || (*p != '?' && *p != *s)) return false;
	}
	return !*s;
}
// "kernel8.img *.mod overlays/" contient-il rel ?
static inline bool in_patterns (const char *pats, const char *rel)
{
	for (const char *s = pats; s && *s; )
	{
		while (*s == ' ' || *s == ',' || *s == '\t') s++;
		const char *e = s; while (*e && *e != ' ' && *e != ',' && *e != '\t') e++;
		size_t l = (size_t) (e - s);
		if (l)
		{
			char pat[128]; if (l >= sizeof pat) l = sizeof pat - 1;
			memcpy (pat, s, l); pat[l] = 0;
			if (pat[l - 1] == '/') { if (!strncmp (rel, pat, l)) return true; }
			else if (strpbrk (pat, "*?")) { if (glob_match (pat, rel)) return true; }
			else if (!strcmp (pat, rel)) return true;
		}
		s = e;
	}
	return false;
}
// le dossier des fichiers de demarrage : "/boot" (vfs le monte a la demande : VFS_BOOT_BEGIN), sinon "" (la racine) ; demande au
// premier fichier de demarrage, rendu par boot_release (BootScope)
extern "C" long __armpi_vfs_call (void *m);
struct VfsMsg { long op, a, b, c, d; unsigned len, pid; unsigned char data[464]; };
enum { VFS_BOOT_BEGIN_ = 50, VFS_BOOT_END_ = 51 };
static char g_bootVol[8];
static bool g_bootTaken, g_bootAsked, g_bootFail;
static inline const char *boot_volume ()
{
	if (!g_bootAsked)
	{
		VfsMsg m; memset (&m, 0, sizeof m); m.op = VFS_BOOT_BEGIN_;
		long r = __armpi_vfs_call (&m);
		g_bootAsked = true;
		// refus de vfs (pas l'uid 0, /boot absent ou indisponible) : les fichiers de demarrage ne vont PAS
		// a la racine (un pkg non eleve y ecrivait le noyau, plan de securite E25) ; boot_refused () le dit
		g_bootFail = r < 0;
		if (r < 0 || !m.data[0]) g_bootVol[0] = 0;		// (la racine du volume systeme : pas de /boot decrit)
		else { cpy (g_bootVol, (const char *) m.data, sizeof g_bootVol); g_bootTaken = m.a != 0; }
	}
	return g_bootVol;
}
// /boot rendu (vfs le demonte a la derniere demande)
static inline void boot_release ()
{
	if (g_bootTaken) { VfsMsg m; memset (&m, 0, sizeof m); m.op = VFS_BOOT_END_; __armpi_vfs_call (&m); g_bootTaken = false; }
	g_bootVol[0] = 0;
	g_bootAsked = false;
	g_bootFail = false;
}
// un fichier de demarrage (motifs boot =) a ecrire alors que vfs a refuse /boot ?
static inline bool boot_refused (const char *bootPats, const char *rel)
{
	if (g_pkgroot[0] || !in_patterns (bootPats, rel)) return false;
	boot_volume ();
	return g_bootFail;
}
struct BootScope { ~BootScope () { boot_release (); } };

// rel (chemin de la carte), motifs `boot =` du paquet -> "/boot/rel" ou "/rel"
static inline void card_path (char *out, size_t cap, const char *bootPats, const char *rel)
{
	snprintf (out, cap, "%s/%s", g_pkgroot[0] ? g_pkgroot : in_patterns (bootPats, rel) ? boot_volume () : "", rel);
}
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

// ---- A/B (docs/etude/MISES-A-JOUR-AB.md, décision du 25/11/2026) : le noyau et l'initrd du PC dans deux emplacements,
// \korin\a et \korin\b du volume de démarrage, et \korin\ab.txt (lu et décompté par le chargeur, ab.inc) :
// « confirmed=a trial=b tries=3 booted=a ». Une mise à jour s'écrit dans l'emplacement en essai ; la version actuelle
// ne devient la version de secours que sur la réponse de root (g_abKeep, posée par pkg ou pkgman avant l'étage, notée
// « ab_keep = 1 » dans la fiche d'étage), et seulement si c'est bien la version en essai qui a démarré.
static int g_abKeep;
static inline bool ab_file (const char *f) { return eq (f, "korin/kernel.elf") || eq (f, "korin/initrd.tar"); }
struct AbState { char conf, trial, tries, booted; };
static inline bool ab_read (const char *path, AbState &s)
{
	char *t = read_file (path);
	bool ok = t && strlen (t) >= 36 && !strncmp (t, "confirmed=", 10) && !strncmp (t + 11, " trial=", 7) && !strncmp (t + 19, " tries=", 7)
		&& !strncmp (t + 27, " booted=", 8) && (t[10] == 'a' || t[10] == 'b') && (t[18] == 'a' || t[18] == 'b' || t[18] == '-')
		&& t[26] >= '0' && t[26] <= '9' && (t[35] == 'a' || t[35] == 'b' || t[35] == '-');
	if (ok) { s.conf = t[10]; s.trial = t[18]; s.tries = t[26]; s.booted = t[35]; }
	free (t);
	return ok;
}
static inline bool ab_write (const char *path, const AbState &s)
{
	char b[40]; int n = snprintf (b, sizeof b, "confirmed=%c trial=%c tries=%c booted=%c\r\n", s.conf, s.trial, s.tries, s.booted);
	char tmp[220]; snprintf (tmp, sizeof tmp, "%s.new", path);
	return write_file (tmp, b, (u64) n) && rename (tmp, path) == 0;
}
// la version qui tourne est-elle la version en essai (donc à confirmer, sur la réponse de root) ?
static inline bool ab_can_confirm ()
{
	if (g_pkgroot[0]) return false;
	char p[200]; snprintf (p, sizeof p, "%s/korin/ab.txt", boot_volume ());
	AbState s; bool r = ab_read (p, s) && s.trial != '-' && s.trial != s.conf && s.booted == s.trial;
	boot_release ();
	return r;
}
// un démarrage réussi (init2, la session ouverte) : la version en essai retrouve ses trois essais. Ce n'est pas une
// confirmation (elle reste en essai, la version de secours ne change pas) : sans cela, le chargeur, qui décompte
// chaque démarrage d'essai, la quitterait au quatrième démarrage même si elle marche
static inline int ab_boot_ok ()
{
	char p[200]; snprintf (p, sizeof p, "%s/korin/ab.txt", boot_volume ());
	AbState s; int r = 1;
	bool ok = !g_bootFail && ab_read (p, s);
	if (ok && s.trial != '-' && s.trial != s.conf && s.booted == s.trial && s.tries != '3')
	{
		s.tries = '3';
		r = ab_write (p, s) ? 0 : 2;
	}
	boot_release ();
	// une copie lisible par tous (/boot ne l'est qu'à l'uid 0) : Logiciels y voit s'il faut poser la question de secours
	if (ok) ab_write (PKG_AB_COPY, s); else unlink (PKG_AB_COPY);
	return r;
}
// un fichier est-il un chargeur de Korin ? (ses messages : « boot: \korin\boot.cfg ») ; un autre (Windows,
// Linux) n'est jamais remplacé
static inline bool korin_loader (const char *path)
{
	u64 n = 0; char *b = read_file (path, &n);
	static const char mark[] = "boot: \\korin\\boot.cfg";
	bool ok = false;
	for (u64 i = 0; b && i + sizeof mark - 1 <= n && !ok; i++) ok = !memcmp (b + i, mark, sizeof mark - 1);
	free (b);
	return ok;
}
static inline bool copy_plain (const char *from, const char *to)
{
	u64 n = 0; char *b = read_file (from, &n);
	bool ok = b && write_file (to, b, n);
	free (b);
	return ok;
}
// Une liste de lignes changee par l'utilisateur (`merge =` du manifeste, services.txt) + celle du paquet -> malloc :
// la sienne entiere, puis chaque ligne du paquet (hors commentaire) qu'elle n'a pas, meme en commentaire (« # ligne » :
// coupee expres), avec les lignes de commentaire qui la precedent dans le paquet ; 0 : rien a ajouter
static inline const char *ml_norm (const char *s, const char *e, size_t *n)
{
	while (s < e && (*s == ' ' || *s == '\t' || *s == '#')) s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
	*n = (size_t) (e - s);
	return s;
}
static inline char *merge_lines (const char *mine, const char *pkg)
{
	size_t ml = strlen (mine), cap = ml + strlen (pkg) + 2, ol = ml;
	char *out = (char *) malloc (cap + 1);
	if (!out) return 0;
	memcpy (out, mine, ml);
	bool added = false;
	const char *blk = pkg;				// le debut du bloc de commentaire en cours
	for (const char *l = pkg; *l; )
	{
		const char *e = strchr (l, '\n'); if (!e) e = l + strlen (l);
		const char *nx = *e ? e + 1 : e;
		const char *t = l; while (t < e && (*t == ' ' || *t == '\t' || *t == '\r')) t++;
		if (t == e) { blk = nx; l = nx; continue; }			// ligne vide : le bloc recommence
		if (*t == '#') { l = nx; continue; }
		size_t kn; const char *k = ml_norm (l, e, &kn);
		bool have = false;
		for (const char *m = mine; *m && !have; )
		{
			const char *me = strchr (m, '\n'); if (!me) me = m + strlen (m);
			size_t mn; const char *mk = ml_norm (m, me, &mn);
			have = mn == kn && !memcmp (mk, k, kn);
			m = *me ? me + 1 : me;
		}
		if (!have)
		{
			if (ol && out[ol - 1] != '\n') out[ol++] = '\n';
			memcpy (out + ol, blk, (size_t) (nx - blk)); ol += (size_t) (nx - blk);
			if (out[ol - 1] != '\n') out[ol++] = '\n';
			added = true;
		}
		blk = nx; l = nx;
	}
	if (!added) { free (out); return 0; }
	out[ol] = 0;
	return out;
}
// Un programme epingle (/etc/preload.txt) : vfs oublie son image quand le fichier change, elle est rechargee
static inline void repin (const char *path)
{
	char *t = read_file (RP (PKG_PRELOAD));
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
// Le niveau des tables de kits du systeme en service (/etc/pkg/abi, du paquet armpi : il change au commit) :
// ce qu'etait `kapi` sur Onyx. Un paquet construit contre des tables plus grandes attend le systeme.
static inline int abi_level ()
{
	char *t = read_file (RP (PKG_ABI));
	int v = t ? atoi (t) : 0;
	free (t);
	return v;
}

// ---- l'index du depot ---------------------------------------------------------------------------------------------------
struct Pkg
{
	const char *name, *title, *version, *category, *author, *summary, *file, *sha256, *icon, *needs, *replaces;
	const char *part;		// une partie d'un gros paquet (split) : ce paquet ; "" : un paquet a part entiere
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
			k.part = ini.get (s, "part");
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
	// chaque composant : ni vide, ni « . » ni « .. », ni fini par un point ou une espace (FAT les ignore) :
	// « ./etc/shadow.ini », « etc//keyvault », « etc./shadow.ini » passaient les comparaisons plus bas (L4)
	for (const char *s = rel; s; )
	{
		const char *e = strchr (s, '/');
		size_t n = e ? (size_t) (e - s) : strlen (s);
		if (!n ? e != 0 : (s[n - 1] == '.' || s[n - 1] == ' ')) return true;	// (un « / » final : dossier, permis)
		s = e ? e + 1 : 0;
	}
	// (les mots de passe, les clés du coffre, le repère de la carte : aucun paquet ne les livre ; un paquet ne les écrase
	// pas, L4 ; sans casse : FAT n'en tient pas compte, « VAR/pkg/... » écrivait /var/pkg). Du dossier etc/keyvault,
	// seulement les clés de la machine : le paquet pc livre etc/keyvault/interpreters.ini, et le refuser laissait
	// le paquet étagé à chaque démarrage (26/11/2026 : l'A/B jamais finalisé, l'écran gris du ZBook)
	static const char *const no[] = { "var/", "etc/wifi", "etc/remote", "etc/shadow", "etc/keyvault/machine", "etc/secure-mark", 0 };
	for (int i = 0; no[i]; i++) if (!strncasecmp (rel, no[i], strlen (no[i]))) return true;
	return !strcasecmp (rel, "var");
}

// ---- les paquets installes (/var/pkg/db) --------------------------------------------------------------------------------
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
			char p[300]; snprintf (p, sizeof p, "/%s", k);
			if (!exists (p)) missing = true;
		}
	}
	const char *mode () const { return ini.get ("package", "mode", "manual"); }
	const char *boot () const { return ini.get ("package", "boot"); }	// ses fichiers de demarrage (motifs)
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
		DIR *d = opendir (RP (PKG_DB));
		if (!d) return;
		struct dirent *e;
		while ((e = readdir (d)) != 0)
		{
			int l = (int) strlen (e->d_name);
			if (l < 5 || strcmp (e->d_name + l - 4, ".ini")) continue;
			Inst *x = new Inst;
			char p[400]; snprintf (p, sizeof p, "%s/%s", RP (PKG_DB), e->d_name);
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

struct Url { char scheme[8], host[128], path[2048]; int port; };	// (Release : redirection de pres de 1 Ko)
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
	char url[2176]; cpy (url, first, sizeof url);
	for (int hop = 0; hop < 6; hop++)
	{
		Url u;
		if (!url_parse (url, u)) { r.sayf ("adresse illisible : %s", url); return false; }
		bool tls = eq (u.scheme, "https");
		Conn c (r);
		if (!c.begin (u.host, u.port, tls)) return false;
		char q[2400];
		int n = snprintf (q, sizeof q, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Korin-pkg/1.0\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", u.path, u.host);
		if (!c.send_all (q, n)) { r.sayf ("%s : envoi impossible", u.host); return false; }
		char l[2304];
		if (!c.line (l, sizeof l) || strncmp (l, "HTTP/", 5)) { r.sayf ("%s : réponse illisible", u.host); return false; }
		const char *sp = strchr (l, ' ');
		int status = sp ? atoi (sp + 1) : 0;
		long long clen = -1; bool chunked = false; char loc[2176]; loc[0] = 0;
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
			char was[2176]; cpy (was, url, sizeof was);
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
// un programme lance, sans attendre (AppKit passe par init2 pour /sys) -> false : echec
static inline bool launch (const char *path, const char *arg = 0)
{
	return __armpi_fsys (SYS_SPAWN_, (long) path, (long) arg, 0) > 0;
}
// l'uid du process de ce nom (le premier) -> uid, -1 : aucun (ou pas d'uid dans le noyau : le Pi)
static inline long proc_uid (const char *name)
{
	Proc p[128]; int n = proc_list (p, 128); long self = getpid ();
	for (int i = 0; i < n; i++)
		if (p[i].pid != self && eq (p[i].name, name)) { long u = sys_call (SYS_PROC_UID_, p[i].pid, 0); return u < 0 ? -1 : u; }
	return -1;
}
// un programme lance sous cet uid quand l'appelant est le systeme (uid 0 : pkgd, pkg eleve ; SYS_SPAWN_AS
// d'AppKit) ; sinon (uid <= 0, un autre appelant, le Pi) : launch
static inline bool launch_as (const char *path, long uid)
{
	if (uid > 0 && armpi_syscall (SYS_GETUID_, 0, 0, 0, 0, 0, 0) == 0)
	{
		long r = __armpi_fsys (SYS_SPAWN_AS_, (long) path, 0, uid);
		if (r != -38)			// (E_NOSYS)
			return r > 0;
	}
	return launch (path);
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
// l'avis korin.apps.changed sur le bus (busd : docs/BUS.md ; BUS_OP_PUBLISH, valeur : l'heure en ms, pas gardee)
// -> nombre d'abonnes prevenus (busd), 0 ou moins : personne, ou pas de busd
static inline long apps_changed_notice ()
{
	long ch = sys_call (SYS_SVC_OPEN_, (long) "bus", 500);
	if (ch < 0) return -1;
	char m[512]; memset (m, 0, sizeof m);
	*(long *) m = 1;				// BUS_OP_PUBLISH ; +8 drapeaux : aucun (pas gardee)
	static const char topic[] = "korin.apps.changed";
	memcpy (m + 48, topic, sizeof topic);
	int n = snprintf (m + 48 + sizeof topic, 64, "%ld", sys_call (0x19, 0, 0));	// (SYS_TIME_MS)
	*(long *) (m + 16) = n;				// la longueur de la valeur
	*(unsigned *) (m + 40) = (unsigned) (sizeof topic + (size_t) n);
	return sys_call (SYS_SVC_CALL_, ch, (long) m);
}
// la machine redemarre (INIT_OP_REBOOT d'init2)
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
		Ini c; c.load (RP (PKG_CONF));
		cpy (repo, c.get ("", "repo", PKG_REPO), sizeof repo);
		cpy (key, RP (c.get ("", "key", PKG_KEY)), sizeof key);
		db.load ();
	}

	bool local_repo () const { return !starts (repo, "http://") && !starts (repo, "https://"); }
	// rel du depot -> sink
	// (une adresse complete, « https://… » : telle quelle ; les gros paquets sont dans une Release)
	bool fetch (const char *rel, Sink &sink, u64 want, Report &r)
	{
		if (strstr (rel, "://")) return http_get (rel, sink, want, strrchr (rel, '/') + 1, r);
		char url[600]; cpy (url, repo, sizeof url);
		int l = (int) strlen (url); while (l && url[l - 1] == '/') url[--l] = 0;
		// « ../pkgs/x.opk » : un paquet sans code machine, publie une seule fois pour toutes les plateformes, que
		// l'index du PC (packages/pc/) cite dans celui du Pi (packages/pkgs/) ; resolu ici, le serveur ne voit pas « .. »
		while (starts (rel, "../"))
		{
			char *s = strrchr (url, '/'); const char *sch = strstr (url, "://");
			if (!s || (sch && s <= sch + 2)) { r.sayf ("%s : hors du dépôt", rel); return false; }
			*s = 0; rel += 3;
		}
		if (strstr (rel, "..")) { r.sayf ("%s : hors du dépôt", rel); return false; }
		l = (int) strlen (url); url[l++] = '/'; url[l] = 0;
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

	// L'index du depot, sa signature verifiee (puis garde en /var/pkg/index.txt)
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
		// l'index d'une autre carte (ses programmes ne sont pas pour celle-ci : le Pi, le PC) : refuse
		{
			Ini ii; ii.parse (sdup (ti.b));
			const char *ib = ii.get ("repo", "board", "?");
			if (!eq (ib, PKG_BOARD)) { r.sayf ("ce dépôt est pour la carte « %s », pas pour %s : refusé", ib, PKG_BOARD); return E_SIG; }
			// (K2) le numéro de série de l'index, signé avec lui : jamais plus petit que le dernier vu pour ce dépôt ;
			// un ancien index signé (un miroir, un dépôt local périmé) gelait les mises à jour de sécurité. Sans
			// numéro : 0 (les index d'avant)
			unsigned long long ser = strtoull (ii.get ("repo", "serial", "0"), 0, 10), last = 0;
			char line[700]; snprintf (line, sizeof line, "%s = ", repo);
			char *seen = read_file (RP (PKG_VAR "/serial.txt"));
			for (char *l = seen; l && *l; )
			{
				char *e = strchr (l, '\n'); if (e) *e = 0;
				if (starts (l, line)) last = strtoull (l + strlen (line), 0, 10);
				l = e ? e + 1 : 0;
			}
			free (seen);
			if (ser < last) { r.sayf ("l'index de ce dépôt (n° %llu) est plus ancien que le dernier vu (n° %llu) : refusé", ser, last); return E_SIG; }
			if (ser > last)
			{
				u64 ol = 0;
				char *old = read_file (RP (PKG_VAR "/serial.txt"), &ol);
				char *out = (char *) malloc (ol + 800); size_t k = 0;
				for (char *l = old; out && l && *l; )
				{
					char *e = strchr (l, '\n'); if (e) *e = 0;
					if (*l && !starts (l, line)) { size_t n = strlen (l); memcpy (out + k, l, n); k += n; out[k++] = '\n'; }
					l = e ? e + 1 : 0;
				}
				free (old);
				if (out)
				{
					k += (size_t) snprintf (out + k, 790, "%s%llu\n", line, ser);
					write_file (RP (PKG_VAR "/serial.txt"), out, k);
					free (out);
				}
			}
		}
		write_file (RP (PKG_VAR "/index.txt"), ti.b, ti.n);
		index.ini.parse (ti.take ()); index.build (); haveIndex = verified = true;
		return OK;
	}
	bool load_cached (Report &r)
	{
		if (!index.ini.load (RP (PKG_VAR "/index.txt"))) return false;
		index.build (); haveIndex = true; verified = false;
		r.sayf ("(dépôt injoignable : son dernier index, du %s)", index.date);
		return true;
	}
	bool load_cached_quiet () { if (!index.ini.load (RP (PKG_VAR "/index.txt"))) return false; index.build (); haveIndex = true; return true; }

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
		if (!depth && *p->part) { r.sayf ("%s fait partie de %s : installer %s", name, p->part, p->part); *err = E_NOTFOUND; return 0; }
		if (depth > 8) { *err = E_NEEDS; return 0; }
		Need nd[16]; int nn = parse_needs (p->needs, nd, 16), k = 0;
		for (int i = 0; i < nn; i++)
		{
			if (eq (nd[i].name, "abi"))
			{
				if (abi_level () < atoi (nd[i].ver))
				{ r.sayf ("%s demande un système plus récent (abi %s, celui-ci %d) : mettre à jour %s puis redémarrer", name, nd[i].ver, abi_level (), PKG_SYSTEM); *err = E_NEEDS; return 0; }
				continue;
			}
			Inst *in = db.find (nd[i].name);
			const Pkg *pt = index.find (nd[i].name);
			bool part = pt && eq (pt->part, name);		// (une de ses parties : la version de l'index)
			if (in && vcmp (in->version (), nd[i].ver) >= 0 && !(part && vcmp (pt->version, in->version ()) > 0)) continue;
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
		if (!*p.part)				// ses parties absentes ou plus anciennes d'abord
		{
			int nparts = 0;
			for (int i = 0; i < index.n; i++)
			{
				const Pkg &q = index.p[i];
				if (!eq (q.part, p.name)) continue;
				nparts++;
				Inst *pi = db.find (q.name);
				if (!force && pi && !pi->missing && vcmp (q.version, pi->version ()) <= 0) continue;
				int rc = install (q, r, force);
				if (rc != OK) return rc;
			}
			Inst *was = db.find (p.name);			// (seules ses parties changeaient)
			if (nparts && was && !was->missing && !force && !vcmp (was->version (), p.version)) return OK;
		}
		Inst *old = db.find (p.name);
		if (!p.restart && old && !force && !eq (p.name, "pkgman"))
		{
			char app[40];
			if (running (old->ini, "files", app, sizeof app)) { r.sayf ("%s tourne : le fermer, puis recommencer", app); return E_RUNNING; }
		}
		// l'archive : sa taille et son SHA-256 sont ceux de l'index signe, verifies avant toute ecriture sur la carte
		const char *dl = getuid () == 0 ? PKG_DL0 : PKG_DL;
		mkdirs (dl);
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
		bool name_ok = *name && strlen (name) < 40 && name[0] != '.';	// [a-z0-9._+-] : le nom fait des chemins
		for (const char *c = name; *c; c++)				// (/var/pkg/stage/<nom>, db/<nom>.ini : K4)
			if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '+' || *c == '-')) name_ok = false;
		if (!name_ok) { ar_close (a); r.sayf ("%s : nom de paquet refusé", path); return E_BADPKG; }
		if (!*name || (p && (!eq (name, p->name) || !eq (ver, p->version))))
		{ ar_close (a); r.sayf ("%s : le manifeste ne correspond pas à l'index", path); return E_BADPKG; }
		bool stage = !g_pkgroot[0] && eq (man.get ("", "restart", "0"), "1");	// (une autre racine : directement)
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
		const char *bootPats = man.get ("", "boot");		// (ses fichiers de demarrage : /boot/)
		BootScope bootScope;					// (/boot rendu en sortant)
		char root[200];
		if (stage) { snprintf (root, sizeof root, PKG_STAGE "/%s", name); remove_tree (root); mkdirs (root); }
		else snprintf (root, sizeof root, "%s", g_pkgroot);	// (la racine : « /cible », ou --root)
		// l'entree de la base
		size_t cap = 4096 + (size_t) count * 120; char *dbt = (char *) malloc (cap); size_t dl = 0;
		auto put = [&] (const char *s) { size_t l = strlen (s); if (dl + l + 1 >= cap) { cap = (dl + l + 1) * 2; dbt = (char *) realloc (dbt, cap); } memcpy (dbt + dl, s, l + 1); dl += l; };
		char line[512];
		put ("# installe par pkg\n[package]\n");
		static const char *const keys[] = { "name", "title", "version", "category", "author", "summary", "needs", "required", "restart", "config", "fetch", 0 };
		for (int i = 0; keys[i]; i++) { snprintf (line, sizeof line, "%s = %s\n", keys[i], man.get ("", keys[i])); put (line); }
		if (*bootPats) { put ("boot = "); put (bootPats); put ("\n"); }
		if (stage && g_abKeep) put ("ab_keep = 1\n");		// (la réponse de root : la version actuelle gardée de secours)
		if (p && *p->part) { snprintf (line, sizeof line, "part = %s\n", p->part); put (line); }
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
			char card[300]; card_path (card, sizeof card, bootPats, rel);
			// le fichier de l'utilisateur : remplace seulement s'il manque, ou tel que le paquet l'a laisse
			bool isCfg = user_tree (rel);
			for (const char *s = cfg; !isCfg && (s = strstr (s, rel)) != 0; s += el) if ((s == cfg || s[-1] == ' ') && (s[el] == 0 || s[el] == ' ')) isCfg = true;
			bool skip = false, merge = false;
			if (isCfg && exists (card))
			{
				char now[65]; const char *was = old ? old->hash (rel) : 0;
				const char *repl = man.get ("", "replaces");		// (un paquet renomme : le hash de son predecesseur)
				for (int k = 0; !was && *repl && k < db.n; k++) if (in_list (repl, db.v[k]->name)) was = db.v[k]->hash (rel);
				if (!sha_file (card, now) || !was || !eq (now, was))
				{
					if (user_tree (rel)) { kept++; skip = true; }
					else if (in_list (man.get ("", "merge"), rel)) merge = true;	// (ses lignes + les nouvelles du paquet)
					else { kept++; strncat (target, ".new", sizeof target - strlen (target) - 1); }
				}
			}
			char dest[320], tmp[330];
			if (!stage && boot_refused (bootPats, rel)) { rc = E_IO; r.sayf ("%s : %s : /boot refusé par vfs (élever pkg)", name, rel); break; }
			if (!stage && in_patterns (bootPats, rel)) snprintf (dest, sizeof dest, "%s/%s", g_pkgroot[0] ? root : boot_volume (), target);
			else snprintf (dest, sizeof dest, "%s/%s", root, target);
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
				if (merge)		// (la base garde le hash du paquet : la liste reste « changée »)
				{
					char *mine = read_file (card), *theirs = read_file (tmp);
					char *both = mine && theirs ? merge_lines (mine, theirs) : 0;
					const char *put_ = both ? both : mine;		// (rien de nouveau : la sienne telle quelle)
					bool ok = put_ && write_file (tmp, put_, strlen (put_));
					if (both) r.sayf ("%s : %s", rel, TR ("vos lignes gardées, les nouvelles du paquet ajoutées"));
					free (mine); free (theirs); free (both);
					if (!ok) { unlink (tmp); rc = E_IO; r.sayf ("écriture impossible : %s", tmp); break; }
				}
				if (!stage && !move (tmp, dest)) { unlink (tmp); rc = E_IO; r.sayf ("remplacement impossible : %s", dest); break; }
			}
			snprintf (line, sizeof line, "%s = %s\n", rel, hh); put (line);
			if (stage && !skip) { snprintf (line, sizeof line, "%s = 1\n", target); if (!ml) putm ("[moves]\n"); putm (line); }
			files++; done += (u64) ar_entry_size (a, i);
			r.step (name, done, total);
		}
		ar_close (a);
		// fetch = CHEMIN TAILLE SHA256 URL [; …] : des fichiers que le paquet ne porte pas (le logiciel d'un éditeur qui
		// ne se redistribue pas : Claude Code, docs/etude/LINUX-HOST.md), téléchargés en HTTPS à l'installation, de la
		// taille et du SHA-256 du manifeste (signé avec l'index) ; inscrits dans la base, retirés avec le paquet. Un
		// fichier déjà là, du même SHA-256, n'est pas retéléchargé (une mise à jour qui ne change que le paquet).
		const char *fx = man.get ("", "fetch");
		if (rc == OK && *fx && stage) { rc = E_BADPKG; r.sayf ("%s : fetch et restart ensemble : refusé", name); }
		for (const char *f = fx; rc == OK && *f; )
		{
			while (*f == ' ' || *f == ';') f++;
			if (!*f) break;
			const char *e = strchr (f, ';'); size_t fl = e ? (size_t) (e - f) : strlen (f);
			char ent[2400]; if (fl >= sizeof ent) { rc = E_BADPKG; break; }
			memcpy (ent, f, fl); ent[fl] = 0; f += fl;
			char rel[260], hx[80], url[2200]; unsigned long long want = 0;
			if (sscanf (ent, "%259s %llu %79s %2199s", rel, &want, hx, url) != 4 || strlen (hx) != 64 || forbidden (rel)
			    || strncmp (url, "https://", 8) || !want)
			{ rc = E_BADPKG; r.sayf ("%s : fetch illisible : %s", name, ent); break; }
			char dest[320], tmp[330], hh[65];
			snprintf (dest, sizeof dest, "%s/%s", root, rel);
			if (sha_file (dest, hh) && eq (hh, hx)) r.sayf ("%s : %s déjà là", name, rel);
			else
			{
				snprintf (tmp, sizeof tmp, "%s.pkgtmp", dest);
				mkparent (tmp);
				r.sayf ("%s : téléchargement de %s (%llu Mo)", name, url, want >> 20);
				bool ok; u64 got;
				{
					FileSink fs (tmp);
					if (!fs.ok ()) { rc = E_IO; r.sayf ("écriture impossible : %s", tmp); break; }
					ok = http_get (url, fs, (u64) want, strrchr (rel, '/') ? strrchr (rel, '/') + 1 : rel, r);
					fs.sha.hex (hh); got = fs.n;
				}
				if (!ok) { unlink (tmp); rc = r.cancelled () ? E_CANCEL : E_NET; break; }
				if (got != (u64) want || !eq (hh, hx))
				{ unlink (tmp); rc = E_BADPKG; r.sayf ("%s : %s : taille ou SHA-256 différents de ceux du paquet : refusé", name, rel); break; }
				if (!move (tmp, dest)) { unlink (tmp); rc = E_IO; r.sayf ("remplacement impossible : %s", dest); break; }
			}
			snprintf (line, sizeof line, "%s = %s\n", rel, hx); put (line);
			files++;
		}
		if (rc != OK) { free (dbt); free (mv); if (stage) { char st[200]; snprintf (st, sizeof st, PKG_STAGE "/%s", name); remove_tree (st); } return rc; }
		// les fichiers de l'ancienne version absents de la nouvelle s'en vont (ceux que l'utilisateur a changes restent)
		Ini now; now.parse (sdup (dbt));
		int gone = 0;
		if (old)
			for (int i = 0; i < old->ini.n; i++)
			{
				if (!eq (old->ini.kv[i].sec, "files") || now.get ("files", old->ini.kv[i].key, 0)) continue;
				if (db.owned_elsewhere (old->ini.kv[i].key, name)) continue;
				char card[300]; card_path (card, sizeof card, old->boot (), old->ini.kv[i].key);
				char hh[65];
				if (old->is_config (old->ini.kv[i].key) && sha_file (card, hh) && !eq (hh, old->ini.kv[i].val)) continue;
				if (stage) { snprintf (line, sizeof line, "%s = 1\n", old->ini.kv[i].key); if (!strstr (mv, "[remove]")) putm ("\n[remove]\n"); putm (line); }
				else if (!boot_refused (old->boot (), old->ini.kv[i].key)) { unlink (card); prune (card); }
				gone++;
			}
		char dbp[200];
		if (stage) { put ("\n"); put (mv); snprintf (dbp, sizeof dbp, PKG_STAGE "/%s.ini", name); }
		else snprintf (dbp, sizeof dbp, "%s/%s.ini", RP (PKG_DB), name);
		bool ok = write_file (dbp, dbt, dl);
		if (ok && !stage && strstr (dbt, "\napps/")) appsChanged = true;
		free (dbt); free (mv);
		if (!ok) { r.sayf ("écriture impossible : %s", dbp); return E_IO; }
		r.sayf ("%s %s : %d fichiers %s%s", name, ver, files, TR (stage ? "étagés : redémarrer pour finir" : "installés"),
			kept ? TR (" (vos réglages gardés, les nouveaux à côté en .new)") : "");
		db.load ();
		return OK;
	}

	// Une app ajoutee ou retiree : l'avis korin.apps.changed (25/11/2026) ; le panel, la barre de menus et le lanceur
	// relisent leur catalogue. Personne ne l'ecoute (un shell d'avant cet avis), ou pas de busd : relances, comme
	// avant (le panneau de configuration fait de meme)
	void refresh_desktop ()
	{
		if (!appsChanged || g_pkgroot[0]) return;	// (une autre racine : son bureau ne tourne pas)
		appsChanged = false;
		if (apps_changed_notice () > 0) return;
		static const struct { const char *name, *path; } shell[] = { { "panel", "/sys/panel" }, { "menubar", "/sys/menubar" } };
		for (auto &s : shell)
		{
			// relance sous l'uid qu'il avait (celui de la session) : pkgd et pkg eleve sont en uid 0, et le panneau relance
			// par eux lancait ensuite chaque app en uid 0 (HOME = /home : Spotify ecrivait dans /home/spotify, 21/11/2026)
			long uid = proc_uid (s.name);
			if (!proc_kill (s.name)) continue;
			for (int i = 0; i < 30 && proc_running (s.name); i++) usleep (50000);
			usleep (200000);
			launch_as (s.path, uid);
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
		char whole[40]; cpy (whole, part_of (name), sizeof whole);
		if (*whole && db.find (whole) && !force) { r.sayf ("%s fait partie de %s : retirer %s", name, whole, whole); return E_USED; }
		for (int i = 0; i < db.n && !replaced; i++)
		{
			Need nd[16]; int nn = parse_needs (db.v[i]->needs (), nd, 16);
			for (int k = 0; k < nn; k++) if (eq (nd[k].name, name)) { r.sayf ("%s en a besoin : retirer d'abord %s", db.v[i]->name, db.v[i]->name); return E_USED; }
		}
		char app[40];
		if (!force && running (in->ini, "files", app, sizeof app)) { r.sayf ("%s tourne : le fermer, puis recommencer", app); return E_RUNNING; }
		int gone = 0, kept = 0;
		BootScope bootScope;					// (/boot rendu en sortant)
		for (int i = 0; i < in->ini.n; i++)
		{
			if (!eq (in->ini.kv[i].sec, "files")) continue;
			if (db.owned_elsewhere (in->ini.kv[i].key, name)) continue;
			char card[300]; card_path (card, sizeof card, in->boot (), in->ini.kv[i].key);
			char hh[65];
			if ((!purge || user_tree (in->ini.kv[i].key)) && in->is_config (in->ini.kv[i].key) && sha_file (card, hh) && !eq (hh, in->ini.kv[i].val)) { kept++; continue; }
			if (starts (in->ini.kv[i].key, "apps/")) appsChanged = true;
			if (boot_refused (in->boot (), in->ini.kv[i].key)) continue;	// (pas le fichier de meme nom a la racine)
			if (unlink (card) == 0) gone++;
			prune (card);
		}
		char dbp[400]; snprintf (dbp, sizeof dbp, "%s/%s.ini", RP (PKG_DB), name);
		unlink (dbp);
		r.sayf ("%s retiré : %d fichiers%s", name, gone, kept ? TR (" (vos réglages changés gardés)") : "");
		db.load ();
		if (!*whole && !replaced)			// ses parties avec lui
		{
			char parts[32][40]; int np = 0;
			for (int i = 0; i < db.n && np < 32; i++) if (eq (part_of (db.v[i]->name), name)) cpy (parts[np++], db.v[i]->name, 40);
			for (int i = 0; i < np; i++) remove (parts[i], purge, r, true);
		}
		return OK;
	}

	// Ce qui ne repart qu'au prochain demarrage : ce que le firmware lit (a la racine, ou `boot =` du paquet) et init2 lui-meme
	static bool boot_file (const char *f, const char *bootPats) { return !strchr (f, '/') || in_patterns (bootPats, f) || eq (f, "sys/svc/init2"); }

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
		BootScope bootScope;					// (/boot rendu en sortant)
		for (int k = 0; k < n; k++)
		{
			char ip[200], dir[200]; snprintf (ip, sizeof ip, PKG_STAGE "/%s.ini", names[k]); snprintf (dir, sizeof dir, PKG_STAGE "/%s", names[k]);
			Ini st; if (!st.load (ip)) { r.sayf ("%s : fichier d'étage illisible : laissé étagé", names[k]); left = true; continue; }
			int moved = 0, missing = 0, failed = 0, abMoved = 0, abFailed = 0;
			const char *bootPats = st.get ("package", "boot");	// (ses fichiers de demarrage : /boot/)
			// A/B : l'emplacement cible, une fois par paquet qui porte le noyau ou l'initrd du PC
			AbState ab = { 'a', '-', '0', 'a' }; char abTarget = 0, abPath[200] = "";
			for (int i = 0; i < st.n && !abTarget && !g_pkgroot[0]; i++)
				if (eq (st.kv[i].sec, "moves") && ab_file (st.kv[i].key) && in_patterns (bootPats, st.kv[i].key) && !boot_refused (bootPats, st.kv[i].key))
				{
					const char *bv = boot_volume ();
					snprintf (abPath, sizeof abPath, "%s/korin/ab.txt", bv);
					if (!ab_read (abPath, ab))	// (la première fois : la version installée devient a, la confirmée)
					{
						char d[200]; snprintf (d, sizeof d, "%s/korin/a", bv); mkdirs (d);
						static const char *const fs[] = { "kernel.elf", "initrd.tar", 0 };
						for (int k = 0; fs[k]; k++)
						{
							char from[260], to[260]; snprintf (from, sizeof from, "%s/korin/%s", bv, fs[k]); snprintf (to, sizeof to, "%s/%s", d, fs[k]);
							if (exists (from)) copy_plain (from, to);
						}
						ab = { 'a', '-', '0', 'a' };
					}
					if (eq (st.get ("package", "ab_keep"), "1") && ab.trial != '-' && ab.booted == ab.trial)
					{
						r.sayf (TR ("la version en essai (%c) devient la version de secours"), ab.trial);
						ab.conf = ab.trial;
					}
					abTarget = ab.trial != '-' && ab.trial != ab.conf ? ab.trial : (ab.conf == 'a' ? 'b' : 'a');
				}
			for (int i = 0; i < st.n; i++)
			{
				const char *f = st.kv[i].key;
				if (eq (st.kv[i].sec, "moves"))
				{
					char from[320], to[320]; snprintf (from, sizeof from, "%s/%s", dir, f); card_path (to, sizeof to, bootPats, f);
					if (forbidden (f) || boot_refused (bootPats, f)) { failed++; continue; }
					if (!exists (from)) { missing++; continue; }	// (deja la : un commit interrompu reprend)
					if (abTarget && ab_file (f))	// (A/B : dans l'emplacement en essai, .new puis SHA-256 vérifié, puis renommé)
					{
						kernelChanged = true;
						const char *bv = boot_volume ();
						char d[200], nw[340]; snprintf (d, sizeof d, "%s/korin/%c", bv, abTarget); mkdirs (d);
						snprintf (to, sizeof to, "%s/%s", d, f + 6); snprintf (nw, sizeof nw, "%s.new", to);
						char hh[65]; const char *want = st.get ("files", f);
						if (move (from, nw) && sha_file (nw, hh) && (!*want || eq (hh, want)) && rename (nw, to) == 0)
						{
							moved++; abMoved++;
							// et aux chemins d'avant A/B (\korin\kernel.elf...) : un chargeur qui ne lit pas ab.txt (une
							// copie pas mise à jour, \EFI\BOOT\BOOTX64.EFI du ZBook) démarre ainsi la même version que
							// les programmes (l'écran gris du 26/11/2026) ; le chargeur A/B, lui, prend l'emplacement
							char leg[320], legn[340]; snprintf (leg, sizeof leg, "%s/%s", bv, f); snprintf (legn, sizeof legn, "%s.new", leg);
							if (!(copy_plain (to, legn) && rename (legn, leg) == 0)) { unlink (legn); r.sayf (TR ("%s : copie d'avant A/B impossible"), leg); }
						}
						else { unlink (nw); failed++; abFailed++; r.sayf (TR ("%s : écrit dans l'emplacement %c mais empreinte fausse ou écriture impossible"), f, abTarget); }
						continue;
					}
					if (boot_file (f, bootPats))
					{
						kernelChanged = true;
						if ((eq (f, "kernel8.img") || eq (f, "kernel_2712.img") || eq (f, "korin/kernel.elf")) && exists (to))
						{ char old[320]; snprintf (old, sizeof old, "%s.old", to); rename (to, old); }
					}
					if (move (from, to))
					{
						moved++;
						// le chargeur : aussi ses autres copies sur la même partition (\EFI\BOOT\BOOTX64.EFI, posé par
						// osinstall ; le firmware du ZBook lançait celle-là, jamais mise à jour), si ce sont les nôtres
						if (eq (f, "EFI/Korin/korin.efi") && !g_pkgroot[0])
						{
							char cp[320]; snprintf (cp, sizeof cp, "%s/EFI/BOOT/BOOTX64.EFI", boot_volume ());
							if (korin_loader (cp))
							{
								char cpn[340]; snprintf (cpn, sizeof cpn, "%s.new", cp);
								if (copy_plain (to, cpn) && rename (cpn, cp) == 0) r.sayf (TR ("%s : chargeur mis à jour aussi"), cp);
								else { unlink (cpn); r.sayf (TR ("%s : copie du chargeur impossible"), cp); }
							}
						}
					}
					else failed++;
				}
				else if (eq (st.kv[i].sec, "remove") && !forbidden (f) && !boot_refused (bootPats, f) && !db.owned_elsewhere (f, names[k])) { char c[320]; card_path (c, sizeof c, bootPats, f); unlink (c); prune (c); }
			}
			// A/B : le noyau et l'initrd en place dans l'emplacement en essai -> ab.txt tout de suite, même si un autre
			// fichier a échoué : sinon les programmes déjà déplacés tournent avec l'ancien noyau et les anciens kits de
			// l'initrd (l'écran gris du 26/11/2026) ; le paquet reste étagé pour le reste
			if (abTarget && !abFailed && abMoved)
			{
				ab.trial = abTarget; ab.tries = '3';
				if (!ab_write (abPath, ab)) r.sayf (TR ("%s : écriture impossible"), abPath);
				else r.sayf (TR ("nouvelle version dans l'emplacement %c (en essai), version de secours : %c"), abTarget, ab.conf);
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
		char p[400]; snprintf (p, sizeof p, "%s/%s.ini", RP (PKG_DB), name);
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
	// une partie (part) : le paquet dont elle fait partie (sa base, sinon l'index) ; "" : aucun
	const char *part_of (const char *name) const
	{
		Inst *in = db.find (name);
		const char *w = in ? in->ini.get ("package", "part") : "";
		if (*w) return w;
		const Pkg *p = index.find (name);
		return p ? p->part : "";
	}
	// une mise a jour pour celui-ci ? -> l'entree de l'index (une de ses parties plus recente ou absente : la sienne)
	const Pkg *update_for (const Inst &in) const
	{
		const Pkg *p = index.find (in.name);
		if (*part_of (in.name)) return 0;		// une partie : avec son paquet
		if (p && vcmp (p->version, in.version ()) > 0) return p;
		if (p)
		{
			for (int i = 0; i < index.n; i++)
			{
				if (!eq (index.p[i].part, in.name)) continue;
				Inst *pi = db.find (index.p[i].name);
				if (!pi || pi->missing || vcmp (index.p[i].version, pi->version ()) > 0) return p;
			}
			return 0;
		}
		for (int i = 0; i < index.n; i++) if (index.p[i].replaces && in_list (index.p[i].replaces, in.name)) return &index.p[i];
		return 0;
	}
};

} // namespace pkg

#endif
