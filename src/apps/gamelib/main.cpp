/*
 * gamelib -- la bibliotheque de jeux (docs/EMULATEURS.md) : le lanceur des emulateurs, pas un emulateur.
 *
 * A gauche les consoles, avec leur nombre de jeux ; a droite les cartes, une image et un nom. Un clic choisit, un
 * double-clic ou Entree joue : la ROM part avec son emulateur, en fenetre ou, avec View > Play Full Screen, en plein
 * ecran. Clavier : les fleches, Entree. Manette : la croix, Start ou B (le bouton de droite, le A
 * d'une manette Nintendo ; comme l'asm, dont l'en-tete dit A), L et R changent de console.
 *
 * Dossiers : SD0:/home/roms, SD1:/roms, et ceux de SD0:/home/gamelib/folders.txt (Library > Add Folder...),
 * sous-dossiers compris. Images : chaque jeu tourne quelques secondes sans etre montre, par son emulateur
 * (nesemu --thumb sortie rom), un a la fois, en arriere-plan ; gardees dans SD0:/home/gamelib/thumbs. N64, DS,
 * GameCube et 3DS : le nom seul (pas d'image).
 *
 * Port en C++ depuis la Game Library d'Onyx (user/Apps/gamelib/main.cpp : les jeux par console puis par nom, le
 * filtre par console, la carte choisie qui reste visible, la manette, le plein ecran) et de gamelib.asm de Korin,
 * dont le comportement fait foi : memes consoles, memes dossiers et fichiers, memes cartes, memes menus, et les
 * emulateurs lances exactement comme l'asm (f_spawn d'AppKit, la meme ligne d'arguments). Un seul source pour le Pi
 * et le PC ; une console n'apparait que si son emulateur est installe (le PC n'en a presque pas : la liste montre
 * alors seulement « All Games 0 » et le message sans jeu). L'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <armpi/kit.h>
#include <armpi/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <uikit.h>
#include "../../inc/gamepad.h"

#define GL_W		980
#define GL_H		580
#define GL_SIDE		200
#define GL_SIDE_MIN	130
#define TW		160			/* une image : 160 x 120, 0x00RRGGBB */
#define TH		120
#define CARD_W		176
#define CARD_H		160
#define CARD_GAP	10
#define GL_HEAD		40			/* en haut, le nom de la console (rien sous les boutons) */
#define G_MAX		2000
#define SYS_N		8
#define DEPTH_MAX	4
#define TH_WAIT_MS	120000

enum { ID_REFRESH = 1, ID_ADD, ID_QUIT, ID_FULL };

#define SYS_TIME_MS_	0x19
#define AK_F_SPAWN	26			/* f_spawn d'AppKit (src/kits/appkit/appkit.abi : rang 26) */

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static unsigned long time_ms (void) { return (unsigned long) armpi_syscall (SYS_TIME_MS_, 0, 0, 0, 0, 0, 0); }

/* ---- les consoles : celles dont l'emulateur est installe ---- */
struct sys
{
	const char *name;
	const char *emu;			/* SD0:/apps/<emu>.app/main */
	const char *ext[5];			/* les extensions (en minuscules ; la casse du nom ne compte pas) */
	int thumb;				/* 1 : image par l'emulateur (--thumb) */
	long count, on;
};
static struct sys g_sys[SYS_N] = {
	{ "NES", "SD0:/apps/nesemu.app/main", { ".nes" }, 1, 0, 0 },
	{ "Super Nintendo", "SD0:/apps/snesemu.app/main", { ".sfc", ".smc" }, 1, 0, 0 },
	{ "Game Boy", "SD0:/apps/gbemu.app/main", { ".gb", ".gbc" }, 1, 0, 0 },
	{ "Game Boy Advance", "SD0:/apps/gbaemu.app/main", { ".gba" }, 1, 0, 0 },
	{ "Nintendo 64", "SD0:/apps/n64emu.app/main", { ".z64", ".n64", ".v64" }, 0, 0, 0 },
	{ "Nintendo DS", "SD0:/apps/ndsemu.app/main", { ".nds" }, 0, 0, 0 },
	{ "GameCube", "SD0:/apps/gcemu.app/main", { ".iso", ".gcm", ".dol" }, 0, 0, 0 },
	{ "Nintendo 3DS", "SD0:/apps/n3dsemu.app/main", { ".3ds", ".cci", ".cxi", ".3dsx" }, 0, 0, 0 },
};

/* ---- les jeux ---- */
struct game
{
	char path[200];
	char name[64];				/* le nom du fichier, sans l'extension */
	long sys;
	uint32_t *thumb;			/* TW x TH pixels, 0 : pas encore */
	long tried;
};

static const char s_menus[] = "MLibrary\nI1\tRefresh\tF5\nI2\tAdd Folder...\n-\nI3\tQuit\tCtrl+Q\nMView\nI4\tPlay Full Screen\n";
static const char d_home[] = "SD0:/home", d_lib[] = "SD0:/home/gamelib", d_thumbs[] = "SD0:/home/gamelib/thumbs",
	d_roms[] = "SD0:/home/roms", d_roms1[] = "SD1:/roms", p_folders[] = "SD0:/home/gamelib/folders.txt";

static const unsigned *g_tok;
static struct game *g_games;
static long g_ng, g_list, g_view, g_filter, g_rowsel, g_sel, g_scroll, g_nvis, g_vw = 400, g_vh = 300, g_full;
static unsigned long g_padold, g_tstart;
static long g_tgame;					/* le jeu dont l'image se fait (rang + 1), 0 : aucun */
static int g_vis[G_MAX];				/* les jeux montres (rangs dans g_games), dans l'ordre */
static char g_tpath[320];
static long (*f_spawn) (long, long);

static unsigned tok (int off) { return g_tok[off / 4]; }

static void refilter (void);

/* la ligne d'arguments telle quelle a l'emulateur (comme l'asm : pas de guillemets autour d'un chemin avec espaces) */
static long spawn (const char *path, const char *args)
{
	return f_spawn ? f_spawn ((long) path, (long) args) : -2;
}

/* largeur de la fenetre -> largeur de la colonne : 3/8 au plus, entre GL_SIDE_MIN et GL_SIDE */
static long side_width (long w)
{
	unsigned long s = ((unsigned long) w >> 3) * 3;
	if (s >= GL_SIDE)
		s = GL_SIDE;
	if (s < GL_SIDE_MIN)
		s = GL_SIDE_MIN;
	return (long) s;
}

static long on_resize (long w, long h)
{
	long s = side_width (w);
	ui_w_move (g_list, 0, 0, s, h);
	ui_w_move (g_view, s, 0, w - s, h);
	ui_view_dirty (g_view);
	ui_w_draw (0);
	ui_update_all ();
	return 0;
}

static void sys_init (void)
{
	struct stat st;
	for (int i = 0; i < SYS_N; i++)
		g_sys[i].on = stat (g_sys[i].emu, &st) >= 0;
}

/* nom fini par ext (sans tenir compte de la casse ASCII) */
static int ends (const char *s, const char *e)
{
	size_t n = strlen (s), m = strlen (e);
	if (n < m)
		return 0;
	for (size_t i = 0; i < m; i++)
	{
		unsigned char c = (unsigned char) s[n - m + i];
		if (c >= 'A' && c <= 'Z')
			c = (unsigned char) (c + 32);
		if (c != (unsigned char) e[i])
			return 0;
	}
	return 1;
}

/* au plus n caracteres de src, puis un 0 -> la fin */
static char *copy_n (char *d, const char *s, size_t n)
{
	while (n && *s)
	{
		*d++ = *s++;
		n--;
	}
	*d = 0;
	return d;
}

/* path, name : une ROM d'une console installee ? */
static void add_game (const char *path, const char *name)
{
	for (long k = 0; k < SYS_N; k++)
	{
		struct sys *y = &g_sys[k];
		if (!y->on)
			continue;
		for (int e = 0; e < 5 && y->ext[e]; e++)
		{
			if (!ends (name, y->ext[e]))
				continue;
			y->count++;		/* (compte aussi au-dela de G_MAX, comme l'asm) */
			if (g_ng >= G_MAX)
				return;
			struct game *g = &g_games[g_ng++];
			memset (g, 0, sizeof *g);
			g->sys = k;
			copy_n (g->path, path, 199);
			char *end = copy_n (g->name, name, 63);
			while (end > g->name)	/* le nom, sans l'extension */
				if (*--end == '.')
				{
					*end = 0;
					break;
				}
			return;
		}
	}
}

/* dossier, profondeur : ses ROM, et celles des sous-dossiers */
static void scan_dir (const char *dir, long depth)
{
	DIR *d = opendir (dir);
	if (!d)
		return;
	struct dirent *e;
	char path[512];
	while ((e = readdir (d)))
	{
		if (e->d_name[0] == '.')
			continue;
		size_t n = strlen (dir);		/* path_join d'AppKit */
		if (n + 2 + strlen (e->d_name) > sizeof path)
			continue;
		memcpy (path, dir, n);
		if (!n || dir[n - 1] != '/')
			path[n++] = '/';
		strcpy (path + n, e->d_name);
		if (e->d_type == DT_DIR)
		{
			if (depth < DEPTH_MAX)
				scan_dir (path, depth + 1);
		}
		else
			add_game (path, e->d_name);
	}
	closedir (d);
}

/* a, b = jeux -> 1 : a apres b (par console, puis par nom, chaque octet | 0x20 comme l'asm) */
static int game_gt (const struct game *a, const struct game *b)
{
	if (a->sys != b->sys)
		return a->sys > b->sys;
	for (size_t i = 0; i < sizeof a->name; i++)
	{
		unsigned char x = (unsigned char) a->name[i], y = (unsigned char) b->name[i];
		unsigned char p = x | 0x20, q = y | 0x20;
		if (p != q)
			return p > q;
		if (!x)
			return 0;
	}
	return 0;
}

/* tri par insertion : quelques centaines de jeux */
static void sort_games (void)
{
	for (long i = 1; i < g_ng; i++)
		for (long j = i; j && game_gt (&g_games[j - 1], &g_games[j]); j--)
		{
			struct game t = g_games[j - 1];
			g_games[j - 1] = g_games[j];
			g_games[j] = t;
		}
}

/* v en decimal dans p, puis un 0 */
static void fmt_dec (long v, char *p)
{
	char t[24];
	int n = 0;
	unsigned long u = (unsigned long) v;
	do
		t[n++] = (char) ('0' + u % 10);
	while (u /= 10);
	while (n)
		*p++ = t[--n];
	*p = 0;
}

/* la colonne : Tous les jeux, puis chaque console installee, avec son nombre */
static void side_fill (void)
{
	char line[128];
	ui_list_clear (g_list);
	char *p = copy_n (line, TR ("All Games"), 100);
	*p++ = ' ';
	fmt_dec (g_ng, p);
	ui_list_add (g_list, (long) line, 0);
	for (long k = 0; k < SYS_N; k++)
	{
		if (!g_sys[k].on)
			continue;
		p = copy_n (line, g_sys[k].name, 100);
		*p++ = ' ';
		fmt_dec (g_sys[k].count, p);
		ui_list_add (g_list, (long) line, k + 1);
	}
	ui_w_set_value (g_list, g_rowsel);
}

static void rescan (void)
{
	for (long i = 0; i < g_ng; i++)			/* les images d'avant */
	{
		free (g_games[i].thumb);
		g_games[i].thumb = 0;
	}
	g_ng = 0;
	for (int k = 0; k < SYS_N; k++)
		g_sys[k].count = 0;
	scan_dir (d_roms, 0);
	scan_dir (d_roms1, 0);
	int fd = open (p_folders, O_RDONLY);		/* les dossiers ajoutes */
	if (fd >= 0)
	{
		static char folders[4096];
		long n = read (fd, folders, sizeof folders - 1);
		close (fd);
		if (n > 0)
		{
			folders[n] = 0;
			for (char *s = folders; *s;)
			{
				char *e = s;
				while (*e && *e != '\n' && *e != '\r')
					e++;
				char *next = *e ? e + 1 : e;
				*e = 0;
				if (*s)
					scan_dir (s, 0);
				s = next;
			}
		}
	}
	sort_games ();
	side_fill ();
	refilter ();
}

/* row = rang dans la colonne -> filtre */
static void set_filter (long row)
{
	g_rowsel = row;
	long data = 0;
	if (row)
		uikit_list_get (g_list, row, &data);
	g_filter = data;
	g_scroll = 0;
	g_sel = 0;
	refilter ();
}

static void refilter (void)
{
	long n = 0;
	for (long i = 0; i < g_ng; i++)
		if (!g_filter || g_games[i].sys + 1 == g_filter)
			g_vis[n++] = (int) i;
	g_nvis = n;
	ui_view_dirty (g_view);
}

/* ---- les cartes ---- */

/* -> colonnes (g_vw : largeur de la vue) */
static long columns (void)
{
	unsigned long c = (unsigned long) (g_vw - CARD_GAP) / (CARD_W + CARD_GAP);
	return c < 1 ? 1 : (long) c;
}

/* v = vue, i = rang montre, x, y */
static void card (long v, long i, long x, long y)
{
	struct game *g = &g_games[g_vis[i]];
	int sel = g_sel == i;
	unsigned bg = tok (sel ? TJ_ACCENT : TJ_SURFACE);
	ui_rbox (x, y, CARD_W, CARD_H, 8, bg, bg);
	if (!g->thumb)
	{	/* pas d'image : le nom de la console (comme l'asm : TJ_DARK, un drapeau, fait un fond noir) */
		ui_fill (x + 8, y + 8, TW, TH, tok (TJ_DARK));
		ui_text_fit ((long) g_sys[g->sys].name, x + 16, y + 72, 0xFFFFFF, 14, (long) ui.font, TW - 16);
	}
	else
	{
		long cw, ch;
		uint32_t *px = uikit_canvas_pixels (v, &cw, &ch);
		if (px)
		{
			const uint32_t *src = g->thumb;
			for (long r = 0, yy = y + 8; r < TH; r++, yy++, src += TW)
			{
				if (yy < 0)
					continue;
				if (yy >= ch)
					break;
				memcpy (px + yy * cw + x + 8, src, TW * 4);
			}
		}
	}
	ui_text_fit ((long) g->name, x + 8, y + TH + 26, tok (sel ? TJ_ACCENT_INK : TJ_TEXT1), 13, (long) ui.font, TW);
}

/* (vue, largeur, hauteur) */
static long v_paint (long v, long w, long h)
{
	g_vw = w;
	g_vh = h;
	ui_fill (0, 0, w, h, tok (TJ_FACE));
	if (!g_nvis)
		ui_text ((long) TR ("No games yet: put ROMs in SD0:/home/roms or SD1:/roms."), CARD_GAP, GL_HEAD + 24,
			tok (TJ_TEXT2), 15, (long) ui.font, 0);
	else
	{
		long cols = columns ();
		for (long i = 0; i < g_nvis; i++)
		{
			long y = i / cols * (CARD_H + CARD_GAP) + GL_HEAD - g_scroll;
			long x = i % cols * (CARD_W + CARD_GAP) + CARD_GAP;
			if (y < -CARD_H)
				continue;
			if (y >= h)
				break;
			card (v, i, x, y);
		}
	}
	ui_fill (0, 0, w, GL_HEAD, tok (TJ_FACE));	/* l'en-tete, par-dessus les cartes */
	const char *head = g_filter ? g_sys[g_filter - 1].name : TR ("All Games");
	ui_text_fit ((long) head, CARD_GAP, 28, tok (TJ_TEXT1), 18, (long) ui.font_b,
		w - (CARD_GAP + FRAME_CAP_W + FRAME_CAP_MARGIN));
	return 0;
}

/* la ROM choisie, par son emulateur */
static void play (void)
{
	if (!g_nvis)
		return;
	struct game *g = &g_games[g_vis[g_sel]];
	char line[512];
	char *p = line;
	if (g_full)
		p = copy_n (p, "--full ", 16);
	copy_n (p, g->path, 200);
	spawn (g_sys[g->sys].emu, line);
}

/* d = pas : la carte choisie bouge, reste visible */
static void move_sel (long d)
{
	long s = g_sel + d;
	if (s < 0)
		s = 0;
	if (!g_nvis)
		return;
	if (s > g_nvis - 1)
		s = g_nvis - 1;
	g_sel = s;
	long top = s / columns () * (CARD_H + CARD_GAP);
	if ((unsigned long) top < (unsigned long) g_scroll)
		g_scroll = top;
	else
	{
		long bottom = top + (CARD_H + CARD_GAP) + CARD_GAP - g_vh + GL_HEAD;
		if (bottom > g_scroll)
			g_scroll = bottom;
	}
	ui_view_dirty (g_view);
}

/* (vue, genre, x, y, infos : clics | boutons << 8) */
static long v_mouse (long v, long kind, long x, long y, long info)
{
	(void) v;
	if (kind != 1)
		return 0;
	unsigned long col = (unsigned long) (x - CARD_GAP) / (CARD_W + CARD_GAP);
	if (y < GL_HEAD)
	{
		ui_window_drag ();			/* l'en-tete deplace la fenetre */
		return 0;
	}
	unsigned long row = (unsigned long) (y + g_scroll - GL_HEAD) / (CARD_H + CARD_GAP);
	unsigned long cols = (unsigned long) columns ();
	if (col >= cols)
		return 0;
	unsigned long i = row * cols + col;
	if (i >= (unsigned long) g_nvis)
		return 0;
	g_sel = (long) i;
	ui_view_dirty (g_view);
	if ((info & 0xFF) >= 2)				/* double-clic : on joue */
		play ();
	return 0;
}

/* (vue, code HID, caractere, mods) -> 1 : touche prise */
static long v_key (long v, long code, long ch, long mods)
{
	(void) v, (void) ch, (void) mods;
	switch (code)
	{
	case KEY_RIGHT: move_sel (1); return 1;
	case KEY_LEFT: move_sel (-1); return 1;
	case KEY_DOWN: move_sel (columns ()); return 1;
	case KEY_UP: move_sel (-columns ()); return 1;
	case KEY_ENTER: play (); return 1;
	}
	return 0;
}

/* (vue, crans) */
static long v_wheel (long v, long n)
{
	long s = g_scroll - n * 60;
	g_scroll = s < 0 ? 0 : s;
	ui_view_dirty (v);
	return 0;
}

/* d = -1 / 1 : la console d'avant, d'apres */
static void next_filter (long d)
{
	long r = g_rowsel + d;
	if (r < 0)
		r = 0;
	long last = ui_list_count (g_list) - 1;
	if (r > last)
		r = last;
	ui_w_set_value (g_list, r);
	set_filter (r);
}

/* WEV_PAD (Elegant : cette fenetre a le clavier) : les fronts */
static void on_pad (void)
{
	if (!ui_ev_field (&ui, EV_KIND))		/* manette 0 : le clavier, deja arrive en touches */
		return;
	unsigned long b = (unsigned long) (unsigned) ui_ev_field (&ui, WEV_PAD_BUTTONS);
	unsigned long press = b & ~g_padold;		/* les boutons qui viennent d'etre pris */
	g_padold = b;
	if (press & PAD_UP)
		move_sel (-columns ());
	if (press & PAD_DOWN)
		move_sel (columns ());
	if (press & PAD_LEFT)
		move_sel (-1);
	if (press & PAD_RIGHT)
		move_sel (1);
	if (press & (PAD_START | PAD_B))
		play ();
	if (press & PAD_L)
		next_filter (-1);
	if (press & PAD_R)
		next_filter (1);
}

static void command (long id)
{
	if (id == ID_QUIT)
		exit (0);
	if (id == ID_REFRESH)
		rescan ();
	else if (id == ID_ADD)
	{
		static char ask[200];
		ask[0] = 0;
		if (!ui_ask ((long) "Add Folder", (long) TR ("Folder to watch (SD1:/games):"), (long) ask, 190))
			return;
		int fd = open (p_folders, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd < 0)
			return;
		size_t n = strlen (ask);
		ask[n++] = '\n';
		write (fd, ask, n);
		close (fd);
		rescan ();
	}
	else if (id == ID_FULL)
	{
		g_full ^= 1;
		ui_menu_state (ID_FULL, g_full ? UMS_CHECKED : 0);
	}
}

/* ---- les images, en arriere-plan ---- */

/* g -> g_tpath = le fichier de son image */
static void thumb_path (const struct game *g)
{
	char *p = copy_n (g_tpath, d_thumbs, sizeof g_tpath);
	*p++ = '/';
	p = copy_n (p, g->name, 64);
	copy_n (p, ".raw", 4);
}

/* g -> 1 : image lue */
static int thumb_load (struct game *g)
{
	thumb_path (g);
	int fd = open (g_tpath, O_RDONLY);
	if (fd < 0)
		return 0;
	uint32_t *px = (uint32_t *) calloc (TW * TH, 4);
	if (!px)
	{
		close (fd);
		return 0;
	}
	free (g->thumb);
	g->thumb = px;
	size_t got = 0;
	long r;
	while (got < TW * TH * 4 && (r = read (fd, (char *) px + got, TW * TH * 4 - got)) > 0)
		got += (size_t) r;
	close (fd);
	ui_view_dirty (g_view);
	return 1;
}

/* un pas : l'image attendue est-elle la ? sinon le jeu suivant */
static void thumb_tick (void)
{
	if (g_tgame)
	{
		struct game *g = &g_games[g_tgame - 1];
		struct stat st;
		thumb_path (g);
		if (stat (g_tpath, &st) >= 0 && (unsigned long) st.st_size >= TW * TH * 4)
		{
			thumb_load (g);
			g_tgame = 0;
		}
		else if (time_ms () - g_tstart >= TH_WAIT_MS)
			g_tgame = 0;			/* (trop long : tant pis pour celui-ci) */
		return;
	}
	for (long i = 0; i < g_ng; i++)
	{
		struct game *g = &g_games[i];
		if (g->tried)
			continue;
		g->tried = 1;
		if (thumb_load (g))
			return;
		const struct sys *y = &g_sys[g->sys];
		if (!y->thumb)
			return;				/* (pas d'image pour cette console) */
		char line[512];				/* « --thumb sortie rom » */
		char *p = copy_n (line, "--thumb ", 8);
		p = copy_n (p, g_tpath, 319);
		*p++ = ' ';
		copy_n (p, g->path, 199);
		if (spawn (y->emu, line) < 0)
			return;
		g_tgame = i + 1;
		g_tstart = time_ms ();
		return;
	}
}

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	void *ak = armpi_kit_open ("appkit", AK_F_SPAWN + 1);
	if (ak)
		f_spawn = (long (*) (long, long)) armpi_kit_fn (ak, AK_F_SPAWN);
	long scr[4] = { 0, 0, 0, 0 };
	ui_screen ((long) scr);
	unsigned long w = (unsigned long) (scr[0] - 8), h = (unsigned long) (scr[1] - scr[2] - scr[3] - 128);
	if (w > GL_W)					/* (au plus GL_W x GL_H) */
		w = GL_W;
	if (h > GL_H)
		h = GL_H;
	if (ui_window_ex (0, scr[3], (long) w, (long) h, (long) "Game Library", WF_RESIZABLE) < 0)
		return 0;
	ui_min_size (480, 320);
	ui_on_resize ((long) on_resize, 0);
	g_tok = (const unsigned *) ui_tokens ();
	ui_menu ((long) s_menus);
	mkdir (d_home, 0755);
	mkdir (d_lib, 0755);
	mkdir (d_thumbs, 0755);
	mkdir (d_roms, 0755);
	g_games = (struct game *) calloc (G_MAX, sizeof (struct game));
	if (!g_games)
		return 0;
	sys_init ();
	long s = side_width (ui.w);			/* les controles */
	g_list = ui_list (0, 0, s, ui.h, UL_SIDEBAR);
	g_view = ui_view (s, 0, ui.w - s, ui.h, UVW_FOCUS);
	ui_view_on (g_view, (long) v_paint, (long) v_mouse, (long) v_key, (long) v_wheel);
	rescan ();
	ui_w_focus (g_view);
	ui_view_dirty (g_view);
	ui_w_draw (0);
	ui_update_all ();
	for (;;)
	{
		long t = ui_event (50);
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MENU)
			command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_PAD)
			on_pad ();
		else if (t <= 0)
			ui_w_idle ();
		else
		{
			long kind = t == WEV_KEY ? ui_ev_field (&ui, EV_KIND) : 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code && w == g_list && code == UN_CHANGE)
				set_filter (ui_w_get_value (g_list));
			if (kind && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0)
					command (id);
			}
		}
		ui_view_flush ();
		thumb_tick ();
	}
	exit (0);
}
