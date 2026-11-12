/* rdpd - le bureau à distance d'Onyx (docs/RESEAU.md 5.4, étape 12), port 3390,
 * lancé par init2 (SD0:/etc/services.txt) ; traduction de rdpd.asm (mêmes
 * messages, même protocole, mêmes octets LZ4, mêmes bizarreries : « comme
 * l'asm »). Client : Korin Remote (tools/remote, .NET ; copie d'Onyx Remote,
 * pc/OnyxRemote d'Onyx) : chaque fenêtre de Korin devient une fenêtre du PC, son
 * contenu et (au choix du client) son cadre, au lieu d'une image de l'écran.
 * Protocole d'Onyx (user/BinUtils/rdpd.c), petit-boutiste, plus un mot de passe
 * (celui de l'accès à distance, SD0:/etc/remote.txt, remotepw.c, relu à chaque
 * connexion), un client à la fois (un nouveau client prend la place de
 * l'ancien : le PC qui se reconnecte après une coupure) :
 *  * salut : « ONYXRDP2 », largeur, hauteur, 96 (u16 ; le client y lit la
 *    version de l'ABI d'Onyx et veut au moins 56), puis un défi de 16 octets
 *    (SYS_GETRANDOM) ; le client répond « ONYXRDP2 » + options (1 : 16 bits,
 *    2 : sans cadres, 4 : rondes en pipeline -> CAPS, 8 : COPY, ignoré ici)
 *    + HMAC-SHA256 (clé : le mot de passe en UTF-8, message : le défi ; 32
 *    octets, sha256.c) ; le serveur répond un octet : 0 accepté, 1 mot de
 *    passe faux, 2 aucun mot de passe (tout est refusé) ; sauf 0, il ferme ;
 *  * serveur -> client : type (u8), longueur (u32), charge : 1 WIN, 2 GONE,
 *    3 ZORDER, 4 PIXELS (LZ4 si plus court, sinon brut ; 32 bits, ou 16 bits
 *    RGB565 si demandé, sauf les cadres et les fenêtres WF_ALPHA : 32 bits,
 *    octet haut = transparence), 5 END, 8 SCREEN, 9 CAPS, 10 PING, 11 CURSOR ;
 *  * client -> serveur : 1 READY, 2 PTR, 3 KEY, 4 RAISE, 5 CLOSE, 6 CHAR,
 *    7 DESKTOP, 8 PONG, 9 MOVE.
 * Une ronde par crédit (un READY en donne un, 3 en vol au plus en pipeline),
 * seulement si quelque chose a changé : WS_OP_WIN_LIST d'Elegant ; pour chaque
 * fenêtre dont la génération du contenu a bougé, WS_OP_WIN_READ par bandes,
 * comparaison par carrés de 64 x 64 avec la copie qu'a le client, envoi des
 * carrés changés (côte à côte : un rectangle) ; le cadre quand sa génération
 * bouge. Les fenêtres du fond (WF_BACK : le fond d'écran) ne sont pas dites :
 * elles sont dans la pseudo-fenêtre du bureau (0xFFFFFFFF, si le client la
 * demande : message DESKTOP). Entrées : pilote virtuel de HIDManager (comme
 * vncd) ; le pointeur arrive en coordonnées de la fenêtre et repart à sa place
 * sur l'écran (la fenêtre passe devant au clic).
 *
 * Bizarreries visibles de l'extérieur, comme l'asm :
 *  * un rectangle de plus de PACK_MAX octets (cadre de plus de 2304 pixels de
 *    large) n'est pas envoyé ;
 *  * MOVE ou RAISE d'un id inconnu peut tomber sur une place libre (id 0) ;
 *  * le titre : 32 octets UTF-8 lus, un premier octet de 2 octets en dernière
 *    position lit l'octet qui suit le titre (le début de la place suivante). */
#include "korinrt.h"
#include "netkit_import.h"
#include "../inc/remotepw.h"
#include "sha256.h"
#include "lz4.h"

#define PORT 3390
#define HID_OP_PILOTE 3
#define RING_EV 64
#define RING_N 64
#define EV_KEY 1
#define EV_MOUSE_ABS 3
#define EV_WHEEL 4
#define IN_MAX 4096
#define OUT_MAX 65536 /* messages : envoyés par paquets de 64 Ko */
#define TILE 64
#define PACK_MAX (2304 * TILE * 4) /* une bande de 64 lignes (cadre de 2048 + 8) */
#define LZ_MAX (PACK_MAX + PACK_MAX / 255 + 64)
#define MAXWIN 34 /* 32 fenêtres d'Elegant + le bureau */
#define PIPE_ROUNDS 3
#define MIN_ROUND_MS 20   /* 50 rondes par seconde au plus */
#define CURSOR_POLL_MS 50 /* la forme du curseur */
#define IDLE_PING_MS 2000 /* client en pipeline silencieux : un PING */
#define SILENT_MS 12000   /* ... sans rien entendre : la session finit */
#define POLL_MS 500       /* un nouveau client ? */
#define DPI 96
#define DESK 0xFFFFFFFFUL /* pseudo-fenêtre du bureau */
#define AUTH_LEN (8 + 1 + 32) /* la réponse au salut : « ONYXRDP2 », options, HMAC */
#define AUTH_NONE 2           /* verdicts : 0 accepté, 1 faux, 2 aucun mot de passe */

/* src/inc/armpi.inc */
#define WS_OP_RAISE 8
#define WS_OP_WIN_LIST 14
#define WS_OP_WIN_READ 15
#define WS_OP_WIN_MOVE 16
#define WS_OP_WIN_CLOSE 17
#define WS_OP_CURSOR_GET 28
#define WF_FRAMELESS 1
#define WF_BACK 2
#define WF_TOP 4
#define WF_NOFOCUS 8
#define WF_ALPHA 256
#define WE_ID 0
#define WE_X 8
#define WE_Y 10
#define WE_W 12
#define WE_H 14
#define WE_FLAGS 16
#define WE_STATE 20
#define WE_GEN 24
#define WE_CGEN 28
#define WE_TITLE 32
#define WE_SIZE 64
#define WE_PER_MSG 7
#define WES_KEYS 1
#define WES_MINI 2
#define FRAME_TITLE_H 12
#define FRAME_BORDER 12

#define MW(off) (*(u64 *)(msg + (off)))
#define ML (*(u32 *)(msg + WM_LEN))
#define E32(e, off) (*(u32 *)((e) + (off)))

/* une fenêtre telle que le client l'a (160 octets, comme SL_* de l'asm) */
struct slot {
	u64 id; /* 0 : place libre */
	long x, y;
	u64 w, h;
	u64 flags; /* WF_* */
	u64 state; /* WES_* */
	u64 gen, cgen;
	u64 sent;  /* son WIN est parti */
	u64 stale; /* pixels à envoyer en entier */
	u64 alive;
	u8 *buf;   /* copie du contenu qu'a le client */
	u64 cap;   /* (octets ; gardé quand la place change de fenêtre) */
	u64 bw, bh;
	u8 title[32];
};
_Static_assert(sizeof(struct slot) == 160, "SL_SIZE"); /* le titre est suivi de la place suivante */

static const char s_magic[] = "ONYXRDP2"; /* (8 octets) */
static const char m_ready[] = "rdpd : en écoute sur le port 3390 (Korin Remote, mot de passe)\n";
static const char m_refused[] = "rdpd : mot de passe faux, client refusé\n";
static const char m_none[] = "rdpd : aucun mot de passe (SD0:/etc/remote.txt), client refusé\n";
static const char m_client[] = "rdpd : client\n";
static const char m_session[] = "rdpd : session ouverte\n";
static const char m_gone[] = "rdpd : client parti\n";
static const char m_closed[] = "rdpd : le client a fermé la connexion\n";
static const char m_silent[] = "rdpd : client silencieux, session finie\n";
static const char m_next[] = "rdpd : un nouveau client prend la place\n";
static const char m_bad[] = "rdpd : message inconnu, session finie\n";
static const char m_send[] = "rdpd : envoi impossible, session finie\n";
static const char m_nomem[] = "rdpd : mémoire insuffisante\n";
/* longueur des messages du client (type compris), par type */
static const u8 msglen[10] = {0, 1, 11, 6, 5, 5, 5, 2, 5, 9};
/* UC_* -> KAPI_CURSOR_* : flèche, texte, main, H, V, attente, croix, glisser, déplacer, coins, interdit */
static const u8 cur_kapi[12] = {0, 2, 1, 4, 5, 10, 9, 3, 3, 6, 7, 11};

/* touches spéciales : keysym, code HID, bit de modificateur (octet HID) */
static const u32 keysyms[] = {
	0xFF08, 0x2A, 0,    /* BackSpace */
	0xFF09, 0x2B, 0,    /* Tab */
	0xFF0D, 0x28, 0,    /* Return */
	0xFF8D, 0x58, 0,    /* KP_Enter */
	0xFF1B, 0x29, 0,    /* Escape */
	0xFF50, 0x4A, 0,    /* Home */
	0xFF51, 0x50, 0,    /* Left */
	0xFF52, 0x52, 0,    /* Up */
	0xFF53, 0x4F, 0,    /* Right */
	0xFF54, 0x51, 0,    /* Down */
	0xFF55, 0x4B, 0,    /* Page_Up */
	0xFF56, 0x4E, 0,    /* Page_Down */
	0xFF57, 0x4D, 0,    /* End */
	0xFF63, 0x49, 0,    /* Insert */
	0xFFFF, 0x4C, 0,    /* Delete */
	0xFFBE, 0x3A, 0,    /* F1..F12 */
	0xFFBF, 0x3B, 0,
	0xFFC0, 0x3C, 0,
	0xFFC1, 0x3D, 0,
	0xFFC2, 0x3E, 0,
	0xFFC3, 0x3F, 0,
	0xFFC4, 0x40, 0,
	0xFFC5, 0x41, 0,
	0xFFC6, 0x42, 0,
	0xFFC7, 0x43, 0,
	0xFFC8, 0x44, 0,
	0xFFC9, 0x45, 0,
	0xFFE3, 0xE0, 0x01, /* Control_L */
	0xFFE1, 0xE1, 0x02, /* Shift_L */
	0xFFE9, 0xE2, 0x04, /* Alt_L */
	0xFFE7, 0xE3, 0x08, /* Meta_L */
	0xFFEB, 0xE3, 0x08, /* Super_L */
	0xFFE4, 0xE4, 0x10, /* Control_R */
	0xFFE2, 0xE5, 0x20, /* Shift_R */
	0xFFEA, 0xE6, 0x40, /* Alt_R */
	0xFE03, 0xE6, 0x40, /* ISO_Level3_Shift (AltGr) */
	0,
};

static volatile u64 *ring; /* anneau de HIDManager */
static u8 *outbuf;
static u64 outlen;
static u8 *packbuf;
static u8 *lzbuf;
static u32 *htab;
static u8 *rbuf; /* tampon d'Elegant (WIN_READ) */
static u64 rbuf_size;
static u64 rbuf_num;
static long lsock;
static long sock;
static long next_sock = -1;
static u64 dead;
static u64 scr[3]; /* scr_w, scr_h, barre (SCREEN_INFO) */
#define scr_w scr[0]
#define scr_h scr[1]
static u64 opt_16;
static u64 opt_noframes;
static u64 opt_pipe;
static u64 opt_desktop;
static long credit;
static long rounds_max = 1;
static u64 roundmsgs;
static u64 last_round;
static u64 round_wait;
static u64 last_rx;
static u64 cur_at;
static u64 cur_sent;
static u64 last_ping;
static u64 last_poll;
static u64 btn;
static u64 mods;
static u64 ptr_pos[2]; /* dernière place (0..32767) */
static u64 in_n;
static u64 wl_n;
static long order_n = -1;
static u64 norder_n;
static u64 order[MAXWIN];
static u64 norder[MAXWIN];
static u64 tmp[8];
static u8 ttl[64];
static u8 msg[WS_MSG] __attribute__((aligned(16)));
static u8 inbuf[IN_MAX] __attribute__((aligned(8)));
static u8 wl[MAXWIN * WE_SIZE] __attribute__((aligned(8)));
/* les places, puis le défi et son HMAC, à la suite comme dans l'asm (le titre
 * de la dernière place peut lire un octet du défi : title_latin1) */
static struct {
	struct slot slots[MAXWIN];
	u8 challenge[16]; /* le défi du mot de passe (une session) */
	u8 expected[32];  /* son HMAC */
} sd __attribute__((aligned(8)));
#define slots sd.slots

/* ---- petits outils */

/* comme udiv d'AArch64 : division par zéro -> 0 */
static u64 udiv(u64 a, u64 b)
{
	return b ? a / b : 0;
}

static u64 now_ms(void)
{
	return (u64)korin_sys0(SYS_TIME_MS);
}

/* requête à Elegant (la réponse dans msg) */
static long wscall(u64 op, u64 a, u64 b, u64 c, u64 d)
{
	MW(WM_OP) = op;
	MW(WM_A) = a;
	MW(WM_B) = b;
	MW(WM_C) = c;
	MW(WM_D) = d;
	ML = 0;
	return korin_sys1(SYS_WS_CALL, msg);
}

/* 32 bits petit-boutistes */
static u64 get32(const u8 *p)
{
	return (u64)p[0] | (u64)p[1] << 8 | (u64)p[2] << 16 | (u64)p[3] << 24;
}

/* 16 bits signés */
static long get16s(const u8 *p)
{
	return (int16_t)(p[0] | p[1] << 8);
}

/* ---- sortie : les messages s'accumulent dans outbuf, envoyés par flush_out */

/* outbuf (en entier : net_send attend la place) ; échec -> dead */
static void flush_out(void)
{
	const u8 *p = outbuf;
	u64 n = outlen;
	while (n && !dead) {
		long r = net_send(sock, (long)p, (long)n);
		if (r <= 0) {
			dead = 1;
			ak_puts(m_send);
			break;
		}
		p += r;
		n -= (u64)r;
	}
	outlen = 0;
}

static void put(const void *data, u64 n)
{
	const u8 *p = data;
	while (n) {
		u64 room = OUT_MAX - outlen; /* place */
		if (!room) {
			flush_out();
			continue;
		}
		if (room > n)
			room = n;
		memcpy(outbuf + outlen, p, room);
		outlen += room;
		p += room;
		n -= room;
	}
}

/* valeur : n octets petit-boutistes */
static void putn(u64 v, u64 n)
{
	u8 b[8];
	for (int i = 0; i < 8; i++)
		b[i] = (u8)(v >> (8 * i));
	put(b, n);
}

#define put32(v) putn((u64)(v), 4)
#define put16(v) putn((u64)(v), 2)
#define put8(v) putn((u64)(v), 1)

/* type, longueur de la charge : en-tête d'un message (ceux de la ronde
 * comptés, sauf END, PING, CURSOR) */
static void msg_head(u64 type, u64 len)
{
	if (type != 5 && type != 10 && type != 11)
		roundmsgs++;
	put8(type);
	put32(len);
}

/* ---- entrée */

/* attente (ms) : ce qui est arrivé -> inbuf ; 0 si la connexion est fermée (ou
 * en erreur), 1 sinon */
static int fill_in(long wait)
{
	if (in_n < IN_MAX) { /* (plein : le client parle) */
		long r = net_recv(sock, (long)(inbuf + in_n), (long)(IN_MAX - in_n), wait);
		if (r == E_AGAIN) /* rien */
			return 1;
		if (r <= 0)
			return 0;
		in_n += (u64)r;
	}
	last_rx = now_ms();
	return 1;
}

/* octets, attente totale (ms) -> 1 si inbuf en a autant */
static int need(u64 n, u64 wait)
{
	u64 end = now_ms() + wait;
	for (;;) {
		if (in_n >= n)
			return 1;
		if (!fill_in(100))
			return 0;
		if (now_ms() >= end)
			return 0;
	}
}

/* octets retirés du début d'inbuf */
static void consume(u64 n)
{
	in_n -= n;
	memmove(inbuf, inbuf + n, in_n);
}

/* ---- entrées vers HIDManager */

/* un événement dans l'anneau de HIDManager (perdu s'il est plein) */
static void ring_push(u64 type, u64 a, u64 b, u64 c)
{
	volatile u64 *r = ring;
	if (!r)
		return;
	u64 head = r[0], tail = r[1]; /* tête (ici), queue (HIDManager) */
	if (head - tail >= RING_N)
		return;
	volatile u64 *e = r + (RING_EV + (head & (RING_N - 1)) * 32) / 8;
	e[0] = type;
	e[1] = a;
	e[2] = b;
	e[3] = c;
	__atomic_store_n(&r[0], head + 1, __ATOMIC_RELEASE); /* l'événement avant la tête */
}

/* id -> sa place (0 : aucune) ; id 0 : une place libre */
static struct slot *find_slot(u64 id)
{
	for (int i = 0; i < MAXWIN; i++)
		if (slots[i].id == id)
			return &slots[i];
	return 0;
}

/* id, x, y (dans le contenu ; le bureau : l'écran), boutons (1 gauche, 2 droit,
 * 4 milieu : comme HIDManager), molette */
static void pointer(u64 id, long x, long y, u64 b, long wheel)
{
	b &= 7;
	if (id != DESK) {
		struct slot *s = find_slot(id);
		if (!s)
			return;
		x += s->x; /* à sa place sur l'écran */
		y += s->y;
		/* un clic : la fenêtre passe devant d'abord (sinon une autre, devant sur
		 * le Pi, recevrait le clic) ; (déjà au clavier : rien) */
		if (b && !btn && !(s->state & WES_KEYS) && !(s->flags & (WF_BACK | WF_TOP | WF_NOFOCUS)))
			wscall(WS_OP_RAISE, id, 0, 0, 0);
	}
	if (wheel)
		ring_push(EV_WHEEL, (u64)wheel, 0, 0);
	long mx = (long)scr_w - 1, my = (long)scr_h - 1; /* 0..32767 */
	if (x < 0)
		x = 0;
	if (x > mx)
		x = mx;
	if (y < 0)
		y = 0;
	if (y > my)
		y = my;
	u64 px = udiv((u64)x * 32767, (u64)mx), py = udiv((u64)y * 32767, (u64)my);
	ptr_pos[0] = px;
	ptr_pos[1] = py;
	ring_push(EV_MOUSE_ABS, px, py, b);
	btn = b;
}

/* appui (1) / relâche (0), keysym X11 d'une touche spéciale (les caractères
 * arrivent par CHAR) */
static void key_sym(u64 down, u32 sym)
{
	const u32 *k = keysyms; /* keysym, code HID, bit de modificateur */
	while (k[0] && k[0] != sym)
		k += 3;
	if (!k[0])
		return;
	if (k[2])
		mods = down ? mods | k[2] : mods & ~(u64)k[2];
	ring_push(EV_KEY, k[1], down, mods); /* caractère : calculé par HIDManager */
}

/* caractère (Latin-1, la disposition du PC appliquée) : appui et relâche
 * (Ctrl + lettre arrive en 1..26 : la lettre, sans caractère, Ctrl tenu) */
static void char_in(u64 c)
{
	if (!c || c > 255)
		return;
	u64 code = 0, ch = c;
	if (c < 32) {
		if (c > 26)
			return;
		code = c + 0x04 - 1; /* Ctrl + lettre */
		ch = 0;
	} else { /* lettres, chiffres, espace */
		u32 l = (u32)((c | 0x20) - 'a');
		if (l <= 25 && c <= 'z')
			code = l + 0x04;
		else if (c - '1' <= 8)
			code = c - '1' + 0x1E;
		else if (c == '0')
			code = 0x27;
		else if (c == ' ')
			code = 0x2C;
	}
	u64 flags = mods | 1UL << 31; /* caractère déjà traduit (bit 31) */
	ring_push(EV_KEY | ch << 32, code, 1, flags); /* appui */
	ring_push(EV_KEY | ch << 32, code, 0, flags); /* relâche */
}

/* fin de session : aucun bouton ni modificateur laissé tenu */
static void release_all(void)
{
	if (btn) {
		btn = 0;
		ring_push(EV_MOUSE_ABS, ptr_pos[0], ptr_pos[1], 0);
	}
	mods = 0;
}

/* id : RAISE du client (sa fenêtre a pris le focus sur le PC) */
static void do_raise(u64 id)
{
	struct slot *s = find_slot(id);
	if (!s || (s->flags & (WF_BACK | WF_TOP)) || id == DESK)
		return;
	wscall(WS_OP_RAISE, id, 0, 0, 0);
}

/* les messages complets d'inbuf ; un message inconnu -> dead */
static void parse(void)
{
	while (in_n) {
		const u8 *p = inbuf;
		u64 type = p[0];
		if (!type || type > 9 || (type >= 8 && !opt_pipe)) { /* PONG, MOVE : clients en pipeline */
			ak_puts(m_bad);
			dead = 1;
			return;
		}
		u64 len = msglen[type];
		if (in_n < len) /* incomplet : la suite plus tard */
			return;
		switch (type) {
		case 1: /* READY */
			credit = credit + 1 > rounds_max ? rounds_max : credit + 1;
			break;
		case 2: /* PTR : id, x, y, boutons, molette */
			pointer(get32(p + 1), get16s(p + 5), get16s(p + 7), p[9], (int8_t)p[10]);
			break;
		case 3: /* KEY : drapeaux 1 appui, 2 tenue seulement (les jeux d'Onyx : rien ici) */
			if (!(p[1] & 2))
				key_sym(p[1] & 1, (u32)get32(p + 2));
			break;
		case 4: /* RAISE */
			do_raise(get32(p + 1));
			break;
		case 5: { /* CLOSE */
			u64 id = get32(p + 1);
			if (id != DESK)
				wscall(WS_OP_WIN_CLOSE, id, 0, 0, 0);
			break;
		}
		case 6: /* CHAR */
			char_in(get32(p + 1));
			break;
		case 7: /* DESKTOP */
			opt_desktop = p[1] != 0;
			break;
		case 9: { /* MOVE */
			long x = get16s(p + 5), y = get16s(p + 7);
			u64 id = get32(p + 1);
			struct slot *s = find_slot(id);
			if (s && !(s->flags & (WF_BACK | WF_TOP)))
				wscall(WS_OP_WIN_MOVE, id, (u64)x, (u64)y, 0);
			break;
		}
		default: /* 8 PONG : rien */
			break;
		}
		consume(len);
	}
}

/* ---- le mot de passe */

/* inbuf = la réponse au salut (« ONYXRDP2 », options, HMAC-SHA256 du défi) ->
 * verdict : 0 accepté, 1 mot de passe faux, AUTH_NONE aucun (SD0:/etc/
 * remote.txt absent ou vide : tout est refusé). Comparaison en temps constant ;
 * le mot de passe ne reste pas en mémoire. */
static u64 auth(void)
{
	long n = rp_read();
	u64 v = AUTH_NONE;
	if (n) {
		hmac_sha256((const u8 *)rp_pw, (u64)n, sd.challenge, 16, sd.expected);
		u8 d = 0;
		for (int i = 0; i < 32; i++)
			d |= sd.expected[i] ^ inbuf[9 + i];
		v = d != 0;
	}
	memset(rp_pw, 0, RP_MAX + 1); /* (le mot de passe, le haché attendu) */
	memset(sd.expected, 0, 32);
	return v;
}

/* now : la forme du curseur a changé -> CURSOR (KAPI_CURSOR_* d'Onyx, OnyxRemote) */
static void cursor_poll(u64 now)
{
	if (now - cur_at < CURSOR_POLL_MS)
		return;
	cur_at = now;
	long r = wscall(WS_OP_CURSOR_GET, 0, 0, 0, 0);
	if (r < 0 || r >= 12)
		return;
	u64 k = cur_kapi[r];
	if (k == cur_sent)
		return;
	cur_sent = k;
	msg_head(11, 1);
	put8(k);
	flush_out();
}

/* ---- une ronde */

/* le tampon d'Elegant (WS_OP_WIN_READ y recopie) : mappé une fois */
static void rbuf_map(void)
{
	if (rbuf)
		return;
	if (wscall(WS_OP_WIN_READ, DESK, 0, 0, 0) < 0)
		return;
	rbuf_size = MW(WM_C);
	rbuf_num = MW(WM_D);
	long b = korin_sys1(SYS_BUF_MAP, rbuf_num);
	if (b >= 0)
		rbuf = (u8 *)b;
}

/* la liste des fenêtres -> wl (WE_* ; le bureau d'abord s'il est demandé), wl_n */
static void get_list(void)
{
	u64 n = 0; /* entrées */
	if (opt_desktop) { /* le bureau : l'écran entier */
		memset(wl, 0, WE_SIZE);
		E32(wl, WE_ID) = (u32)DESK;
		*(u16 *)(wl + WE_W) = (u16)scr_w;
		*(u16 *)(wl + WE_H) = (u16)scr_h;
		n = 1;
	}
	u64 first = 0;
	for (;;) {
		long r = wscall(WS_OP_WIN_LIST, first, 0, 0, 0);
		if (r < 0) { /* (Elegant absent : aucune) */
			wl_n = 0;
			return;
		}
		long k = r - (long)first; /* entrées dans ce message */
		if (k <= 0)
			break;
		if (k > WE_PER_MSG)
			k = WE_PER_MSG;
		if (k > (long)(MAXWIN - n))
			k = (long)(MAXWIN - n);
		if (k <= 0)
			break;
		memcpy(wl + n * WE_SIZE, msg + WM_DATA, (u64)k * WE_SIZE);
		n += (u64)k;
		first += (u64)k;
		if (first >= (u64)r)
			break;
	}
	if (opt_desktop) { /* génération du bureau : celles des fenêtres du fond, leur place */
		u64 g = 1;
		for (u64 i = 1; i < n; i++) {
			const u8 *e = wl + i * WE_SIZE;
			if (!(E32(e, WE_FLAGS) & WF_BACK))
				continue;
			g = g * 31 + E32(e, WE_ID);
			g = g * 31 + E32(e, WE_GEN);
			g = g * 31 + *(const u64 *)(e + WE_X); /* x, y, l, h */
			g = g * 31 + E32(e, WE_STATE);
		}
		E32(wl, WE_GEN) = (u32)g;
	}
	wl_n = n;
}

/* titre UTF-8 (32 octets au plus, NUL), destination -> octets en Latin-1 (les
 * caractères au-delà : '?') */
static u64 title_latin1(const u8 *src, u8 *dst)
{
	u64 w = 0, r = 0;
	while (r < 32) {
		u32 c = src[r];
		if (!c)
			break;
		r++;
		if (c >= 0x80) {
			if (c < 0xC0) /* (suite isolée : ignorée) */
				continue;
			if (c <= 0xC3) { /* deux octets : U+0080..U+00FF */
				u32 c2 = src[r]; /* (r = 32 : l'octet qui suit le titre, comme l'asm) */
				if ((c2 & 0xC0) != 0x80) /* (coupé : ignoré) */
					continue;
				r++;
				c = (c2 & 0x3F) | (c & 3) << 6;
			} else {
				c = '?';
				while (r < 32 && (src[r] & 0xC0) == 0x80)
					r++;
			}
		}
		dst[w++] = (u8)c;
	}
	return w;
}

/* place : son message WIN */
static void send_win(struct slot *s)
{
	u64 n = title_latin1(s->title, ttl); /* titre en Latin-1 (le client le lit ainsi) */
	msg_head(1, n + 27);
	put32(s->id);
	put16(s->x);
	put16(s->y);
	put16(s->w);
	put16(s->h);
	u64 fw = 0, fh = 0; /* cadre : l + 2 bords, h + titre + bord */
	if (!(s->flags & WF_FRAMELESS) && s->id != DESK) {
		fw = s->w + 2 * FRAME_BORDER;
		fh = s->h + FRAME_TITLE_H + FRAME_BORDER;
	}
	put16(fw);
	put16(fh);
	put16(fw ? FRAME_BORDER : 0);
	put16(fw ? FRAME_TITLE_H : 0);
	u64 f = s->flags & 7; /* drapeaux d'Onyx : sans cadre 1, fond 2, dessus 4 (les mêmes bits), ALPHA 32 */
	if (s->flags & WF_ALPHA)
		f |= 32;
	put32(f);
	put8(255); /* opacité */
	u64 st = s->state & 1; /* état : 1 au clavier, 4 réduite */
	if (s->state & WES_MINI)
		st |= 4;
	put8(st);
	put8(n);
	put(ttl, n);
}

/* largeur (pixels) -> lignes par lecture (tampon d'Elegant, par multiples de 64) */
static u64 band_rows(u64 w)
{
	u64 r = udiv(rbuf_size >> 2, w);
	if (r >= TILE)
		r &= ~(u64)(TILE - 1);
	return r;
}

/* id, partie (bit 8 : en 32 bits quoi qu'il arrive), pixels (coin du rectangle),
 * pas (octets), x, y, largeur, hauteur : un message PIXELS (LZ4 s'il est plus court) */
static void send_rect(u64 id, u64 part, const u8 *px, u64 stride, u64 x, u64 y, u64 w, u64 h)
{
	u64 is16 = part & 0x100 ? 0 : opt_16; /* 16 bits ? */
	u64 bytes = w * h * (is16 ? 2 : 4);
	if (bytes > PACK_MAX) /* (rien d'envoyé, comme l'asm) */
		return;
	u8 *o = packbuf; /* les lignes bout à bout */
	for (u64 r = 0; r < h; r++, px += stride) {
		if (!is16) {
			memcpy(o, px, w * 4);
			o += w * 4;
			continue;
		}
		const u32 *q = (const u32 *)px; /* RGB565 */
		for (u64 i = 0; i < w; i++) {
			u32 v = q[i];
			*(u16 *)o = (u16)(((v >> 8) & 0xF800) | ((v >> 5) & 0x07E0) | ((v >> 3) & 0x1F));
			o += 2;
		}
	}
	u64 c = lz4_compress(packbuf, bytes, lzbuf, htab); /* octets compressés */
	u64 lz = c < bytes;
	msg_head(4, (lz ? c : bytes) + 15);
	put32(id);
	put8(part & 0xFF);
	put8(is16 ? 16 : 32); /* profondeur */
	put8(lz);
	put16(x);
	put16(y);
	put16(w);
	put16(h);
	put(lz ? lzbuf : packbuf, lz ? c : bytes);
}

/* actuel, copie, pas (octets), largeur, hauteur (pixels) -> 1 si le carré diffère */
static int tile_diff(const u8 *a, const u8 *b, u64 stride, u64 w, u64 h)
{
	for (; h; h--, a += stride, b += stride) {
		const u32 *p = (const u32 *)a, *q = (const u32 *)b;
		for (u64 i = 0; i < w; i++)
			if (p[i] != q[i])
				return 1;
	}
	return 0;
}

/* place : son contenu, les carrés de 64 x 64 changés depuis la copie du client
 * (tous si stale ou une nouvelle taille) */
static void send_content(struct slot *s)
{
	u64 all = s->stale, w = s->w, h = s->h;
	if (!rbuf)
		return;
	u64 size = w * h * 4; /* une copie de la bonne taille */
	if (!s->buf || s->bw != w || s->bh != h) {
		if (s->cap < size) {
			/* (l'asm perdait l'ancienne, malloc n'ayant pas de free ; ici elle
			 * est rendue) */
			u8 *b = malloc(size);
			if (!b)
				return;
			free(s->buf);
			s->buf = b;
			s->cap = size;
		}
		s->bw = w;
		s->bh = h;
		all = 1;
	}
	u64 rows = band_rows(w); /* lignes par lecture */
	if (!rows)
		return;
	u64 stride = w * 4;
	for (u64 y0 = 0; y0 < h;) { /* y0 : début de la bande */
		u64 bh = h - y0 < rows ? h - y0 : rows; /* hauteur de la bande */
		/* (cadres natifs du PC : le contenu seul, partie 3) */
		if (wscall(WS_OP_WIN_READ, s->id, opt_noframes ? 3 : 0, y0 << 32, w | bh << 32) < 0)
			return;
		for (u64 ty = 0; ty < bh; ty += TILE) { /* bande de carrés dans la lecture */
			u64 th = bh - ty > TILE ? TILE : bh - ty;
			long run = -1; /* début d'une suite de carrés changés */
			for (u64 tx = 0; tx < w + TILE; tx += TILE) {
				int ch = 0; /* changé ? (après le dernier : non, la suite finit) */
				if (tx < w) {
					ch = 1;
					if (!all)
						ch = tile_diff(rbuf + ty * stride + tx * 4, s->buf + (y0 + ty) * stride + tx * 4,
							stride, w - tx > TILE ? TILE : w - tx, th);
				}
				if (ch) {
					if (run < 0)
						run = (long)tx;
					continue;
				}
				if (run < 0)
					continue;
				u64 rx = (u64)run, rw = (tx >= w ? w : tx) - rx; /* fin de la suite : un rectangle */
				send_rect(s->id, s->flags & WF_ALPHA ? 0x100 : 0, rbuf + ty * stride + rx * 4, stride,
					rx, y0 + ty, rw, th); /* (transparence : 32 bits) */
				const u8 *src = rbuf + ty * stride + rx * 4; /* la copie du client suit */
				u8 *dst = s->buf + (y0 + ty) * stride + rx * 4;
				for (u64 k = th; k; k--, src += stride, dst += stride)
					memcpy(dst, src, rw * 4);
				run = -1;
			}
		}
		y0 += bh;
		if (dead)
			return;
	}
}

/* place : son cadre, les deux copies (active, inactive), en entier, en 32 bits
 * (les coins, l'ombre : transparence) */
static void send_chrome(struct slot *s)
{
	if (!rbuf)
		return;
	u64 fw = s->w + 2 * FRAME_BORDER, fh = s->h + FRAME_TITLE_H + FRAME_BORDER;
	u64 rows = band_rows(fw);
	if (!rows)
		return;
	for (u64 part = 1; part <= 2; part++) {
		for (u64 y0 = 0; y0 < fh;) {
			u64 bh = fh - y0 > rows ? rows : fh - y0;
			if (wscall(WS_OP_WIN_READ, s->id, part, y0 << 32, fw | bh << 32) < 0)
				return;
			for (u64 ty = 0; ty < bh; ty += TILE) {
				u64 th = bh - ty > TILE ? TILE : bh - ty;
				send_rect(s->id, part | 0x100, rbuf + ty * fw * 4, fw * 4, 0, y0 + ty, fw, th); /* (32 bits) */
			}
			y0 += bh;
			if (dead)
				return;
		}
	}
}

/* -> 1 si une ronde est partie (... END), 0 si rien n'a changé (rien envoyé :
 * le crédit reste) */
static int round_send(void)
{
	roundmsgs = 0;
	korin_sys1(SYS_SCREEN_INFO, tmp); /* nouvelle taille de l'écran : SCREEN */
	if (tmp[0] != scr_w || tmp[1] != scr_h) {
		scr_w = tmp[0];
		scr_h = tmp[1];
		rbuf = 0; /* le tampon d'Elegant est à refaire */
		korin_sys1(SYS_BUF_FREE, rbuf_num);
		msg_head(8, 4);
		put16(scr_w);
		put16(scr_h);
	}
	rbuf_map();
	get_list();
	for (int i = 0; i < MAXWIN; i++) /* toutes pas vues */
		slots[i].alive = 0;
	norder_n = 0;
	for (u64 i = 0; i < wl_n; i++) {
		const u8 *e = wl + i * WE_SIZE;
		u64 flags = E32(e, WE_FLAGS), state = E32(e, WE_STATE), id = E32(e, WE_ID);
		if (flags & WF_BACK) /* (dans le bureau) */
			continue;
		long x = *(const int16_t *)(e + WE_X), y = *(const int16_t *)(e + WE_Y);
		u64 w = *(const u16 *)(e + WE_W), h = *(const u16 *)(e + WE_H);
		if (!w || !h)
			continue;
		/* hors de l'écran : pas dite */
		if (x + (long)w <= 0 || y + (long)h <= 0 || x >= (long)scr_w || y >= (long)scr_h)
			continue;
		struct slot *s = find_slot(id);
		if (!s) {
			s = find_slot(0); /* une place libre */
			if (!s)
				continue;
			s->id = id;
			s->sent = 0;
			s->stale = 1;
		}
		s->alive = 1;
		u64 said = s->sent ^ 1; /* à dire (WIN) ? */
		said += s->x != x;
		said += s->y != y;
		said += s->w != w;
		said += s->h != h;
		said += s->flags != flags;
		said += s->state != state;
		for (int k = 0; k < 32; k += 8) { /* titre (32 octets) */
			u64 a = *(const u64 *)(e + WE_TITLE + k);
			u64 *b = (u64 *)(s->title + k);
			said += a != *b;
			*b = a;
		}
		s->x = x;
		s->y = y;
		s->w = w;
		s->h = h;
		s->flags = flags;
		s->state = state;
		if (said)
			send_win(s);
		s->sent = 1;
		norder[norder_n++] = id; /* ordre d'affichage */
		if (s->state & WES_MINI) { /* réduite : pixels plus tard */
			s->stale = 1;
			continue;
		}
		/* le cadre */
		if (!(s->flags & WF_FRAMELESS) && s->id != DESK && !opt_noframes &&
			(s->stale || E32(e, WE_CGEN) != s->cgen))
			send_chrome(s);
		if (s->stale || E32(e, WE_GEN) != s->gen)
			send_content(s);
		s->gen = E32(e, WE_GEN);
		s->cgen = E32(e, WE_CGEN);
		s->stale = 0;
		if (dead) {
			outlen = 0;
			return 0;
		}
	}
	for (int i = 0; i < MAXWIN; i++) { /* GONE : celles qui ne sont plus là */
		struct slot *s = &slots[i];
		if (!s->id || s->alive)
			continue;
		if (s->sent) {
			msg_head(2, 4);
			put32(s->id);
		}
		s->id = 0;
		s->sent = 0;
	}
	int same = order_n == (long)norder_n; /* ZORDER s'il a changé */
	for (u64 i = 0; same && i < norder_n; i++)
		same = order[i] == norder[i];
	if (!same) {
		msg_head(3, norder_n * 4 + 2);
		put16(norder_n);
		order_n = (long)norder_n;
		for (u64 i = 0; i < norder_n; i++) {
			order[i] = norder[i];
			put32(order[i]);
		}
	}
	if (!roundmsgs) { /* rien n'a changé : rien */
		outlen = 0;
		return 0;
	}
	msg_head(5, 0); /* END */
	flush_out();
	return 1;
}

/* ---- une session : sock (le client), jusqu'à sa fin */

static void session(void)
{
	in_n = 0; /* remise à zéro */
	outlen = 0;
	dead = 0;
	opt_desktop = 0;
	btn = 0;
	mods = 0;
	order_n = -1;
	for (int i = 0; i < MAXWIN; i++) { /* les fenêtres oubliées (copies gardées) */
		slots[i].id = 0;
		slots[i].sent = 0;
	}
	korin_sys1(SYS_SCREEN_INFO, scr); /* taille de l'écran */
	if (korin_sys2(SYS_GETRANDOM, sd.challenge, 16) != 16) /* le défi du mot de passe */
		goto out;
	put(s_magic, 8); /* salut */
	put16(scr_w);
	put16(scr_h);
	put16(DPI);
	put(sd.challenge, 16);
	flush_out();
	if (!need(AUTH_LEN, 15000)) /* la réponse du client */
		goto out;
	if (memcmp(inbuf, s_magic, 8))
		goto out;
	u64 v = auth(); /* le mot de passe -> verdict */
	put8(v);
	flush_out();
	if (v) {
		ak_puts(v == AUTH_NONE ? m_none : m_refused);
		goto out;
	}
	u8 o = inbuf[8]; /* options */
	opt_16 = o & 1;
	opt_noframes = (o >> 1) & 1;
	opt_pipe = (o >> 2) & 1;
	consume(AUTH_LEN);
	rounds_max = opt_pipe ? PIPE_ROUNDS : 1; /* crédit : 1 ronde en vol, 3 en pipeline */
	credit = rounds_max - 1;                 /* (+ le premier READY du client) */
	if (opt_pipe) {
		msg_head(9, 2); /* CAPS : protocole 2 (MOVE), 3 rondes */
		put8(2);
		put8(PIPE_ROUNDS);
	}
	cur_sent = 0; /* CURSOR : la flèche, puis cursor_poll */
	msg_head(11, 1);
	put8(0);
	flush_out();
	u64 now = now_ms();
	last_rx = now;
	last_ping = now;
	last_poll = now;
	last_round = now - 1000;
	round_wait = MIN_ROUND_MS;
	ak_puts(m_session);
	for (;;) {
		if (dead)
			goto out;
		if (!fill_in(credit > 0 ? 5 : 30)) { /* attente : courte si une ronde peut partir */
			ak_puts(m_closed);
			goto out;
		}
		parse();
		if (dead)
			goto out;
		now = now_ms();
		if (credit > 0 && now - last_round >= round_wait) {
			last_round = now; /* une ronde */
			if (round_send())
				credit--;
			u64 t = (now_ms() - now) << 1; /* prochaine : 2 x le temps pris, 20 ms au moins */
			round_wait = t < MIN_ROUND_MS ? MIN_ROUND_MS : t;
			now = now_ms();
		}
		cursor_poll(now);
		if (dead)
			goto out;
		if (opt_pipe) { /* pipeline : PING si silencieux ; fin si plus rien depuis SILENT_MS */
			u64 q = now - last_rx;
			if (q > SILENT_MS) {
				ak_puts(m_silent);
				goto out;
			}
			if (q >= IDLE_PING_MS && now - last_ping >= IDLE_PING_MS) {
				last_ping = now;
				msg_head(10, 4);
				put32(now);
				flush_out();
			}
		}
		if (now - last_poll < POLL_MS) /* un nouveau client prend la place */
			continue;
		last_poll = now;
		long c = net_accept(lsock, 0);
		if (c < 0)
			continue;
		next_sock = c;
		ak_puts(m_next);
		goto out;
	}
out:
	release_all();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!(outbuf = malloc(OUT_MAX)) || !(packbuf = malloc(PACK_MAX)) || !(lzbuf = malloc(LZ_MAX)) ||
		!(htab = malloc(LZ4_HT_BYTES))) {
		ak_puts(m_nomem);
		exit(1);
	}
	next_sock = -1;
	long h = korin_sys2(SYS_SVC_OPEN, "hid", 10000); /* pilote virtuel de HIDManager */
	if (h >= 0) {
		MW(WM_OP) = HID_OP_PILOTE;
		ML = 0;
		long b = korin_sys2(SYS_SVC_CALL, h, msg);
		if (b >= 0) {
			b = korin_sys1(SYS_BUF_MAP, b);
			if (b >= 0)
				ring = (volatile u64 *)b;
		}
	}
	for (;;) {
		long ls = net_socket(SOCK_TCP);
		if (ls >= 0) { /* (netd pas encore là : attendre) */
			lsock = ls;
			if (net_bind(ls, PORT) >= 0 && net_listen(ls, 2) >= 0) {
				ak_puts(m_ready);
				for (;;) {
					long c = next_sock; /* un client arrivé pendant la session */
					if (c < 0) {
						c = net_accept(ls, 3600000);
						if (c == E_AGAIN)
							continue;
						if (c < 0) {
							if (c == E_PIPE) /* netd relancé : tout reprendre */
								break;
							continue;
						}
					}
					next_sock = -1;
					sock = c;
					ak_puts(m_client);
					session();
					net_close(sock);
					ak_puts(m_gone);
				}
			}
			net_close(ls);
		}
		korin_sys1(SYS_SLEEP, 2000);
	}
}
