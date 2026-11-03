//
// disks -- les volumes (l'application Disks d'Onyx sur UIKit) : ceux que vfs a montes (VFS_VOLUMES), chacun avec son
// etiquette, son systeme de fichiers, sa capacite et sa place libre (FS_INFO de son service, « fs.<volume> »). Ouvrir un
// volume dans Fichiers, ejecter une cle USB, la remonter. Rien d'autre : ARMPI ne sait pas formater, et aucune operation
// de cette application n'ecrit sur un volume. SD0: (le systeme) et SD1: (la partition personnelle) ne s'ejectent pas.
// `disks USB0:` s'ouvre sur ce volume. La liste suit les cles qui vont et viennent.
//
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <spawn.h>
#include <unistd.h>
#include <initializer_list>
#include <uikit.h>
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (data/lang-en.txt : le source est en francais)

extern char **environ;

static struct ui_vars ui;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2 };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
typedef unsigned long u64;

#define W_WANT		640
#define H_WANT		470
#define MAXV		8			// (56 octets par volume dans un message de 464)

// ---- le dialogue avec vfs et les services de fichiers (src/inc/fs.inc) -----------------------------------------------
enum { SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32 };
enum { VFS_MONTER = 20, VFS_DEMONTER = 21, VFS_VOLUMES = 22, FS_INFO = 19, FS_T_FAT32 = 1, FS_T_EXFAT = 2, FS_T_RAM = 3 };
static inline long sys_call (long n, long a = 0, long b = 0)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x8) : "memory");
	return x0;
}
struct Msg { long op, a, b, c, d; int len, pid; char data[464]; };

struct Volume
{
	char name[16], source[24], service[16];	// « sd0 », « blk.sd0p1 », « fatfs »
	char label[40];
	long total, free_, cluster, type;	// FS_INFO (total 0 : inconnu)
	long chan;
	bool ejected;				// ejecte ici : on peut le remonter
};
static Volume g_v[MAXV]; static int g_n;
static long g_vfs = -1;
static char g_sig[MAXV * 56]; static int g_nsig = -1;
static char g_selName[16];

static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static void up_name (const char *s, char *o, int cap)		// « sd0 » -> « SD0: »
{
	int i = 0;
	for (; s[i] && i < cap - 2; i++) o[i] = s[i] >= 'a' && s[i] <= 'z' ? (char) (s[i] - 32) : s[i];
	o[i++] = ':'; o[i] = 0;
}
static bool starts (const char *s, const char *p) { return !strncmp (s, p, strlen (p)); }
static bool is_usb (const Volume &v) { return starts (v.source, "blk.usb"); }
static bool is_sd (const Volume &v) { return starts (v.source, "blk.sd"); }
static const char *kind_of (const Volume &v)
{
	if (is_usb (v)) return TR ("Clé USB");
	if (is_sd (v)) return TR ("Carte SD");
	if (starts (v.source, "ram")) return TR ("Mémoire");
	if (starts (v.source, "blk.img")) return TR ("Image disque");
	return "Volume";
}
static void size_text (u64 b, char *o, int cap)			// « 14,9 Go »
{
	const char *u = TR (" Ko"); u64 d = 1024;
	if (b >= (1ul << 40)) { u = TR (" To"); d = 1ul << 40; }
	else if (b >= (1ul << 30)) { u = TR (" Go"); d = 1ul << 30; }
	else if (b >= (1ul << 20)) { u = TR (" Mo"); d = 1ul << 20; }
	u64 w = b / d, t = (b % d) * 10 / d;
	if (d > 1024 && w < 100) snprintf (o, (size_t) cap, TR ("%lu,%lu%s"), w, t, u);
	else snprintf (o, (size_t) cap, "%lu%s", w, u);
}
static const char *type_text (const Volume &v)
{
	return v.ejected ? "" : v.type == FS_T_FAT32 ? "FAT32" : v.type == FS_T_EXFAT ? "exFAT" : v.type == FS_T_RAM ? "RAM" : "?";
}
static void query_info (Volume &v)
{
	if (v.chan <= 0)
	{
		char s[32]; snprintf (s, sizeof s, "fs.%s", v.name);
		v.chan = sys_call (SYS_SVC_OPEN_, (long) s, 200);
		if (v.chan < 0) { v.chan = 0; return; }
	}
	static Msg m; memset (&m, 0, sizeof m);
	m.op = FS_INFO;
	long r = sys_call (SYS_SVC_CALL_, v.chan, (long) &m);
	if (r < 0) { v.chan = 0; return; }
	v.total = m.a; v.free_ = m.b; v.cluster = m.c; v.type = m.d;
	scpy (v.label, m.data, sizeof v.label);
}
// la liste de vfs -> true : elle a change
static bool read_volumes ()
{
	if (g_vfs < 0) g_vfs = sys_call (SYS_SVC_OPEN_, (long) "vfs", 0);
	if (g_vfs < 0) return false;
	static Msg m; memset (&m, 0, sizeof m);
	m.op = VFS_VOLUMES;
	if (sys_call (SYS_SVC_CALL_, g_vfs, (long) &m) < 0) { g_vfs = -1; return false; }
	int n = (int) m.b; if (n > MAXV) n = MAXV;
	if (n == g_nsig && !memcmp (g_sig, m.data, (size_t) n * 56)) return false;
	static Volume keep[MAXV]; int nk = 0;			// les ejectes restent montres
	for (int i = 0; i < g_n; i++) if (g_v[i].ejected) keep[nk++] = g_v[i];
	static Volume old[MAXV]; int no = g_n; memcpy (old, g_v, sizeof old);
	g_nsig = n; memcpy (g_sig, m.data, (size_t) n * 56);
	g_n = 0;
	for (int i = 0; i < n; i++)
	{
		const char *e = m.data + i * 56;
		Volume &v = g_v[g_n++];
		memset (&v, 0, sizeof v);
		scpy (v.name, e, sizeof v.name); scpy (v.source, e + 16, sizeof v.source); scpy (v.service, e + 40, sizeof v.service);
		for (int k = 0; k < no; k++) if (!strcmp (old[k].name, v.name)) v.chan = old[k].chan;
		query_info (v);
	}
	for (int i = 0; i < nk && g_n < MAXV; i++)
	{
		bool back = false;
		for (int k = 0; k < g_n; k++) if (!strcmp (g_v[k].name, keep[i].name)) back = true;
		if (!back) g_v[g_n++] = keep[i];
	}
	return true;
}

// ---- la fenetre ----------------------------------------------------------------------------------------------------
static long g_list, g_l1, g_l2, g_l3, g_bar, g_bopen, g_beject, g_bmount, g_brefresh, g_status, g_head;

static const char *text_state (const Volume &v) { return TR (v.ejected ? "éjecté : on peut le retirer" : "monté"); }
static void fill_list ()
{
	ui_list_clear (g_list);
	int sel = -1;
	for (int i = 0; i < g_n; i++)
	{
		const Volume &v = g_v[i];
		char row[160], n[20], a[24] = "", b[24] = "";
		up_name (v.name, n, sizeof n);
		if (v.total) size_text ((u64) v.total, a, sizeof a);
		if (v.total && v.free_ >= 0) size_text ((u64) v.free_, b, sizeof b);
		snprintf (row, sizeof row, "%s\t%s\t%s\t%s\t%s", n, v.label[0] ? v.label : kind_of (v), type_text (v), a, b);
		ui_list_add (g_list, (long) row, 2);
		if (!strcmp (v.name, g_selName)) sel = i;
	}
	if (sel < 0 && g_n) sel = 0;
	if (sel >= 0) ui_w_set_value (g_list, sel);
	ui_w_draw (g_list);
}
static void show_details ()
{
	long s = ui_w_get_value (g_list);
	bool have = s >= 0 && s < g_n;
	ui_w_enable (g_bopen, have && !g_v[s].ejected);
	ui_w_enable (g_beject, have && is_usb (g_v[s]) && !g_v[s].ejected);
	ui_w_enable (g_bmount, have && g_v[s].ejected);
	if (!have)
	{
		ui_w_set_text (g_l1, (long) ""); ui_w_set_text (g_l2, (long) ""); ui_w_set_text (g_l3, (long) "");
		ui_w_set_value (g_bar, 0);
		return;
	}
	const Volume &v = g_v[s];
	scpy (g_selName, v.name, sizeof g_selName);
	char t[220], n[20], a[24], b[24], c[24];
	up_name (v.name, n, sizeof n);
	snprintf (t, sizeof t, "%s  %s%s%s  -  %s", n, v.label, v.label[0] ? ", " : "", kind_of (v), text_state (v));
	ui_w_set_text (g_l1, (long) t);
	t[0] = 0;
	long bar = 0;
	if (v.total && !v.ejected)
	{
		size_text ((u64) v.total, a, sizeof a);
		if (v.free_ >= 0)
		{
			size_text ((u64) v.free_, b, sizeof b); size_text ((u64) (v.total - v.free_), c, sizeof c);
			snprintf (t, sizeof t, TR ("%s, %s  -  %s utilisés, %s libres"), type_text (v), a, c, b);
			bar = (long) ((u64) (v.total - v.free_) / ((u64) v.total / 1000 + 1));
		}
		else snprintf (t, sizeof t, TR ("%s, %s  -  place libre inconnue"), type_text (v), a);
	}
	ui_w_set_value (g_bar, bar);
	ui_w_set_text (g_l2, (long) t);
	if (!strcmp (v.name, "sd0")) snprintf (t, sizeof t, TR ("Le volume du système  -  source %s, service %s"), v.source, v.service);
	else if (!strcmp (v.name, "sd1")) snprintf (t, sizeof t, TR ("La partition personnelle  -  source %s, service %s"), v.source, v.service);
	else snprintf (t, sizeof t, TR ("Source %s, service %s"), v.source, v.service);
	ui_w_set_text (g_l3, (long) t);
}
static void refresh (bool force)
{
	if (force) g_nsig = -1;
	if (!read_volumes ()) return;
	fill_list ();
	show_details ();
}
static void on_open ()
{
	long s = ui_w_get_value (g_list);
	if (s < 0 || s >= g_n || g_v[s].ejected) return;
	char p[32]; up_name (g_v[s].name, p, sizeof p - 1); strcat (p, "/");
	char prog[] = "SD0:/apps/files.app/main";
	char *argv[] = { prog, p, 0 };
	pid_t pid;
	if (posix_spawn (&pid, prog, 0, 0, argv, environ)) ui_w_set_text (g_status, (long) TR ("Fichiers ne se lance pas."));
}
static void on_eject ()
{
	long s = ui_w_get_value (g_list);
	if (s < 0 || s >= g_n || !is_usb (g_v[s])) return;		// (jamais la carte)
	Volume &v = g_v[s];
	static Msg m; memset (&m, 0, sizeof m);
	m.op = VFS_DEMONTER;
	scpy (m.data, v.name, 16);
	long r = sys_call (SYS_SVC_CALL_, g_vfs, (long) &m);
	char n[20], t[120]; up_name (v.name, n, sizeof n);
	if (r < 0) { snprintf (t, sizeof t, TR ("%s n'a pas pu être éjecté."), n); ui_w_set_text (g_status, (long) t); return; }
	v.ejected = true; v.chan = 0;
	snprintf (t, sizeof t, TR ("%s peut être retiré."), n);
	ui_w_set_text (g_status, (long) t);
	usleep (200000);					// (le volume part a la fin de son service)
	refresh (true);
}
static void on_mount ()
{
	long s = ui_w_get_value (g_list);
	if (s < 0 || s >= g_n || !g_v[s].ejected) return;
	Volume &v = g_v[s];
	static Msg m; memset (&m, 0, sizeof m);
	m.op = VFS_MONTER;
	char n[20]; up_name (v.name, n, sizeof n); n[strlen (n) - 1] = 0;
	snprintf (m.data, sizeof m.data, "%s %s %s", n, v.source, v.service);
	long r = sys_call (SYS_SVC_CALL_, g_vfs, (long) &m);
	char t[120];
	if (r < 0) { snprintf (t, sizeof t, TR ("%s: n'a pas pu être monté."), n); ui_w_set_text (g_status, (long) t); return; }
	v.ejected = false;
	snprintf (t, sizeof t, TR ("%s: est monté."), n);
	ui_w_set_text (g_status, (long) t);
	usleep (300000);
	refresh (true);
}
static void lay_out (long w, long h)
{
	long ex = h - H_WANT;
	ui_w_move (g_head, 12, 8, 200, 24);
	ui_w_move (g_list, 10, 36, w - 20, 250 + ex);
	long y = 292 + ex;
	ui_w_move (g_l1, 12, y, w - 24, 22);
	ui_w_move (g_l2, 12, y + 24, w - 24, 22);
	ui_w_move (g_bar, 12, y + 52, w - 24, 12);
	ui_w_move (g_l3, 12, y + 70, w - 24, 22);
	long by = h - 76, bx = 10;
	for (long b : { g_bopen, g_beject, g_bmount }) { ui_w_move (b, bx, by, 120, 30); bx += 128; }
	ui_w_move (g_brefresh, w - 130, by, 120, 30);
	ui_w_move (g_status, 12, h - 34, w - 24, 22);
}
static void on_resize (long w, long h) { ui_fill (0, 0, w, h, jet (J_FACE)); lay_out (w, h); ui_w_draw (0); }

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/disks.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	s_jet = (const unsigned *) ui_jetons ();
	if (ui_window_ex (-1, -1, W_WANT, H_WANT, (long) TR ("Disques"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (500, 400);
	g_head = ui_label (0, 0, 100, 24, (long) "Volumes", ULB_TITLE);
	g_list = ui_list (0, 0, 100, 100, UL_HEADER | UL_NOSORT | UL_ICONS);
	ui_list_column (g_list, (long) "Volume", 90, 0);
	ui_list_column (g_list, (long) TR ("Étiquette"), 190, 0);
	ui_list_column (g_list, (long) "Type", 80, 0);
	ui_list_column (g_list, (long) TR ("Taille"), 110, ULC_RIGHT);
	ui_list_column (g_list, (long) TR ("Libre"), 110, ULC_RIGHT);
	g_l1 = ui_label (0, 0, 100, 22, (long) "", ULB_BOLD);
	g_l2 = ui_label (0, 0, 100, 22, (long) "", 0);
	g_l3 = ui_label (0, 0, 100, 22, (long) "", ULB_DIM);
	g_bar = ui_progress (0, 0, 100, 12, 1000, 0);
	g_bopen = ui_pushbutton (0, 0, 120, 30, (long) "Ouvrir", UB_DEFAULT);
	g_beject = ui_pushbutton (0, 0, 120, 30, (long) "Éjecter", 0);
	g_bmount = ui_pushbutton (0, 0, 120, 30, (long) "Remonter", 0);
	g_brefresh = ui_pushbutton (0, 0, 120, 30, (long) "Actualiser", 0);
	g_status = ui_label (0, 0, 100, 22, (long) "", ULB_DIM);
	ui_on_resize ((long) on_resize, 0);
	lay_out (ui.w, ui.h);
	if (argc > 1 && argv[1][0])				// `disks USB0:` : ce volume
	{
		int n = 0;
		for (; argv[1][n] && argv[1][n] != ':' && n < 15; n++) g_selName[n] = argv[1][n] >= 'A' && argv[1][n] <= 'Z' ? (char) (argv[1][n] + 32) : argv[1][n];
		g_selName[n] = 0;
	}
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	refresh (true);
	ui_w_draw (0);
	ui_update_all ();
	for (;;)
	{
		long t = ui_event (1000);
		if (t == WEV_CLOSE) break;
		if (t > 0)
		{
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (w == g_list && code == UN_CHANGE) show_details ();
			else if (w == g_list && code == UN_ACTIVATE) on_open ();
			else if (code == UN_ACTIVATE && w == g_bopen) on_open ();
			else if (code == UN_ACTIVATE && w == g_beject) on_eject ();
			else if (code == UN_ACTIVATE && w == g_bmount) on_mount ();
			else if (code == UN_ACTIVATE && w == g_brefresh)
			{
				for (int i = 0; i < g_n; i++) if (!g_v[i].ejected) query_info (g_v[i]);
				fill_list (); show_details ();
				ui_w_set_text (g_status, (long) "");
			}
		}
		else { ui_w_idle (); refresh (false); }
	}
	return 0;
}
