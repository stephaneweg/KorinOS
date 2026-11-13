/* hidmanager - point d'arrivée de toutes les entrées (docs/FEUILLE-DE-ROUTE.md,
 * section 2), repris de hidmanager.asm ; lancé par init2 avant Elegant.
 * Commun au Pi et au PC.
 *  * il lit l'anneau partagé de chaque pilote (gt911, usbkeyboard,
 *    usbmouse...) : 10 ms au plus d'attente ;
 *  * il applique la disposition du clavier : intégrée (FR), « keyb= » de
 *    SD0:/etc/options.txt, ou SD0:/res/keyb/<NOM>.txt (op HID_OP_KEYMAP) ;
 *    les touches mortes (« dead:^ ») se composent avec la suivante par
 *    SD0:/res/keyb/compose.tab (docs/CLAVIER.md) ;
 *  * il répète la touche tenue ;
 *  * les manettes (src/inc/pad.inc) : le mapping de SD0:/etc/gamepad.ini aux
 *    boutons des manettes et aux touches du clavier (EV_PAD) ;
 *  * il dépose les événements dans un anneau partagé par abonné (Elegant) :
 *    l'abonné les lit sans appel système ;
 *  * plusieurs souris à la fois (touchpad, souris USB, PS/2, tablette) : les
 *    boutons de chaque pilote sont gardés à part, l'événement porte leur OU
 *    (un pilote qui bouge sans bouton ne relâche pas celui qu'une autre souris
 *    tient) ; une seule souris : l'événement tel quel ;
 *  * vitesse du pointeur (SD0:/etc/pointer.ini, docs/etude/TOUCHPAD-PC.md) :
 *    « mouse » (%, souris relatives) avec « accel » (petite accélération),
 *    « touchpad » (%, mouvements marqués EV_REL_TOUCHPAD par i2chid, qui a sa
 *    propre accélération) ; sans fichier : 100, 0, 200 (une souris comme
 *    avant).
 * Service « hid » :
 *  HID_OP_ABONNER (1) -> numéro d'un tampon partagé (mappé chez l'appelant par
 *    buf_map) : +0 tête (écrite par hidmanager), +8 queue (écrite par
 *    l'abonné), +ANNEAU_EV : ANNEAU_N événements de 32 octets (format
 *    d'input.asm, le caractère de la disposition en +4 pour une touche)
 *  HID_OP_KEYMAP (2), données = nom (« FR », « US »...) -> 0 ou -2
 *  HID_OP_PILOTE (3) -> numéro d'un tampon partagé, même anneau, mais c'est
 *    le pilote qui écrit (la tête) et hidmanager qui lit (la queue)
 *  HID_OP_PADMAP (4) -> 0 : relit SD0:/etc/gamepad.ini
 *  HID_OP_POINTER (5) -> 0 : relit SD0:/etc/pointer.ini (Réglages, Souris) */
#include "../svclib/svclib.h"

#define HID_OP_ABONNER 1
#define HID_OP_KEYMAP 2
#define HID_OP_PILOTE 3
#define HID_OP_PADMAP 4
#define HID_OP_POINTER 5
#define EV_MOUSE_REL 2
#define EV_MOUSE_ABS 3
#define EV_REL_TOUCHPAD (1ull << 30) /* (boutons d'EV_MOUSE_REL : mouvement du touchpad, i2chid) */
#define ABONNES_MAX 4
#define PILOTES_MAX 16 /* pilotes en services (tactile, claviers, souris...) */
#define ANNEAU_EV 64
#define ANNEAU_N 64 /* puissance de 2 */
#define EV_KEY 1
#define EV_PAD_RAW 16
#define EV_PAD 17
#define REP_DELAI 400 /* touche maintenue : première répétition (ms) */
#define REP_PAS 35    /* puis toutes les REP_PAS ms */

#define KEY_MAP_N 0x65 /* codes HID 0x00..0x64 */
#define KB_COLS 3
#define KB_FICHIER_MAX 8192
#define COMPOSE_MAX 128 /* compositions de compose.tab */

#define PAD_MAX 4
#define PAD_UP (1 << 0)
#define PAD_DOWN (1 << 1)
#define PAD_LEFT (1 << 2)
#define PAD_RIGHT (1 << 3)
#define PAD_A (1 << 4)
#define PAD_B (1 << 5)
#define PAD_X (1 << 6)
#define PAD_Y (1 << 7)
#define PAD_L (1 << 8)
#define PAD_R (1 << 9)
#define PAD_SELECT (1 << 12)
#define PAD_START (1 << 13)
#define PAD_NBUTTONS 17
#define PADSET_SIZE 24 /* vid << 16 | pid (0 : libre), 17 numéros de bouton */
#define PADSET_MAX 8
#define PAD_KEYS_MAX 40

/* une place de abonnes ou de pilotes (AB_*) */
struct ring_slot {
	volatile u64 pid;
	u64 buf;
	volatile u64 *adr; /* +0 tête, +8 queue, +ANNEAU_EV les événements */
};

static long canal = -1;
static u64 rep_time; /* touche tenue : prochaine répétition */
static volatile u64 rep_on;
static u64 rep_ev[4];
/* (l'asm range pilotes juste après abonnes : desabonner parcourt les deux) */
static struct ring_slot abonnes[ABONNES_MAX];
static struct ring_slot pilotes[PILOTES_MAX];
static u64 pilotes_n;
static u64 ev_pilote[4];
static u32 pad_btn[PAD_MAX];
static u64 pad_ax[PAD_MAX];
static u64 pad_ev[4];
static u32 pad_rawb[PAD_MAX]; /* boutons bruts */
static u32 pad_id[PAD_MAX];   /* modèle : vid << 16 | pid */
static u64 pad_keys_n;
static u64 pad_keys[PAD_KEYS_MAX]; /* +0 code HID (0 : par le caractère), +1 caractère, +4 bouton */
static u8 pad_sets[PADSET_MAX * PADSET_SIZE];
static u8 msg[512] __attribute__((aligned(16)));
static u16 kb_live[KB_COLS * KEY_MAP_N];
static u16 kb_tmp[KB_COLS * KEY_MAP_N];
static u8 kb_dead_live[KB_COLS * KEY_MAP_N]; /* 1 : touche morte (même rang que kb_live) */
static u8 kb_dead_tmp[KB_COLS * KEY_MAP_N];
struct compose {
	u16 dead, base, out;
};
static struct compose compose_t[COMPOSE_MAX]; /* SD0:/res/keyb/compose.tab */
static u64 compose_n;
static u32 dead_pending; /* touche morte en attente (son caractère), 0 : aucune
			    (le thread lecteur seul : traduire) */
static char kb_fichier[KB_FICHIER_MAX];

static inline void dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

/* ---- dispositions intégrées (kb_use_builtin) : tables HID -> Unicode (0x00..0x64) */

#define KM(letters, digits, p2d, p2e, p2f, p30, p31, p32, p33, p34, p35, p36, p37, p38, p64) \
	{0, 0, 0, 0, letters, digits, 10, 27, 8, 9, ' ', p2d, p2e, p2f, p30, p31, p32, p33, p34, p35, \
	 p36, p37, p38, [0x64] = p64}
#define L26(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z) \
	a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z
#define D10(a, b, c, d, e, f, g, h, i, j) a, b, c, d, e, f, g, h, i, j

static const u16 map_us_n[KEY_MAP_N] = KM(
	L26('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u',
	    'v', 'w', 'x', 'y', 'z'),
	D10('1', '2', '3', '4', '5', '6', '7', '8', '9', '0'), '-', '=', '[', ']', '\\', '#', ';', '\'', '`', ',',
	'.', '/', '\\');
static const u16 map_us_s[KEY_MAP_N] = KM(
	L26('A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U',
	    'V', 'W', 'X', 'Y', 'Z'),
	D10('!', '@', '#', '$', '%', '^', '&', '*', '(', ')'), '_', '+', '{', '}', '|', '~', ':', '"', '~', '<', '>',
	'?', '|');
/* AZERTY : positions physiques US -> caractères français */
static const u16 map_fr_n[KEY_MAP_N] = KM(
	L26('q', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', ',', 'n', 'o', 'p', 'a', 'r', 's', 't', 'u',
	    'v', 'z', 'x', 'y', 'w'),
	D10('&', 0xE9, '"', '\'', '(', '-', 0xE8, '_', 0xE7, 0xE0), ')', '=', '^', '$', '*', '*', 'm', 0xF9, 0xB2,
	';', ':', '!', '<');
static const u16 map_fr_s[KEY_MAP_N] = KM(
	L26('Q', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', '?', 'N', 'O', 'P', 'A', 'R', 'S', 'T', 'U',
	    'V', 'Z', 'X', 'Y', 'W'),
	D10('1', '2', '3', '4', '5', '6', '7', '8', '9', '0'), 0xB0, '+', 0xA8, 0xA3, 0xB5, 0xB5, 'M', '%', 0xB2, '.',
	'/', 0xA7, '>');
static const u16 map_fr_a[KEY_MAP_N] = KM(
	L26(0, 0, 0, 0, 0x20AC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
	D10(0, '~', '#', '{', '[', '|', '`', '\\', '^', '@'), ']', '}', 0, 0xA4, 0, 0, 0, 0, 0, 0, 0, 0, '|');

/* fr = 0 US / 1 FR : disposition intégrée dans kb_live */
static void kb_use_builtin(int fr)
{
	mem_move(kb_live, fr ? map_fr_n : map_us_n, sizeof map_us_n);
	mem_move(kb_live + KEY_MAP_N, fr ? map_fr_s : map_us_s, sizeof map_us_n);
	for (int i = 0; i < KEY_MAP_N; i++) /* AltGr : vide par défaut */
		kb_live[2 * KEY_MAP_N + i] = 0;
	if (fr)
		mem_move(kb_live + 2 * KEY_MAP_N, map_fr_a, sizeof map_us_n);
	memset(kb_dead_live, 0, sizeof kb_dead_live); /* (intégrées : sans touche morte) */
}

/* ---- fichier de disposition SD0:/res/keyb/<NOM>.txt (une touche par ligne,
 * # : commentaire) : « <code HID en hexa>  <normal>  [<Maj>  [<AltGr>]] » ;
 * chaque caractère tel quel (UTF-8), ou « U+20AC », ou « - » pour aucun ;
 * « dead:X » : touche morte de caractère X (docs/CLAVIER.md) */

static const char *kp, *ke; /* position, fin (x19, x20 de kb_parse) */

static void kp_blank(void)
{
	while (kp < ke && (*kp == ' ' || *kp == 9))
		kp++;
}

static long kp_hexval(u8 c)
{
	u32 v = (u32)c - '0';
	if (v <= 9)
		return v;
	v = (u32)(c | 0x20) - 'a'; /* minuscule */
	if (v > 5)
		return -1;
	return v + 10;
}

/* un caractère en kp -> point de code (0 : « - ») */
static u32 kp_token(void)
{
	u8 c = (u8)*kp;
	if (c == '-') { /* « - » seul : aucun caractère */
		const char *n = kp + 1;
		if (n >= ke || *n == ' ' || *n == 9 || *n == 10 || *n == 13) {
			kp++;
			return 0;
		}
	} else if (c == 'U' && kp + 1 < ke && kp[1] == '+') { /* « U+XXXX » */
		kp += 2;
		u64 v = 0;
		for (; kp < ke; kp++) {
			long h = kp_hexval((u8)*kp);
			if (h < 0)
				break;
			v = (v << 4) + (u64)h;
		}
		return (u32)(v & 0xFFFF);
	}
	c = (u8)*kp++; /* un caractère UTF-8 (1 à 3 octets) */
	if (!(c & 0x80))
		return c;
	u32 v;
	if ((c & 0xE0) == 0xC0)
		v = c & 0x1F;
	else { /* (4 octets, ou octet de suite : comme trois, comme l'asm) */
		v = c & 0x0F;
		if (kp >= ke)
			return v;
		v = (v << 6) | ((u8)*kp++ & 0x3F);
	}
	if (kp >= ke)
		return v;
	return (v << 6) | ((u8)*kp++ & 0x3F);
}

/* texte, taille -> kb_tmp rempli, nombre de touches lues */
static long kb_parse(const char *text, long n)
{
	kp = text;
	ke = text + n;
	long keys = 0;
	while (kp < ke) {
		kp_blank();
		if (kp < ke && *kp == '#')
			goto skip;
		u64 code = 0, digits = 0; /* code HID (hexa) */
		for (; kp < ke; kp++, digits++) {
			long h = kp_hexval((u8)*kp);
			if (h < 0)
				break;
			code = (code << 4) + (u64)h;
		}
		if (!digits || code >= KEY_MAP_N)
			goto skip;
		long col = 0;
		for (; col < KB_COLS; col++) {
			kp_blank();
			if (kp >= ke || *kp == 10 || *kp == 13)
				break;
			u8 dead = 0; /* « dead:X » : touche morte */
			if (ke - kp > 5 && kp[0] == 'd' && kp[1] == 'e' && kp[2] == 'a' && kp[3] == 'd' &&
			    kp[4] == ':') {
				kp += 5;
				dead = 1;
			}
			u32 c = kp_token();
			kb_tmp[col * KEY_MAP_N + code] = (u16)c;
			kb_dead_tmp[col * KEY_MAP_N + code] = c ? dead : 0;
		}
		if (col)
			keys++;
skip:
		for (;;) { /* fin de ligne */
			if (kp >= ke)
				return keys;
			if (*kp++ == 10)
				break;
		}
	}
	return keys;
}

/* SD0:/res/keyb/compose.tab : « <morte>  <touche>  <résultat> » par ligne
 * (# : commentaire) -> compose_t, compose_n ; sans fichier : aucune
 * composition (la morte, puis la touche) */
static void compose_load(void)
{
	compose_n = 0;
	long fd = sys_open("SD0:/res/keyb/compose.tab", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, kb_fichier, KB_FICHIER_MAX);
	sys_close(fd);
	if (n <= 0)
		return;
	kp = kb_fichier;
	ke = kb_fichier + n;
	while (kp < ke && compose_n < COMPOSE_MAX) {
		kp_blank();
		u32 v[3];
		int k = 0;
		for (; k < 3; k++) {
			if (kp >= ke || *kp == 10 || *kp == 13 || (*kp == '#' && !k))
				break;
			v[k] = kp_token();
			kp_blank();
		}
		if (k == 3 && v[0] && v[1] && v[2]) {
			compose_t[compose_n].dead = (u16)v[0];
			compose_t[compose_n].base = (u16)v[1];
			compose_t[compose_n].out = (u16)v[2];
			compose_n++;
		}
		while (kp < ke && *kp++ != 10) /* fin de ligne */
			;
	}
}

/* morte, caractère -> caractère composé, 0 : aucun */
static u32 compose(u32 dead, u32 c)
{
	for (u64 i = 0; i < compose_n; i++)
		if (compose_t[i].dead == dead && compose_t[i].base == c)
			return compose_t[i].out;
	return 0;
}

/* nom de la disposition (« US », « FR »...) -> 1 si chargée depuis
 * SD0:/res/keyb/<nom>.txt */
static int kb_load(const char *name)
{
	char path[64];
	char *p = str_cat(path, "SD0:/res/keyb/"); /* (str_cat d'AppKit : copie, rend la fin) */
	for (int i = 0; i < 15 && name[i]; i++) /* nom : 15 caractères au plus */
		*p++ = name[i];
	str_cat(p, ".txt");
	long fd = sys_open(path, O_READ);
	if (fd < 0) {
		say("hidmanager : fichier absent ");
		goto err;
	}
	long n = sys_read(fd, kb_fichier, KB_FICHIER_MAX);
	sys_close(fd);
	if (n > 0) {
		memset(kb_tmp, 0, sizeof kb_tmp); /* table de travail : vide */
		memset(kb_dead_tmp, 0, sizeof kb_dead_tmp);
		long keys = kb_parse(kb_fichier, n);
		if (keys) {
			u64 deads = 0;
			for (u64 i = 0; i < sizeof kb_dead_tmp; i++)
				deads += kb_dead_tmp[i];
			if (deads) /* (relu à chaque disposition : un fichier changé est pris ;
				      kb_fichier réutilisé : après kb_parse) */
				compose_load();
			mem_move(kb_live, kb_tmp, sizeof kb_tmp); /* remplacement de la table active */
			mem_move(kb_dead_live, kb_dead_tmp, sizeof kb_dead_tmp);
			say("hidmanager : disposition ");
			say(name);
			say(" (");
			say_dec(keys);
			say(" touches");
			if (deads) {
				say(", ");
				say_dec((long)deads);
				say(" mortes, ");
				say_dec((long)compose_n);
				say(" compositions");
			}
			say(")\n");
			return 1;
		}
	}
	say("hidmanager : aucune touche dans ");
err:
	say(path);
	say("\n");
	return 0;
}

/* code HID, modificateurs -> rang dans kb_live (-1 : aucun caractère) ;
 * AltGr (Alt droite) : 3e colonne, à défaut la colonne normale / Maj */
static long key_slot(u64 code, u64 mods)
{
	if (code >= KEY_MAP_N || (mods & 0x11)) /* Ctrl : pas de caractère */
		return -1;
	if ((mods & 0x40) && kb_live[2 * KEY_MAP_N + code])
		return (long)(2 * KEY_MAP_N + code);
	return (long)(((mods & 0x22) ? KEY_MAP_N : 0) + code); /* Maj gauche / droite */
}

/* code HID, modificateurs -> caractère Unicode (0 si aucun ; une touche morte :
 * son caractère) */
static u32 key_to_char(u64 code, u64 mods)
{
	long i = key_slot(code, mods);
	return i < 0 ? 0 : kb_live[i];
}

/* (ini_read de options.txt) « keyb = NOM » */
static void option_line(char *key, char *val)
{
	if (str_eq(key, "keyb"))
		kb_load(val);
}

/* ---- souris : boutons par pilote, vitesse */

static u8 pil_btn[PILOTES_MAX]; /* boutons tenus par chaque pilote */
static u64 pil_t[PILOTES_MAX];  /* (ms) son dernier événement de souris */
#define BTN_STALE_MS 3000      /* les boutons d'une autre souris muette depuis 3 s ne comptent plus (13/11 :
				  une souris PS/2 du ZBook gardait un bouton « tenu » et bloquait les clics de l'USB) */
static long sp_mouse = 100, sp_touchpad = 200, sp_accel = 0;
static long rest_x[2], rest_y[2]; /* restes (1/100 de point) : souris, touchpad */

static void pointer_line(char *key, char *val)
{
	long v = (long)parse_dec(val);
	if (v < 10)
		v = 10;
	if (v > 1000)
		v = 1000;
	if (str_eq(key, "mouse"))
		sp_mouse = v;
	else if (str_eq(key, "touchpad"))
		sp_touchpad = v;
	else if (str_eq(key, "accel"))
		sp_accel = parse_dec(val) != 0;
}

static void pointer_load(void)
{
	sp_mouse = 100;
	sp_touchpad = 200;
	sp_accel = 0;
	ini_read("SD0:/etc/pointer.ini", pointer_line);
	rest_x[0] = rest_x[1] = rest_y[0] = rest_y[1] = 0;
}

static long iabs(long v)
{
	return v < 0 ? -v : v;
}

/* un mouvement relatif à la vitesse réglée (k : 0 souris, 1 touchpad) */
static void scale(long *dx, long *dy, int k)
{
	long pct = k ? sp_touchpad : sp_mouse;
	if (!k && sp_accel) { /* petite accélération : jusqu'à x2 au-delà de 16 points par événement */
		long v = iabs(*dx) + iabs(*dy);
		if (v > 4)
			pct = pct * (100 + (v >= 16 ? 100 : (v - 4) * 100 / 12)) / 100;
	}
	if (pct == 100)
		return;
	long x = *dx * pct + rest_x[k], y = *dy * pct + rest_y[k];
	*dx = x / 100;
	*dy = y / 100;
	rest_x[k] = x - *dx * 100;
	rest_y[k] = y - *dy * 100;
}

/* l'événement d'une souris du pilote i : vitesse, boutons de toutes les souris */
static void souris(u64 *ev, u64 i)
{
	u32 type = *(u32 *)ev;
	if (type != EV_MOUSE_REL && type != EV_MOUSE_ABS)
		return;
	int tp = (ev[3] & EV_REL_TOUCHPAD) != 0;
	if (type == EV_MOUSE_REL) {
		long dx = (long)ev[1], dy = (long)ev[2];
		scale(&dx, &dy, tp);
		ev[1] = (u64)dx;
		ev[2] = (u64)dy;
	}
	pil_btn[i] = (u8)(ev[3] & 0xFF);
	u64 now = (u64)sys_time_ms();
	pil_t[i] = now;
	u64 b = 0;
	for (u64 j = 0; j < PILOTES_MAX; j++)
		if (pilotes[j].pid && (j == i || now - pil_t[j] < BTN_STALE_MS))
			b |= pil_btn[j];
	ev[3] = b;
}

/* ---- abonnés et pilotes */

/* pid : ses anneaux sont rendus (abonné et pilote) */
static void desabonner(u64 pid)
{
	for (int i = 0; i < ABONNES_MAX + PILOTES_MAX; i++) {
		struct ring_slot *s = i < ABONNES_MAX ? &abonnes[i] : &pilotes[i - ABONNES_MAX];
		if (s->pid == pid) {
			s->pid = 0;
			dmb();
			korin_sys1(SYS_BUF_FREE, s->buf);
			if (i >= ABONNES_MAX)
				pil_btn[i - ABONNES_MAX] = 0; /* (ses boutons ne restent pas tenus) */
		}
	}
}

/* pid, table (abonnes ou pilotes) -> numéro du tampon partagé (anneau), ou erreur */
static long abonner(u64 pid, struct ring_slot *t)
{
	desabonner(pid); /* (un seul anneau par process) */
	int max = t == pilotes ? PILOTES_MAX : ABONNES_MAX;
	for (int i = 0; i < max; i++, t++) {
		if (t->pid)
			continue;
		long b = korin_sys1(SYS_BUF_CREATE, 4096);
		if (b < 0)
			return b;
		long r = korin_sys2(SYS_BUF_GRANT, b, pid);
		if (r < 0)
			return r;
		long a = korin_sys1(SYS_BUF_MAP, b);
		if (a < 0)
			return a;
		volatile u64 *ring = (volatile u64 *)a;
		ring[0] = 0; /* tête, queue */
		ring[1] = 0;
		t->adr = ring;
		t->buf = (u64)b;
		dmb();
		t->pid = pid; /* (le lecteur le voit après l'adresse) */
		return b;
	}
	return E_NOMEM;
}

/* événement (32 o) : dans l'anneau de chaque abonné (perdu s'il est plein) */
static void diffuser(const u64 *e)
{
	for (int i = 0; i < ABONNES_MAX; i++) {
		struct ring_slot *s = &abonnes[i];
		if (!s->pid)
			continue;
		dmb();
		volatile u64 *r = s->adr;
		u64 head = r[0], tail = r[1];
		if (head - tail >= ANNEAU_N)
			continue;
		volatile u64 *d = r + (ANNEAU_EV + (head & (ANNEAU_N - 1)) * 32) / 8;
		d[0] = e[0];
		d[1] = e[1];
		d[2] = e[2];
		d[3] = e[3];
		dmb(); /* l'événement avant la tête */
		r[0] = head + 1;
	}
}

/* ---- manettes */

/* manette : EV_PAD dans l'anneau des abonnés */
static void pad_emit(u32 n)
{
	u32 *e = (u32 *)pad_ev;
	e[0] = EV_PAD;
	e[1] = n;
	e[2] = pad_btn[n];
	e[3] = pad_rawb[n];
	pad_ev[2] = pad_ax[n];
	pad_ev[3] = pad_id[n];
	diffuser(pad_ev);
}

/* événement EV_PAD_RAW, rang du pilote : le mapping du modèle, puis EV_PAD */
static void pad_raw(const u64 *ev, u64 rank)
{
	const u8 *e = (const u8 *)ev;
	u32 n = (u32)(rank & 1) + 1; /* manette 1 ou 2 */
	u32 model = *(const u32 *)(e + 24); /* vid, pid -> jeu de boutons */
	const u8 *set = pad_sets; /* (le premier : [default] ; le dernier qui correspond l'emporte) */
	for (int i = 0; i < PADSET_MAX; i++) {
		u32 id = *(const u32 *)(pad_sets + i * PADSET_SIZE);
		if (id && id == model)
			set = pad_sets + i * PADSET_SIZE;
	}
	u32 raw = *(const u32 *)(e + 8), btn = 0;
	for (int i = 0; i < PAD_NBUTTONS; i++) {
		u32 b = set[4 + i];
		if (b && ((raw >> ((b - 1) & 31)) & 1)) /* (décalage modulo 32, comme lsr) */
			btn |= 1u << i;
	}
	btn |= raw >> 28; /* chapeau */
	int16_t lx = *(const int16_t *)(e + 16), ly = *(const int16_t *)(e + 18); /* le stick gauche fait
										    aussi la croix */
	if (lx < -500)
		btn |= PAD_LEFT;
	if (lx > 500)
		btn |= PAD_RIGHT;
	if (ly < -500)
		btn |= PAD_UP;
	if (ly > 500)
		btn |= PAD_DOWN;
	u64 axes = ev[2];
	pad_id[n] = model;
	u32 old_raw = pad_rawb[n];
	pad_rawb[n] = raw;
	if (old_raw == raw && pad_btn[n] == btn && pad_ax[n] == axes)
		return;
	pad_btn[n] = btn;
	pad_ax[n] = axes;
	pad_emit(n);
}

/* événement : une touche passe par le mapping ; si elle donne un bouton de
 * manette, EV_PAD (manette 0). Elle reste une touche (feuille de route). */
static void pad_touche(const u64 *ev)
{
	if (*(const u32 *)ev != EV_KEY)
		return;
	u64 code = ev[1], down = ev[2];
	u32 c = 0; /* caractère, sans modificateur */
	if (code < 0xE0)
		c = key_to_char(code, 0) | 0x20; /* (aucun caractère : 0x20, comme l'asm) */
	u32 b = 0;
	for (u64 i = 0; i < pad_keys_n; i++) {
		const u8 *k = (const u8 *)&pad_keys[i];
		u32 bit = (u32)(pad_keys[i] >> 32);
		if (k[0] ? k[0] == (u32)code : k[1] == c)
			b |= bit;
	}
	if (!b)
		return;
	u32 old = pad_btn[0], nb = down ? old | b : old & ~b;
	if (nb == old)
		return;
	pad_btn[0] = nb;
	pad_emit(0);
}

/* ---- le mapping : SD0:/etc/gamepad.ini (clés d'Onyx, src/inc/pad.inc)
 *	[keyboard]	a = z		bouton = touche : une lettre, un chiffre,
 *					up down left right enter backspace space
 *					tab esc prtsc pause, none
 *	[default], [vvvv:pppp]	a = 3	bouton = numéro du bouton brut (1 : le
 *					premier), un modèle par section */

static const char pad_w_btn[][8] = {"up", "down", "left", "right", "a", "b", "x", "y", "l",
				    "r", "l2", "r2", "select", "start", "l3", "r3", "home", ""};
static const char pad_w_sect[][12] = {"keyboard", "default", ""};
static const char pad_w_key[][12] = {"up", "down", "left", "right", "enter", "backspace",
				     "space", "tab", "esc", "prtsc", "pause", "none", ""};
static const u8 pad_c_key[] = {0x52, 0x51, 0x50, 0x4F, 0x28, 0x2A, 0x2C, 0x2B, 0x29, 0x46, 0x48, 0};

#define PAD_K(hid, car, bouton) ((u64)(bouton) << 32 | (u64)(car) << 8 | (u64)(hid))
static const u64 pad_def_keys[] = { /* flèches, A B X Y L R telles quelles, Impr. écran et
				       Retour Select, Pause et Entrée Start */
	PAD_K(0x52, 0, PAD_UP), PAD_K(0x51, 0, PAD_DOWN), PAD_K(0x50, 0, PAD_LEFT),
	PAD_K(0x4F, 0, PAD_RIGHT), PAD_K(0x46, 0, PAD_SELECT), PAD_K(0x2A, 0, PAD_SELECT),
	PAD_K(0x48, 0, PAD_START), PAD_K(0x28, 0, PAD_START), PAD_K(0, 'a', PAD_A),
	PAD_K(0, 'b', PAD_B), PAD_K(0, 'x', PAD_X), PAD_K(0, 'y', PAD_Y), PAD_K(0, 'l', PAD_L),
	PAD_K(0, 'r', PAD_R), 0};
/* manette HID ordinaire (l'ordre générique d'Onyx) */
static const u8 pad_def_btn[PAD_NBUTTONS] = {0, 0, 0, 0, 3, 2, 4, 1, 5, 6, 7, 8, 9, 10, 11, 12, 13};

/* [s, e[ : texte, liste de mots de wl octets (un mot vide à la fin) -> rang, -1 */
static long pad_mot(const char *s, const char *e, const char *list, long wl)
{
	u64 n = (u64)(e - s); /* (non signé, comme l'asm) */
	for (long r = 0; list[0]; r++, list += wl) {
		u64 i = 0;
		while (i < n && s[i] == list[i])
			i++;
		if (i >= n && !list[i])
			return r;
	}
	return -1;
}

static void pad_trim(const char **s, const char **e)
{
	while (*s < *e && (**s == ' ' || **s == 9))
		(*s)++;
	while (*s < *e && ((*e)[-1] == ' ' || (*e)[-1] == 9 || (*e)[-1] == 13))
		(*e)--;
}

/* « vvvv:pppp » -> vid << 16 | pid, 0 : autre chose */
static u32 pad_hex2(const char *s, const char *e)
{
	if (e - s != 9)
		return 0;
	u32 v = 0;
	for (int i = 0; i < 9; i++) {
		if (i == 4)
			continue; /* (le « : » n'est pas vérifié, comme l'asm) */
		u32 d = (u32)(u8)s[i] - '0';
		if (d > 9) {
			d = (u32)((u8)s[i] | 0x20) - 'a';
			if (d > 5)
				return 0;
			d += 10;
		}
		v = d | v << 4;
	}
	return v;
}

/* touche [s, e[, numéro du bouton : remplace sa touche */
static void pad_set_key(const char *s, const char *e, long btn)
{
	u32 bit = 1u << (btn & 31);
	u64 n = 0; /* ses entrées sortent de la table */
	for (u64 i = 0; i < pad_keys_n; i++)
		if ((u32)(pad_keys[i] >> 32) != bit)
			pad_keys[n++] = pad_keys[i];
	pad_keys_n = n;
	u64 k;
	if (e - s == 1)
		k = (u64)(((u8)s[0] | 0x20) << 8); /* une lettre, un chiffre */
	else {
		long w = pad_mot(s, e, pad_w_key[0], 12);
		if (w < 0 || !pad_c_key[w])
			return; /* none */
		k = pad_c_key[w];
	}
	if (pad_keys_n >= PAD_KEYS_MAX)
		return;
	pad_keys[pad_keys_n++] = (u64)bit << 32 | k;
}

/* relit le mapping : les valeurs par défaut, puis le fichier */
static void pad_load(void)
{
	u64 n = 0; /* les touches par défaut */
	while (pad_def_keys[n]) {
		pad_keys[n] = pad_def_keys[n];
		n++;
	}
	pad_keys_n = n;
	memset(pad_sets, 0, sizeof pad_sets); /* les boutons : [default] */
	for (int i = 0; i < PAD_NBUTTONS; i++)
		pad_sets[4 + i] = pad_def_btn[i];
	long fd = f_open("SD0:/etc/gamepad.ini", O_READ);
	if (fd < 0)
		return;
	long len = f_read(fd, kb_fichier, KB_FICHIER_MAX - 1);
	f_close(fd);
	if (len <= 0)
		return;
	char *p = kb_fichier, *end = kb_fichier + len;
	*end = 0;
	int kbd = 0;        /* 1 : [keyboard] */
	u8 *set = 0;        /* un jeu de boutons */
	u8 *last = pad_sets; /* le dernier pris */
	while (p < end) {
		char *le = p; /* fin de ligne */
		while (le < end && *le != 10)
			le++;
		const char *s = p, *e = le;
		p = le + 1;
		pad_trim(&s, &e);
		if (s >= e || *s == '#' || *s == ';')
			continue;
		if (*s == '[') {
			s++;
			e--; /* (sans le ]) */
			kbd = 0;
			set = 0;
			long w = pad_mot(s, e, pad_w_sect[0], 12);
			if (w == 0)
				kbd = 1;
			else if (w == 1)
				set = pad_sets;
			else {
				u32 id = pad_hex2(s, e); /* vvvv:pppp */
				if (!id)
					continue;
				last += PADSET_SIZE;
				if (last >= pad_sets + sizeof pad_sets)
					return;
				*(u32 *)last = id;
				set = last;
			}
			continue;
		}
		const char *q = s; /* clé = valeur */
		while (q < e && *q != '=')
			q++;
		if (q >= e)
			continue;
		const char *ks = s, *kend = q;
		pad_trim(&ks, &kend);
		long btn = pad_mot(ks, kend, pad_w_btn[0], 8);
		if (btn < 0)
			continue;
		const char *vs = q + 1, *ve = e;
		pad_trim(&vs, &ve);
		if (kbd) {
			pad_set_key(vs, ve, btn);
			continue;
		}
		if (!set)
			continue;
		*(char *)ve = 0;
		set[4 + btn] = (u8)parse_dec(vs);
	}
}

/* ---- le lecteur des entrées (thread) */

/* événement de touche, caractère -> une copie avec ce caractère, aux abonnés
 * (la touche morte non composée, avant la touche qui la suit) */
static void diffuser_car(const u64 *ev, u32 c)
{
	u64 e[4] = {ev[0], ev[1], ev[2], ev[3]};
	((u32 *)e)[1] = c;
	diffuser(e);
}

/* événement brut : une touche reçoit son caractère en +4. Touches mortes
 * (docs/CLAVIER.md) : l'appui d'une morte ne donne rien et la garde en attente ;
 * l'appui suivant qui donne un caractère : composé (compose.tab), ou la morte
 * seule (espace), ou la morte puis lui (sans composition : un événement de plus),
 * ou lui seul (caractère de commande : Entrée, Échap, Retour... annulent la
 * morte) ; les modificateurs (Maj, AltGr) laissent la morte en attente, les
 * autres touches sans caractère (Ctrl+touche, flèches) l'annulent. Le
 * relâchement garde le caractère de la touche (0 pour une morte). */
static void traduire(u64 *ev)
{
	if (*(u32 *)ev != EV_KEY)
		return;
	if (ev[3] & 0x80000000u) { /* bit 31 des modificateurs : déjà traduit (pilotes
				      virtuels : vncd, rdpd), gardé tel quel */
		ev[3] &= 0x7FFFFFFF;
		return;
	}
	long i = key_slot(ev[1], ev[3]);
	u32 c = i < 0 ? 0 : kb_live[i];
	int dead = i >= 0 && kb_dead_live[i];
	u32 *car = (u32 *)ev + 1;
	if (!ev[2]) { /* relâchement */
		*car = dead ? 0 : c;
		return;
	}
	u32 p = dead_pending;
	if (dead) {
		*car = 0;
		if (!p) {
			dead_pending = c; /* en attente de la touche suivante */
			return;
		}
		dead_pending = 0; /* deux mortes : composées, sinon l'une puis l'autre */
		u32 r = compose(p, c);
		if (r) {
			*car = r;
			return;
		}
		diffuser_car(ev, p);
		*car = c;
		return;
	}
	*car = c;
	if (!p)
		return;
	if (!c) { /* sans caractère : Maj, AltGr... (codes 0xE0..) laissent la morte en
		     attente ; Ctrl+touche, flèches, F1... l'abandonnent */
		if (ev[1] < 0xE0)
			dead_pending = 0;
		return;
	}
	dead_pending = 0;
	if (c == ' ') {
		*car = p;
		return;
	}
	if (c < 0x20 || c == 0x7F) /* commande : la morte est abandonnée */
		return;
	u32 r = compose(p, c);
	if (r) {
		*car = r;
		return;
	}
	diffuser_car(ev, p);
}

/* appui d'une touche -> répétée tant qu'elle est tenue ; relâchée -> plus de
 * répétition (codes HID 0xE0.. : modificateurs, jamais) */
static void tenir(const u64 *ev)
{
	if (*(const u32 *)ev != EV_KEY || ev[1] >= 0xE0)
		return;
	if (ev[2]) {
		rep_ev[0] = ev[0]; /* copie de l'appui */
		rep_ev[1] = ev[1];
		rep_ev[2] = ev[2];
		rep_ev[3] = ev[3];
		rep_time = (u64)sys_time_ms() + REP_DELAI;
		rep_on = 1;
	} else if (rep_ev[1] == ev[1])
		rep_on = 0;
}

static void repeter(void)
{
	if (!rep_on)
		return;
	u64 now = (u64)sys_time_ms();
	if (now < rep_time)
		return;
	rep_time = now + REP_PAS;
	diffuser(rep_ev);
}

/* les anneaux des pilotes en services : chaque événement comme une entrée brute
 * du noyau -> pilotes_n = nombre de pilotes */
static void lire_pilotes(void)
{
	u64 seen = 0;
	for (u64 i = 0; i < PILOTES_MAX; i++) {
		struct ring_slot *s = &pilotes[i];
		if (!s->pid)
			continue;
		seen++;
		dmb();
		volatile u64 *r = s->adr;
		for (;;) {
			u64 head = r[0], tail = r[1]; /* tête (pilote), queue (ici) */
			if (head == tail)
				break;
			dmb(); /* l'événement après la tête */
			volatile u64 *src = r + (ANNEAU_EV + (tail & (ANNEAU_N - 1)) * 32) / 8;
			ev_pilote[0] = src[0]; /* copie (le pilote peut réécrire) */
			ev_pilote[1] = src[1];
			ev_pilote[2] = src[2];
			ev_pilote[3] = src[3];
			dmb();
			r[1] = r[1] + 1;
			if (*(u32 *)ev_pilote == EV_PAD_RAW) {
				pad_raw(ev_pilote, i); /* (la manette : le rang du pilote) */
				continue;
			}
			souris(ev_pilote, i);
			traduire(ev_pilote);
			pad_touche(ev_pilote);
			tenir(ev_pilote);
			diffuser(ev_pilote);
		}
	}
	pilotes_n = seen;
}

#if defined(__x86_64__)
__attribute__((force_align_arg_pointer))
#endif
static NORETURN void lecteur(void)
{
	for (;;) {
		lire_pilotes();
		/* attente : courte si une touche est tenue, ou s'il y a des pilotes en services
		   (les pilotes écrivent dans leurs anneaux) */
		sys_sleep((rep_on | pilotes_n) ? 10 : 50);
		repeter();
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	kb_use_builtin(1); /* disposition : FR intégré, ou keyb= */
	ini_read("SD0:/etc/options.txt", option_line);
	canal = korin_sys2(SYS_SVC_REGISTER, "hid", SVF_ENDS); /* (abonnés qui s'arrêtent) */
	if (canal < 0)
		return 1; /* (sur une erreur : init2 relance) */
	pad_load(); /* le mapping (pad.inc) */
	pointer_load();
	u8 *stack = ak_malloc(16384); /* le lecteur des entrées : un thread */
	if (!stack)
		return 1;
	korin_sys3(SYS_THREAD_CREATE, lecteur, stack + 16384, 0);
	say("hidmanager : en service (disposition, repetition, anneaux)\n");
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, 60000);
		if (req < 0)
			return 1;
		if (!req)
			continue;
		long r;
		switch (M64(msg, 0)) {
		case SVC_OP_END: /* (avis) : un abonné de moins */
			desabonner(M64(msg, 8));
			continue;
		case HID_OP_ABONNER:
			r = abonner(M32(msg, 44), abonnes); /* pid de l'appelant */
			break;
		case HID_OP_PADMAP:
			pad_load();
			r = 0;
			break;
		case HID_OP_POINTER:
			pointer_load();
			r = 0;
			break;
		case HID_OP_PILOTE:
			r = abonner(M32(msg, 44), pilotes);
			break;
		case HID_OP_KEYMAP:
			msg[48 + 15] = 0; /* nom : 15 caractères au plus */
			r = kb_load((char *)msg + 48) ? 0 : E_NOENT;
			break;
		default:
			r = E_INVAL;
		}
		M64(msg, 0) = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
