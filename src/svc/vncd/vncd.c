/* vncd - l'écran à distance (docs/RESEAU.md 5.1 et 5.4, étape 11), port 5900,
 * lancé par init2 (/etc/services.txt) ; traduction de vncd.asm (mêmes
 * messages, même protocole, mêmes bizarreries : « comme l'asm »).
 * RFB 3.3 / 3.7 / 3.8 (RFC 6143), sécurité « VNC Authentication » (type 2,
 * RFC 6143 7.2.2) : défi de 16 octets aléatoires (getrandom), réponse DES-ECB
 * dont la clé est le mot de passe de l'accès à distance (/etc/remote.txt,
 * remotepw.c, relu à chaque client ; 8 octets au plus comptent, bits de chaque
 * octet inversés : usage de VNC). Sans mot de passe défini, toute connexion est
 * refusée (avec la raison). DES : des.c. Un client à la fois ; tout client VNC
 * (TigerVNC, RealVNC, TightVNC), ou vncdotool (Python) pour les scripts.
 *  * image : l'écran composé d'Elegant (op WS_OP_SNAPSHOT : seules les zones
 *    changées depuis la dernière mise à jour), codage Raw, dans le format de
 *    pixels du client (8, 16 ou 32 bits, n'importe quels décalages) ;
 *  * entrées : pilote virtuel de HIDManager (op HID_OP_PILOTE, comme le
 *    GT911) : pointeur absolu, boutons, molette ; touches : keysyms X11 ->
 *    caractère déjà traduit (la disposition du PC est appliquée) + code HID
 *    pour les lettres, chiffres et touches spéciales.
 * Pas de CopyRect pour l'instant (Raw seulement).
 *
 * Bizarreries visibles de l'extérieur, comme l'asm :
 *  * la version du client est le seul chiffre « x » de « RFB 003.00x » : en
 *    dessous de '7', le dialogue 3.3 ; '7' ou plus (n'importe quel octet), celui
 *    de 3.7 ; la raison d'un mot de passe refusé n'est envoyée qu'à partir de '8' ;
 *  * le drapeau « vraies couleurs » et la profondeur de SetPixelFormat sont
 *    ignorés (toujours des tables de vraies couleurs) ;
 *  * un mot de passe refusé : SecurityResult 1 (et la raison), puis la
 *    connexion est fermée ;
 *  * un écran qui change de taille : le client est déconnecté (RFB ne le dit
 *    pas à un client ordinaire, il se reconnecte) ;
 *  * un message de type inconnu (1, 7...) : déconnecté. */
#include "korinrt.h"
#include "netkit_import.h"
#include "../inc/remotepw.h"
#include "des.h"

#define PORT 5900
#define HID_OP_PILOTE 3
#define RING_EV 64
#define RING_N 64
#define EV_KEY 1
#define EV_MOUSE_ABS 3
#define EV_WHEEL 4
#define IN_MAX 4096
#define OUT_MAX 49152 /* envoi par morceaux */

#define MW(off) (*(u64 *)(msg + (off)))
#define ML (*(u32 *)(msg + WM_LEN))

static const char s_version[] = "RFB 003.008\n"; /* (12 octets envoyés) */
static const char s_name[8] = "Korin";
static const char m_ready[] = "vncd : en écoute sur le port 5900 (mot de passe : etc/remote.txt)\n";
static const char m_refused[] = "vncd : mot de passe refusé\n";
static const char m_no_pw[] = "vncd : aucun mot de passe défini, client refusé\n";
static const char s_wrong[] = "Mot de passe incorrect";
static const char s_no_pw[] = "Pas de mot de passe : Réglages, Accès à distance";
static const char m_client[] = "vncd : client\n";
static const char m_gone[] = "vncd : client parti\n";

/* format du serveur : 32 bits, profondeur 24, petit-boutiste, vraies couleurs,
 * maxima 255 (gros-boutistes), décalages 16, 8, 0 */
static const u8 pf_server[16] = {32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0};

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
static u64 screen[3];      /* largeur, hauteur, barre (SCREEN_INFO) */
static u32 *screen_buf;    /* tampon de l'écran (Elegant), son numéro */
static u64 screen_num;
static u64 gen;            /* génération déjà envoyée */
static u64 pending;        /* une demande de mise à jour attend */
static u64 in_n;
static u64 buttons;
static u64 mods;
static u64 pf_bytes = 4;
static u64 pf_big;
static u64 pf_direct = 1;
static u8 pf_client[16];
static u8 tmp[64] __attribute__((aligned(8)));
static u8 msg[WS_MSG] __attribute__((aligned(16)));
static u8 inbuf[IN_MAX];
/* clé, sous-clés, attendu : d'un bloc (effacés ensemble) */
static struct {
	u8 key[8];
	u64 sub[16];
	u8 expected[16];
} dk;
static u8 challenge[16];
static u8 response[16];
static u32 tab[3][256]; /* tables : rouge, vert, bleu */

/* comme udiv d'AArch64 : division par zéro -> 0 */
static u64 udiv(u64 a, u64 b)
{
	return b ? a / b : 0;
}

static u32 be32(const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u32 be16(const u8 *p)
{
	return (u32)p[0] << 8 | p[1];
}

static void put_be32(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static void put_be16(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

/* socket, données, octets -> < 0 si échec (envoi) */
static long send_all(long sock, const void *data, u64 n)
{
	const u8 *p = data;
	while (n) {
		long r = net_send(sock, (long)p, (long)n);
		if (r <= 0)
			return -1;
		p += r;
		n -= (u64)r;
	}
	return 0;
}

/* socket, tampon, octets : tous (15 s au plus par réception) -> < 0 (recu_tout) */
static long recv_all(long sock, void *buf, u64 n)
{
	u8 *p = buf;
	while (n) {
		long r = net_recv(sock, (long)p, (long)n, 15000);
		if (r <= 0)
			return -1;
		p += r;
		n -= (u64)r;
	}
	return 0;
}

/* socket, raison : longueur (u32 gros-boutiste) puis le texte (UTF-8) -> < 0
 * si échec (envoi_raison) */
static long send_reason(long sock, const char *s)
{
	u64 n = (u64)str_len(s);
	put_be32(tmp, (u32)n);
	if (send_all(sock, tmp, 4) < 0)
		return -1;
	return send_all(sock, s, n);
}

/* « VNC Authentication » (RFC 6143 7.2.2), rp_read déjà fait (mot de passe non
 * vide dans rp_pw). Socket, version (3, 7 ou 8) -> 0 si le client a la bonne
 * réponse ; sinon SecurityResult 1 (et la raison en 3.8), < 0 : fermer
 * (auth_vnc). */
static long auth_vnc(long sock, u32 version)
{
	/* (un échec avant la réponse laisse rp_pw en mémoire, comme l'asm) */
	if (korin_sys2(SYS_GETRANDOM, challenge, 16) != 16) /* défi : 16 octets du noyau */
		return -1;
	if (send_all(sock, challenge, 16) < 0)
		return -1;
	if (recv_all(sock, response, 16) < 0)
		return -1;
	/* clé : 8 premiers octets, complétés de zéros, chaque octet aux bits
	 * inversés (bit 0 <-> bit 7) */
	memset(dk.key, 0, 8);
	for (int i = 0; i < 8 && rp_pw[i]; i++) {
		u8 c = (u8)rp_pw[i], r = 0;
		for (int b = 0; b < 8; b++)
			r |= (u8)(((c >> b) & 1) << (7 - b));
		dk.key[i] = r;
	}
	des_keys(dk.key, dk.sub);
	des_block(dk.sub, challenge, dk.expected); /* la réponse attendue */
	des_block(dk.sub, challenge + 8, dk.expected + 8);
	u8 diff = 0; /* comparaison en temps constant */
	for (int i = 0; i < 16; i++)
		diff |= dk.expected[i] ^ response[i];
	memset(&dk, 0, sizeof dk); /* effacer clé, sous-clés, mot de passe */
	memset(rp_pw, 0, RP_MAX + 1);
	if (!diff) {
		memset(tmp, 0, 4); /* SecurityResult 0 : bon */
		return send_all(sock, tmp, 4);
	}
	ak_puts(m_refused);
	put_be32(tmp, 1); /* SecurityResult 1 : échec */
	if (send_all(sock, tmp, 4) < 0)
		return -1;
	if (version >= 8) /* 3.8 : la raison */
		send_reason(sock, s_wrong);
	return -1;
}

/* format de pixels RFB (16 o) : tables de conversion du client (pf_regler) */
static void pf_set(const u8 *pf)
{
	memcpy(pf_client, pf, 16);
	u64 b = pf[0] >> 3; /* octets par pixel */
	if (b != 1 && b != 2)
		b = 4;
	pf_bytes = b;
	pf_big = pf[2];
	/* direct : 32 bits, petit-boutiste, 255/255/255, décalages 16/8/0 (vraies
	 * couleurs et profondeur pas regardées, comme l'asm) */
	pf_direct = pf[0] == 32 && !pf[2] && be16(pf + 4) == 255 && be16(pf + 6) == 255 &&
		be16(pf + 8) == 255 && pf[10] == 16 && pf[11] == 8 && !pf[12];
	for (int c = 0; c < 3; c++) { /* une table par composante */
		u64 max = be16(pf + 4 + 2 * c), shift = pf[10 + c];
		for (u64 v = 0; v < 256; v++) /* (v * max + 127) / 255 << décalage */
			tab[c][v] = (u32)(((v * max + 127) / 255) << (shift & 63)); /* (décalage modulo 64 : lsl d'AArch64) */
	}
}

/* un événement dans l'anneau de HIDManager (perdu s'il est plein) (anneau_pousser) */
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

/* code HID d'un caractère : lettres, chiffres, espace (0 sinon) */
static u64 hid_code(u32 c)
{
	u32 l = (c | 0x20) - 'a';
	if (l <= 25 && c <= 'z')
		return l + 0x04;
	if (c - '1' <= 8)
		return c - '1' + 0x1E;
	if (c == '0')
		return 0x27;
	if (c == ' ')
		return 0x2C;
	return 0;
}

/* appui (non nul) / relâche (0), keysym X11 : une touche à HIDManager (touche) */
static void key(u64 down, u32 sym)
{
	const u32 *k = keysyms; /* touches spéciales */
	while (k[0] && k[0] != sym)
		k += 3;
	u64 code, ch, flags;
	if (k[0]) {
		code = k[1];
		if (k[2])
			mods = down ? mods | k[2] : mods & ~(u64)k[2];
		ch = 0; /* caractère : calculé par HIDManager */
		flags = 0;
	} else {
		if (sym >= 0x01000000) /* un caractère (Latin-1, ou Unicode 0x01xxxxxx) */
			ch = sym & 0xFFFFFF;
		else if (sym > 0xFF)
			return; /* (autre keysym : ignoré) */
		else
			ch = sym;
		code = hid_code((u32)ch);
		flags = 1UL << 31; /* caractère déjà traduit (bit 31) */
	}
	ring_push(EV_KEY | ch << 32, code, down, flags | mods); /* caractère en +4 */
}

/* boutons RFB, x, y : pointeur absolu, boutons, molette (pointeur) */
static void pointer(u64 mask, u64 x, u64 y)
{
	u64 old = buttons; /* molette : bits 3 (haut) et 4 (bas), à l'appui */
	buttons = mask;
	u64 pressed = mask & ~old;
	if (pressed & 8)
		ring_push(EV_WHEEL, 1, 0, 0);
	if (pressed & 16)
		ring_push(EV_WHEEL, (u64)-1, 0, 0);
	x = udiv(x * 32767, screen[0] - 1); /* 0..32767 */
	if (x > 32767)
		x = 32767;
	y = udiv(y * 32767, screen[1] - 1);
	if (y > 32767)
		y = 32767;
	u64 b = mask & 1; /* boutons : gauche, droit (RFB 4), milieu (RFB 2) */
	if (mask & 4)
		b |= 2;
	if (mask & 2)
		b |= 4;
	ring_push(EV_MOUSE_ABS, x, y, b);
}

/* inbuf : messages complets du client -> < 0 si le client est fautif */
static long messages(void)
{
	for (;;) {
		u64 n = in_n, len;
		u8 *p = inbuf;
		if (!n)
			return 0;
		switch (p[0]) {
		case 0: /* SetPixelFormat */
			len = 20;
			if (n < len)
				return 0;
			pf_set(p + 4);
			break;
		case 2: /* SetEncodings : Raw toujours */
			if (n < 4)
				return 0;
			if (be16(p + 2) > 64)
				return -1; /* (une liste sans borne bloquait la connexion : client fautif, S11) */
			len = (u64)be16(p + 2) * 4 + 4;
			if (n < len)
				return 0;
			break;
		case 3: /* FramebufferUpdateRequest */
			len = 10;
			if (n < len)
				return 0;
			if (!p[1]) /* non incrémental : tout l'écran */
				gen = 0;
			pending = 1;
			break;
		case 4: /* KeyEvent */
			len = 8;
			if (n < len)
				return 0;
			key(p[1], be32(p + 4));
			break;
		case 5: /* PointerEvent */
			len = 6;
			if (n < len)
				return 0;
			pointer(p[1], be16(p + 2), be16(p + 4));
			break;
		case 6: /* ClientCutText : ignoré */
			if (n < 8)
				return 0;
			len = (u64)be32(p + 4) + 8;
			if (len > IN_MAX)
				return -1;
			if (n < len)
				return 0;
			break;
		default:
			return -1;
		}
		in_n = n - len; /* message consommé */
		memmove(inbuf, inbuf + len, in_n);
	}
}

/* pixels 0x00RRGGBB, nombre, destination -> fin (convertir) */
static u8 *convert(const u32 *src, u64 n, u8 *dst)
{
	if (pf_direct) { /* direct : copie */
		memcpy(dst, src, n * 4);
		return dst + n * 4;
	}
	while (n--) {
		u32 s = *src++;
		u32 v = tab[0][(s >> 16) & 0xFF] | tab[1][(s >> 8) & 0xFF] | tab[2][s & 0xFF];
		if (pf_bytes == 1) {
			*dst++ = (u8)v;
		} else if (pf_bytes == 2) {
			if (pf_big)
				v = (v >> 8 & 0xFF) | (v & 0xFF) << 8;
			*(u16 *)dst = (u16)v;
			dst += 2;
		} else {
			if (pf_big)
				v = __builtin_bswap32(v);
			*(u32 *)dst = v;
			dst += 4;
		}
	}
	return dst;
}

/* une demande en attente : l'écran change depuis « gen » (Elegant), envoyé en
 * Raw dans le format du client -> < 0 si le client est parti (mise_a_jour) */
static long update(long sock)
{
	MW(WM_OP) = WS_OP_SNAPSHOT;
	MW(WM_B) = gen;
	ML = 0;
	long r = korin_sys1(SYS_WS_CALL, msg);
	if (r <= 0) /* (Elegant absent : plus tard) ; rien de neuf */
		return 0;
	gen = MW(WM_C); /* génération vue */
	u64 old = screen_num; /* le tampon d'Elegant (refait si l'écran change de taille) */
	screen_num = MW(WM_D);
	if (!screen_buf || screen_num != old) {
		if (screen_buf) {
			screen_buf = 0;
			korin_sys1(SYS_BUF_FREE, old);
		}
		long b = korin_sys1(SYS_BUF_MAP, MW(WM_D));
		if (b < 0)
			return 0;
		screen_buf = (u32 *)b;
	}
	/* autre taille : RFB ne la dit pas à un client ordinaire, il se reconnecte */
	if (MW(WM_A) != screen[0] || MW(WM_B) != screen[1])
		return -1;
	long x = (long)MW(WM_DATA), y = (long)MW(WM_DATA + 8);
	long w = (long)MW(WM_DATA + 16) - x, h = (long)MW(WM_DATA + 24) - y; /* largeur, hauteur */
	if (w <= 0 || h <= 0)
		return 0;
	pending = 0;
	u8 *o = outbuf; /* en-tête : FramebufferUpdate, 1 rectangle */
	o[0] = 0;
	o[1] = 0;
	put_be16(o + 2, 1);
	put_be16(o + 4, (u32)x);
	put_be16(o + 6, (u32)y);
	put_be16(o + 8, (u32)w);
	put_be16(o + 10, (u32)h);
	put_be32(o + 12, 0); /* codage 0 : Raw */
	u64 n = 16; /* octets dans outbuf */
	while (h) {
		/* source : ligne y, à partir de x */
		n = (u64)(convert(screen_buf + (y * (long)screen[0] + x), (u64)w, o + n) - o);
		y++;
		h--;
		if (pf_bytes * (u64)w + n <= OUT_MAX) /* place pour une ligne de plus ? */
			continue;
		if (send_all(sock, o, n) < 0)
			return -1;
		n = 0;
	}
	if (n && send_all(sock, o, n) < 0)
		return -1;
	return 0;
}

/* un client (socket), jusqu'à son départ */
static void client(long sock)
{
	korin_sys1(SYS_SCREEN_INFO, screen); /* taille de l'écran */
	if (send_all(sock, s_version, 12) < 0) /* version */
		return;
	if (recv_all(sock, tmp, 12) < 0)
		return;
	u32 version = (u32)tmp[10] - '0'; /* « RFB 003.00x » : x = 3, 7 ou 8 */
	long pw = rp_read(); /* le mot de passe, relu à chaque client */
	if (version >= 7) {
		if (pw) {
			tmp[0] = 1; /* 3.7, 3.8 : la liste {VNC Authentication} */
			tmp[1] = 2;
			if (send_all(sock, tmp, 2) < 0)
				return;
			if (recv_all(sock, tmp, 1) < 0)
				return;
			if (tmp[0] != 2) /* le client doit choisir le type 2 */
				return;
			goto auth;
		}
		tmp[0] = 0; /* aucun type (0), puis la raison */
		if (send_all(sock, tmp, 1) < 0)
			return;
		goto no_pw;
	}
	if (pw) {
		put_be32(tmp, 2); /* 3.3 : le type imposé (2), gros-boutiste */
		if (send_all(sock, tmp, 4) < 0)
			return;
		goto auth;
	}
	memset(tmp, 0, 4); /* type 0 : échec, puis la raison */
	if (send_all(sock, tmp, 4) < 0)
		return;
no_pw:
	ak_puts(m_no_pw);
	send_reason(sock, s_no_pw);
	return;
auth:
	if (auth_vnc(sock, version) < 0)
		return;
	if (recv_all(sock, tmp, 1) < 0) /* ClientInit (partage) : ignoré */
		return;
	put_be16(tmp, (u32)screen[0]); /* ServerInit */
	put_be16(tmp + 2, (u32)screen[1]);
	memcpy(tmp + 4, pf_server, 16); /* format de pixels du serveur */
	put_be32(tmp + 20, 5);          /* nom : « Korin » (5, gros-boutiste) */
	memcpy(tmp + 24, s_name, 5);
	if (send_all(sock, tmp, 29) < 0)
		return;
	pf_set(pf_server); /* le client garde ce format au départ */
	gen = 0;           /* tout l'écran à la première demande */
	pending = 0;
	in_n = 0;
	buttons = 0;
	mods = 0;
	for (;;) {
		/* une demande en attente : 40 ms */
		long r = net_recv(sock, (long)(inbuf + in_n), (long)(IN_MAX - in_n), pending ? 40 : 1000);
		if (r != E_AGAIN) {
			if (r <= 0)
				return;
			in_n += (u64)r;
			if (messages() < 0)
				return;
		}
		if (pending && update(sock) < 0)
			return;
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	outbuf = malloc(OUT_MAX + 4096);
	if (!outbuf)
		exit(1);
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
		long sd = net_socket(SOCK_TCP);
		if (sd >= 0) { /* (netd pas encore là : attendre) */
			if (net_bind(sd, PORT) >= 0 && net_listen(sd, 1) >= 0) {
				ak_puts(m_ready);
				for (;;) {
					long c = net_accept(sd, 3600000);
					if (c == E_AGAIN)
						continue;
					if (c < 0) {
						if (c == E_PIPE) /* netd relancé : tout reprendre */
							break;
						continue;
					}
					ak_puts(m_client);
					client(c);
					net_close(c);
					ak_puts(m_gone);
				}
			}
			net_close(sd);
		}
		korin_sys1(SYS_SLEEP, 2000);
	}
}
