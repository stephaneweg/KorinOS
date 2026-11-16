/* console - la console texte des programmes, quand il n'y a pas d'interface
 * graphique (docs/etude/PC-PILOTES.md, « Démarrage jusqu'au shell ») : lancée
 * par init2 (shell=console, ou pas d'Elegant), relancée si elle tombe.
 *
 *  * un pseudo-terminal de vterm (docs/VTERM.md), /bin/bash s'il est là,
 *    sinon /bin/sh, sur l'esclave, dans un groupe à lui au premier plan
 *    (comme telnetd) ; sh fini : un nouveau ;
 *  * les touches : abonnée de hidmanager (HID_OP_ABONNER, l'anneau des
 *    événements avec le caractère de la disposition), traduites en octets
 *    comme le Terminal (src/apps/terminal/vt_keys.inc : Entrée CR, Retour
 *    arrière DEL, Ctrl+lettre 1..26, flèches ESC [ A...) et écrites sur le bout
 *    maître : la discipline de ligne de vterm fait l'écho, l'effacement, et
 *    Ctrl+C (SIGINT au programme au premier plan) ;
 *  * la sortie du maître va à la console du noyau (SYS_PRINT : grille de
 *    caractères, défilement, curseur, UTF-8 en Latin-1) ; des séquences
 *    d'échappement, seul l'effacement de l'écran (ESC [ 2 J, « clear ») est
 *    rendu (saut de page) ; les autres (couleurs, position) sont retirées.
 * La taille du pty est celle de la console du noyau (src/kernel/core/console.c :
 * police 8 x 16, doublée à partir de 2560 points, une ligne pour la barre).
 * Un seul fil : touches et sortie sondées, 10 ms d'attente quand rien ne bouge.
 * Commun au Pi et au PC (le Pi garde Elegant, même en shell=console). */
#include "korinrt.h"

/* src/inc/vterm.inc */
#define VT_OPEN 1
#define VT_READ 2
#define VT_WRITE 3
#define VT_CLOSE 4
#define VT_SWINSZ 8
#define VT_SPGRP 10
#define FS_DATA_MAX 464

/* service « hid » (src/svc/hidmanager) */
#define HID_OP_ABONNER 1
#define RING_EV 64
#define RING_N 64
#define EV_KEY 1

#define MOD_CTRL 0x11
#define MOD_SHIFT 0x22
#define MOD_ALT 0x44
#define MOD_LALT 0x04
#define MOD_RALT 0x40

#define MW(m, off) (*(long *)((u8 *)(m) + (off)))
#define ML(m) (*(u32 *)((u8 *)(m) + MSG_LEN))

static long vtc = -1;    /* canal de vterm */
static long master;      /* bout maître du pty du shell (0 : aucun) */
static long sh_pid;
static volatile u64 *ring; /* abonnement à hidmanager */
static long rows = 24, cols = 80;
static u8 m[MSG_SIZE] __attribute__((aligned(16)));

static void print(const void *s, u64 n)
{
	if (n)
		korin_sys2(SYS_PRINT, s, n);
}

static void print_str(const char *s)
{
	print(s, (u64)str_len(s));
}

static long vt_call(void)
{
	return korin_sys2(SYS_SVC_CALL, vtc, m);
}

static void vt_close(long h)
{
	MW(m, 0) = VT_CLOSE;
	MW(m, MSG_A) = h;
	ML(m) = 0;
	vt_call();
}

/* la grille de la console du noyau (src/kernel/core/console.c) */
static void screen_size(void)
{
	u64 info[3] = {0, 0, 0};
	if (korin_sys1(SYS_SCREEN_INFO, info) < 0 || !info[0] || !info[1])
		return;
	u64 scale = info[0] >= 2560 ? 2 : 1;
	cols = (long)(info[0] / (8 * scale));
	rows = (long)(info[1] / (16 * scale));
	if (rows > 1)
		rows--; /* la barre du haut */
}

/* un pty, le shell sur son esclave -> 0 ; < 0 : rien d'ouvert */
static long start_sh(void)
{
	MW(m, 0) = VT_OPEN;
	ML(m) = 0;
	if (vt_call() < 0)
		return -1;
	long slave = MW(m, MSG_B);
	master = MW(m, MSG_A);
	MW(m, 0) = VT_SWINSZ;
	MW(m, MSG_A) = master;
	MW(m, MSG_B) = rows;
	MW(m, MSG_C) = cols;
	MW(m, MSG_D) = 0;
	ML(m) = 0;
	vt_call();
	long fds[3] = {slave, slave, slave};
	long pid = f_spawn_stdio("/bin/bash", 0, fds);
	if (pid < 0)
		pid = f_spawn_stdio("/bin/sh", 0, fds);
	if (pid >= 0) {
		korin_sys2(SYS_SETPGID, pid, pid); /* un groupe à lui, au premier plan : Ctrl+C va au shell */
		MW(m, 0) = VT_SPGRP;              /* et à ses commandes (elles prennent le premier plan) */
		MW(m, MSG_A) = master;
		MW(m, MSG_B) = pid;
		ML(m) = 0;
		vt_call();
	}
	vt_close(slave); /* notre copie de l'esclave */
	if (pid < 0) {
		vt_close(master);
		master = 0;
		return pid;
	}
	sh_pid = pid;
	return 0;
}

/* sur le bout maître (pty plein : 2 ms d'attente) -> 0, < 0 si le pty est parti */
static long to_master(const u8 *p, u64 n)
{
	while (n) {
		MW(m, 0) = VT_WRITE;
		MW(m, MSG_A) = master;
		u64 k = n > FS_DATA_MAX ? FS_DATA_MAX : n;
		ML(m) = (u32)k;
		mem_move(m + MSG_DATA, p, k);
		long r = vt_call();
		if (r < 0)
			return r;
		if (!r) {
			sys_sleep(2);
			continue;
		}
		p += r;
		n -= (u64)r;
	}
	return 0;
}

/* ---- touches -> octets (src/apps/terminal/vt_keys.inc) */

struct special {
	u8 code, kind, fin; /* kind 1 : CSI final, 2 : CSI n ~, 3 : SS3 final */
};
static const struct special specials[] = {
	{0x52, 1, 'A'}, {0x51, 1, 'B'}, {0x4F, 1, 'C'}, {0x50, 1, 'D'}, {0x4A, 1, 'H'}, {0x4D, 1, 'F'},
	{0x49, 2, 2},   {0x4C, 2, 3},   {0x4B, 2, 5},   {0x4E, 2, 6},   {0x3A, 3, 'P'}, {0x3B, 3, 'Q'},
	{0x3C, 3, 'R'}, {0x3D, 3, 'S'}, {0x3E, 2, 15},  {0x3F, 2, 17},  {0x40, 2, 18},  {0x41, 2, 19},
	{0x42, 2, 20},  {0x43, 2, 21},  {0x44, 2, 23},  {0x45, 2, 24},
};

static u8 *put_dec(u8 *d, u32 v)
{
	if (v >= 10)
		d = put_dec(d, v / 10);
	*d++ = (u8)('0' + v % 10);
	return d;
}

/* code HID, caractère de la disposition, modificateurs -> octets dans out (16) */
static u64 key_encode(u32 code, u32 ch, u32 mods, u8 *out)
{
	u8 *d = out;
	u32 mp = 1 + ((mods & MOD_SHIFT) ? 1 : 0) + ((mods & MOD_ALT) ? 2 : 0) + ((mods & MOD_CTRL) ? 4 : 0);
	for (u64 i = 0; i < sizeof specials / sizeof specials[0]; i++) {
		const struct special *s = &specials[i];
		if (s->code != code)
			continue;
		*d++ = 0x1B;
		if (s->kind == 2) { /* ESC [ n (; m) ~ */
			*d++ = '[';
			d = put_dec(d, s->fin);
			if (mp != 1) {
				*d++ = ';';
				d = put_dec(d, mp);
			}
			*d++ = '~';
		} else if (mp != 1) { /* ESC [ 1 ; m X */
			*d++ = '[';
			*d++ = '1';
			*d++ = ';';
			d = put_dec(d, mp);
			*d++ = s->fin;
		} else { /* ESC [ X, ou SS3 (F1-F4) */
			*d++ = s->kind == 3 ? 'O' : '[';
			*d++ = s->fin;
		}
		return (u64)(d - out);
	}
	switch (code) {
	case 0x28: /* Entrée */
	case 0x58: /* Entrée du pavé */
		*d++ = 0x0D;
		return 1;
	case 0x29: /* Échap */
		*d++ = 0x1B;
		return 1;
	case 0x2A: /* Retour arrière : DEL (VERASE de vterm) */
		*d++ = 0x7F;
		return 1;
	case 0x2B: /* Tab, Maj+Tab : ESC [ Z */
		if (mods & MOD_SHIFT) {
			*d++ = 0x1B;
			*d++ = '[';
			*d++ = 'Z';
			return 3;
		}
		*d++ = 9;
		return 1;
	}
	if (mods & MOD_CTRL) {
		u32 l = (ch | 0x20) - 'a';
		if (l >= 26)
			l = code - 4; /* la place HID (0x04..0x1D : a..z du QWERTY), comme le Terminal */
		if (l < 26) {
			*d++ = (u8)(l + 1);
			return 1;
		}
		if (ch == ' ' || code == 0x2C) {
			*d++ = 0;
			return 1;
		}
		if (ch == '[' || ch == '\\' || ch == ']') {
			*d++ = ch == '[' ? 27 : ch == '\\' ? 28 : 29;
			return 1;
		}
		return 0;
	}
	if (!ch)
		return 0;
	if ((mods & MOD_LALT) && !(mods & MOD_RALT)) /* Alt gauche : ESC devant (Alt droite est AltGr) */
		*d++ = 0x1B;
	d += utf8_put(ch, (char *)d);
	return (u64)(d - out);
}

/* les touches arrivées -> le bout maître ; 1 si une touche est passée */
static int keys(void)
{
	int any = 0;
	volatile u64 *r = ring;
	while (r[1] != r[0]) {
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		volatile u64 *e = r + (RING_EV + (r[1] & (RING_N - 1)) * 32) / 8;
		u64 e0 = e[0], code = e[1], pressed = e[2], mods = e[3];
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		r[1] = r[1] + 1;
		any = 1;
		if ((u32)e0 != EV_KEY || !pressed || !master)
			continue;
		u8 out[16];
		u64 n = key_encode((u32)code, (u32)(e0 >> 32), (u32)mods, out);
		if (n)
			to_master(out, n);
	}
	return any;
}

/* ---- sortie : séquences d'échappement filtrées */

static int esc_state; /* 0 texte, 1 après ESC, 2 CSI, 3 OSC */
static u32 csi_param;
static u8 obuf[1024];
static u64 on;

static void out_byte(u8 c)
{
	if (on == sizeof obuf) {
		print(obuf, on);
		on = 0;
	}
	obuf[on++] = c;
}

static void output(const u8 *p, long n)
{
	for (long i = 0; i < n; i++) {
		u8 c = p[i];
		switch (esc_state) {
		case 0:
			if (c == 0x1B)
				esc_state = 1;
			else if (c && c != 7) /* (le noyau coupe sa chaîne au 0 ; la cloche : rien) */
				out_byte(c);
			break;
		case 1:
			esc_state = c == '[' ? 2 : c == ']' ? 3 : 0;
			csi_param = 0;
			break;
		case 2:
			if (c >= '0' && c <= '9')
				csi_param = csi_param * 10 + (c - '0');
			else if (c >= 0x40 && c <= 0x7E) { /* final */
				if (c == 'J' && (csi_param == 2 || csi_param == 3))
					out_byte('\f'); /* l'écran effacé */
				esc_state = 0;
			}
			break;
		default: /* OSC : jusqu'à la cloche ou ESC \ */
			if (c == 7)
				esc_state = 0;
			else if (c == 0x1B)
				esc_state = 1;
		}
	}
	print(obuf, on);
	on = 0;
}

/* la sortie du shell -> la console ; 1 si quelque chose est passé, -1 : le shell est fini */
static int from_master(void)
{
	MW(m, 0) = VT_READ;
	MW(m, MSG_A) = master;
	MW(m, MSG_B) = FS_DATA_MAX;
	MW(m, MSG_C) = 1; /* sans attente */
	ML(m) = 0;
	long r = vt_call();
	if (r == E_AGAIN)
		return 0;
	if (r <= 0)
		return -1; /* plus d'esclave : sh et ses fils sont finis */
	output(m + MSG_DATA, r);
	return 1;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long hid = korin_sys2(SYS_SVC_OPEN, "hid", 10000);
	if (hid < 0) {
		ak_puts("console : hidmanager absent\n");
		return 1; /* (init2 relance) */
	}
	MW(m, 0) = HID_OP_ABONNER;
	long b = korin_sys2(SYS_SVC_CALL, hid, m);
	if (b < 0 || (b = korin_sys1(SYS_BUF_MAP, b)) < 0) {
		ak_puts("console : abonnement à hidmanager refusé\n");
		return 1;
	}
	ring = (volatile u64 *)b;
	vtc = korin_sys2(SYS_SVC_OPEN, "vterm", 10000);
	if (vtc < 0) {
		ak_puts("console : vterm absent (/etc/services.txt)\n");
		return 1;
	}
	screen_size();
	int fails = 0;
	long started = 0;
	for (;;) {
		if (!master) {
			if (start_sh() < 0) {
				ak_puts("console : le shell ne démarre pas (/bin/sh)\n");
				if (++fails >= 5)
					return 1;
				sys_sleep(2000);
				continue;
			}
			fails = 0;
			started = sys_time_ms();
			print_str("\nKorin OS : console texte. Ctrl+C arrête la commande en cours, « exit » relance le shell.\n");
		}
		int busy = keys();
		int o = from_master();
		if (o < 0) {
			vt_close(master);
			master = 0;
			print_str("\n[shell fini]\n");
			if (sys_time_ms() - started < 2000)
				sys_sleep(2000); /* (un shell qui s'arrête aussitôt : pas de relance en rafale) */
			continue;
		}
		if (!busy && !o)
			sys_sleep(10);
	}
}
