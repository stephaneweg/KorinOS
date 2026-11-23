/* elegant.c : Elegant, le serveur graphique de Korin (/sys/elegant), repris de elegant.asm :
 * la boucle, le choix du mode, le tas ; l'état commun (segment de données de l'asm).
 *
 * Lancé par le noyau au démarrage. S'enregistre comme serveur (SYS_WS_REGISTER), prend l'écran
 * et les entrées, puis sert les programmes : fenêtres, surfaces partagées, événements. Le mode
 * (shell= de /etc/system.ini, ou l'argument) choisit la table de réglages : « desktop » (le
 * bureau), « poche » (une app devant, pour le PocketTerm : docs/MODE-POCHE.md) ; « console »
 * rend la main (la console texte du noyau garde l'écran). « shell=desktop » dans
 * /etc/options.txt (ou /options.txt) force le bureau : la sortie de secours du mode
 * poche (avec Maj au démarrage). */
#include "elegant.h"

/* tampon arrière : pour la plus grande résolution (SCR_BB_BYTES de screen.inc : SCR_MAX_W x
 * SCR_MAX_H, FB_MAX_W et FB_MAX_H du noyau) */
#define BB_BYTES (2560L * 1600 * 4)

/* ---- état commun (segment de données d'elegant.asm) */

const struct policy *policy; /* table du mode actif */
long scr_w, scr_h;
u32 *fb_va;
long fb_pitch;
u32 *bb;
long bb_pitch;
long dirty_n, dirty_overflow;
struct rect dirty_list[DIRTY_MAX], cmp_list[DIRTY_MAX];
long cmp_n;
struct rect clip_r;
long cover_only;
long cur_mods, cur_x, cur_y, cur_buttons;
struct win *drag_win;
long drag_dx[2];
struct win *capture_win, *lock_win;
u64 cur_req_win;
long cur_req, cur_shape;
long drag_on, drag_type, drag_len;
struct win *resize_win;
long resize_st[5];
long win_count;
struct win *win_focus;
long desk_cur, desk_count = 1, desk_gen;
long curtain_on;
struct win *win_z[MAX_WINDOWS];
long next_win_id, win_cascade;
long area_top, area_bot;
long wins_dirty, need_all;
u8 *hid_ring;
long hist_n, diag_bad, measure_opaque;
long menu_gen, menu_last_gen, menu_serial;
long frame_next;
u64 info[5];
u8 msg[WS_MSG] __attribute__((aligned(16)));
u8 evbuf[32] __attribute__((aligned(16)));
char mode_name[32];
char ini_val[32];
char ini_buf[INI_MAX];
struct win windows[MAX_WINDOWS] __attribute__((aligned(16)));
long clip_len, clip_gen;
u8 *clip_buf;
u8 drag_data[WS_DRAG_MAX];
long win_restore[MAX_WINDOWS][WIN_RESTORE_N];
u8 menu_specs[MAX_WINDOWS][WS_MENU_MAX] __attribute__((aligned(16)));

/* modes connus (le premier par défaut) */
static const struct policy *const modes[] = { &mode_desktop, &mode_pocket, 0 };

static const char path_ini[] = "/etc/system.ini";
static const char path_opts[] = "/etc/options.txt";
static const char path_opts_root[] = "/options.txt";
static const char str_shell[] = "shell";
static const char str_console[] = "console";
static const char m_ready[] = "elegant : ecran pris, en service\n";
static const char m_busy[] = "elegant : un serveur graphique tourne deja\n";
static const char m_nomem[] = "elegant : memoire insuffisante\n";

/* l'argument de l'asm (x0 : la ligne du noyau) : le premier mot ; 0 : aucun */
int main(int argc, char **argv)
{
	const char *arg = argc > 1 ? argv[1] : 0;
	if (!mode_choose(arg))
		_exit(0); /* console */
	if (korin_sys0(SYS_WS_REGISTER) < 0 || korin_sys1(SYS_WS_DISPLAY, info) < 0) {
		ak_puts(m_busy);
		_exit(1);
	}
	fb_va = (u32 *)info[0]; /* écran : adresse, largeur, hauteur, pas */
	scr_w = (long)info[1];
	scr_h = (long)info[2];
	fb_pitch = (long)info[3];
	if (!present_init(BB_BYTES)) { /* tampon arrière, présentation : present.c */
		ak_puts(m_nomem);
		_exit(1);
	}
	cur_x = (long)((u64)scr_w >> 1); /* curseur au centre */
	cur_y = (long)((u64)scr_h >> 1);
	dirty_all();
	policy->init(); /* (session du bureau) */
	hid_subscribe(); /* entrées : HIDManager */
	ak_puts(m_ready);

	for (;;) {
		do_input();       /* entrées */
		policy->tick();
		screen_tick();    /* résolution non confirmée */
		notify_listeners(); /* liste des fenêtres changée */
		damage_collect(); /* présent asynchrone des programmes */
		compose();
		/* -> attente : 8 ms au plus pour la première requête (ou un signal), puis toutes
		   celles en file */
		long wait = frames_post();
		if (ov_busy()) /* (une animation de la vue d'ensemble : l'image suivante tout de suite) */
			wait = 1;
		for (;;) {
			long n = korin_sys2(SYS_WS_NEXT, msg, wait);
			if (!n)
				break;
			if (n < 0) {
				korin_sys1(SYS_SLEEP, 8);
				break;
			}
			if (M64(msg, WM_OP) != SVC_OP_SIGNAL) /* (signal : damage_collect, au tour suivant) */
				req_handle(n);
			wait = 0;
		}
	}
}

/* argument (0 : aucun) -> policy = table du mode ; 0 si « console » */
long mode_choose(const char *arg)
{
	const char *name = arg;
	if (!name || !*name) {
		read_shell(); /* shell= de /etc/system.ini */
		name = mode_name;
		if (force_desktop()) /* (options.txt : shell=desktop) */
			name = str_mode_desktop;
	}
	if (str_eq(name, str_console))
		return 0;
	const struct policy *p = modes[0]; /* modes connus ; inconnu : le premier */
	for (long i = 0; modes[i]; i++)
		if (str_eq(name, modes[i]->name)) {
			p = modes[i];
			break;
		}
	policy = p;
	return 1;
}

/* -> 1 si /etc/options.txt ou /options.txt a une ligne « shell=desktop » (la sortie de
 * secours du mode poche) */
long force_desktop(void)
{
	if (read_key_shell(path_opts) && str_eq(ini_val, str_mode_desktop))
		return 1;
	return read_key_shell(path_opts_root) && str_eq(ini_val, str_mode_desktop);
}

/* lit /etc/system.ini et recopie la valeur de « shell= » dans mode_name (vide si absent) */
void read_shell(void)
{
	read_key_shell(path_ini);
	char *d = mode_name;
	const char *s = ini_val;
	while ((*d++ = *s++))
		;
}

/* fichier : la valeur de sa ligne « shell= » dans ini_val (vide si absente) -> 1 si trouvée */
long read_key_shell(const char *file)
{
	return read_key(file, str_shell);
}

/* fichier, clé : la valeur de sa ligne « clé = » dans ini_val (vide si absente ; jusqu'au
 * premier espace, 30 octets au plus) -> 1 si trouvée */
long read_key(const char *file, const char *key)
{
	long found = 0;
	ini_val[0] = 0;
	long fd = sys_open(file, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, ini_buf, INI_MAX - 1);
	sys_close(fd);
	if (n <= 0)
		return 0;
	ini_buf[n] = 0;
	const u8 *p = (const u8 *)ini_buf; /* début de ligne */
	while (*p) {
		long k = 0; /* la ligne commence par « clé = » ? */
		while (key[k] && (u8)key[k] == p[k])
			k++;
		if (!key[k]) {
			const u8 *q = p + k; /* « shell », espaces, « = », espaces */
			while (*q == ' ' || *q == 9)
				q++;
			if (*q == '=') {
				q++;
				while (*q == ' ' || *q == 9)
					q++;
				found = 1;
				long i = 0;
				while (q[i] > ' ' && q[i] != '#' && i < 30) /* fin, espace, tabulation, CR, LF */
					ini_val[i] = (char)q[i], i++;
				ini_val[i] = 0;
				return found;
			}
			p = q; /* (pas de « = » : la ligne est sautée depuis là, comme l'asm) */
		}
		for (;;) { /* ligne suivante */
			u8 c = *p++;
			if (!c)
				return found;
			if (c == 10)
				break;
		}
	}
	return found;
}

/* taille -> bloc (tas du programme, aligné 16), 0 si échec */
void *heap_alloc(long size)
{
	long r = korin_sys1(SYS_SBRK, (size + 15) & ~15L);
	return r < 0 ? 0 : (void *)r;
}
