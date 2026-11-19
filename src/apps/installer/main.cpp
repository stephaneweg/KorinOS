//
// installer -- l'installateur de Korin OS, plein écran, lancé par la session de la clé d'installation
// (docs/DESIGN-INSTALLATEUR.md ; les maquettes validées : docs/maquettes/installateur-*.html ; l'étude :
// docs/etude/INSTALLATEUR.md). L'interface ne fait que présenter et choisir : le disque est lu par
// « osinstall --list » et tout ce qui s'écrit passe par « sudo osinstall /tmp/install/plan.ini » (src/bin/osinstall),
// au bouton du Résumé, en une fois.
//
//   Bienvenue   langue, pays, clavier (pris tout de suite), fuseau ; un Korin déjà installé (osinstall --list, la ligne
//               K) : Réinstaller (en gardant /home : ni Disque ni Compte, maquette installateur-reinstaller.html) ou
//               Nouvelle installation
//   Disque      la liste des partitions à la Windows 7 : Actualiser, Supprimer, Nouveau (gardés en mémoire, écrits
//               à la fin) ; la ligne choisie est formatée (partition) ou reçoit Korin (espace libre) ; l'EFI est
//               réutilisée, sinon une neuve est créée. « Utiliser… » donne un rôle à une ligne (/ ou /home, formatée
//               dans le système de fichiers choisi : maquette installateur-partitions.html) ; avec / donné ainsi,
//               l'étape Mes fichiers est sautée
//   Fichiers    la taille de /home (0 : dans le système), son système de fichiers (exFAT ou ext4)
//   Internet    les réseaux Wi-Fi ; câble : passé
//   Logiciels   les catégories de l'index de la clé (/packages/pc/index.txt), à cocher, le détail à déplier ; connecté,
//               l'index en ligne (« pkg check » : /var/pkg/index.txt, signature vérifiée) : un paquet vient de la clé
//               si elle l'a dans la même version ou plus récente, sinon du dépôt (osinstall : pkg --root)
//   Compte      nom, ordinateur, utilisateur, mot de passe (haché ici : seule l'empreinte va dans le plan), session,
//               administrateur, mot de passe de root
//   Résumé      les choix, ce qui sera effacé, la confirmation par le nom du disque
//   Installation, Terminé
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <armpi/syscall.h>
#include <uikit.h>
#include <netkit_abi.h>

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
static long str_len (const char *s) { return (long) strlen (s); }
#include "../../svc/authd/pwhash.h"

#define TR(s)	((const char *) ui_tr ((long) (s)))
enum { SYS_GETRANDOM_ = 0x63, SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32 };
static long sysc (long n, long a, long b) { return armpi_syscall (n, a, b, 0, 0, 0, 0); }

static struct ui_vars ui;
static const unsigned *s_jet;
static unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }
static void rbox (int x, int y, int w, int h, int r, unsigned c) { ui_rbox (x, y, w, h, r, c, c); }
static void text (int x, int y, const char *s, unsigned c, int px = 14, int bold = 0, int w = 4000)
{
	ui_text_fit ((long) s, x, y + px, c, px, bold ? (long) ui.font_b : (long) ui.font, w);
}
static void cpy (char *d, const char *s, size_t cap) { snprintf (d, cap, "%s", s ? s : ""); }
static bool eq (const char *a, const char *b) { return a && b && !strcmp (a, b); }

enum { P_WELCOME, P_DISK, P_HOME, P_NET, P_PKG, P_ACCOUNT, P_SUMMARY, P_INSTALL, P_DONE, P_N };
static const char *const TITRES[P_N] = { "Installer Korin OS", "Où voulez-vous installer Korin OS ?",
	"Voulez-vous une partition à part pour vos fichiers ?", "Se connecter à Internet", "Que voulez-vous installer ?",
	"Qui êtes-vous ?", "Prêt à installer", "Installation de Korin OS", "Korin OS est installé" };
static const char *const CHAPEAUX[P_N] = {
	"Korin s'installe sur le disque de cet ordinateur, à côté de Windows ou à sa place. Comptez une dizaine de minutes.",
	"Choisissez une partition (elle sera formatée) ou un espace non alloué. Rien n'est écrit avant le résumé.",
	"Avec une partition /home, vous pourrez réinstaller Korin plus tard sans perdre vos documents. À 0, vos fichiers restent dans la partition du système.",
	"Pour choisir parmi tous les logiciels et avoir les dernières versions. Sans Internet, Korin s'installe depuis la clé ; le reste viendra plus tard par Logiciels.",
	"", "", "", "", "" };
enum { BAND = 48, FOOT = 72, COLW = 680, ROWH = 44 };
static int SW, SH, X0, CW, Y0;			// écran, colonne, haut du contenu
static int g_page, g_back_to_summary;
static long g_band, g_head, g_foot, g_back, g_next, g_alt, g_status;

struct Item { long w; int page; };
static Item g_items[160]; static int g_nitems;
static long item (int page, long w) { g_items[g_nitems].w = w; g_items[g_nitems++].page = page; return w; }

// ---- fichiers ----------------------------------------------------------------------------------------------------
static char *lire (const char *chemin)
{
	FILE *f = fopen (chemin, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc ((size_t) n + 1);
	if (b) { n = (long) fread (b, 1, (size_t) n, f); b[n < 0 ? 0 : n] = 0; }
	fclose (f);
	return b;
}
static void ini_lit (const char *chemin, const char *k, char *out, size_t cap)
{
	char *b = lire (chemin);
	size_t kl = strlen (k);
	for (char *l = b; l && *l; )
	{
		char *e = strchr (l, '\n');
		if (e) *e = 0;
		char *s = l; while (*s == ' ') s++;
		if (!strncmp (s, k, kl))
		{
			char *v = s + kl; while (*v == ' ') v++;
			if (*v == '=')
			{
				v++; while (*v == ' ') v++;
				cpy (out, v, cap);
				for (char *c = out + strlen (out); c > out && (c[-1] == '\r' || c[-1] == ' '); ) *--c = 0;
				break;
			}
		}
		l = e ? e + 1 : l + strlen (l);
	}
	free (b);
}
static char s_dec = ',';		// le séparateur décimal : « , » en français, « . » en anglais (main)
static void go_txt (char *o, size_t cap, u64 sectors)	// « 400 Go », « 260 Mo » (« GB », « MB » en anglais)
{
	double g = (double) sectors * 512.0 / (1024.0 * 1024.0 * 1024.0);
	if (g < 1) snprintf (o, cap, "%d %s", (int) (g * 1024 + 0.5), TR ("Mo"));
	else if (g < 10) snprintf (o, cap, "%.1f %s", g, TR ("Go"));
	else snprintf (o, cap, "%d %s", (int) (g + 0.5), TR ("Go"));
	for (char *c = o; *c; c++) if (*c == '.') *c = s_dec;
}
static const u64 GIB = 2097152, MIB = 2048;

// ---- 1 Bienvenue -----------------------------------------------------------------------------------------------
static const char *const PAYS[][5] = { { "BE", "Belgique", "BE", "Europe/Brussels" }, { "FR", "France", "FR", "Europe/Paris" },
	{ "CH", "Suisse", "FR", "Europe/Zurich" }, { "LU", "Luxembourg", "FR", "Europe/Luxembourg" },
	{ "CA", "Canada", "US", "America/Toronto" }, { "DE", "Allemagne", "DE", "Europe/Berlin" },
	{ "GB", "Royaume-Uni", "US", "Europe/London" }, { "US", "États-Unis", "US", "America/New_York" } };
enum { N_PAYS = 8, N_KEYB = 24, N_TZ = 600 };
static long g_lang, g_pays, g_keyb, g_essai, g_tzlab, g_tzbtn, g_tzreg, g_tzville;
static char g_keybs[N_KEYB][16]; static int g_nkeyb;
static char g_kb[16] = "BE", g_tz[48] = "Europe/Brussels", g_paysc[4] = "BE", g_langc[4] = "fr";
static char *s_tzbuf; static const char *s_tz[N_TZ]; static int s_ntz;
static const char *const REGIONS[] = { "Africa", "America", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific", "UTC" };
enum { N_REG = 9 };
static int s_ville[N_TZ], s_nville; static bool s_tzopen;

static void clavier_applique (const char *nom)
{
	long ch = sysc (SYS_SVC_OPEN_, (long) "hid", 1000);
	if (ch >= 0)
	{
		char m[512]; memset (m, 0, sizeof m);
		*(long *) m = 2;					// HID_OP_KEYMAP
		cpy (m + 48, nom, 16);
		sysc (SYS_SVC_CALL_, ch, (long) m);
	}
	cpy (g_kb, nom, sizeof g_kb);
	ui_view_dirty (g_band);
}
static void claviers_lit (char *out, size_t cap)
{
	g_nkeyb = 0; out[0] = 0;
	DIR *d = opendir ("/res/keyb");
	if (!d) { cpy (g_keybs[g_nkeyb++], "FR", 16); cpy (out, "FR", cap); return; }
	struct dirent *e;
	while ((e = readdir (d)) && g_nkeyb < N_KEYB)
	{
		size_t n = strlen (e->d_name);
		if (n < 5 || n > 15 || strcmp (e->d_name + n - 4, ".txt")) continue;
		cpy (g_keybs[g_nkeyb], e->d_name, n - 3);
		if (out[0]) strncat (out, "|", cap - strlen (out) - 1);
		strncat (out, g_keybs[g_nkeyb], cap - strlen (out) - 1);
		g_nkeyb++;
	}
	closedir (d);
}
static void fuseaux_lit ()
{
	s_tzbuf = lire ("/res/tz.txt");
	for (char *l = s_tzbuf; l && *l && s_ntz < N_TZ; )
	{
		char *e = strchr (l, '\n');
		if (e) *e = 0;
		char *sp = strchr (l, ' ');
		if (*l != '#' && sp) { *sp = 0; s_tz[s_ntz++] = l; }
		l = e ? e + 1 : l + strlen (l);
	}
	if (!s_ntz) { s_tz[s_ntz++] = "Europe/Brussels"; s_tz[s_ntz++] = "Europe/Paris"; s_tz[s_ntz++] = "UTC"; }
}
static int region_de (const char *z)
{
	for (int r = 0; r < N_REG; r++)
	{
		size_t n = strlen (REGIONS[r]);
		if (!strncmp (z, REGIONS[r], n) && (z[n] == '/' || !z[n])) return r;
	}
	return -1;
}
static void villes_remplit (int r)
{
	ui_list_clear (g_tzville);
	s_nville = 0;
	int sel = 0;
	for (int i = 0; i < s_ntz; i++)
	{
		if (region_de (s_tz[i]) != r) continue;
		const char *v = strchr (s_tz[i], '/'); v = v ? v + 1 : s_tz[i];
		char t[64]; int k = 0;
		for (; *v && k < 60; v++) t[k++] = *v == '_' ? ' ' : *v;
		t[k] = 0;
		if (eq (s_tz[i], g_tz)) sel = s_nville;
		ui_list_add (g_tzville, (long) t, s_nville);
		s_ville[s_nville++] = i;
	}
	ui_w_set_value (g_tzville, sel);
	ui_w_draw (g_tzville);
}
static void fuseau_montre ()
{
	ui_w_set_text (g_tzlab, (long) g_tz); ui_w_draw (g_tzlab);
	int r = region_de (g_tz);
	if (r >= 0) { ui_w_set_value (g_tzreg, r); ui_w_draw (g_tzreg); villes_remplit (r); }
}
static void pays_choisi ()
{
	long v = ui_w_get_value (g_pays);
	if (v < 0 || v >= N_PAYS) return;
	cpy (g_paysc, PAYS[v][0], sizeof g_paysc);
	char c[8]; snprintf (c, sizeof c, " %s ", PAYS[v][0]);
	wifi_country ((long) c);
	for (int i = 0; i < g_nkeyb; i++)
		if (eq (g_keybs[i], PAYS[v][2])) { ui_w_set_value (g_keyb, i); ui_w_draw (g_keyb); clavier_applique (g_keybs[i]); }
	cpy (g_tz, PAYS[v][3], sizeof g_tz);
	fuseau_montre ();
}
static void langue_choisie ()
{
	const char *l = ui_w_get_value (g_lang) == 0 ? "en" : "fr";
	if (eq (l, g_langc)) return;
	mkdir ("/tmp/install", 0700);
	FILE *f = fopen ("/tmp/install/lang", "w");
	if (f) { fputs (l, f); fclose (f); }
	execl ("/apps/installer.app/main", "installer", (char *) 0);	// (un dictionnaire se lit au démarrage)
}

// ---- 2 Disque ----------------------------------------------------------------------------------------------------
enum { G_EFI, G_MSR, G_REC, G_WIN, G_DATA, G_FREE, G_NEW };
struct Part { int num; u64 first, last; int genre; char content[12], name[80]; int role, fs; };	// role : R_* ; fs : FS_*
enum { R_NONE, R_ROOT, R_HOME };
enum { FS_FAT32, FS_EXFAT, FS_EXT4 };
static const char *const FS_NOMS[] = { "FAT32", "exFAT", "ext4" }, *const FS_PLAN[] = { "fat32", "exfat", "ext4" };
enum { NPART = 64 };
static Part g_p[NPART]; static int g_np;
// un Korin déjà installé (la ligne K de osinstall --list) : KORIN OS, KORIN HOME (0 sans), la version, les comptes
static int g_kroot, g_khome; static char g_kver[40], g_kusers[200]; static bool g_reinst, g_sel_init;
static long g_wreinst, g_wneuve, g_wreinstl, g_wneuvel;
static char g_dev[16]; static u64 g_sectors; static bool g_gpt, g_lu;
static int g_del[32], g_ndel, g_newnum = 100;
static long g_dlist, g_dact, g_dsup, g_dnouv, g_dnote, g_defi, g_duse, g_draz;
static int role_ligne (int r) { for (int i = 0; i < g_np; i++) if (g_p[i].role == r) return i; return -1; }
static bool roles_on () { return role_ligne (R_ROOT) >= 0; }	// (/ donné par « Utiliser… » : le parcours des rôles)

static void disque_lit ()
{
	g_np = 0; g_ndel = 0; g_dev[0] = 0; g_lu = true; g_kroot = g_khome = 0; g_kver[0] = g_kusers[0] = 0;
	FILE *f = popen ("/bin/sudo /sbin/osinstall --list", "r");
	if (!f) return;
	char l[256]; bool premier = true;
	while (fgets (l, sizeof l, f))
	{
		l[strcspn (l, "\r\n")] = 0;
		if (l[0] == 'D') { if (!premier) break; premier = false; char g[8] = ""; sscanf (l + 2, "%15s %llu %7s", g_dev, &g_sectors, g); g_gpt = eq (g, "gpt"); }
		else if (l[0] == 'K') sscanf (l + 2, "%d %d %39s %199s", &g_kroot, &g_khome, g_kver, g_kusers);
		else if ((l[0] == 'P' || l[0] == 'F') && g_np < NPART)
		{
			Part &p = g_p[g_np];
			memset (&p, 0, sizeof p);
			if (l[0] == 'F') { sscanf (l + 2, "%llu %llu", &p.first, &p.last); p.genre = G_FREE; g_np++; continue; }
			char ty[12] = ""; int n = 0;
			if (sscanf (l + 2, "%d %llu %llu %11s %11s %n", &p.num, &p.first, &p.last, ty, p.content, &n) < 5) continue;
			cpy (p.name, l + 2 + n, sizeof p.name);
			p.genre = eq (ty, "efi") ? G_EFI : eq (ty, "msr") ? G_MSR : eq (ty, "recovery") ? G_REC :
				  (eq (p.content, "ntfs") || eq (p.content, "bitlocker")) ? G_WIN : G_DATA;
			g_np++;
		}
	}
	pclose (f);
}
static void fusionne ()
{
	int w = 0;
	for (int i = 0; i < g_np; i++)
	{
		if (w > 0 && g_p[w - 1].genre == G_FREE && g_p[i].genre == G_FREE) { g_p[w - 1].last = g_p[i].last; continue; }
		g_p[w++] = g_p[i];
	}
	g_np = w;
}
static bool efi_reste () { for (int i = 0; i < g_np; i++) if (g_p[i].genre == G_EFI) return true; return false; }
static const char *nom_de (const Part &p, char *o, size_t cap)
{
	if (p.genre == G_FREE) cpy (o, TR ("Espace non alloué"), cap);
	else if (p.genre == G_NEW) snprintf (o, cap, "%s %d : %s", TR ("Partition"), p.num, TR ("nouvelle"));
	else snprintf (o, cap, "%s %d : %s", TR ("Partition"), p.num, p.name[0] ? p.name :
		p.genre == G_EFI ? "EFI" : p.genre == G_MSR ? "MSR" : p.genre == G_REC ? TR ("Récupération") : TR ("sans nom"));
	return o;
}
static const char *type_de (const Part &p)
{
	switch (p.genre)
	{
	case G_FREE: return "";
	case G_EFI: return TR ("Système (EFI)");
	case G_MSR: return TR ("Réservé (MSR)");
	case G_REC: return TR ("Récupération");
	case G_NEW: return TR ("Principal (non formatée)");
	case G_WIN: return eq (p.content, "bitlocker") ? TR ("Principal (BitLocker)") : TR ("Principal (NTFS)");
	default:
		return eq (p.content, "fat32") ? TR ("Principal (FAT32)") : eq (p.content, "exfat") ? TR ("Principal (exFAT)") :
		       eq (p.content, "ext") ? TR ("Principal (ext4)") : TR ("Principal");
	}
}
static const char *usage_de (const Part &p, char *o, size_t cap)
{
	o[0] = 0;
	if (p.genre == G_EFI) cpy (o, TR ("démarrage (gardée)"), cap);
	else if (p.role) snprintf (o, cap, "%s · %s, %s", p.role == R_ROOT ? "/" : "/home",
		p.genre == G_FREE || p.genre == G_NEW ? TR ("nouvelle partition") : TR ("formatée"), FS_NOMS[p.fs]);
	return o;
}
static void disque_remplit (int sel)
{
	ui_list_clear (g_dlist);
	for (int i = 0; i < g_np; i++)
	{
		char n[120], t[32], u[80], l[300];
		go_txt (t, sizeof t, g_p[i].last - g_p[i].first + 1);
		snprintf (l, sizeof l, "%s\t%s\t%s\t%s", nom_de (g_p[i], n, sizeof n), t, type_de (g_p[i]), usage_de (g_p[i], u, sizeof u));
		ui_list_add (g_dlist, (long) l, i);
	}
	if (sel >= g_np) sel = g_np - 1;
	ui_w_set_value (g_dlist, sel);
	ui_w_draw (g_dlist);
}
static int disque_sel () { long v = ui_w_get_value (g_dlist); return v >= 0 && v < g_np ? (int) v : -1; }

struct Verdict { bool ok, danger; char note[240], etat[120]; };
static const u64 MIN_KORIN = 10 * GIB, EFI_NEUVE = 512 * MIB;
static Verdict verdict ()
{
	Verdict v; memset (&v, 0, sizeof v);
	int s = disque_sel ();
	if (!g_dev[0]) { cpy (v.note, TR ("Aucun disque interne trouvé (NVMe). Branchez le disque, puis Actualiser."), sizeof v.note); return v; }
	int ir = role_ligne (R_ROOT), ih = role_ligne (R_HOME);
	if (ir >= 0)	// (les rôles : / et peut-être /home sur des lignes choisies)
	{
		if (!efi_reste ()) { cpy (v.note, TR ("Avec « Utiliser… », la partition EFI de ce disque doit être gardée : sans elle, laissez les rôles et choisissez une place."), sizeof v.note); return v; }
		char a[120], b[120], ta[32], tb[32], z[300] = "";
		go_txt (ta, sizeof ta, g_p[ir].last - g_p[ir].first + 1);
		snprintf (z, sizeof z, TR ("/ dans %s (%s, %s)"), nom_de (g_p[ir], a, sizeof a), ta, FS_NOMS[g_p[ir].fs]);
		if (ih >= 0)
		{
			go_txt (tb, sizeof tb, g_p[ih].last - g_p[ih].first + 1);
			char y[200]; snprintf (y, sizeof y, TR (", /home dans %s (%s, %s)."), nom_de (g_p[ih], b, sizeof b), tb, FS_NOMS[g_p[ih].fs]);
			strncat (z, y, sizeof z - strlen (z) - 1);
		}
		else strncat (z, TR (", /home dans le système."), sizeof z - strlen (z) - 1);
		bool fr = g_p[ir].genre != G_FREE && g_p[ir].genre != G_NEW, fh = ih >= 0 && g_p[ih].genre != G_FREE && g_p[ih].genre != G_NEW;
		if (fr || fh)
		{
			v.danger = true;
			char y[200]; snprintf (y, sizeof y, " %s %s%s%s.", TR ("Seront formatées :"), fr ? nom_de (g_p[ir], a, sizeof a) : "", fr && fh ? ", " : "", fh ? nom_de (g_p[ih], b, sizeof b) : "");
			strncat (z, y, sizeof z - strlen (z) - 1);
		}
		cpy (v.note, z, sizeof v.note);
		cpy (v.etat, ih >= 0 ? TR ("/ et /home sur des partitions choisies") : TR ("/ sur une partition choisie"), sizeof v.etat);
		v.ok = true;
		return v;
	}
	if (ih >= 0) { cpy (v.note, TR ("/home est choisi : choisissez aussi la place du système (/), avec « Utiliser… »."), sizeof v.note); cpy (v.etat, TR ("Il manque le système (/)."), sizeof v.etat); return v; }
	if (s < 0) { cpy (v.etat, TR ("Choisissez où installer Korin OS."), sizeof v.etat); return v; }
	Part &p = g_p[s];
	char t[32], n[120]; go_txt (t, sizeof t, p.last - p.first + 1);
	if (p.genre == G_EFI) { cpy (v.note, TR ("La partition EFI sert au démarrage : elle ne peut pas recevoir Korin. Elle sera réutilisée pour le démarrer."), sizeof v.note); return v; }
	if (p.genre == G_MSR || p.genre == G_REC) { cpy (v.note, TR ("Cette partition sert à Windows (réservée ou récupération) : elle ne peut pas recevoir Korin."), sizeof v.note); return v; }
	u64 besoin = MIN_KORIN + (efi_reste () ? 0 : EFI_NEUVE);
	if (p.last - p.first + 1 < besoin) { char b[32]; go_txt (b, sizeof b, besoin); snprintf (v.note, sizeof v.note, TR ("Il faut au moins %s pour Korin ; cette ligne n'en a que %s."), b, t); return v; }
	v.ok = true;
	if (p.genre == G_FREE) { snprintf (v.note, sizeof v.note, TR ("Korin s'installe dans cet espace libre (%s). Rien d'autre ne change."), t); snprintf (v.etat, sizeof v.etat, TR ("Korin dans %s d'espace libre"), t); }
	else if (p.genre == G_NEW) { snprintf (v.note, sizeof v.note, TR ("Korin s'installe dans la nouvelle partition (%s)."), t); snprintf (v.etat, sizeof v.etat, TR ("Korin dans la nouvelle partition de %s"), t); }
	else
	{
		v.danger = true;
		if (g_kroot && p.num == g_kroot)
		{
			cpy (v.note, TR ("KORIN OS sera formatée, avec tout ce qu'elle contient. Pour garder vos fichiers, revenez à l'accueil et choisissez « Réinstaller »."), sizeof v.note);
			snprintf (v.etat, sizeof v.etat, TR ("Korin à la place de l'ancien Korin (%s)"), t);
			return v;
		}
		snprintf (v.note, sizeof v.note, p.genre == G_WIN ? TR ("Cette partition sera formatée : Windows et tout ce que contient %s seront effacés.")
			: TR ("Cette partition sera formatée : tout ce que contient %s sera effacé."), nom_de (p, n, sizeof n));
		snprintf (v.etat, sizeof v.etat, TR ("Korin à la place de %s (%s)"), p.name[0] ? p.name : nom_de (p, n, sizeof n), t);
	}
	return v;
}
static void disque_note ()
{
	Verdict v = verdict ();
	int s = disque_sel ();
	ui_w_set_text (g_dnote, (long) v.note); ui_w_draw (g_dnote);
	ui_w_set_text (g_defi, (long) (efi_reste () ? TR ("La partition EFI est gardée et réutilisée pour démarrer Korin ; le menu de démarrage proposera aussi Windows.")
		: TR ("Plus de partition EFI : Korin en créera une de 512 Mo au début de la place choisie."))); ui_w_draw (g_defi);
	ui_w_enable (g_dsup, s >= 0 && g_p[s].genre != G_FREE);
	ui_w_enable (g_dnouv, s >= 0 && g_p[s].genre == G_FREE);
	ui_w_enable (g_duse, s >= 0 && g_p[s].genre != G_EFI && g_p[s].genre != G_MSR && g_p[s].genre != G_REC);
	ui_w_enable (g_draz, role_ligne (R_ROOT) >= 0 || role_ligne (R_HOME) >= 0);
	ui_w_enable (g_next, v.ok);
	ui_w_set_text (g_status, (long) v.etat); ui_w_draw (g_status);
}
// un dialogue au-dessus de l'installateur. Elegant met une fenêtre WF_TOP sans cadre qui couvre l'écran dans la couche
// du plein écran, au-dessus de toute autre (core.c, win_layer) : un dialogue ordinaire, et même WF_TOP, s'ouvrait dessous,
// invisible, et l'installateur ne répondait plus. Le dialogue couvre donc l'écran lui aussi (ui_dialog_at : sans cadre,
// au-dessus) : un fond, la boîte au centre (g_bx, g_by), son titre, sa bordure et ses deux boutons (g_bok, g_bko)
static long g_bx, g_by, g_bok, g_bko;
static long dlg_debut (int w, int h, const char *titre, const char *ok, const char *ko)
{
	ui_dialog_at (0, 0);
	long d = ui_dialog_begin (SW, SH, (long) titre);
	if (!d) return 0;
	g_bx = (SW - w) / 2; g_by = (SH - h) / 2;
	ui_fill (0, 0, SW, SH, jet (TJ_LINE));
	ui_fill (g_bx, g_by, w, h, jet (TJ_FACE));
	ui_fill (g_bx, g_by, w, 1, jet (TJ_LINE_STRONG)); ui_fill (g_bx, g_by + h - 1, w, 1, jet (TJ_LINE_STRONG));
	ui_fill (g_bx, g_by, 1, h, jet (TJ_LINE_STRONG)); ui_fill (g_bx + w - 1, g_by, 1, h, jet (TJ_LINE_STRONG));
	ui_label (g_bx + 16, g_by + 12, w - 32, 24, (long) titre, 0);
	g_bok = ui_pushbutton (g_bx + w - 12 - 120, g_by + h - 12 - 32, 120, 32, (long) ok, UB_DEFAULT);
	g_bko = ui_pushbutton (g_bx + w - 12 - 248, g_by + h - 12 - 32, 120, 32, (long) ko, 0);
	ui_dialog_cancel (d, 0);
	return d;
}
static long dlg_fn (long d, long w, long code)
{
	if (code == UN_ACTIVATE) { if (w == g_bok) ui_dialog_close (d, 1); else if (w == g_bko) ui_dialog_close (d, 0); }
	return 0;
}
static void disque_supprime ()
{
	int s = disque_sel ();
	if (s < 0 || g_p[s].genre == G_FREE) return;
	Part &p = g_p[s];
	char n[120], q[300];
	const char *av = p.genre == G_EFI ? TR ("Sans elle, Windows ne démarrera plus, et Korin devra en créer une neuve.")
		: p.genre == G_REC ? TR ("Windows ne pourra plus se réparer depuis ce disque.")
		: p.genre == G_WIN ? TR ("Windows, ses programmes et tous les fichiers de cette partition seront perdus.")
		: TR ("Tout ce qu'elle contient sera perdu.");
	snprintf (q, sizeof q, "%s\n%s\n%s", nom_de (p, n, sizeof n), av, TR ("Elle ne sera effacée qu'au bouton du résumé."));
	long d = dlg_debut (460, 190, TR ("Supprimer la partition ?"), TR ("Supprimer"), TR ("Annuler"));
	if (!d) return;
	ui_label (g_bx + 16, g_by + 44, 428, 90, (long) q, ULB_WRAP);
	long r = ui_dialog_run (d, (long) dlg_fn, 0);
	ui_dialog_end (d);
	ui_w_draw (0);
	if (r != 1) return;
	if (p.genre != G_NEW && g_ndel < 32) g_del[g_ndel++] = p.num;
	p.genre = G_FREE; p.name[0] = 0; p.content[0] = 0; p.role = R_NONE;
	fusionne ();
	int sel = 0;
	for (int i = 0; i < g_np; i++) if (g_p[i].first <= p.first && g_p[i].last >= p.first) sel = i;
	disque_remplit (s < g_np ? s : sel);
	disque_note ();
}
static long g_nslider, g_ntext, g_nerr; static u64 g_nmax;
static long nouveau_fn (long d, long w, long code)
{
	dlg_fn (d, w, code);
	if (code != UN_CHANGE) return 0;
	if (w == g_nslider) { char t[24]; snprintf (t, sizeof t, "%ld", ui_w_get_value (g_nslider)); ui_w_set_text (g_ntext, (long) t); ui_w_draw (g_ntext); }
	else if (w == g_ntext)
	{
		long v = atol ((const char *) ui_w_get_text (g_ntext));
		if (v >= 1 && (u64) v <= g_nmax) { ui_w_set_value (g_nslider, v); ui_w_draw (g_nslider); ui_w_set_text (g_nerr, (long) ""); }
		else { char t[64]; snprintf (t, sizeof t, TR ("Entre 1 et %llu Go."), g_nmax); ui_w_set_text (g_nerr, (long) t); }
		ui_w_draw (g_nerr);
	}
	return 0;
}
static void disque_nouveau ()
{
	int s = disque_sel ();
	if (s < 0 || g_p[s].genre != G_FREE || g_np >= NPART - 1) return;
	Part f = g_p[s];
	g_nmax = (f.last - f.first + 1) / GIB;
	if (g_nmax < 1) return;
	long d = dlg_debut (460, 210, TR ("Nouvelle partition"), TR ("Créer"), TR ("Annuler"));
	if (!d) return;
	char t[160], b[32]; go_txt (b, sizeof b, f.last - f.first + 1);
	snprintf (t, sizeof t, TR ("Dans l'espace non alloué de %s. Taille, en Go :"), b);
	ui_label (g_bx + 16, g_by + 44, 428, 22, (long) t, 0);
	g_nslider = ui_slider (g_bx + 16, g_by + 78, 300, 28, 1, (long) g_nmax, (long) g_nmax);
	g_ntext = ui_textbox (g_bx + 330, g_by + 76, 80, 32, UTF_RIGHT);
	snprintf (t, sizeof t, "%llu", g_nmax); ui_w_set_text (g_ntext, (long) t);
	ui_label (g_bx + 416, g_by + 82, 30, 22, (long) TR ("Go"), 0);
	g_nerr = ui_label (g_bx + 16, g_by + 116, 428, 22, (long) "", ULB_DIM);
	long r = ui_dialog_run (d, (long) nouveau_fn, 0);
	long v = atol ((const char *) ui_w_get_text (g_ntext));
	ui_dialog_end (d);
	ui_w_draw (0);
	if (r != 1 || v < 1 || (u64) v > g_nmax) return;
	u64 n = (u64) v * GIB;
	if (n > f.last - f.first + 1) n = f.last - f.first + 1;
	memmove (&g_p[s + 1], &g_p[s], (size_t) (g_np - s) * sizeof (Part));
	g_np++;
	Part &p = g_p[s];
	memset (&p, 0, sizeof p);
	p.genre = G_NEW; p.num = g_newnum++; p.first = f.first; p.last = f.first + n - 1;
	if (p.last >= f.last - MIB) { p.last = f.last; memmove (&g_p[s + 1], &g_p[s + 2], (size_t) (g_np - s - 2) * sizeof (Part)); g_np--; }
	else { g_p[s + 1].first = p.last + 1; }
	disque_remplit (s);
	disque_note ();
}

// « Utiliser… » : le rôle de la ligne choisie (rien, /, /home) et son système de fichiers
static long g_ur[3], g_ufs_root, g_ufs_home, g_uavert;
static void utilise_maj ()
{
	bool r = ui_w_get_value (g_ur[1]) != 0, h = ui_w_get_value (g_ur[2]) != 0;
	int sp = disque_sel (); bool petit = sp >= 0 && g_p[sp].last - g_p[sp].first + 1 < MIN_KORIN;
	ui_w_show (g_ufs_root, r && !petit); ui_w_show (g_ufs_home, h);
	int s = disque_sel ();
	char n[120], z[240] = "";
	if (s >= 0 && (r || h))
	{
		if (g_p[s].genre == G_FREE || g_p[s].genre == G_NEW) cpy (z, TR ("Korin y crée une partition qui prend toute la place."), sizeof z);
		else snprintf (z, sizeof z, TR ("Elle sera formatée : tout ce que contient %s sera effacé."), nom_de (g_p[s], n, sizeof n));
	}
	ui_w_set_text (g_uavert, (long) z);
	ui_w_draw (0);
}
static long utilise_fn (long d, long w, long code)
{
	dlg_fn (d, w, code);
	if (code == UN_CHANGE && (w == g_ur[0] || w == g_ur[1] || w == g_ur[2])) utilise_maj ();
	return 0;
}
static void disque_utilise ()
{
	int s = disque_sel ();
	if (s < 0 || g_p[s].genre == G_EFI || g_p[s].genre == G_MSR || g_p[s].genre == G_REC) return;
	Part &p = g_p[s];
	char n[120], t[32], ti[200];
	go_txt (t, sizeof t, p.last - p.first + 1);
	snprintf (ti, sizeof ti, TR ("Utiliser %s (%s)"), nom_de (p, n, sizeof n), t);
	long d = dlg_debut (480, 300, ti, TR ("OK"), TR ("Annuler"));
	if (!d) return;
	bool petit = p.last - p.first + 1 < MIN_KORIN;
	g_ur[0] = ui_radio (g_bx + 16, g_by + 48, 448, 24, (long) TR ("Ne pas utiliser"), 4);
	g_ur[1] = ui_radio (g_bx + 16, g_by + 78, petit ? 448 : 230, 24, (long) (petit ? TR ("Système de Korin (/) : trop petite, il faut au moins 10 Go") : TR ("Système de Korin (/)")), 4);
	g_ur[2] = ui_radio (g_bx + 16, g_by + 108, 230, 24, (long) TR ("Mes fichiers (/home)"), 4);
	ui_w_enable (g_ur[1], !petit);
	ui_w_set_value (g_ur[p.role], 1);
	g_ufs_root = ui_dropdown (g_bx + 250, g_by + 74, 214, 32, (long) TR ("FAT32 (conseillé)|exFAT|ext4 (pas encore de journal)"));
	g_ufs_home = ui_dropdown (g_bx + 250, g_by + 104, 214, 32, (long) TR ("exFAT (conseillé)|ext4 (pas encore de journal)"));
	ui_w_set_value (g_ufs_root, p.role == R_ROOT ? (p.fs == FS_EXFAT ? 1 : p.fs == FS_EXT4 ? 2 : 0) : 0);
	ui_w_set_value (g_ufs_home, p.role == R_HOME && p.fs == FS_EXT4 ? 1 : 0);
	g_uavert = ui_label (g_bx + 16, g_by + 150, 448, 60, (long) "", ULB_WRAP);
	utilise_maj ();
	long r = ui_dialog_run (d, (long) utilise_fn, 0);
	int role = ui_w_get_value (g_ur[1]) ? R_ROOT : ui_w_get_value (g_ur[2]) ? R_HOME : R_NONE;
	long vr = ui_w_get_value (g_ufs_root);
	int fs = role == R_ROOT ? (vr == 1 ? FS_EXFAT : vr == 2 ? FS_EXT4 : FS_FAT32) : (ui_w_get_value (g_ufs_home) == 1 ? FS_EXT4 : FS_EXFAT);
	ui_dialog_end (d);
	ui_w_draw (0);
	if (r != 1) return;
	if (role) { int o = role_ligne (role); if (o >= 0) g_p[o].role = R_NONE; }	// (un seul / et un seul /home)
	p.role = role; p.fs = fs;
	disque_remplit (s);
	disque_note ();
}

// le choix fait sur la page Disque, pour la suite
static Part g_choix; static bool g_efi_neuve; static u64 g_place;	// place utile (secteurs)

// ---- 3 Mes fichiers ----------------------------------------------------------------------------------------------
static long g_hview, g_hslider, g_htext, g_hbornes, g_hnote, g_hfsl, g_hexfat, g_hext4, g_hext4n, g_hrootl, g_hroot, g_hrootn;
static bool g_ext4;
static int g_rootfs;	// le système de fichiers de / : FS_FAT32 (conseillé), FS_EXFAT, FS_EXT4
static long g_home_go;
static const long SYS_GO = 10;
static long home_max () { long m = (long) (g_place / GIB) - SYS_GO; return m < 0 ? 0 : m; }
static void hview_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	long sys = (long) (g_place / GIB) - g_home_go;
	int x = 0, bw = (int) w;
	if (g_efi_neuve) { rbox (0, 0, 64, (int) h, 8, jet (TJ_LINE_STRONG)); text (10, 14, "EFI", jet (TJ_TEXT1), 13); text (10, 34, s_dec == ',' ? "0,5 Go" : "0.5 GB", jet (TJ_TEXT1), 15, 1); x = 68; bw -= 68; }
	long tot = sys + g_home_go; if (tot <= 0) tot = 1;
	int ws = (int) (bw * sys / tot); if (g_home_go && ws > bw - 80) ws = bw - 80;
	rbox (x, 0, g_home_go ? ws - 2 : bw, (int) h, 8, jet (TJ_ACCENT));
	char t[48]; snprintf (t, sizeof t, "%ld %s", sys, TR ("Go"));
	text (x + 12, 14, TR ("Korin, système (/)"), 0xFFFFFF, 13); text (x + 12, 34, t, 0xFFFFFF, 15, 1);
	if (g_home_go)
	{
		rbox (x + ws, 0, bw - ws, (int) h, 8, jet (TJ_ACCENT_DARK));
		snprintf (t, sizeof t, "%ld %s", g_home_go, TR ("Go"));
		text (x + ws + 12, 14, TR ("Mes fichiers (/home)"), 0xFFFFFF, 13, 0, bw - ws - 20); text (x + ws + 12, 34, t, 0xFFFFFF, 15, 1);
	}
}
static void home_montre ()
{
	long m = home_max ();
	if (g_home_go > m) g_home_go = m;
	ui_w_set_range (g_hslider, 0, m, 1, 1);
	ui_w_set_value (g_hslider, g_home_go); ui_w_draw (g_hslider);
	char t[160]; snprintf (t, sizeof t, "%ld", g_home_go); ui_w_set_text (g_htext, (long) t); ui_w_draw (g_htext);
	snprintf (t, sizeof t, TR ("0 : pas de /home     %ld Go : le maximum (le système garde %ld Go)"), m, SYS_GO);
	ui_w_set_text (g_hbornes, (long) t); ui_w_draw (g_hbornes);
	ui_view_dirty (g_hview);
	long sys = (long) (g_place / GIB) - g_home_go;
	if (g_home_go) snprintf (t, sizeof t, TR ("/ %ld Go · /home %ld Go (%s)"), sys, g_home_go, g_ext4 ? "ext4" : "exFAT"); else snprintf (t, sizeof t, TR ("/home dans le système (%ld Go)"), sys);
	ui_w_set_text (g_status, (long) t); ui_w_draw (g_status);
	bool fs = g_home_go > 0;	// (le choix du système de fichiers : avec une partition /home seulement)
	ui_w_show (g_hfsl, fs); ui_w_show (g_hexfat, fs); ui_w_show (g_hext4, fs); ui_w_show (g_hext4n, fs && g_ext4);
	ui_w_set_value (g_hexfat, !g_ext4); ui_w_set_value (g_hext4, g_ext4);
	ui_w_set_value (g_hroot, g_rootfs); ui_w_show (g_hrootn, g_rootfs == FS_EXT4);
	ui_w_draw (0);
}

// ---- 4 Internet ----------------------------------------------------------------------------------------------------
static long g_wlist, g_wpass, g_wvoir, g_wjoin, g_wscan, g_wetat;
static unsigned char s_scan[32][48]; static int s_nscan, s_ordre[32];
static char s_ssid[40], s_cle[72];
static volatile bool s_tourne, s_fini; static int s_travail; static volatile long s_rc; static pthread_t s_tid;
enum { T_SCAN = 1, T_JOIN };
static bool g_connecte, g_cable;
static void *travail (void *)
{
	if (s_travail == T_SCAN) { long n = wifi_scan ((long) s_scan, 32); s_rc = n; s_nscan = n > 0 ? (int) n : 0; }
	else { s_rc = wifi_join ((long) s_ssid, s_cle[0] ? (long) s_cle : 0); if (s_rc == 0) for (int i = 0; i < 3 && !net_time (0); i++) net_ntp_sync (1); }
	s_fini = true;
	return 0;
}
static bool lance (int quoi)
{
	if (s_tourne) return false;
	s_travail = quoi; s_tourne = true; s_fini = false;
	if (pthread_create (&s_tid, 0, travail, 0) != 0) { s_tourne = false; return false; }
	return true;
}
static void cable_voit ()
{
	unsigned char b[16 * 64]; memset (b, 0, sizeof b);
	long n = net_ifaces ((long) b, 16);
	g_cable = false;
	for (long i = 0; i < n; i++)
	{
		unsigned char *e = b + i * 64;
		if (*(u32 *) (e + 8) == 1 && *(u32 *) (e + 16)) g_cable = true;	// Ethernet avec une adresse
	}
}
static void wifi_remplit ()
{
	ui_list_clear (g_wlist);
	int n = 0;
	for (int i = 0; i < s_nscan; i++) if (s_scan[i][32]) s_ordre[n++] = i;
	for (int i = 1; i < n; i++)	// par force du signal
		for (int j = i; j > 0 && *(short *) (s_scan[s_ordre[j]] + 36) > *(short *) (s_scan[s_ordre[j - 1]] + 36); j--)
		{ int t = s_ordre[j]; s_ordre[j] = s_ordre[j - 1]; s_ordre[j - 1] = t; }
	for (int k = 0; k < n; k++)
	{
		unsigned char *e = s_scan[s_ordre[k]];
		char t[96], ss[40]; int l = e[32] < 32 ? e[32] : 32;
		memcpy (ss, e, (size_t) l); ss[l] = 0;
		int r = *(short *) (e + 36), bars = r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : 1;
		snprintf (t, sizeof t, "%s\t%s\t%s", ss, e[34] ? TR ("protégé") : TR ("ouvert"), bars == 4 ? "▂▄▆█" : bars == 3 ? "▂▄▆" : bars == 2 ? "▂▄" : "▂");
		ui_list_add (g_wlist, (long) t, s_ordre[k]);
	}
	ui_w_draw (g_wlist);
}
static void net_etat ()
{
	cable_voit ();
	const char *e = g_cable ? TR ("Connecté par câble : cette étape est faite.") : g_connecte ? TR ("Connecté.") : "";
	if (*e) { ui_w_set_text (g_wetat, (long) e); ui_w_draw (g_wetat); }
	bool en = g_cable || g_connecte;
	ui_w_show (g_alt, g_page == P_NET && !en);
	ui_w_set_text (g_status, (long) (g_cable ? TR ("Connecté par câble") : g_connecte ? s_ssid : TR ("Aucun réseau choisi")));
	ui_w_draw (g_status);
	ui_w_enable (g_next, en);
}
static void wifi_connecte ()
{
	long v = ui_w_get_value (g_wlist);
	if (v < 0 || v >= ui_list_count (g_wlist)) { ui_w_set_text (g_wetat, (long) TR ("Choisissez un réseau dans la liste.")); ui_w_draw (g_wetat); return; }
	long data = 0; uikit_list_get (g_wlist, v, &data);
	unsigned char *e = s_scan[data]; int l = e[32] < 32 ? e[32] : 32;
	memcpy (s_ssid, e, (size_t) l); s_ssid[l] = 0;
	cpy (s_cle, (const char *) ui_w_get_text (g_wpass), sizeof s_cle);
	if (e[34] && !s_cle[0]) { ui_w_set_text (g_wetat, (long) TR ("Entrez le mot de passe du réseau.")); ui_w_draw (g_wetat); return; }
	if (lance (T_JOIN)) { ui_w_set_text (g_wetat, (long) TR ("Connexion…")); ui_w_draw (g_wetat); ui_w_enable (g_wjoin, 0); }
}

// ---- 5 Logiciels -----------------------------------------------------------------------------------------------------
struct Pq { char name[40], title[80], cat[32], ver[24]; u64 size; bool cle, req, coche; };
// la valeur de « clé = ... » dans la section [name] d'un index -> out (vide sans)
static void idx_get (const char *idx, const char *name, const char *key, char *out, size_t cap)
{
	out[0] = 0;
	if (!idx) return;
	char sect[64]; snprintf (sect, sizeof sect, "[%s]", name);
	size_t sl = strlen (sect), kl = strlen (key);
	for (const char *p = idx; *p; )
	{
		if (!strncmp (p, sect, sl) && (p[sl] == '\n' || p[sl] == '\r'))
		{
			for (const char *l = p + sl; *l && *l != '['; )
			{
				while (*l == '\n' || *l == '\r') l++;
				if (!strncmp (l, key, kl) && (l[kl] == ' ' || l[kl] == '='))
				{
					const char *v = l + kl; while (*v == ' ' || *v == '=') v++;
					size_t n = strcspn (v, "\r\n"); if (n >= cap) n = cap - 1;
					memcpy (out, v, n); out[n] = 0; return;
				}
				l += strcspn (l, "\n");
			}
			return;
		}
		p += strcspn (p, "\n"); if (*p) p++;
	}
}
// deux versions, nombre par nombre : < 0, 0, > 0
static int vcmp (const char *a, const char *b)
{
	while (*a || *b)
	{
		unsigned long long x = strtoull (a, (char **) &a, 10), y = strtoull (b, (char **) &b, 10);
		if (x != y) return x < y ? -1 : 1;
		while (*a && (*a < '0' || *a > '9')) a++;
		while (*b && (*b < '0' || *b > '9')) b++;
	}
	return 0;
}
// l'index en ligne : « sudo pkg check » le télécharge et le vérifie (/var/pkg/index.txt, écrit seulement avec une
// signature bonne ; la clé n'en a pas d'autre) ; lu une fois, dès qu'on est connecté
static volatile bool s_idx_tourne, s_idx_fini; static bool g_online, s_idx_fait; static pthread_t s_idx_tid;
static void *idx_thread (void *)
{
	if (FILE *f = popen ("/bin/sudo /bin/pkg check", "r")) { char l[256]; while (fgets (l, sizeof l, f)) {} pclose (f); }
	s_idx_fini = true;
	return 0;
}
static void index_en_ligne ()
{
	if (s_idx_tourne || s_idx_fait) return;
	s_idx_tourne = true; s_idx_fini = false;
	if (pthread_create (&s_idx_tid, 0, idx_thread, 0) != 0) { s_idx_tourne = false; s_idx_fait = true; }
}
struct Cat { char nom[32]; int first, n; bool ouvert; };
enum { NPQ = 400, NCAT = 24 };
static Pq g_pq[NPQ]; static int g_npq; static Cat g_cat[NCAT]; static int g_ncat;
static long g_pview, g_pbandeau; static int s_ptop;
static const char *ORDRE[24] = { "Système", "Accessoires", "Internet", "Bureautique", "Multimédia", "Création", "Développement", "Jeux", "Émulateurs", 0 };
static const char *DEFAUT[24] = { "Système", "Accessoires", "Internet", "Bureautique", 0 };
// /packages/pc/installer.ini (tools/pc/installkey.ini) : l'ordre des catégories et celles cochées d'office
static void categories_lit ()
{
	static char o[512], d[512];
	ini_lit ("/packages/pc/installer.ini", "order", o, sizeof o);
	ini_lit ("/packages/pc/installer.ini", "default", d, sizeof d);
	auto coupe = [](char *s, const char **t) {
		if (!*s) return;
		int n = 0;
		for (char *p = s; *p && n < 23; )
		{
			while (*p == ' ' || *p == ',') p++;
			if (!*p) break;
			t[n++] = p;
			while (*p && *p != ',') p++;
			char *e = p; while (e > t[n - 1] && e[-1] == ' ') e--;
			bool fin = !*p; *e = 0; if (!fin) p++;
			if (fin) break;
		}
		t[n] = 0;
	};
	coupe (o, ORDRE); coupe (d, DEFAUT);
}
static bool dans (const char *const *l, const char *s) { for (int i = 0; l[i]; i++) if (eq (l[i], s)) return true; return false; }
static bool pq_dispo (const Pq &p) { return p.cle || g_connecte || g_cable; }
static void paquets_lit ()
{
	g_npq = 0; g_ncat = 0;
	categories_lit ();
	char *kb = lire ("/packages/pc/index.txt");		// (la clé : ses archives, leurs versions)
	struct stat sto;
	bool on = g_online && stat ("/var/pkg/index.txt", &sto) == 0;
	char *b = on ? lire ("/var/pkg/index.txt") : kb;
	Pq cur; bool in = false; memset (&cur, 0, sizeof cur);
	char file[160] = "";
	auto fin = [&]() {
		if (in && cur.name[0] && cur.cat[0] && g_npq < NPQ && !eq (cur.name, "repo"))
		{
			char kf[160], kv[24];		// (l'archive et la version de la clé, même avec l'index en ligne)
			if (on) { idx_get (kb, cur.name, "file", kf, sizeof kf); idx_get (kb, cur.name, "version", kv, sizeof kv); }
			else { cpy (kf, file, sizeof kf); cpy (kv, cur.ver, sizeof kv); }
			if (kf[0])	// (une archive de la Release : son seul nom, dans pkgs/ de la clé)
			{
				char p[220]; const char *b = strrchr (kf, '/');
				if (strstr (kf, "://")) snprintf (p, sizeof p, "/packages/pc/pkgs/%s", b ? b + 1 : kf);
				else snprintf (p, sizeof p, "/packages/pc/%s", kf);
				struct stat st; cur.cle = stat (p, &st) == 0 && (!on || vcmp (kv, cur.ver) >= 0);	// (en ligne plus récent : téléchargé)
			}
			if (!cur.cle)	// (déjà dans le système de la clé, que l'installation copie : pc, pkgman)
			{
				char p[200]; snprintf (p, sizeof p, "/var/pkg/db/%s.ini", cur.name);
				struct stat st; cur.cle = stat (p, &st) == 0;
			}
			g_pq[g_npq++] = cur;
		}
		memset (&cur, 0, sizeof cur); file[0] = 0;
	};
	for (char *l = b; l && *l; )
	{
		char *e = strchr (l, '\n'); if (e) *e = 0;
		if (*l == '[') { fin (); in = true; char *c = strchr (l, ']'); if (c) *c = 0; cpy (cur.name, l + 1, sizeof cur.name); }
		else if (in)
		{
			char *eqp = strchr (l, '=');
			if (eqp)
			{
				*eqp = 0; char *k = l, *v = eqp + 1;
				while (*k == ' ') k++; for (char *c = k + strlen (k); c > k && c[-1] == ' '; ) *--c = 0;
				while (*v == ' ') v++; for (char *c = v + strlen (v); c > v && (c[-1] == ' ' || c[-1] == '\r'); ) *--c = 0;
				if (eq (k, "title")) cpy (cur.title, v, sizeof cur.title);
				else if (eq (k, "category")) cpy (cur.cat, v, sizeof cur.cat);
				else if (eq (k, "size")) cur.size = strtoull (v, 0, 10);
				else if (eq (k, "version")) cpy (cur.ver, v, sizeof cur.ver);
				else if (eq (k, "required")) cur.req = eq (v, "1");
				else if (eq (k, "file")) cpy (file, v, sizeof file);
			}
		}
		l = e ? e + 1 : l + strlen (l);
	}
	fin ();
	if (b != kb) free (b);
	free (kb);
	if (!g_npq)	// (une clé sans dépôt : le système copié seulement)
	{
		Pq &p = g_pq[g_npq++]; memset (&p, 0, sizeof p);
		cpy (p.name, "pc", sizeof p.name); cpy (p.title, TR ("Système Korin OS"), sizeof p.title); cpy (p.cat, "Système", sizeof p.cat);
		p.size = 85ull << 20; p.cle = p.req = true;
	}
	// les catégories, dans l'ordre voulu puis les autres ; les paquets triés par catégorie
	Pq *t = (Pq *) malloc (sizeof (Pq) * (size_t) g_npq); int nt = 0;
	auto ajoute_cat = [&](const char *c) {
		int f = nt;
		for (int i = 0; i < g_npq; i++) if (eq (g_pq[i].cat, c)) t[nt++] = g_pq[i];
		if (nt > f && g_ncat < NCAT) { Cat &k = g_cat[g_ncat++]; cpy (k.nom, c, sizeof k.nom); k.first = f; k.n = nt - f; k.ouvert = false; }
	};
	for (int i = 0; ORDRE[i]; i++) ajoute_cat (ORDRE[i]);
	for (int i = 0; i < g_npq; i++)
	{
		bool vu = false;
		for (int c = 0; c < g_ncat; c++) if (eq (g_cat[c].nom, g_pq[i].cat)) vu = true;
		if (!vu) ajoute_cat (g_pq[i].cat);
	}
	memcpy (g_pq, t, sizeof (Pq) * (size_t) nt); g_npq = nt; free (t);
	for (int c = 0; c < g_ncat; c++)
		for (int i = g_cat[c].first; i < g_cat[c].first + g_cat[c].n; i++)
			g_pq[i].coche = g_pq[i].req || dans (DEFAUT, g_cat[c].nom);
}
static void mo_txt (char *o, size_t cap, u64 b) { if (b >= (1ull << 30)) snprintf (o, cap, "%.1f %s", (double) b / (1 << 30), TR ("Go")); else snprintf (o, cap, "%llu %s", (b >> 20) ? (b >> 20) : 1, TR ("Mo")); for (char *c = o; *c; c++) if (*c == '.') *c = s_dec; }
enum { CARTE_H = 72, PQ_H = 34 };
static int pview_hauteur ()
{
	int h = 0;
	for (int c = 0; c < g_ncat; c++) h += CARTE_H + 10 + (g_cat[c].ouvert ? g_cat[c].n * PQ_H + 6 : 0);
	return h;
}
static int cat_etat (int c, u64 *taille)	// 0 rien, 1 en partie, 2 tout (des paquets disponibles)
{
	int dispo = 0, coches = 0; u64 t = 0;
	for (int i = g_cat[c].first; i < g_cat[c].first + g_cat[c].n; i++)
		if (pq_dispo (g_pq[i])) { dispo++; if (g_pq[i].coche) { coches++; t += g_pq[i].size; } }
	if (taille) *taille = t;
	return !coches ? 0 : coches == dispo ? 2 : 1;
}
static void case_dessine (int x, int y, int etat, bool actif)
{
	unsigned c = actif ? jet (TJ_ACCENT) : jet (TJ_LINE_STRONG);
	if (!etat) { rbox (x, y, 18, 18, 4, jet (TJ_LINE_STRONG)); rbox (x + 2, y + 2, 14, 14, 3, jet (TJ_SURFACE)); return; }
	rbox (x, y, 18, 18, 4, c);
	if (etat == 1) ui_fill (x + 4, y + 8, 10, 2, 0xFFFFFF);
	else { ui_vp_line ((x + 4) * 16, (y + 9) * 16, (x + 8) * 16, (y + 13) * 16, 32, 0xFFFFFF); ui_vp_line ((x + 8) * 16, (y + 13) * 16, (x + 14) * 16, (y + 5) * 16, 32, 0xFFFFFF); }
}
static void pview_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	int m = pview_hauteur () - (int) h; if (s_ptop > m) s_ptop = m; if (s_ptop < 0) s_ptop = 0;
	int y = -s_ptop;
	for (int c = 0; c < g_ncat; c++)
	{
		Cat &k = g_cat[c];
		int hc = CARTE_H + (k.ouvert ? k.n * PQ_H + 6 : 0);
		u64 t; int e = cat_etat (c, &t);
		bool fixe = eq (k.nom, "Système");
		int dispo = 0, cle = 0;
		for (int i = k.first; i < k.first + k.n; i++) { dispo += pq_dispo (g_pq[i]); cle += g_pq[i].cle; }
		if (y + hc > 0 && y < h)
		{
			rbox (0, y, (int) w, hc, 10, e ? jet (TJ_ACCENT) : jet (TJ_LINE));
			rbox (e ? 2 : 1, y + (e ? 2 : 1), (int) w - (e ? 4 : 2), hc - (e ? 4 : 2), 9, e ? jet (TJ_ACCENT_LIGHT) : jet (TJ_SURFACE));
			case_dessine (14, y + 14, fixe ? 2 : e, !fixe && dispo);
			text (44, y + 12, TR (k.nom), dispo ? jet (TJ_TEXT1) : jet (TJ_TEXT3), 15, 1, (int) w - 200);
			char tt[48] = "", tl[64]; if (e) mo_txt (tt, sizeof tt, t);
			snprintf (tl, sizeof tl, "%s%s%s", fixe ? TR ("Toujours installé") : "", fixe ? " · " : "", tt);
			int tw = (int) ui_text_width ((long) tl, 13, (long) ui.font); text ((int) w - 14 - tw, y + 14, tl, jet (TJ_TEXT2), 13);
			char apps[200] = ""; int na = 0;
			for (int i = k.first; i < k.first + k.n && na < 5; i++, na++) { if (na) strncat (apps, ", ", sizeof apps - strlen (apps) - 1); strncat (apps, g_pq[i].title, sizeof apps - strlen (apps) - 1); }
			if (k.n > 5) { char z[32]; snprintf (z, sizeof z, " %s", TR ("et %d autres")); { char zz[32]; snprintf (zz, sizeof zz, z, k.n - 5); cpy (z, zz, sizeof z); } strncat (apps, z, sizeof apps - strlen (apps) - 1); }
			text (44, y + 34, apps, jet (TJ_TEXT2), 13, 0, (int) w - 60);
			char orig[96];
			if (!dispo) cpy (orig, TR ("Avec Internet seulement, plus tard dans Logiciels"), sizeof orig);
			else if (cle == k.n) cpy (orig, TR ("Sur la clé"), sizeof orig);
			else if (cle) snprintf (orig, sizeof orig, TR ("%d sur %d sur la clé"), cle, k.n);
			else cpy (orig, TR ("À télécharger"), sizeof orig);
			text (44, y + 52, orig, cle == k.n ? jet (TJ_SUCCESS) : jet (TJ_TEXT3), 12);
			if (!fixe && dispo) { const char *ch = k.ouvert ? TR ("Replier") : TR ("Choisir"); int cw = (int) ui_text_width ((long) ch, 12, (long) ui.font); text ((int) w - 14 - cw, y + 52, ch, jet (TJ_ACCENT), 12); }
			if (k.ouvert)
				for (int i = k.first, yy = y + CARTE_H; i < k.first + k.n; i++, yy += PQ_H)
				{
					Pq &p = g_pq[i];
					ui_fill (44, yy, (int) w - 58, 1, jet (TJ_LINE));
					case_dessine (44, yy + 8, p.coche && pq_dispo (p) ? 2 : 0, pq_dispo (p));
					text (72, yy + 8, p.title, pq_dispo (p) ? jet (TJ_TEXT1) : jet (TJ_TEXT3), 13, 0, (int) w - 220);
					char s[32]; mo_txt (s, sizeof s, p.size);
					const char *o = p.cle ? TR ("sur la clé") : pq_dispo (p) ? TR ("à télécharger") : TR ("avec Internet");
					char r[80]; snprintf (r, sizeof r, "%s · %s", o, s);
					int rw = (int) ui_text_width ((long) r, 12, (long) ui.font); text ((int) w - 14 - rw, yy + 10, r, jet (TJ_TEXT3), 12);
				}
		}
		y += hc + 10;
	}
}
static void paquets_etat ()
{
	u64 tot = 0, dl = 0;
	for (int i = 0; i < g_npq; i++) if (g_pq[i].coche && pq_dispo (g_pq[i])) { tot += g_pq[i].size * 3; if (!g_pq[i].cle) dl += g_pq[i].size; }	// (installé ≈ 3 fois l'archive)
	u64 place = g_place * 512 - (u64) g_home_go * (1ull << 30);
	char a[32], b[32], c[32], t[160];
	mo_txt (a, sizeof a, tot); mo_txt (b, sizeof b, dl); mo_txt (c, sizeof c, place > tot ? place - tot : 0);
	bool trop = tot + (1ull << 30) > place;
	if (trop) snprintf (t, sizeof t, TR ("Trop grand pour la place de Korin (%s)"), a);
	else if (dl) snprintf (t, sizeof t, TR ("%s, dont %s à télécharger · %s libres après"), a, b, c);
	else snprintf (t, sizeof t, TR ("%s · %s libres après"), a, c);
	ui_w_set_text (g_status, (long) t); ui_w_draw (g_status);
	ui_w_enable (g_next, !trop);
	ui_w_set_text (g_pbandeau, (long) (g_connecte || g_cable ? "" : TR ("Sans Internet : seuls les logiciels de la clé. Le reste s'installera ensuite par Logiciels.")));
	ui_w_draw (g_pbandeau);
	ui_view_dirty (g_pview);
}
static int s_pdown, s_pdowny, s_pdowntop; static bool s_pdrag;
static void pview_clic (int my, int mx, int w)
{
	int y = -s_ptop;
	for (int c = 0; c < g_ncat; c++)
	{
		Cat &k = g_cat[c];
		int hc = CARTE_H + (k.ouvert ? k.n * PQ_H + 6 : 0);
		if (my >= y && my < y + hc)
		{
			bool fixe = eq (k.nom, "Système");
			if (my < y + CARTE_H)
			{
				if (fixe) return;
				if (mx > w - 120 && my > y + 44) { k.ouvert = !k.ouvert; ui_view_dirty (g_pview); return; }
				int e = cat_etat (c, 0);
				for (int i = k.first; i < k.first + k.n; i++) if (pq_dispo (g_pq[i])) g_pq[i].coche = e != 2;
			}
			else
			{
				int i = k.first + (my - y - CARTE_H) / PQ_H;
				if (i < k.first + k.n && pq_dispo (g_pq[i]) && !g_pq[i].req) g_pq[i].coche = !g_pq[i].coche;
			}
			paquets_etat ();
			return;
		}
		y += hc + 10;
	}
}
static long pview_mouse (long, long kind, long mx, long my, long info)
{
	if (kind == 1 && (info >> 8 & 1)) { s_pdown = 1; s_pdowny = (int) my; s_pdowntop = s_ptop; s_pdrag = false; }
	else if (kind == 0 && s_pdown)
	{
		int dy = (int) my - s_pdowny;
		if (s_pdrag || dy > 8 || dy < -8) { s_pdrag = true; s_ptop = s_pdowntop - dy; ui_view_dirty (g_pview); }
	}
	else if (kind == 2 && s_pdown) { s_pdown = 0; if (!s_pdrag) pview_clic ((int) my, (int) mx, CW); }
	return 0;
}
static long pview_wheel (long, long steps) { s_ptop -= (int) steps * 48; ui_view_dirty (g_pview); return 0; }

// ---- 6 Compte --------------------------------------------------------------------------------------------------------
static long g_anom, g_apc, g_auser, g_apw, g_apw2, g_avoir, g_aauto, g_adem, g_aadmin, g_arootl, g_arpw, g_arpw2, g_amsg, g_ajauge;
static bool s_pcmain, s_usermain;
static void simple (const char *s, char *o, size_t cap)	// « Stéphane » -> « stephane »
{
	size_t k = 0;
	for (const unsigned char *p = (const unsigned char *) s; *p && *p != ' ' && k < cap - 1; p++)
	{
		unsigned c = *p;
		if (c >= 'A' && c <= 'Z') o[k++] = (char) (c + 32);
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o[k++] = (char) c;
		else if (c == 0xC3 && p[1])	// les accents courants du latin-1
		{
			unsigned d = p[1] | 0x20; p++;
			const char *m = "aaaaaaaceeeeiiii_nooooo_ouuuuy_y";
			if (d >= 0xA0 && d <= 0xBF) { char x = m[d - 0xA0]; if (x != '_') o[k++] = x; }
		}
	}
	o[k] = 0;
}
static int force (const char *p)
{
	size_t n = strlen (p); if (!n) return 0;
	bool maj = false, min = false, chi = false, aut = false;
	for (const char *c = p; *c; c++) { if (*c >= 'A' && *c <= 'Z') maj = true; else if (*c >= 'a' && *c <= 'z') min = true; else if (*c >= '0' && *c <= '9') chi = true; else aut = true; }
	int s = (n >= 8) + (n >= 12) + (maj && min) + (chi && aut);
	return s < 1 ? 1 : s;
}
static bool user_ok (const char *u)
{
	if (!u[0] || !(u[0] >= 'a' && u[0] <= 'z')) return false;
	for (const char *c = u; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return false;
	return !eq (u, "system") && !eq (u, "root") && !eq (u, "admin") && !eq (u, "user");
}
static void compte_maj ()
{
	char u[32];
	if (!s_usermain) { simple ((const char *) ui_w_get_text (g_anom), u, sizeof u); ui_w_set_text (g_auser, (long) u); ui_w_draw (g_auser); }
	if (!s_pcmain && !((const char *) ui_w_get_text (g_apc))[0]) { ui_w_set_text (g_apc, (long) "korin-pc"); ui_w_draw (g_apc); }
	const char *user = (const char *) ui_w_get_text (g_auser), *pw = (const char *) ui_w_get_text (g_apw), *pw2 = (const char *) ui_w_get_text (g_apw2);
	bool admin = ui_w_get_value (g_aadmin) != 0, root = !admin || !pw[0];
	static int root_vu = -1;
	ui_w_show (g_arootl, root); ui_w_show (g_arpw, root); ui_w_show (g_arpw2, root);
	if (root != root_vu)	/* (un widget caché laisse son dessin : la zone est refaite) */
	{
		root_vu = root;
		ui_fill (X0, Y0 + 256, CW, 64, jet (TJ_FACE));
		ui_w_draw (0); ui_update_all ();
	}
	const char *msg = "";
	static char m[200];
	if (!user_ok (user)) msg = TR ("Nom d'utilisateur : minuscules et chiffres, une lettre d'abord ; ni system, ni root, ni admin, ni user.");
	else if (pw2[0] && strcmp (pw, pw2)) msg = TR ("Les deux mots de passe sont différents.");
	else if (root)
	{
		const char *r = (const char *) ui_w_get_text (g_arpw), *r2 = (const char *) ui_w_get_text (g_arpw2);
		if (!r[0]) msg = !admin ? TR ("Quelqu'un d'autre administrera cet ordinateur : choisissez le mot de passe de root.") : TR ("Sans mot de passe pour vous, root en a quand même un : choisissez-le.");
		else if (strcmp (r, r2)) msg = TR ("Les deux mots de passe de root sont différents.");
		else if (!admin && pw[0] && eq (r, pw)) msg = TR ("Choisissez pour root un autre mot de passe que le vôtre.");
	}
	else if (!pw[0]) msg = TR ("Sans mot de passe, n'importe qui peut ouvrir votre session.");
	bool ok = user_ok (user) && !(pw2[0] && strcmp (pw, pw2)) && (!pw[0] || eq (pw, pw2));
	if (root) { const char *r = (const char *) ui_w_get_text (g_arpw), *r2 = (const char *) ui_w_get_text (g_arpw2); ok = ok && r[0] && eq (r, r2) && !(!admin && pw[0] && eq (r, pw)); }
	cpy (m, msg, sizeof m);
	ui_w_set_text (g_amsg, (long) m); ui_w_draw (g_amsg);
	static const char *const MOTS[] = { "", "Mot de passe faible", "Mot de passe moyen", "Bon mot de passe", "Mot de passe fort" };
	ui_w_set_text (g_ajauge, (long) TR (MOTS[force (pw)])); ui_w_draw (g_ajauge);
	ui_w_enable (g_next, ok);
	snprintf (m, sizeof m, "%s (%s)%s", (const char *) ui_w_get_text (g_anom), user, admin ? TR (", administrateur") : "");
	ui_w_set_text (g_status, (long) (ok ? m : "")); ui_w_draw (g_status);
}
static void empreinte (const char *pw, char *o, size_t cap)
{
	if (!pw[0]) { o[0] = 0; return; }
	u8 salt[16], h[32];
	sysc (SYS_GETRANDOM_, (long) salt, 16);
	pbkdf2 (pw, salt, 16, 20000, h);
	char sh[33], hh[65];
	for (int i = 0; i < 16; i++) snprintf (sh + 2 * i, 3, "%02x", salt[i]);
	for (int i = 0; i < 32; i++) snprintf (hh + 2 * i, 3, "%02x", h[i]);
	snprintf (o, cap, "pbkdf2-sha256$20000$%s$%s", sh, hh);
}

// ---- 7 Résumé --------------------------------------------------------------------------------------------------------
static long g_sview, g_sconf, g_sconfl, g_sdef, g_sdelai;
static bool s_danger; static char s_disque_nom[16] = "DISQUE0";
static const Part *part_num (int n) { for (int i = 0; i < g_np; i++) if (g_p[i].genre != G_FREE && g_p[i].num == n) return &g_p[i]; return 0; }
static void sview_reinst (long w, long h)
{
	(void) h;
	char t[32], t2[32], z[220];
	const Part *r = part_num (g_kroot), *hm = part_num (g_khome);
	go_txt (t, sizeof t, r ? r->last - r->first + 1 : 0);
	// la barre : EFI gardée, puis KORIN OS (et KORIN HOME gardée, à part) ; sans KORIN HOME, une seule bande
	rbox (0, 0, 64, 64, 8, jet (TJ_LINE_STRONG)); text (10, 14, "EFI", jet (TJ_TEXT1), 13); text (10, 34, TR ("gardée"), jet (TJ_TEXT1), 14, 1);
	int x = 68, bw = (int) w - 68;
	if (hm)
	{
		go_txt (t2, sizeof t2, hm->last - hm->first + 1);
		u64 a = r ? r->last - r->first + 1 : 1, b = hm->last - hm->first + 1;
		int ws = (int) (bw * a / (a + b)); if (ws < 180) ws = 180; if (ws > bw - 180) ws = bw - 180;
		rbox (x, 0, ws - 2, 64, 8, jet (TJ_ACCENT));
		text (x + 12, 14, TR ("KORIN OS, reformatée"), 0xFFFFFF, 13, 0, ws - 24); text (x + 12, 34, t, 0xFFFFFF, 15, 1);
		rbox (x + ws, 0, bw - ws, 64, 8, jet (TJ_SUCCESS));
		text (x + ws + 12, 14, TR ("KORIN HOME, gardée"), 0xFFFFFF, 13, 0, bw - ws - 24); text (x + ws + 12, 34, t2, 0xFFFFFF, 15, 1);
	}
	else
	{
		rbox (x, 0, bw, 64, 8, jet (TJ_ACCENT));
		text (x + 12, 14, TR ("Korin OS : le système, remis à neuf, vos fichiers préservés"), 0xFFFFFF, 13, 0, bw - 24); text (x + 12, 34, t, 0xFFFFFF, 15, 1);
	}
	// ce qui est repris, les logiciels, le démarrage
	int y0 = 80;
	rbox (0, y0, (int) w, 4 * 46 + 2, 8, jet (TJ_LINE)); rbox (1, y0 + 1, (int) w - 2, 4 * 46, 8, jet (TJ_SURFACE));
	char l[4][220]; const char *lb[4] = { "Comptes", "Réglages repris", "Logiciels", "Démarrage" };
	char us[200]; cpy (us, g_kusers, sizeof us); for (char *c = us; *c; c++) if (*c == ',') *c = ' ';
	snprintf (l[0], 220, TR ("repris, avec leurs mots de passe : %s"), us[0] && !eq (us, "?") ? us : TR ("ceux de l'ancien système"));
	cpy (l[1], TR ("nom de l'ordinateur, langue, clavier, réseaux Wi-Fi"), 220);
	int nc = 0; for (int i = 0; i < g_ncat; i++) if (cat_etat (i, 0)) nc++;
	snprintf (l[2], 220, nc > 1 ? TR ("%d catégories choisies") : TR ("%d catégorie choisie"), nc);
	bool win = false; for (int i = 0; i < g_np; i++) if (g_p[i].genre == G_WIN) win = true;
	if (win) snprintf (l[3], 220, TR ("Menu Korin OS / Windows, %s dans %ld s"), ui_w_get_value (g_sdef) ? "Windows" : "Korin", ui_w_get_value (g_sdelai) * 5);
	else cpy (l[3], TR ("Korin OS démarre directement"), 220);
	for (int i = 0; i < 4; i++)
	{
		int y = y0 + 1 + i * 46;
		if (i) ui_fill (1, y, (int) w - 2, 1, jet (TJ_LINE));
		text (14, y + 13, TR (lb[i]), jet (TJ_TEXT2), 13, 0, 170);
		text (190, y + 13, l[i], jet (TJ_TEXT1), 14, 0, (int) w - 200);
	}
	int y = y0 + 4 * 46 + 16;
	rbox (0, y, (int) w, 54, 8, jet (TJ_WARNING));
	rbox (1, y + 1, (int) w - 2, 52, 8, jet (TJ_SURFACE));
	snprintf (z, sizeof z, "%s", hm ? TR ("Sera effacé : le système Korin (KORIN OS), avec ses logiciels. Vos fichiers de KORIN HOME ne sont pas touchés.")
					: TR ("Sera effacé : tout le contenu de KORIN OS sauf /home, avec ses logiciels. Vos fichiers ne sont pas touchés."));
	ui_text_wrap ((long) z, 14, y + 8, w - 28, jet (TJ_WARNING), (long) ui.font_b, 14, 0);
}
static void sview_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	if (g_reinst) { sview_reinst (w, h); return; }
	rbox (0, 0, (int) w, 6 * 46 + 2, 8, jet (TJ_LINE)); rbox (1, 1, (int) w - 2, 6 * 46, 8, jet (TJ_SURFACE));
	char l[6][200]; const char *lb[6] = { "Langue et clavier", "Disque", "Logiciels", "Compte", "Démarrage", "Nom de l'ordinateur" };
	snprintf (l[0], 200, "%s · %s · %s %s", g_langc[0] == 'e' ? "English" : "Français", g_paysc, TR ("clavier"), g_kb);
	char t[32], n[120]; go_txt (t, sizeof t, g_choix.last - g_choix.first + 1);
	int ir = role_ligne (R_ROOT), ih = role_ligne (R_HOME);
	if (ir >= 0)
	{
		char a[120], b[120], ta[32], tb[32];
		go_txt (ta, sizeof ta, g_p[ir].last - g_p[ir].first + 1);
		snprintf (l[1], 200, "/ : %s, %s (%s)", nom_de (g_p[ir], a, sizeof a), ta, FS_NOMS[g_p[ir].fs]);
		if (ih >= 0) { go_txt (tb, sizeof tb, g_p[ih].last - g_p[ih].first + 1); char z[120]; snprintf (z, sizeof z, " · /home : %s, %s (%s)", nom_de (g_p[ih], b, sizeof b), tb, FS_NOMS[g_p[ih].fs]); strncat (l[1], z, 199 - strlen (l[1])); }
	}
	else if (g_choix.genre == G_FREE) snprintf (l[1], 200, TR ("Korin dans %s d'espace libre"), t);
	else if (g_choix.genre == G_NEW) snprintf (l[1], 200, TR ("Korin dans la nouvelle partition (%s)"), t);
	else snprintf (l[1], 200, TR ("Korin à la place de %s (%s)"), nom_de (g_choix, n, sizeof n), t);
	if (ir < 0 && g_rootfs != FS_FAT32) { char z[48]; snprintf (z, sizeof z, " · / %s", FS_NOMS[g_rootfs]); strncat (l[1], z, 199 - strlen (l[1])); }
	if (g_home_go && ir < 0) { char z[64]; snprintf (z, sizeof z, " · %s", TR ("/home %ld Go (%s)")); { char zz[64]; snprintf (zz, sizeof zz, z, g_home_go, g_ext4 ? "ext4" : "exFAT"); cpy (z, zz, sizeof z); } strncat (l[1], z, 199 - strlen (l[1])); }
	int nc = 0; for (int i = 0; i < g_ncat; i++) if (cat_etat (i, 0)) nc++;
	snprintf (l[2], 200, nc > 1 ? TR ("%d catégories choisies") : TR ("%d catégorie choisie"), nc);
	snprintf (l[3], 200, "%s (%s)%s", (const char *) ui_w_get_text (g_anom), (const char *) ui_w_get_text (g_auser), ui_w_get_value (g_aadmin) ? TR (", administrateur") : "");
	bool win = false; for (int i = 0; i < g_np; i++) if (g_p[i].genre == G_WIN) win = true;
	if (win) snprintf (l[4], 200, TR ("Menu Korin OS / Windows, %s dans %ld s"), ui_w_get_value (g_sdef) ? "Windows" : "Korin", ui_w_get_value (g_sdelai) * 5);
	else cpy (l[4], TR ("Korin OS démarre directement"), 200);
	cpy (l[5], (const char *) ui_w_get_text (g_apc), 200);
	for (int i = 0; i < 6; i++)
	{
		int y = 1 + i * 46;
		if (i) ui_fill (1, y, (int) w - 2, 1, jet (TJ_LINE));
		text (14, y + 13, TR (lb[i]), jet (TJ_TEXT2), 13, 0, 170);
		text (190, y + 13, l[i], jet (TJ_TEXT1), 14, 0, (int) w - 290);
		if (i != 4) text ((int) w - 80, y + 13, TR ("Modifier"), jet (TJ_ACCENT), 13);
	}
	int y = 6 * 46 + 16;
	rbox (0, y, (int) w, 54, 8, s_danger ? jet (TJ_DANGER) : jet (TJ_SUCCESS));
	rbox (1, y + 1, (int) w - 2, 52, 8, jet (TJ_SURFACE));
	if (s_danger && ir >= 0)	// (les rôles : les partitions formatées, et celles supprimées)
	{
		char z[300]; cpy (z, TR ("Seront effacées définitivement :"), sizeof z);
		int k = 0;
		for (int i = 0; i < g_np; i++)
			if ((g_p[i].role) && g_p[i].genre != G_FREE && g_p[i].genre != G_NEW)
			{ char a[120], ta[32], y[180]; go_txt (ta, sizeof ta, g_p[i].last - g_p[i].first + 1); snprintf (y, sizeof y, "%s %s (%s)", k++ ? "," : "", nom_de (g_p[i], a, sizeof a), ta); strncat (z, y, sizeof z - strlen (z) - 1); }
		if (g_ndel) { char y[80], f[80]; snprintf (f, sizeof f, " %s", TR ("et %d partition(s) supprimée(s)")); snprintf (y, sizeof y, f, g_ndel); strncat (z, y, sizeof z - strlen (z) - 1); }
		strncat (z, ".", sizeof z - strlen (z) - 1);
		ui_text_wrap ((long) z, 14, y + 8, w - 28, jet (TJ_DANGER), (long) ui.font_b, 14, 0);
	}
	else if (s_danger) { char z[200]; snprintf (z, sizeof z, TR ("Sera effacé définitivement : %s (%s)."), nom_de (g_choix, n, sizeof n), t); text (14, y + 17, z, jet (TJ_DANGER), 14, 1, (int) w - 28); }
	else text (14, y + 17, TR ("Rien ne sera effacé : les partitions gardées restent telles quelles."), jet (TJ_SUCCESS), 14, 1, (int) w - 28);
}
static long sview_mouse (long, long kind, long mx, long my, long)
{
	if (kind != 2 || mx < CW - 100 || g_reinst) return 0;
	int i = (int) my / 46;
	static const int pages[6] = { P_WELCOME, P_DISK, P_PKG, P_ACCOUNT, -1, P_ACCOUNT };
	if (i >= 0 && i < 6 && pages[i] >= 0) { g_back_to_summary = 1; extern void montre (int); montre (pages[i]); }
	return 0;
}
static void resume_etat ()
{
	bool ok = !s_danger || !strcasecmp ((const char *) ui_w_get_text (g_sconf), s_disque_nom);
	ui_w_enable (g_next, ok);
	ui_w_set_text (g_next, (long) (g_reinst ? TR ("Réinstaller") : s_danger ? TR ("Effacer et installer") : TR ("Installer"))); ui_w_draw (g_next);
	ui_w_set_text (g_status, (long) (ok ? TR ("La seule écriture sur le disque : ce bouton.") : TR ("Tapez le nom du disque pour confirmer.")));
	ui_w_draw (g_status);
}

// ---- 8 Installation, 9 Terminé ---------------------------------------------------------------------------------------
static long g_iview, g_iprog, g_ipct, g_itache, g_idet, g_ijournal, g_dmsg;
static volatile int s_ipct, s_istep, s_ifini, s_irc; static char s_itache[160], s_ierr[320];
static char s_journal[16][160]; static int s_nj; static pthread_mutex_t s_jm = PTHREAD_MUTEX_INITIALIZER;
static int s_diapo; static long s_diapo_t; static bool s_detail;
static const char *const DIAPOS[5][2] = {
	{ "Bienvenue dans Korin OS", "Un système léger, écrit pour cet ordinateur. Le bureau Elegant, les apps de base et vos réglages arrivent dans un instant." },
	{ "Vos logiciels, quand vous voulez", "Logiciels installe le reste plus tard : Jet, les jeux, les outils de développement, et leurs mises à jour." },
	{ "Vos fichiers", "Fichiers range vos documents dans votre dossier personnel ; une clé USB s'y montre dès qu'elle est branchée." },
	{ "Givre et Onyx Noir", "Deux thèmes, clair et sombre, se changent d'un clic dans Réglages ; toutes les apps les suivent." },
	{ "Le Terminal et les langages", "bash, C, C++, C#, Python et BASIC : Korin se programme depuis Korin." } };
static const char *const ETAPES[5] = { "Préparer le disque", "Copier le système", "Installer les logiciels", "Régler l'ordinateur", "Rendre Korin démarrable" };
static void *installe (void *)
{
	FILE *f = popen ("/bin/sudo /sbin/osinstall /tmp/install/plan.ini", "r");
	if (!f) { cpy (s_ierr, TR ("osinstall n'a pas pu être lancé."), sizeof s_ierr); s_irc = 1; s_ifini = 1; return 0; }
	char l[256];
	while (fgets (l, sizeof l, f))
	{
		l[strcspn (l, "\r\n")] = 0;
		pthread_mutex_lock (&s_jm);
		if (s_nj == 16) { memmove (s_journal[0], s_journal[1], sizeof s_journal[0] * 15); s_nj = 15; }
		cpy (s_journal[s_nj++], l, sizeof s_journal[0]);
		pthread_mutex_unlock (&s_jm);
		if (l[0] == 'S' && l[1] == ' ') s_istep = atoi (l + 2);
		else if (l[0] == 'P' && l[1] == ' ') { s_ipct = atoi (l + 2); const char *t = strchr (l + 2, ' '); cpy (s_itache, t ? t + 1 : "", sizeof s_itache); }
		else if (l[0] == 'X' && l[1] == ' ') { const char *t = strchr (l + 2, ' '); cpy (s_ierr, t ? t + 1 : l, sizeof s_ierr); s_irc = 1; }
		else if (eq (l, "OK")) s_ipct = 100;
	}
	int rc = pclose (f);
	remove ("/tmp/install/plan.ini");	// (les empreintes des mots de passe : osinstall les retire en le lisant ; lui pas lancé, elles partent ici)
	if (rc && !s_irc) { s_irc = 1; if (!s_ierr[0]) cpy (s_ierr, TR ("L'installation s'est arrêtée."), sizeof s_ierr); }
	s_ifini = 1;
	return 0;
}
static void iview_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	rbox (0, 0, (int) w, (int) h, 10, jet (TJ_LINE)); rbox (1, 1, (int) w - 2, (int) h - 2, 10, jet (TJ_SURFACE));
	rbox (1, 1, 190, (int) h - 2, 10, jet (TJ_ACCENT)); ui_fill (180, 1, 11, (int) h - 2, jet (TJ_ACCENT));
	int cx = 96, cy = (int) h / 2;
	int32_t pts[8] = { cx * 16, (cy - 30) * 16, (cx + 30) * 16, cy * 16, cx * 16, (cy + 30) * 16, (cx - 30) * 16, cy * 16 };
	ui_vp_poly ((long) pts, 4, 0xFFFFFF);
	text (214, 22, TR (DIAPOS[s_diapo][0]), jet (TJ_TEXT1), 20, 1, (int) w - 230);
	ui_text_wrap ((long) TR (DIAPOS[s_diapo][1]), 214, 60, w - 236, jet (TJ_TEXT2), (long) ui.font, 14, 0);
	for (int i = 0; i < 5; i++) rbox (214 + i * 16, (int) h - 22, i == s_diapo ? 14 : 8, 8, 4, i == s_diapo ? jet (TJ_ACCENT) : jet (TJ_LINE_STRONG));
}
static long iview_mouse (long, long kind, long mx, long, long)
{
	if (kind == 2) { s_diapo = mx < 200 ? (s_diapo + 4) % 5 : (s_diapo + 1) % 5; s_diapo_t = 0; ui_view_dirty (g_iview); }
	return 0;
}
static void install_poll ()
{
	if (g_page != P_INSTALL) return;
	// (toutes les 30 ms : seulement ce qui a changé est réécrit et redessiné ; sinon le journal clignote)
	static long d_pct = -1; static char d_t[220], d_tache[sizeof s_itache], d_j[16 * 162]; static bool d_det;
	if (s_ipct != d_pct) { d_pct = s_ipct; ui_w_set_value (g_iprog, s_ipct); ui_w_draw (g_iprog); }
	char t[220];
	int e = s_istep < 1 ? 1 : s_istep > 5 ? 5 : s_istep;
	snprintf (t, sizeof t, TR ("%d %% · Étape %d sur 5 · %s"), s_ipct, e, TR (ETAPES[e - 1]));
	if (strcmp (t, d_t)) { cpy (d_t, t, sizeof d_t); ui_w_set_text (g_ipct, (long) t); ui_w_draw (g_ipct); }
	if (strcmp (s_itache, d_tache)) { cpy (d_tache, s_itache, sizeof d_tache); ui_w_set_text (g_itache, (long) s_itache); ui_w_draw (g_itache); }
	if (s_detail)
	{
		static char j[16 * 162]; j[0] = 0;
		pthread_mutex_lock (&s_jm);
		for (int i = 0; i < s_nj; i++) { strncat (j, s_journal[i], sizeof j - strlen (j) - 2); strcat (j, "\n"); }
		pthread_mutex_unlock (&s_jm);
		if (!d_det || strcmp (j, d_j)) { cpy (d_j, j, sizeof d_j); ui_w_set_text (g_ijournal, (long) j); ui_w_draw (g_ijournal); }
	}
	d_det = s_detail;
	if (++s_diapo_t > 650) { s_diapo_t = 0; s_diapo = (s_diapo + 1) % 5; ui_view_dirty (g_iview); }	// (20 s)
	if (s_ifini) { extern void montre (int); montre (P_DONE); }
}

// ---- le plan --------------------------------------------------------------------------------------------------------
static void plan_ecrit ()
{
	mkdir ("/tmp/install", 0700);
	FILE *f = fopen ("/tmp/install/plan.ini", "w");
	if (!f) return;
	chmod ("/tmp/install/plan.ini", 0600);
	if (g_reinst)	// (la réinstallation : KORIN OS, KORIN HOME ; les comptes et réglages viennent de l'ancien système)
	{
		bool win = false; for (int i = 0; i < g_np; i++) if (g_p[i].genre == G_WIN) win = true;
		fprintf (f, "# plan de l'installateur (apps/installer.app) pour osinstall : réinstallation\n[disk]\ndevice = %s\nmode = reinstall\nroot = %d\nhome_part = %d\n", g_dev, g_kroot, g_khome);
		fprintf (f, "\n[system]\nlang = %s\nkeyb = %s\ntimeout = %ld\n\n[user]\npasswords = none\n", g_langc, g_kb, win ? ui_w_get_value (g_sdelai) * 5 : 0);
		fprintf (f, "\n[packages]\nonline = %s\nlist =", g_online ? "yes" : "no");
		for (int i = 0; i < g_npq; i++) if (g_pq[i].coche && pq_dispo (g_pq[i])) fprintf (f, " %s", g_pq[i].name);
		fprintf (f, "\n");
		fclose (f);
		return;
	}
	if (roles_on ())	// (les rôles : / et /home sur des partitions choisies ou des espaces libres)
	{
		fprintf (f, "# plan de l'installateur (apps/installer.app) pour osinstall : partitions choisies\n[disk]\ndevice = %s\nmode = beside\ndelete = ", g_dev);
		for (int i = 0; i < g_ndel; i++) fprintf (f, "%s%d", i ? ", " : "", g_del[i]);
		fprintf (f, "\n");
		static const char *const quoi[3] = { "", "root", "home" };
		for (int r = R_ROOT; r <= R_HOME; r++)
		{
			int i = role_ligne (r);
			if (i < 0) continue;
			if (g_p[i].genre == G_FREE || g_p[i].genre == G_NEW) fprintf (f, "%s_place = %llu-%llu\n", quoi[r], g_p[i].first, g_p[i].last);
			else fprintf (f, "%s_part = %d\n", quoi[r], g_p[i].num);
			fprintf (f, "%s_fs = %s\n", quoi[r], FS_PLAN[g_p[i].fs]);
		}
		fprintf (f, "min_gb = %ld\n", (long) (MIN_KORIN / GIB));
	}
	else
	{
	fprintf (f, "# plan de l'installateur (apps/installer.app) pour osinstall\n[disk]\ndevice = %s\nmode = %s\n", g_dev, g_gpt ? "beside" : "wipe");
	fprintf (f, "delete = ");
	for (int i = 0; i < g_ndel; i++) fprintf (f, "%s%d", i ? ", " : "", g_del[i]);
	if (g_choix.genre != G_FREE && g_choix.genre != G_NEW) fprintf (f, "%s%d", g_ndel ? ", " : "", g_choix.num);
	fprintf (f, "\nplace = %llu-%llu\nroot_fs = %s\nhome = %s\nroot_gb = %ld\nhome_fs = %s\nboot_mb = 512\nmin_gb = %ld\n",
		g_choix.first, g_choix.last, FS_PLAN[g_rootfs], g_home_go ? "yes" : "no", (long) (g_place / GIB) - g_home_go, g_ext4 ? "ext4" : "exfat", (long) (MIN_KORIN / GIB));
	}
	bool win = false; for (int i = 0; i < g_np; i++) if (g_p[i].genre == G_WIN) win = true;
	fprintf (f, "\n[system]\nhostname = %s\nlang = %s\nkeyb = %s\ntimeout = %ld\n", (const char *) ui_w_get_text (g_apc), g_langc, g_kb,
		win ? ui_w_get_value (g_sdelai) * 5 : 0);
	char h[160], rh[160];
	const char *pw = (const char *) ui_w_get_text (g_apw);
	bool admin = ui_w_get_value (g_aadmin) != 0;
	empreinte (pw, h, sizeof h);
	if (!admin || !pw[0]) empreinte ((const char *) ui_w_get_text (g_arpw), rh, sizeof rh); else cpy (rh, h, sizeof rh);
	fprintf (f, "\n[user]\nname = %s\nfullname = %s\nautologin = %s\nadmin = %s\npasswords = none\nhash = %s\nroot_hash = %s\n",
		(const char *) ui_w_get_text (g_auser), (const char *) ui_w_get_text (g_anom), ui_w_get_value (g_aauto) ? "yes" : "no", admin ? "yes" : "no", h, rh);
	fprintf (f, "\n[packages]\nonline = %s\nlist =", g_online ? "yes" : "no");
	for (int i = 0; i < g_npq; i++) if (g_pq[i].coche && pq_dispo (g_pq[i])) fprintf (f, " %s", g_pq[i].name);
	fprintf (f, "\n");
	fclose (f);
	memset (h, 0, sizeof h); memset (rh, 0, sizeof rh);
}

// ---- le cadre --------------------------------------------------------------------------------------------------------
static const char *const NOMS_ETAPES[] = { "Disque", "Internet", "Logiciels", "Compte", "Résumé" };
static const int ETAPES_NEUVE[] = { 0, 1, 2, 3, 4 }, ETAPES_REINST[] = { 1, 2, 4 };	// (réinstallation : ni Disque ni Compte)
static int n_etapes () { return g_reinst ? 3 : 5; }
static const char *nom_etape (int i) { return NOMS_ETAPES[(g_reinst ? ETAPES_REINST : ETAPES_NEUVE)[i]]; }
static int etape_de (int p)
{
	int e = p == P_DISK || p == P_HOME ? 0 : p == P_NET ? 1 : p == P_PKG ? 2 : p == P_ACCOUNT ? 3 : p == P_SUMMARY ? 4 : -1;
	if (e < 0 || !g_reinst) return e;
	for (int i = 0; i < 3; i++) if (ETAPES_REINST[i] == e) return i;
	return -1;
}
static void band_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	ui_fill (0, h - 1, w, 1, jet (TJ_LINE));
	int32_t pts[8] = { 26 * 16, 16 * 16, 33 * 16, 23 * 16, 26 * 16, 30 * 16, 19 * 16, 23 * 16 };
	ui_vp_poly ((long) pts, 4, jet (TJ_ACCENT));
	text (42, 13, "Korin OS", jet (TJ_TEXT1), 14, 1);
	int e = etape_de (g_page);
	if (e >= 0 && w >= 900)
	{
		int x = (int) w / 2 - (g_reinst ? 130 : 230);
		for (int i = 0; i < n_etapes (); i++)
		{
			char t[48]; snprintf (t, sizeof t, "%s%s", i < e ? "✓ " : "", TR (nom_etape (i)));
			unsigned c = i == e ? jet (TJ_TEXT1) : i < e ? jet (TJ_TEXT2) : jet (TJ_TEXT3);
			int tw = (int) ui_text_width ((long) t, 13, i == e ? (long) ui.font_b : (long) ui.font);
			text (x, 15, t, c, 13, i == e);
			if (i == e) ui_fill (x, (int) h - 4, tw, 2, jet (TJ_ACCENT));
			x += tw + 24;
		}
	}
	else if (e >= 0) { char t[64]; snprintf (t, sizeof t, TR ("Étape %d sur %d · %s"), e + 1, n_etapes (), TR (nom_etape (e))); text ((int) w / 2 - 90, 15, t, jet (TJ_TEXT2), 13); }
	rbox ((int) w - 64, 10, 48, 28, 14, jet (TJ_LINE)); rbox ((int) w - 63, 11, 46, 26, 13, jet (TJ_SURFACE));
	int kw = (int) ui_text_width ((long) g_kb, 12, (long) ui.font_b); text ((int) w - 40 - kw / 2, 15, g_kb, jet (TJ_TEXT2), 12, 1);
}
static void head_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	const char *titre = TR (TITRES[g_page]), *chapeau = TR (CHAPEAUX[g_page]);
	static char ch[320];
	if (g_page == P_DONE && s_irc) titre = TR ("Korin OS n'est pas installé");
	else if (g_page == P_DONE && g_reinst) titre = TR ("Korin OS est réinstallé");
	else if (g_page == P_SUMMARY && g_reinst) titre = TR ("Prêt à réinstaller");
	else if (g_page == P_INSTALL && g_reinst) titre = TR ("Réinstallation de Korin OS");
	else if (g_page == P_WELCOME && g_kroot)
	{
		titre = TR ("Korin OS est déjà installé sur cet ordinateur");
		const Part *hm = part_num (g_khome);
		char t[32] = ""; if (hm) go_txt (t, sizeof t, hm->last - hm->first + 1);
		char us[200]; cpy (us, g_kusers, sizeof us); for (char *c = us; *c; c++) if (*c == ',') *c = ' ';
		char a[240];
		if (hm) snprintf (a, sizeof a, TR ("vos fichiers dans la partition KORIN HOME (%s)"), t); else cpy (a, TR ("vos fichiers dans le dossier /home de KORIN OS"), sizeof a);
		snprintf (ch, sizeof ch, TR ("Version %s, compte %s, %s."), eq (g_kver, "?") ? "-" : g_kver, eq (us, "?") ? "-" : us, a);
		chapeau = ch;
	}
	text (0, 0, titre, jet (TJ_TEXT1), 26, 1, (int) w);
	if (*chapeau) ui_text_wrap ((long) chapeau, 0, 44, w, jet (TJ_TEXT2), (long) ui.font, 15, 0);
}
static void foot_paint (long, long w, long h) { ui_fill (0, 0, w, h, jet (TJ_FACE)); ui_fill (0, 0, w, 1, jet (TJ_LINE)); }

void montre (int p)
{
	int vient = g_page;
	g_page = p;
	for (int i = 0; i < g_nitems; i++) ui_w_show (g_items[i].w, g_items[i].page == p);
	ui_w_show (g_back, p > P_WELCOME && p < P_INSTALL);
	ui_w_set_text (g_back, (long) (g_back_to_summary && p < P_SUMMARY ? TR ("Revenir au résumé") : TR ("Retour")));
	ui_w_show (g_alt, p == P_WELCOME);
	ui_w_set_text (g_alt, (long) TR ("Essayer Korin sans l'installer"));
	ui_w_show (g_next, p != P_INSTALL);
	ui_w_enable (g_next, 1);
	ui_w_set_text (g_next, (long) (p == P_WELCOME ? (g_kroot && g_reinst ? TR ("Réinstaller…") : TR ("Installer")) : p == P_DONE ? TR ("Redémarrer") : g_back_to_summary && p < P_SUMMARY ? TR ("Revenir au résumé") : TR ("Continuer")));
	ui_w_set_text (g_status, (long) ""); ui_w_show (g_status, p != P_DONE);
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	ui_view_dirty (g_band); ui_view_dirty (g_head); ui_view_dirty (g_foot);
	if (p == P_WELCOME)
	{
		ui_w_show (g_tzreg, s_tzopen); ui_w_show (g_tzville, s_tzopen);
		bool k = g_kroot > 0;	// (le choix : seulement si un Korin est installé)
		ui_w_show (g_wreinst, k); ui_w_show (g_wneuve, k); ui_w_show (g_wreinstl, k); ui_w_show (g_wneuvel, k);
		ui_w_set_value (g_wreinst, g_reinst); ui_w_set_value (g_wneuve, !g_reinst);
	}
	if (p == P_INSTALL) ui_w_show (g_ijournal, s_detail);
	if (p == P_DISK)
	{
		int sel = disque_sel ();
		if (!g_sel_init)	/* la première fois : le plus grand espace libre (comme la maquette) */
		{
			if (!g_lu) disque_lit ();
			g_sel_init = true;
			u64 best = 0; sel = 0;
			for (int i = 0; i < g_np; i++) if (g_p[i].genre == G_FREE && g_p[i].last - g_p[i].first + 1 > best) { best = g_p[i].last - g_p[i].first + 1; sel = i; }
		}
		disque_remplit (sel < 0 ? 0 : sel); disque_note ();
	}
	else if (p == P_HOME) home_montre ();
	else if (p == P_NET)
	{
		ui_w_set_text (g_alt, (long) TR ("Continuer sans Internet"));
		if (!s_nscan && !s_tourne) { lance (T_SCAN); ui_w_set_text (g_wetat, (long) TR ("Recherche des réseaux…")); }
		net_etat ();
	}
	else if (p == P_PKG) { if (!g_ncat) paquets_lit (); paquets_etat (); }
	else if (p == P_ACCOUNT) compte_maj ();
	else if (p == P_SUMMARY)
	{
		s_danger = !g_reinst && g_choix.genre != G_FREE && g_choix.genre != G_NEW;	// (réinstallation : vos fichiers restent, pas de nom à taper)
		if (!g_reinst && roles_on ())	// (les rôles : une partition existante formatée)
		{
			s_danger = false;
			for (int i = 0; i < g_np; i++) if (g_p[i].role && g_p[i].genre != G_FREE && g_p[i].genre != G_NEW) s_danger = true;
		}
		for (int i = 0; i < g_ndel && !g_reinst; i++) s_danger = true;
		bool win = false; for (int i = 0; i < g_np; i++) if (g_p[i].genre == G_WIN) win = true;
		ui_w_show (g_sconf, s_danger); ui_w_show (g_sconfl, s_danger);
		ui_w_show (g_sdef, win); ui_w_show (g_sdelai, win);
		g_back_to_summary = 0;
		ui_view_dirty (g_sview);
		resume_etat ();
	}
	else if (p == P_DONE)
	{
		bool win = false; for (int i = 0; i < g_np; i++) if (g_p[i].genre == G_WIN) win = true;
		char m[600];
		if (s_irc) snprintf (m, sizeof m, "%s\n\n%s", TR ("L'installation n'a pas abouti :"), s_ierr);
		else snprintf (m, sizeof m, "%s\n\n%s", TR ("Redémarrez en laissant la clé branchée : au premier démarrage, Korin s'inscrit dans le menu de démarrage de l'ordinateur. Ensuite, retirez la clé ; elle ne sert plus."),
			win ? TR ("À chaque démarrage, un menu laisse choisir Korin OS ou Windows.") : TR ("Korin OS démarre directement."));
		ui_w_set_text (g_dmsg, (long) m);
	}
	(void) vient;
	ui_w_draw (0); ui_view_flush (); ui_update_all ();
}
static void suivant ()
{
	switch (g_page)
	{
	case P_WELCOME:
		if (g_kroot && g_reinst)	// (réinstallation : la place de Korin est KORIN OS, pour la taille des logiciels)
		{
			const Part *r = part_num (g_kroot);
			g_place = r ? r->last - r->first + 1 : 0; g_home_go = 0; g_efi_neuve = false;
			montre (P_NET);
		}
		else montre (P_DISK);
		break;
	case P_DISK:
	{
		int s = disque_sel ();
		if (!verdict ().ok) return;
		if (roles_on ())	// (/ donné par « Utiliser… » : pas d'étape Mes fichiers)
		{
			g_choix = g_p[role_ligne (R_ROOT)]; g_efi_neuve = false;
			g_place = g_choix.last - g_choix.first + 1; g_home_go = 0;
			montre (g_back_to_summary ? P_SUMMARY : P_NET);
			break;
		}
		if (s < 0) return;
		g_choix = g_p[s];
		g_efi_neuve = !efi_reste ();
		g_place = g_choix.last - g_choix.first + 1 - (g_efi_neuve ? EFI_NEUVE : 0);
		g_home_go = 0;
		montre (g_back_to_summary ? P_SUMMARY : P_HOME);
		break;
	}
	case P_HOME: montre (g_back_to_summary ? P_SUMMARY : P_NET); break;
	case P_NET: montre (g_back_to_summary ? P_SUMMARY : P_PKG); break;
	case P_PKG: montre (g_back_to_summary || g_reinst ? P_SUMMARY : P_ACCOUNT); break;
	case P_ACCOUNT: montre (P_SUMMARY); break;
	case P_SUMMARY:
		plan_ecrit ();
		s_ipct = 0; s_istep = 1; s_ifini = 0; s_irc = 0; s_nj = 0; s_ierr[0] = s_itache[0] = 0;
		{ pthread_t t; if (pthread_create (&t, 0, installe, 0) != 0) { cpy (s_ierr, TR ("Pas de thread pour l'installation."), sizeof s_ierr); s_irc = 1; s_ifini = 1; } }
		montre (P_INSTALL);
		break;
	case P_DONE:
		if (FILE *r = popen ("/bin/sudo /bin/reboot", "r")) pclose (r);
		break;
	}
}
static void precedent ()
{
	if (g_back_to_summary && g_page < P_SUMMARY) { montre (P_SUMMARY); return; }
	static const int avant[P_N] = { P_WELCOME, P_WELCOME, P_DISK, P_HOME, P_NET, P_PKG, P_ACCOUNT, P_SUMMARY, P_SUMMARY };
	if (g_reinst && g_page == P_NET) { montre (P_WELCOME); return; }		// (réinstallation : ni Disque ni Compte)
	if (!g_reinst && g_page == P_NET && roles_on ()) { montre (P_DISK); return; }	// (les rôles : pas d'étape Mes fichiers)
	if (g_reinst && g_page == P_SUMMARY) { montre (P_PKG); return; }
	montre (avant[g_page]);
}
static void control (long w, long code)
{
	if (code == UN_ACTIVATE)
	{
		if (w == g_next) suivant ();
		else if (w == g_back) precedent ();
		else if (w == g_alt)
		{
			if (g_page == P_NET) montre (g_back_to_summary ? P_SUMMARY : P_PKG);
			else if (g_page == P_WELCOME) exit (0);			// (le bureau de la clé : l'icône « Installer Korin OS » relance)
		}
		else if (w == g_tzbtn) { s_tzopen = !s_tzopen; ui_w_show (g_tzreg, s_tzopen); ui_w_show (g_tzville, s_tzopen); ui_w_set_text (g_tzbtn, (long) (s_tzopen ? TR ("Fermer") : TR ("Changer"))); ui_w_draw (0); }
		else if (w == g_dact) { disque_lit (); disque_remplit (0); disque_note (); }
		else if (w == g_dsup) disque_supprime ();
		else if (w == g_dnouv) disque_nouveau ();
		else if (w == g_duse) disque_utilise ();
		else if (w == g_draz) { for (int i = 0; i < g_np; i++) g_p[i].role = R_NONE; disque_remplit (disque_sel ()); disque_note (); }
		else if (w == g_wjoin || w == g_wpass) wifi_connecte ();
		else if (w == g_wscan) { if (lance (T_SCAN)) { ui_w_set_text (g_wetat, (long) TR ("Recherche des réseaux…")); ui_w_draw (g_wetat); } }
		else if (w == g_idet) { s_detail = !s_detail; ui_w_show (g_ijournal, s_detail); ui_w_set_text (g_idet, (long) (s_detail ? TR ("Cacher les détails") : TR ("Afficher les détails"))); ui_w_draw (0); }
		else if (w == g_htext) { long v = atol ((const char *) ui_w_get_text (g_htext)); if (v < 0) v = 0; if (v > home_max ()) v = home_max (); g_home_go = v; home_montre (); }
		return;
	}
	if (code != UN_CHANGE) return;
	if (w == g_lang) langue_choisie ();
	else if (w == g_pays) pays_choisi ();
	else if (w == g_keyb) { long v = ui_w_get_value (g_keyb); if (v >= 0 && v < g_nkeyb) clavier_applique (g_keybs[v]); }
	else if (w == g_tzreg) { long r = ui_w_get_value (g_tzreg); if (r >= 0 && r < N_REG) villes_remplit ((int) r); }
	else if (w == g_tzville) { long v = ui_w_get_value (g_tzville); if (v >= 0 && v < s_nville) { cpy (g_tz, s_tz[s_ville[v]], sizeof g_tz); ui_w_set_text (g_tzlab, (long) g_tz); ui_w_draw (g_tzlab); } }
	else if (w == g_dlist) disque_note ();
	else if (w == g_hslider) { g_home_go = ui_w_get_value (g_hslider); home_montre (); }
	else if (w == g_hroot) { long v = ui_w_get_value (g_hroot); g_rootfs = v == 1 ? FS_EXFAT : v == 2 ? FS_EXT4 : FS_FAT32; home_montre (); }
	else if (w == g_hexfat || w == g_hext4) { g_ext4 = w == g_hext4 ? ui_w_get_value (w) != 0 : ui_w_get_value (w) == 0; home_montre (); }
	else if (w == g_wreinst || w == g_wneuve)
	{
		g_reinst = w == g_wreinst ? ui_w_get_value (w) != 0 : ui_w_get_value (w) == 0;
		ui_w_set_text (g_next, (long) (g_reinst ? TR ("Réinstaller…") : TR ("Installer"))); ui_w_draw (g_next);
		ui_view_dirty (g_band);
	}
	else if (w == g_wvoir) { ui_w_style (g_wpass, ui_w_get_value (w) ? 0 : UTF_PASSWORD); ui_w_draw (g_wpass); }
	else if (w == g_avoir) { long s = ui_w_get_value (w) ? 0 : UTF_PASSWORD; ui_w_style (g_apw, s); ui_w_style (g_apw2, s); ui_w_style (g_arpw, s); ui_w_style (g_arpw2, s); ui_w_draw (0); }
	else if (w == g_auser) { s_usermain = true; compte_maj (); }
	else if (w == g_apc) { s_pcmain = true; compte_maj (); }
	else if (w == g_anom || w == g_apw || w == g_apw2 || w == g_arpw || w == g_arpw2 || w == g_aauto || w == g_adem || w == g_aadmin) compte_maj ();
	else if (w == g_sconf) resume_etat ();
	else if (w == g_sdef || w == g_sdelai) ui_view_dirty (g_sview);
}
static void idx_poll ()
{
	if ((g_connecte || g_cable) && !s_idx_fait) index_en_ligne ();
	if (!s_idx_tourne || !s_idx_fini) return;
	pthread_join (s_idx_tid, 0);
	s_idx_tourne = false; s_idx_fait = true;
	struct stat st;
	g_online = stat ("/var/pkg/index.txt", &st) == 0;
	if (!g_online || !g_ncat) return;	// (pas encore lue : elle le sera avec l'index en ligne)
	static char vus[NPQ][40]; static bool coches[NPQ]; int n = 0;
	for (int i = 0; i < g_npq && n < NPQ; i++) { cpy (vus[n], g_pq[i].name, 40); coches[n++] = g_pq[i].coche; }
	paquets_lit ();
	for (int i = 0; i < g_npq; i++) for (int k = 0; k < n; k++) if (eq (g_pq[i].name, vus[k])) g_pq[i].coche = coches[k] || g_pq[i].req;
	if (g_page == P_PKG) paquets_etat ();
}
static void net_poll ()
{
	if (!s_tourne || !s_fini) return;
	pthread_join (s_tid, 0);
	s_tourne = false;
	if (s_travail == T_SCAN)
	{
		wifi_remplit ();
		ui_w_set_text (g_wetat, (long) (s_rc < 0 ? TR ("Pas de Wi-Fi sur cet ordinateur.") : s_nscan ? "" : TR ("Aucun réseau en vue. Actualiser pour chercher encore."))); ui_w_draw (g_wetat);
	}
	else
	{
		ui_w_enable (g_wjoin, 1);
		g_connecte = s_rc == 0;
		ui_w_set_text (g_wetat, (long) (g_connecte ? TR ("✓ Connecté.") : TR ("Mot de passe refusé, ou réseau injoignable : vérifiez le mot de passe, majuscules comprises.")));
		ui_w_draw (g_wetat);
	}
	if (g_page == P_NET) net_etat ();
}

int main (int, char **)
{
	if (!uikit_open (&ui)) return 1;
	netkit_kit_open ();
	if (char *l = lire ("/tmp/install/lang")) { if (!strncmp (l, "en", 2)) cpy (g_langc, "en", sizeof g_langc); free (l); }
	else if (eq ((const char *) ui_lang (), "en")) cpy (g_langc, "en", sizeof g_langc);
	(void) TR ("");		// (le dictionnaire du système d'abord : celui de l'installateur, chargé après, l'emporte)
	if (g_langc[0] == 'e') { s_dec = '.'; ui_tr_load ((long) "/apps/installer.app/lang/en.txt"); }	// (aussi quand le système est en anglais)
	s_jet = (const unsigned *) ui_tokens ();
	long scr[4] = { 1280, 800, 0, 0 };
	ui_screen ((long) scr);
	SW = (int) scr[0]; SH = (int) scr[1];
	if (ui_window_ex (0, 0, SW, SH, (long) TR ("Installer Korin OS"), WF_FRAMELESS | WF_TOP | WF_NOLIST) <= 0) return 1;
	CW = SW - 48 < COLW ? SW - 48 : COLW; X0 = (SW - CW) / 2;
	int ytitre = BAND + (SH < 700 ? 24 : 48);
	Y0 = ytitre + 110;
	int H = SH - FOOT - 16 - Y0;			// hauteur du contenu
	g_band = ui_view (0, 0, SW, BAND, 0); ui_view_on (g_band, (long) band_paint, 0, 0, 0);
	g_head = ui_view (X0, ytitre, CW, 100, 0); ui_view_on (g_head, (long) head_paint, 0, 0, 0);
	g_foot = ui_view (0, SH - FOOT, SW, FOOT, 0); ui_view_on (g_foot, (long) foot_paint, 0, 0, 0);
	g_back = ui_pushbutton (18, SH - FOOT + 18, 170, 36, (long) TR ("Retour"), UB_GHOST);
	g_alt = ui_pushbutton (18, SH - FOOT + 18, 260, 36, (long) "", 0);
	g_status = ui_label (300, SH - FOOT + 26, SW - 300 - 360, 22, (long) "", ULB_DIM);
	g_next = ui_pushbutton (SW - 18 - 200, SH - FOOT + 18, 200, 36, (long) TR ("Continuer"), UB_DEFAULT);
	char l[1024];
	int X = X0, W = CW;

	// 1 Bienvenue
	item (P_WELCOME, ui_label (X, Y0, W, 20, (long) TR ("Langue"), ULB_DIM));
	g_lang = item (P_WELCOME, ui_segmented (X, Y0 + 22, 0, 0, (long) "English|Français"));
	ui_w_set_value (g_lang, g_langc[0] == 'e' ? 0 : 1);
	l[0] = 0;
	for (int i = 0; i < N_PAYS; i++) { if (i) strncat (l, "|", sizeof l - strlen (l) - 1); strncat (l, TR (PAYS[i][1]), sizeof l - strlen (l) - 1); }
	int half = (W - 20) / 2;
	item (P_WELCOME, ui_label (X, Y0 + 72, half, 20, (long) TR ("Pays"), ULB_DIM));
	g_pays = item (P_WELCOME, ui_dropdown (X, Y0 + 94, half, 34, (long) l));
	claviers_lit (l, sizeof l);
	item (P_WELCOME, ui_label (X + half + 20, Y0 + 72, half, 20, (long) TR ("Clavier"), ULB_DIM));
	g_keyb = item (P_WELCOME, ui_dropdown (X + half + 20, Y0 + 94, half, 34, (long) l));
	for (int i = 0; i < g_nkeyb; i++) if (eq (g_keybs[i], "BE")) ui_w_set_value (g_keyb, i);
	item (P_WELCOME, ui_label (X, Y0 + 144, W, 20, (long) TR ("Essayez le clavier ici"), ULB_DIM));
	g_essai = item (P_WELCOME, ui_textbox (X, Y0 + 166, W, 34, 0));
	item (P_WELCOME, ui_label (X, Y0 + 218, W, 20, (long) TR ("Fuseau horaire"), ULB_DIM));
	g_tzlab = item (P_WELCOME, ui_label (X, Y0 + 242, 300, 22, (long) g_tz, ULB_BOLD));
	g_tzbtn = item (P_WELCOME, ui_pushbutton (X + 310, Y0 + 236, 110, 32, (long) TR ("Changer"), UB_GHOST));
	l[0] = 0;
	for (int i = 0; i < N_REG; i++) { if (i) strncat (l, "|", sizeof l - strlen (l) - 1); strncat (l, REGIONS[i], sizeof l - strlen (l) - 1); }
	g_tzreg = item (P_WELCOME, ui_dropdown (X, Y0 + 278, half, 34, (long) l));
	g_tzville = item (P_WELCOME, ui_dropdown (X + half + 20, Y0 + 278, half, 34, (long) "-"));
	fuseaux_lit ();
	// un Korin déjà installé : Réinstaller (par défaut) ou Nouvelle installation
	g_wreinst = item (P_WELCOME, ui_radio (X, Y0 + 332, W, 24, (long) TR ("Réinstaller Korin OS en gardant mes fichiers"), 2));
	g_wreinstl = item (P_WELCOME, ui_label (X + 28, Y0 + 358, W - 28, 40, (long) TR ("Le système est remis à neuf ; vos fichiers de /home ne sont pas touchés. Vos comptes, mots de passe et réseaux Wi-Fi sont repris. Rien à choisir sur le disque."), ULB_WRAP | ULB_DIM));
	g_wneuve = item (P_WELCOME, ui_radio (X, Y0 + 404, W, 24, (long) TR ("Nouvelle installation"), 2));
	g_wneuvel = item (P_WELCOME, ui_label (X + 28, Y0 + 430, W - 28, 22, (long) TR ("Choisir la place sur le disque, comme pour une première installation."), ULB_WRAP | ULB_DIM));

	// 2 Disque
	int lh = H - 150 < 160 ? 160 : H - 150;
	g_dlist = item (P_DISK, ui_list (X, Y0, W, lh, UL_HEADER | UL_NOSORT));
	ui_list_column (g_dlist, (long) TR ("Nom"), W - 90 - 150 - 200, 0);
	ui_list_column (g_dlist, (long) TR ("Taille"), 90, ULC_RIGHT);
	ui_list_column (g_dlist, (long) TR ("Type"), 150, 0);
	ui_list_column (g_dlist, (long) TR ("Utilisation"), 200, 0);
	g_dact = item (P_DISK, ui_pushbutton (X, Y0 + lh + 8, 120, 32, (long) TR ("Actualiser"), UB_GHOST));
	g_dsup = item (P_DISK, ui_pushbutton (X + 128, Y0 + lh + 8, 120, 32, (long) TR ("Supprimer"), UB_GHOST));
	g_dnouv = item (P_DISK, ui_pushbutton (X + 256, Y0 + lh + 8, 120, 32, (long) TR ("Nouveau"), UB_GHOST));
	g_duse = item (P_DISK, ui_pushbutton (X + 384, Y0 + lh + 8, 120, 32, (long) TR ("Utiliser…"), UB_GHOST));
	g_draz = item (P_DISK, ui_pushbutton (X + 512, Y0 + lh + 8, 168, 32, (long) TR ("Effacer les rôles"), UB_GHOST));
	g_dnote = item (P_DISK, ui_label (X, Y0 + lh + 48, W, 44, (long) "", ULB_WRAP));
	g_defi = item (P_DISK, ui_label (X, Y0 + lh + 94, W, 40, (long) "", ULB_WRAP | ULB_DIM));

	// 3 Mes fichiers
	g_hview = item (P_HOME, ui_view (X, Y0, W, 64, 0)); ui_view_on (g_hview, (long) hview_paint, 0, 0, 0);
	g_hslider = item (P_HOME, ui_slider (X, Y0 + 84, W - 130, 28, 0, 100, 0));
	g_htext = item (P_HOME, ui_textbox (X + W - 120, Y0 + 82, 80, 32, UTF_RIGHT));
	item (P_HOME, ui_label (X + W - 34, Y0 + 88, 34, 22, (long) TR ("Go"), 0));
	g_hbornes = item (P_HOME, ui_label (X, Y0 + 124, W, 20, (long) "", ULB_DIM));
	g_hnote = item (P_HOME, ui_label (X, Y0 + 156, W, 40, (long) TR ("Le système de Korin garde au moins 10 Go. Entrée dans le champ : la taille tapée."), ULB_WRAP | ULB_DIM));
	g_hfsl = item (P_HOME, ui_label (X, Y0 + 204, W, 20, (long) TR ("Système de fichiers de /home"), ULB_BOLD));
	g_hexfat = item (P_HOME, ui_radio (X, Y0 + 232, W, 24, (long) TR ("exFAT (conseillé) : lisible aussi par Windows et macOS"), 3));
	g_hext4 = item (P_HOME, ui_radio (X, Y0 + 260, W, 24, (long) TR ("ext4 : garde les droits des fichiers (Unix), lisible par Linux"), 3));
	g_hrootl = item (P_HOME, ui_label (X, Y0 + 344, 260, 22, (long) TR ("Système de fichiers de /"), ULB_BOLD));
	g_hroot = item (P_HOME, ui_segmented (X + 270, Y0 + 338, 0, 0, (long) TR ("FAT32 (conseillé)|exFAT|ext4")));
	g_hrootn = item (P_HOME, ui_label (X, Y0 + 378, W, 40, (long) TR ("/ en ext4 : pas encore de journal (voir plus haut) ; FAT32 reste le plus éprouvé pour le système."), ULB_WRAP));
	g_hext4n = item (P_HOME, ui_label (X, Y0 + 292, W, 44, (long) TR ("L'ext4 de Korin n'a pas encore de journal : après une coupure de courant pendant une écriture, le disque peut garder des erreurs que Korin ne sait pas encore réparer."), ULB_WRAP));

	// 4 Internet
	g_wlist = item (P_NET, ui_list (X, Y0, W, lh - 60, UL_HEADER | UL_NOSORT));
	ui_list_column (g_wlist, (long) TR ("Réseau"), W - 220, 0);
	ui_list_column (g_wlist, (long) TR ("Sécurité"), 110, 0);
	ui_list_column (g_wlist, (long) TR ("Signal"), 110, 0);
	g_wpass = item (P_NET, ui_textbox (X, Y0 + lh - 50, W - 270, 34, UTF_PASSWORD));
	ui_textbox_hint (g_wpass, (long) TR ("Mot de passe du réseau"));
	g_wjoin = item (P_NET, ui_pushbutton (X + W - 260, Y0 + lh - 50, 140, 34, (long) TR ("Se connecter"), 0));
	g_wscan = item (P_NET, ui_pushbutton (X + W - 110, Y0 + lh - 50, 110, 34, (long) TR ("Actualiser"), UB_GHOST));
	g_wvoir = item (P_NET, ui_checkbox (X, Y0 + lh - 8, 0, 0, (long) TR ("Afficher le mot de passe"), 0));
	g_wetat = item (P_NET, ui_label (X, Y0 + lh + 24, W, 40, (long) "", ULB_WRAP));

	// 5 Logiciels
	g_pbandeau = item (P_PKG, ui_label (X, Y0 - 40, W, 22, (long) "", ULB_DIM));
	g_pview = item (P_PKG, ui_view (X, Y0, W, H, 0)); ui_view_on (g_pview, (long) pview_paint, (long) pview_mouse, 0, (long) pview_wheel);

	// 6 Compte
	item (P_ACCOUNT, ui_label (X, Y0 - 60, W, 20, (long) TR ("Votre nom"), ULB_DIM));
	g_anom = item (P_ACCOUNT, ui_textbox (X, Y0 - 38, W, 34, 0));
	item (P_ACCOUNT, ui_label (X, Y0 + 8, half, 20, (long) TR ("Nom de l'ordinateur"), ULB_DIM));
	g_apc = item (P_ACCOUNT, ui_textbox (X, Y0 + 30, half, 34, 0));
	item (P_ACCOUNT, ui_label (X + half + 20, Y0 + 8, half, 20, (long) TR ("Nom d'utilisateur"), ULB_DIM));
	g_auser = item (P_ACCOUNT, ui_textbox (X + half + 20, Y0 + 30, half, 34, 0));
	item (P_ACCOUNT, ui_label (X, Y0 + 76, half, 20, (long) TR ("Mot de passe"), ULB_DIM));
	g_apw = item (P_ACCOUNT, ui_textbox (X, Y0 + 98, half, 34, UTF_PASSWORD));
	item (P_ACCOUNT, ui_label (X + half + 20, Y0 + 76, half, 20, (long) TR ("Confirmer le mot de passe"), ULB_DIM));
	g_apw2 = item (P_ACCOUNT, ui_textbox (X + half + 20, Y0 + 98, half, 34, UTF_PASSWORD));
	g_ajauge = item (P_ACCOUNT, ui_label (X, Y0 + 136, half, 20, (long) "", ULB_DIM));
	g_avoir = item (P_ACCOUNT, ui_checkbox (X + half + 20, Y0 + 136, 0, 0, (long) TR ("Afficher les mots de passe"), 0));
	g_aauto = item (P_ACCOUNT, ui_radio (X, Y0 + 168, W, 24, (long) TR ("Ouvrir ma session automatiquement"), 1));
	g_adem = item (P_ACCOUNT, ui_radio (X, Y0 + 194, W, 24, (long) TR ("Demander mon mot de passe pour ouvrir la session"), 1));
	ui_w_set_value (g_aauto, 1);
	g_aadmin = item (P_ACCOUNT, ui_checkbox (X, Y0 + 228, W, 24, (long) TR ("Peut administrer cet ordinateur (le mot de passe de root sera le même)"), 1));
	g_arootl = item (P_ACCOUNT, ui_label (X, Y0 + 260, W, 20, (long) TR ("Mot de passe de root (à donner à qui administrera cet ordinateur)"), ULB_DIM));
	g_arpw = item (P_ACCOUNT, ui_textbox (X, Y0 + 282, half, 34, UTF_PASSWORD));
	ui_textbox_hint (g_arpw, (long) TR ("Mot de passe de root"));
	g_arpw2 = item (P_ACCOUNT, ui_textbox (X + half + 20, Y0 + 282, half, 34, UTF_PASSWORD));
	ui_textbox_hint (g_arpw2, (long) TR ("Confirmer"));
	g_amsg = item (P_ACCOUNT, ui_label (X, Y0 + 324, W, 40, (long) "", ULB_WRAP));

	// 7 Résumé
	g_sview = item (P_SUMMARY, ui_view (X, Y0 - 60, W, 6 * 46 + 76, 0)); ui_view_on (g_sview, (long) sview_paint, (long) sview_mouse, 0, 0);
	int ys = Y0 - 60 + 6 * 46 + 84;
	g_sdef = item (P_SUMMARY, ui_segmented (X, ys, 0, 0, (long) TR ("Korin d'abord|Windows d'abord")));
	g_sdelai = item (P_SUMMARY, ui_segmented (X + 300, ys, 0, 0, (long) "0 s|5 s|10 s"));
	ui_w_set_value (g_sdelai, 1);
	g_sconfl = item (P_SUMMARY, ui_label (X, ys + 46, W - 200, 22, (long) TR ("Pour confirmer, tapez le nom du disque : DISQUE0"), ULB_BOLD));
	g_sconf = item (P_SUMMARY, ui_textbox (X + W - 190, ys + 40, 190, 34, 0));

	// 8 Installation
	g_iview = item (P_INSTALL, ui_view (X, Y0 - 60, W, 200, 0)); ui_view_on (g_iview, (long) iview_paint, (long) iview_mouse, 0, 0);
	g_iprog = item (P_INSTALL, ui_progress (X, Y0 + 160, W, 10, 100, 0));
	g_ipct = item (P_INSTALL, ui_label (X, Y0 + 180, W, 22, (long) "", ULB_BOLD));
	g_itache = item (P_INSTALL, ui_label (X, Y0 + 206, W, 22, (long) "", ULB_DIM));
	item (P_INSTALL, ui_label (X, Y0 + 234, W, 22, (long) TR ("Laissez la clé branchée et l'ordinateur sur secteur."), ULB_DIM));
	g_idet = item (P_INSTALL, ui_pushbutton (X, Y0 + 262, 200, 30, (long) TR ("Afficher les détails"), UB_GHOST));
	g_ijournal = item (P_INSTALL, ui_textarea (X, Y0 + 298, W, H - 300 > 120 ? H - 300 : 120, UTF_READONLY | UTF_MONO));

	// 9 Terminé
	g_dmsg = item (P_DONE, ui_label (X, Y0 - 40, W, 200, (long) "", ULB_WRAP));

	fuseau_montre ();
	ui_w_show (g_ijournal, 0);
	clavier_applique ("BE");
	disque_lit ();				// (dès l'accueil : un Korin déjà installé y est proposé à la réinstallation)
	g_reinst = g_kroot > 0;
	montre (P_WELCOME);
	for (;;)
	{
		long t = ui_event (30);
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
		}
		else ui_w_idle ();
		net_poll ();
		idx_poll ();
		install_poll ();
		ui_view_flush ();
	}
	return 0;
}
