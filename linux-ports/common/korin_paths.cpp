//
// korin_paths.cpp -- les chemins de Korin sur Linux. Les apps ecrivent « HOME:/arabesque », « /apps/koton.app/plugins »,
// « /res/soundfonts » ; leurs appels de fichiers sont enveloppes (-Wl,--wrap=open,...) et passent ici :
//
//   HOME:/...                    -> $KORIN_HOME/...  (par defaut ~/Korin)
//   /apps/<app>.app/plugins      -> <bin>/<app>-plugins          (les greffons de Koton, construits avec lui)
//   /apps/<app>.app/...          -> <racine>/src/apps/<app>/data/...   (les fichiers livres avec l'app ; s'ils n'y
//                                sont pas : <racine>/src/apps/<app>/..., Forge : lang/)
//   /apps/fmtracker.app/ins      -> <racine>/src/apps/fmtracker/ins   (les instruments de l'export FM Song)
//   /home/public/arabesque       -> <racine>/src/apps/arabesque/presets
//   /home/public/koton           -> <racine>/src/apps/koton/demos
//   /res/soundfonts/GeneralUser-GS.sf2 -> la SoundFont par defaut (kport_soundfont)
//   /res/soundfonts              -> /usr/share/soundfonts
//   /res/lang/fr.txt             -> <racine>/src/apps/koton/lang/fr.txt  (Koton : ses mots en francais)
//   /bin/llm                     -> <bin>/llm
//   /apps/jet.app/main, SD0:/apps/jet.app/main   le navigateur de Korin -> celui du systeme (stat : xdg-open est la ;
//                                posix_spawn : xdg-open <adresse>, sinon « gio open », ou $KORIN_BROWSER)
//
// <bin> : le dossier de l'executable ; <racine> : $KORIN_ROOT, sinon <bin>/../.. (linux-ports/bin dans le depot).
//
#define _GNU_SOURCE 1
#include "korin_port.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>

static char s_app[32], s_bin[PATH_MAX], s_root[PATH_MAX], s_home[PATH_MAX], s_sf[PATH_MAX], s_cfg[PATH_MAX];
static bool s_ready;

extern "C" {
int __real_mkdir (const char *, mode_t);
int __real_open (const char *, int, ...);
FILE *__real_fopen (const char *, const char *);
DIR *__real_opendir (const char *);
struct dirent *__real_readdir (DIR *);
int __real_closedir (DIR *);
int __real_stat (const char *, struct stat *);
int __real_access (const char *, int);
int __real_unlink (const char *);
int __real_remove (const char *);
int __real_rename (const char *, const char *);
int __real_posix_spawn (pid_t *, const char *, const posix_spawn_file_actions_t *, const posix_spawnattr_t *, char *const[], char *const[]);
}

static bool is_dir (const char *p) { struct stat st; return __real_stat (p, &st) == 0 && S_ISDIR (st.st_mode); }
static bool is_file (const char *p) { struct stat st; return __real_stat (p, &st) == 0 && S_ISREG (st.st_mode); }

static void mkdirs (const char *p)
{
	char b[PATH_MAX];
	snprintf (b, sizeof b, "%s", p);
	for (char *s = b + 1; *s; s++)
		if (*s == '/') { *s = 0; __real_mkdir (b, 0755); *s = '/'; }
	__real_mkdir (b, 0755);
}

static void find_soundfont ()
{
	const char *e = getenv ("KORIN_SOUNDFONT");
	if (e && *e && is_file (e)) { snprintf (s_sf, sizeof s_sf, "%s", e); return; }
	FILE *f = __real_fopen (s_cfg, "r");
	if (f)
	{
		char line[PATH_MAX + 16];
		while (fgets (line, sizeof line, f))
		{
			line[strcspn (line, "\r\n")] = 0;
			if (!strncmp (line, "soundfont=", 10) && is_file (line + 10)) snprintf (s_sf, sizeof s_sf, "%s", line + 10);
		}
		fclose (f);
		if (s_sf[0]) return;
	}
	static const char *const known[] = { "/usr/share/soundfonts/FluidR3_GM.sf2", "/usr/share/soundfonts/default.sf2",
		"/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/sounds/sf2/default.sf2", "/usr/share/sounds/sf2/default-GM.sf2" };
	for (const char *k : known) if (is_file (k)) { snprintf (s_sf, sizeof s_sf, "%s", k); return; }
	static const char *const dirs[] = { "/usr/share/soundfonts", "/usr/share/sounds/sf2" };
	for (const char *d : dirs)
	{
		DIR *o = __real_opendir (d);
		if (!o) continue;
		struct dirent *de;
		while ((de = __real_readdir (o)))
		{
			size_t n = strlen (de->d_name);
			if (n > 4 && !strcasecmp (de->d_name + n - 4, ".sf2")) { snprintf (s_sf, sizeof s_sf, "%s/%s", d, de->d_name); break; }
		}
		__real_closedir (o);
		if (s_sf[0]) return;
	}
}

extern "C" void kport_setup (const char *app, const char *argv0)
{
	snprintf (s_app, sizeof s_app, "%s", app);
	char exe[PATH_MAX];
	ssize_t n = readlink ("/proc/self/exe", exe, sizeof exe - 1);
	if (n > 0) exe[n] = 0; else snprintf (exe, sizeof exe, "%s", argv0 ? argv0 : ".");
	char *sl = strrchr (exe, '/');
	if (sl) *sl = 0; else snprintf (exe, sizeof exe, ".");
	snprintf (s_bin, sizeof s_bin, "%s", exe);
	const char *r = getenv ("KORIN_ROOT");
	char probe[PATH_MAX + 32];
	if (r && *r) snprintf (s_root, sizeof s_root, "%s", r);
	else
	{
		// linux-ports/bin -> la racine du depot ; aussi linux-ports/build/<app> (essais sans copie)
		static const char *const up[] = { "/../..", "/../../..", "/.." };
		for (const char *u : up)
		{
			snprintf (probe, sizeof probe, "%s%s/src/apps/%s", s_bin, u, app);
			if (is_dir (probe))
			{
				snprintf (probe, sizeof probe, "%s%s", s_bin, u);
				if (!realpath (probe, s_root)) snprintf (s_root, sizeof s_root, "%s", probe);
				break;
			}
		}
	}
	const char *h = getenv ("KORIN_HOME");
	const char *uh = getenv ("HOME");
	if (h && *h) snprintf (s_home, sizeof s_home, "%s", h);
	else snprintf (s_home, sizeof s_home, "%s/Korin", uh && *uh ? uh : "/tmp");
	mkdirs (s_home);
	const char *xc = getenv ("XDG_CONFIG_HOME");
	if (xc && *xc) snprintf (s_cfg, sizeof s_cfg, "%s/korin-ports/settings.ini", xc);
	else snprintf (s_cfg, sizeof s_cfg, "%s/.config/korin-ports/settings.ini", uh && *uh ? uh : "/tmp");
	find_soundfont ();
	s_ready = true;
}

extern "C" const char *kport_app (void) { return s_app; }
extern "C" const char *kport_bindir (void) { return s_bin; }
extern "C" const char *kport_rootdir (void) { return s_root; }
extern "C" const char *kport_homedir (void) { return s_home; }
extern "C" const char *kport_soundfont (void) { return s_sf; }

extern "C" int kport_soundfont_save (const char *path)
{
	if (!path || !is_file (path)) return 0;
	snprintf (s_sf, sizeof s_sf, "%s", path);
	char d[PATH_MAX];
	snprintf (d, sizeof d, "%s", s_cfg);
	char *sl = strrchr (d, '/');
	if (sl) { *sl = 0; mkdirs (d); }
	FILE *f = __real_fopen (s_cfg, "w");
	if (!f) return 0;
	fprintf (f, "# korin-ports (linux-ports/) : la SoundFont d'Arabesque\nsoundfont=%s\n", path);
	fclose (f);
	return 1;
}

static bool starts (const char *p, const char *pre, const char **rest)
{
	size_t n = strlen (pre);
	if (strncmp (p, pre, n)) return false;
	// (un prefixe de dossier : la suite est vide ou commence par '/')
	if (pre[n - 1] != '/' && p[n] && p[n] != '/') return false;
	*rest = p + n;
	return true;
}

extern "C" const char *kport_path (const char *p, char *buf, size_t cap)
{
	if (!p || !s_ready) return p;
	const char *rest;
	if (starts (p, "HOME:/", &rest) || starts (p, "HOME:", &rest))
	{
		snprintf (buf, cap, "%s/%s", s_home, rest);
		return buf;
	}
	if (p[0] != '/') return p;
	char pre[64];
	snprintf (pre, sizeof pre, "/apps/%s.app/plugins", s_app);
	if (starts (p, pre, &rest)) { snprintf (buf, cap, "%s/%s-plugins%s", s_bin, s_app, rest); return buf; }
	if (s_root[0])
	{
		if (starts (p, "/apps/fmtracker.app/ins", &rest)) { snprintf (buf, cap, "%s/src/apps/fmtracker/ins%s", s_root, rest); return buf; }
		snprintf (pre, sizeof pre, "/apps/%s.app", s_app);
		if (starts (p, pre, &rest))
		{
			// (data/ de l'app ; sinon son dossier : /apps/forge.app/lang -> src/apps/forge/lang)
			snprintf (buf, cap, "%s/src/apps/%s/data%s", s_root, s_app, rest);
			struct stat st;
			if (*rest && __real_stat (buf, &st) != 0)
			{
				char alt[PATH_MAX];
				snprintf (alt, sizeof alt, "%s/src/apps/%s%s", s_root, s_app, rest);
				if (__real_stat (alt, &st) == 0) snprintf (buf, cap, "%s", alt);
			}
			return buf;
		}
		snprintf (pre, sizeof pre, "/home/public/%s", s_app);
		if (starts (p, pre, &rest))
		{
			snprintf (buf, cap, "%s/src/apps/%s/%s%s", s_root, s_app, strcmp (s_app, "koton") ? "presets" : "demos", rest);
			return buf;
		}
		if (starts (p, "/res/lang", &rest)) { snprintf (buf, cap, "%s/skel/sd0/res/lang%s", s_root, rest); return buf; }
	}
	if (!strcmp (p, "/res/soundfonts/GeneralUser-GS.sf2") && s_sf[0]) { snprintf (buf, cap, "%s", s_sf); return buf; }
	if (starts (p, "/res/soundfonts", &rest)) { snprintf (buf, cap, "/usr/share/soundfonts%s", rest); return buf; }
	if (!strcmp (p, "/bin/llm")) { snprintf (buf, cap, "%s/llm", s_bin); return buf; }
	return p;
}

// ---- le navigateur du systeme, a la place de Jet ----------------------------------------------------------------------
static bool is_jet (const char *p)
{
	return p && (!strcmp (p, "/apps/jet.app/main") || !strcmp (p, "SD0:/apps/jet.app/main"));
}
// une commande trouvee dans PATH -> son chemin
static bool which (const char *cmd, char *out, size_t cap)
{
	if (strchr (cmd, '/')) { snprintf (out, cap, "%s", cmd); return __real_access (cmd, X_OK) == 0; }
	const char *path = getenv ("PATH");
	std::string all = path && *path ? path : "/usr/local/bin:/usr/bin:/bin";
	size_t a = 0;
	while (a <= all.size ())
	{
		size_t e = all.find (':', a);
		std::string d = all.substr (a, e == std::string::npos ? std::string::npos : e - a);
		if (!d.empty ())
		{
			snprintf (out, cap, "%s/%s", d.c_str (), cmd);
			if (__real_access (out, X_OK) == 0) return true;
		}
		if (e == std::string::npos) break;
		a = e + 1;
	}
	return false;
}
// la commande qui ouvre une adresse : $KORIN_BROWSER, xdg-open, sinon gio (avec « open » devant l'adresse)
static bool opener (char *exe, size_t cap, bool *gio)
{
	*gio = false;
	const char *b = getenv ("KORIN_BROWSER");
	if (b && *b && which (b, exe, cap)) return true;
	if (which ("xdg-open", exe, cap)) return true;
	if (which ("gio", exe, cap)) { *gio = true; return true; }
	return false;
}
static void *reap (void *p) { int st; waitpid ((pid_t) (long) p, &st, 0); return 0; }
static int spawn_opener (pid_t *pid, const char *what)
{
	char exe[PATH_MAX];
	bool gio;
	if (!opener (exe, sizeof exe, &gio)) return ENOENT;
	char *argv[4]; int n = 0;
	argv[n++] = exe;
	if (gio) argv[n++] = (char *) "open";
	argv[n++] = (char *) what;
	argv[n] = 0;
	pid_t p;
	extern char **environ;
	int r = __real_posix_spawn (&p, exe, 0, 0, argv, environ);
	if (r) return r;
	if (pid) *pid = p;
	// (l'ouvreur rend la main tout de suite : un fil l'attend, pour qu'il ne reste pas en zombie)
	pthread_t t;
	if (pthread_create (&t, 0, reap, (void *) (long) p) == 0) pthread_detach (t);
	return 0;
}
extern "C" int kport_browser_available (void) { char exe[PATH_MAX]; bool gio; return opener (exe, sizeof exe, &gio); }
extern "C" int kport_open_external (const char *what)
{
	if (!what || !*what) return 0;
	char tb[PATH_MAX];
	return spawn_opener (0, kport_path (what, tb, sizeof tb)) == 0;
}

// ---- les appels enveloppes --------------------------------------------------------------------------------------------
#define TP(p) char tb_[PATH_MAX]; const char *tp_ = kport_path ((p), tb_, sizeof tb_)

extern "C" {
int __wrap_mkdir (const char *p, mode_t m) { TP (p); return __real_mkdir (tp_, m); }
int __wrap_open (const char *p, int flags, ...)
{
	mode_t m = 0;
	if (flags & (O_CREAT | O_TMPFILE)) { va_list a; va_start (a, flags); m = (mode_t) va_arg (a, int); va_end (a); }
	TP (p);
	return __real_open (tp_, flags, m);
}
FILE *__wrap_fopen (const char *p, const char *mode) { TP (p); return __real_fopen (tp_, mode); }
// (un dossier se lit trie par nom, comme ceux de la carte de Korin, ecrits dans cet ordre : sous Linux, readdir rend
// l'ordre du systeme de fichiers, et les listes de styles, de traces, de greffons changeraient d'ordre)
struct KDir { DIR *real; std::vector<struct dirent> ents; size_t i; };
DIR *__wrap_opendir (const char *p)
{
	TP (p);
	DIR *r = __real_opendir (tp_);
	if (!r) return 0;
	KDir *k = new KDir;
	k->real = r; k->i = 0;
	for (struct dirent *e; (e = __real_readdir (r)); ) k->ents.push_back (*e);
	std::sort (k->ents.begin (), k->ents.end (), [] (const struct dirent &a, const struct dirent &b) { return strcmp (a.d_name, b.d_name) < 0; });
	return (DIR *) k;
}
struct dirent *__wrap_readdir (DIR *d)
{
	KDir *k = (KDir *) d;
	return k && k->i < k->ents.size () ? &k->ents[k->i++] : 0;
}
int __wrap_closedir (DIR *d)
{
	KDir *k = (KDir *) d;
	if (!k) return -1;
	int r = __real_closedir (k->real);
	delete k;
	return r;
}
int __wrap_stat (const char *p, struct stat *st)
{
	if (is_jet (p))
	{
		char exe[PATH_MAX]; bool gio;
		if (opener (exe, sizeof exe, &gio)) return __real_stat (exe, st);
		errno = ENOENT;
		return -1;
	}
	TP (p);
	return __real_stat (tp_, st);
}
int __wrap_access (const char *p, int m) { TP (p); return __real_access (tp_, m); }
int __wrap_unlink (const char *p) { TP (p); return __real_unlink (tp_); }
int __wrap_remove (const char *p) { TP (p); return __real_remove (tp_); }
int __wrap_rename (const char *a, const char *b)
{
	char b1[PATH_MAX], b2[PATH_MAX];
	return __real_rename (kport_path (a, b1, sizeof b1), kport_path (b, b2, sizeof b2));
}
int __wrap_posix_spawn (pid_t *pid, const char *p, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at, char *const argv[], char *const envp[])
{
	// Jet <adresse> : le navigateur du systeme (les autres arguments de Jet ne servent pas ici)
	if (is_jet (p)) return spawn_opener (pid, argv && argv[0] && argv[1] ? argv[1] : "about:blank");
	TP (p);
	return __real_posix_spawn (pid, tp_, fa, at, argv, envp);
}
}
