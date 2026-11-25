/* notifyd - les notifications (comme celui d'Onyx), lancé par la session (/etc/session,
 * /etc/session.poche). Service « notify » (src/inc/notify.inc ; commande /bin/notify) :
 *  - op 1 : données = titre, 0, texte, 0 [, app, 0] : une notification (app : le paquet
 *    /apps/<app>.app qui l'envoie ; sans : le nom du process appelant). Elle va dans
 *    l'historique (non lue) et, sauf « Ne pas déranger » (korin.notify.dnd = 1), s'affiche en haut
 *    à droite, sous la barre de menus, pendant 4 s, dans une fenêtre sans cadre, au-dessus des
 *    autres, qui ne prend pas le clavier ; un clic la ferme. En mode poche (korin.shell.mode =
 *    poche, ou shell = poche dans /etc/system.ini) elle prend toute la largeur sous la barre.
 *    Elles attendent leur tour (file de NOTE_MAX).
 *  - op 2 : a = rang (0 : la plus récente) -> nombre de notifications gardées ; données :
 *    l'entrée NH_* de ce rang (s'il existe) ;
 *  - op 3 : a = id -> 0, ou -2 si elle n'y est plus : l'efface ;
 *  - op 4 : tout effacer ; op 5 : toutes lues.
 * L'historique garde les NH_MAX dernières. Le nombre de non lues est publié sur le bus :
 * korin.notify.unread (gardé ; docs/MODE-POCHE.md). Les réglages rapides (/sys/quick)
 * montrent l'historique.
 *
 * Traduit ligne à ligne de notifyd.asm. */
#include "sys_consts.h"
#include "uikit_import.h"
#include "buskit_import.h"

/* src/inc/notify.inc */
#define NOTIFY_OP 1
#define NOTIFY_OP_LIST 2
#define NOTIFY_OP_DELETE 3
#define NOTIFY_OP_CLEAR 4
#define NOTIFY_OP_READ 5

#define NOTE_MAX 8
#define NW 330
#define NH 74
#define PH 64 /* (poche) hauteur */
#define MARGIN_R 8 /* à droite */
#define DURATION 4000 /* ms */
#define NH_MAX 32 /* historique */

/* une entrée de l'historique (NOTIFY_OP_LIST : NH_* de notify.inc, 312 octets) */
struct hist {
	u64 id;     /* NH_ID : numéro, jamais réutilisé */
	u64 time;   /* NH_TIME : arrivée, ms depuis le démarrage */
	u64 unread; /* NH_UNREAD : 1 non lue */
	char app[32];    /* NH_APP */
	char title[64];  /* NH_TITRE */
	char text[192];  /* NH_TEXTE */
};
_Static_assert(sizeof(struct hist) == 312, "NH_SIZE");

/* une bulle en attente (NOTE_TITRE, NOTE_TEXTE, NOTE_APP) */
struct note {
	char title[64];
	char text[192];
	char app[32];
};

static struct krt_ui ui;
static long chan = -1;
static long shown;
static u64 end_ms;
static char *rest; /* seconde ligne du texte (0 : aucune) */
static u64 notes_t, notes_n;
static long pocket;
static long ini_pocket;
static long published = -1; /* dernier nombre de non lues publié */
static u64 h_n;
static u64 h_id;
static long tw[2]; /* taille de la bulle */
static long screen[4];
static u8 pinfo[64];
static char value[32];
static char path[96];
static u8 msg[WS_MSG + 8]; /* (8 octets à 0 de plus : une chaîne sans fin s'arrête là) */
static char fbuf[1024];
static struct note notes[NOTE_MAX];
static struct hist h_tab[NH_MAX];

static const char s_name[] = "notify";
static const char s_unread[] = "korin.notify.unread";
static const char s_dnd[] = "korin.notify.dnd";
static const char s_mode[] = "korin.shell.mode";
static const char s_pocket[] = "poche";
static const char s_shell[] = "shell";

static u32 token(long off)
{
	return *(const u32 *)((const u8 *)ui_tokens() + off);
}

/* ---- l'historique : h_tab[0] la plus ancienne .. h_tab[h_n - 1] la plus récente */

/* rang (0 : la plus récente) -> entrée, ou 0 */
static struct hist *h_rank(u64 r)
{
	if (r >= h_n)
		return 0;
	return &h_tab[h_n - r - 1];
}

/* d = destination, s = source, n = taille (0 compris) -> après le 0 de la source (copie) */
static const char *copy(char *d, const char *s, long n)
{
	n--;
	for (;;) {
		char c = *s++;
		if (!c)
			break;
		if (!n)
			continue; /* (trop long : coupé) */
		*d++ = c;
		n--;
	}
	*d = 0;
	return s;
}

/* nom -> 1 si c'est un paquet : [a-z0-9._-], et /apps/<nom>.app/main */
static int app_valid(const char *name)
{
	const char *p = name;
	for (;; p++) {
		u8 c = (u8)*p;
		if (!c)
			break;
		if ((u8)(c - 'a') <= 25 || (u8)(c - '0') <= 9 || c == '_' || c == '-' || c == '.')
			continue;
		return 0;
	}
	if (p == name) /* (vide) */
		return 0;
	path[0] = 0;
	char *e = str_cat(path, "/apps/");
	e = str_cat(e, name);
	str_cat(e, ".app/main");
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return 0;
	sys_close(fd);
	return 1;
}


/* « VOL:/x » : le préfixe d'un ancien volume (« SD0: ») est retiré ; un alias de vfs (HOME:, PUBLIC:, APPS:) est
 * gardé : « HOME:/apps/x.app/main », une app de l'utilisateur, ne devient pas « /apps/x.app/main » (25/11/2026) */
static int exe_alias(const char *e, const char *c)
{
	long n = c - e;
	static const char *const al[] = {"HOME", "PUBLIC", "APPS", 0};
	for (int i = 0; al[i]; i++) {
		long k = 0;
		while (al[i][k] && k < n && (e[k] & ~0x20) == al[i][k])
			k++;
		if (!al[i][k] && k == n)
			return 1;
	}
	return 0;
}

/* l'app que dit l'expéditeur : crue d'un process d'uid 0 (notify -a, les services), ou si son exécutable est
 * /apps/<app>.app/main ; sinon une app se faisait passer pour une autre (icône, nom, E35, D16) */
static u8 pex[296] __attribute__((aligned(8)));
static int app_from_sender(const char *app, long pid)
{
	if (korin_sys1(SYS_PROC_UID, pid) == 0)
		return 1;
	if (korin_sys2(SYS_PROC_EXE, pid, pex) < 0)
		return korin_sys1(SYS_PROC_UID, pid) == -38; /* (le Pi : ni uid ni identité) */
	pex[40 + 127] = 0;
	const char *e = (const char *)pex + 40, *c = e;
	while (*c && *c != ':' && c - e < 8)
		c++;
	if (*c == ':' && c[1] == '/' && !exe_alias(e, c)) /* (un ancien nom de volume devant) */
		e = c + 1;
	const char *pre = "/apps/";
	while (*pre && *e == *pre)
		pre++, e++;
	if (*pre)
		return 0;
	while (*app && *e == *app)
		app++, e++;
	if (*app)
		return 0;
	const char *suf = ".app/main";
	while (*suf && *e == *suf)
		suf++, e++;
	return !*suf && !*e;
}

/* (msg) la notification reçue, en tête de l'historique (non lue) */
static void history(void)
{
	if (h_n >= NH_MAX) { /* pleine : la plus ancienne s'en va */
		h_n--;
		mem_move(h_tab, h_tab + 1, (NH_MAX - 1) * sizeof(struct hist));
	}
	struct hist *h = &h_tab[h_n++];
	h->id = ++h_id; /* id, heure, non lue */
	h->time = (u64)sys_time_ms();
	h->unread = 1;
	const char *s = copy(h->title, (const char *)msg + WM_DATA, sizeof h->title);
	s = copy(h->text, s, sizeof h->text);
	if (s < (const char *)msg + WS_MSG - 1 && *s) { /* l'app, si elle est dite */
		copy(h->app, s, sizeof h->app);
		if (app_valid(h->app) && /* (un vrai paquet : des expéditeurs laissent des restes après le texte) */
		    app_from_sender(h->app, *(u32 *)(msg + WM_PID)))
			return;
	}
	h->app[0] = 0; /* sinon : le nom du process appelant */
	if (korin_sys2(SYS_PROC_INFO, *(u32 *)(msg + WM_PID), pinfo) < 0)
		return;
	copy(h->app, (const char *)pinfo + 24, sizeof h->app);
}

/* rang -> nombre ; l'entrée dans les données de msg */
static long h_copy(u64 r)
{
	struct hist *h = h_rank(r);
	if (h)
		mem_move(msg + WM_DATA, h, sizeof *h);
	return (long)h_n;
}

/* id -> 0, E_NOENT */
static long h_delete(u64 id)
{
	for (u64 i = 0; i < h_n; i++) {
		if (h_tab[i].id != id)
			continue;
		h_n--; /* les suivantes remontent */
		mem_move(&h_tab[i], &h_tab[i + 1], (h_n - i) * sizeof(struct hist));
		return 0;
	}
	return E_NOENT;
}

/* toutes lues */
static void h_read(void)
{
	for (u64 i = 0; i < h_n; i++)
		h_tab[i].unread = 0;
}

/* korin.notify.unread = le nombre de non lues, s'il a changé */
static void publish(void)
{
	long n = 0;
	for (u64 i = 0; i < h_n; i++)
		n += (long)h_tab[i].unread;
	if (n == published)
		return;
	published = n;
	*fmt_dec((u64)n, value) = 0;
	bus_publish((long)s_unread, (long)value, -1, BUS_RETAIN);
}

/* -> 1 si « Ne pas déranger » (korin.notify.dnd = 1) */
static int dnd(void)
{
	*(u64 *)value = 0;
	if (bus_get((long)s_dnd, (long)value, 8) <= 0)
		return 0;
	return value[0] == '1';
}

/* -> 1 en mode poche : korin.shell.mode (Elegant), sinon system.ini */
static long pocket_mode(void)
{
	((u64 *)value)[0] = 0;
	((u64 *)value)[1] = 0;
	if (bus_get((long)s_mode, (long)value, 15) <= 0)
		return ini_pocket;
	return str_eq(value, s_pocket);
}

/* texte, préfixe -> après le préfixe, ou 0 */
static char *prefix(char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	return s;
}

/* /etc/system.ini : « shell = poche » ? -> ini_pocket */
static void read_system(void)
{
	long fd = sys_open("/etc/system.ini", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, fbuf, 1023);
	sys_close(fd);
	if (n <= 0)
		return;
	fbuf[n] = 0;
	char *l = fbuf;
	while (*l) { /* début de ligne : « shell » ? */
		char *s = prefix(l, s_shell);
		if (s) {
			while (*s == ' ' || *s == 9 || *s == '=') /* espaces et « = » */
				s++;
			if (prefix(s, s_pocket))
				ini_pocket = 1;
		}
		for (;;) { /* ligne suivante */
			char c = *l;
			if (!c)
				return;
			l++;
			if (c == 10)
				break;
		}
	}
}

/* ---- les bulles */

/* (msg) l'entrée la plus récente de l'historique : à la fin de la file des bulles (perdue si
 * elle est pleine) */
static void enqueue(void)
{
	if (notes_n >= NOTE_MAX)
		return;
	struct note *n = &notes[(notes_t + notes_n) & (NOTE_MAX - 1)];
	notes_n++;
	struct hist *h = h_rank(0);
	copy(n->title, h->title, 64);
	copy(n->text, h->text, 192);
	copy(n->app, h->app, 32);
}

/* texte : coupe la première ligne au « \n » (rest = la suite, 0 si aucune) */
static void first_line(char *s)
{
	rest = 0;
	for (; *s; s++)
		if (*s == 10) {
			*s = 0;
			rest = s + 1;
			return;
		}
}

/* la bulle (fond du thème, pastille d'accent - en poche, l'icône de l'app -, titre en gras,
 * texte sur deux lignes au plus : coupé au « \n » ou à la largeur) */
static void draw(struct note *n)
{
	long w = tw[0], h = tw[1];
	ui_fill(0, 0, w, h, token(TJ_LINE_STRONG));
	ui_rbox(1, 1, w - 2, h - 2, 6, token(TJ_SURFACE), token(TJ_SURFACE));
	long x = 20; /* x du texte */
	if (pocket && n->app[0]) { /* poche : l'icône de l'app */
		ui_icon((long)n->app, 14, (PH - 28) / 2, 28);
		x = 54;
	} else /* pastille d'accent */
		ui_rbox(8, 10, 4, h - 20, 2, token(TJ_ACCENT), token(TJ_ACCENT));
	ui_text((long)n->title, x, 26, token(TJ_TEXT1), 15, (long)ui.font_b, w - x - 12);
	first_line(n->text); /* texte : première ligne */
	ui_text((long)n->text, x, 46, token(TJ_TEXT2), 13, (long)ui.font, w - x - 12);
	if (rest && h >= NH) /* seconde ligne (s'il y a la place) */
		ui_text((long)rest, x, 64, token(TJ_TEXT2), 13, (long)ui.font, w - x - 12);
	ui_update_all();
}

/* la notification suivante de la file, s'il y en a : fenêtre, dessin */
static void next_note(void)
{
	if (!notes_n)
		return;
	notes_n--;
	struct note *n = &notes[notes_t];
	notes_t = (notes_t + 1) & (NOTE_MAX - 1);
	ui_theme_text(0); /* le thème du moment (sans bulle, notifyd ne lit pas ses événements :
			     korin.theme est passé sans lui) ; sous la barre */
	ui_screen((long)screen);
	pocket = pocket_mode();
	long x, y, w, h;
	if (!pocket) { /* bureau : en haut à droite */
		x = screen[0] - (NW + MARGIN_R);
		y = screen[3] + 6; /* réservé en haut (barre de menus) */
		w = NW;
		h = NH;
	} else { /* poche : toute la largeur */
		w = screen[0] - 12;
		y = screen[3] + 6;
		x = 6;
		h = PH;
	}
	tw[0] = w;
	tw[1] = h;
	if (ui_window_ex(x, y, w, h, (long)n->title, WF_FRAMELESS | WF_TOP | WF_NOFOCUS | WF_NOLIST) < 0)
		return; /* (comme l'asm : la bulle est perdue) */
	draw(n);
	end_ms = (u64)sys_time_ms() + DURATION;
	shown = 1;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ui_init((long)&ui);
	chan = korin_sys2(SYS_SVC_REGISTER, s_name, 0);
	if (chan < 0)
		_exit(1);
	read_system();
	publish(); /* korin.notify.unread = 0 */
	for (;;) {
		long wait;
		if (shown) { /* une notification à l'écran : ses événements (clic), son temps */
			long e = ui_event(50);
			if ((e == WEV_MOUSE && *(u32 *)(ui.ev + EV_KIND) == 1) || /* appui : fermée */
			    (u64)sys_time_ms() >= end_ms) {
				ui_close();
				shown = 0;
				next_note();
				continue;
			}
			wait = 0; /* (les demandes, sans attendre) */
		} else
			wait = 60000; /* rien à l'écran : attendre une demande */
		long req = korin_sys3(SYS_SVC_NEXT, chan, msg, wait);
		if (req < 0)
			_exit(1);
		if (!req)
			continue;
		long r = 0;
		switch (*(u64 *)(msg + WM_OP)) {
		case NOTIFY_OP:
			history();
			publish();
			if (dnd()) /* Ne pas déranger : pas de bulle */
				break;
			enqueue();
			if (!shown)
				next_note();
			break;
		case NOTIFY_OP_LIST:
			r = h_copy(*(u64 *)(msg + WM_A));
			break;
		case NOTIFY_OP_DELETE:
			r = h_delete(*(u64 *)(msg + WM_A));
			publish();
			break;
		case NOTIFY_OP_CLEAR:
			h_n = 0;
			publish();
			break;
		case NOTIFY_OP_READ:
			h_read();
			publish();
			break;
		default:
			r = E_INVAL;
		}
		*(long *)msg = r;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
