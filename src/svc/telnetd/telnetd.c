/* telnetd - accès texte à distance (docs/RESEAU.md 5.1, étape 8), port 23,
 * lancé par init2 (/etc/services.txt) ; traduction de telnetd.asm (mêmes
 * messages, mêmes négociations, mêmes bizarreries : « comme l'asm »).
 *
 * Chaque connexion (8 au plus) a son interpréteur de commandes, /bin/bash
 * s'il est sur la carte, sinon /bin/sh, sur un pseudo-terminal (service
 * vterm, docs/VTERM.md) : telnetd tient le bout maître, le shell l'esclave,
 * dans un groupe à lui au premier plan du pty.
 * Mode caractère : IAC WILL ECHO, WILL SGA ; la discipline de ligne de vterm
 * fait l'écho et l'édition ; Ctrl+C (octet 3, ou IAC IP) devient SIGINT au
 * programme au premier plan. NAWS (IAC DO NAWS) : la taille de la fenêtre du
 * client va au pty (VT_SWINSZ, SIGWINCH). Les autres négociations sont lues et
 * ignorées. Un NOP telnet toutes les 5 min de silence : connexion perdue =
 * session finie (le maître ferme : SIGHUP, fin de l'entrée de sh).
 * Mot de passe (remotepw.c, /etc/remote.txt) demandé avant sh, dans le
 * thread de la connexion : IAC WILL ECHO pendant la saisie, 3 essais, 2 s
 * d'attente après chaque faux. Aucun mot de passe défini : connexion refusée. */
#include "korinrt.h"
#include "netkit_import.h"
#include "../inc/remotepw.h"

#define PORT 23
#define SESS_MAX 8
#define SB_MAX 16
#define TSTACK 32768
#define IAC 255
#define T_SB 250
#define T_SE 240
#define T_IP 244
#define T_NOP 241
#define T_WILL 251
#define T_WONT 252
#define T_DO 253
#define T_DONT 254
#define TO_ECHO 1
#define TO_SGA 3
#define TO_NAWS 31
#define TRIES 3

/* src/inc/vterm.inc */
#define VT_OPEN 1
#define VT_READ 2
#define VT_WRITE 3
#define VT_CLOSE 4
#define VT_SWINSZ 8
#define VT_SPGRP 10
#define FS_DATA_MAX 464

struct session {
	long used;      /* 0 libre ; threads encore en vie */
	long sock;
	long master;    /* bout maître du pty de sh */
	long naws;      /* 1 : nouvelle taille (rows, cols) */
	long pid;
	long state;     /* analyse telnet */
	long cr;        /* un CR vient de passer */
	u8 *stk2;       /* pile de to_client */
	long sbn;       /* octets de la sous-négociation en cours */
	long rows, cols;
	u8 sb[SB_MAX];  /* sous-négociation */
};

static struct session sess[SESS_MAX];
static u8 *stacks[SESS_MAX][2]; /* par place : pile de auth/to_sh, de to_client */
static long rp_lock;
static long vtc; /* canal de vterm */

static const u8 s_crlf[2] = {13, 10};
static const u8 s_mode[9] = {IAC, T_WILL, TO_ECHO, IAC, T_WILL, TO_SGA, IAC, T_DO, TO_NAWS};
static const char m_ready[] = "telnetd : en écoute sur le port 23\n";
static const char m_refused[] = "telnetd : connexion refusée\n";
static const char m_pw[] = "Mot de passe : ";
static const char m_wrong[] = "Mot de passe incorrect.";
static const char m_none[] = "Accès à distance sans mot de passe : définissez-le dans Réglages.";
static const char m_session[] = "telnetd : session, shell pid ";
static const char m_ended[] = "telnetd : session finie\n";
static const char m_full[] = "telnetd : trop de sessions (8)";

#define MW(m, off) (*(long *)((u8 *)(m) + (off)))
#define ML(m) (*(u32 *)((u8 *)(m) + MSG_LEN))

NORETURN static void thread_exit(void)
{
	korin_sys1(SYS_THREAD_EXIT, 0);
	for (;;)
		;
}

static long send(long sock, const void *p, u64 n)
{
	return net_send(sock, (long)p, (long)n);
}

/* ---- vterm */

static long vt_call(void *m)
{
	return korin_sys2(SYS_SVC_CALL, vtc, m);
}

/* le bout maître est rendu (sh lit la fin, SIGHUP) ; une fois */
static void master_close(struct session *s)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	long h = __atomic_exchange_n(&s->master, 0, __ATOMIC_ACQ_REL);
	if (!h)
		return;
	MW(m, 0) = VT_CLOSE;
	MW(m, MSG_A) = h;
	ML(m) = 0;
	vt_call(m);
}

/* sur le bout maître (jusqu'au bout, pty plein : 2 ms d'attente) -> 0, < 0
 * si le pty est parti */
static long to_master(struct session *s, const u8 *p, u64 n)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	while (n) {
		MW(m, 0) = VT_WRITE;
		MW(m, MSG_A) = s->master;
		u64 k = n > FS_DATA_MAX ? FS_DATA_MAX : n;
		ML(m) = (u32)k;
		mem_move(m + MSG_DATA, p, k);
		long r = vt_call(m);
		if (r < 0)
			return r;
		if (!r) {
			korin_sys1(SYS_SLEEP, 2);
			continue;
		}
		p += r;
		n -= (u64)r;
	}
	return 0;
}

/* la taille donnée par NAWS au pty (VT_SWINSZ) */
static void naws_pty(struct session *s)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	s->naws = 0;
	MW(m, 0) = VT_SWINSZ;
	MW(m, MSG_A) = s->master;
	MW(m, MSG_B) = s->rows;
	MW(m, MSG_C) = s->cols;
	MW(m, MSG_D) = 0;
	ML(m) = 0;
	vt_call(m);
}

/* un pty, /bin/bash (sinon /bin/sh) sur son esclave -> pid (< 0 :
 * échec, rien d'ouvert) */
static long start_sh(struct session *s)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	long v = korin_sys2(SYS_SVC_OPEN, "vterm", 5000);
	if (v < 0)
		return -1;
	vtc = v;
	MW(m, 0) = VT_OPEN; /* le pty */
	ML(m) = 0;
	if (vt_call(m) < 0)
		return -1;
	long slave = MW(m, MSG_B);
	s->master = MW(m, MSG_A);
	long fds[3] = {slave, slave, slave}; /* sh : entrée, sortie, erreur = l'esclave */
	long pid = f_spawn_stdio("/bin/bash", 0, fds); /* bash s'il est sur la carte (comme le Terminal) */
	if (pid < 0)
		pid = f_spawn_stdio("/bin/sh", 0, fds);
	if (pid >= 0) {
		/* le shell : un groupe à lui, au premier plan du pty (comme login sous
		 * Unix : Ctrl+C va au shell et à ses commandes) */
		korin_sys2(SYS_SETPGID, pid, pid);
		MW(m, 0) = VT_SPGRP;
		MW(m, MSG_A) = s->master;
		MW(m, MSG_B) = pid;
		ML(m) = 0;
		vt_call(m);
	}
	MW(m, 0) = VT_CLOSE; /* notre copie de l'esclave : rendue */
	MW(m, MSG_A) = slave;
	ML(m) = 0;
	vt_call(m);
	if (pid < 0) {
		master_close(s);
		return -1;
	}
	s->pid = pid;
	ak_puts(m_session); /* « telnetd : session, shell pid N » */
	print_dec(pid);
	ak_puts("\n");
	return pid;
}

/* ---- telnet */

static void send_str(struct session *s, const char *t)
{
	send(s->sock, t, (u64)str_len(t));
}

/* traduit, puis CR LF */
static void send_tr(struct session *s, const char *t)
{
	send_str(s, lang_tr(t));
	send(s->sock, s_crlf, 2);
}

/* IAC cmd ECHO */
static void send_echo(struct session *s, u8 cmd)
{
	u8 b[3] = {IAC, cmd, TO_ECHO};
	send(s->sock, b, 3);
}

/* reçu -> octets pour sh dans out (négociations retirées, CR LF / CR NUL /
 * CR -> LF par le pty, IAC IP -> 3) */
static u64 telnet_in(const u8 *in, u64 n, u8 *out, struct session *s)
{
	u64 w = 0;
	long st = s->state; /* 0 données, 1 IAC, 2 option, 3 SB, 4 IAC dans SB */
	long cr = s->cr;
	while (n--) {
		u8 c = *in++;
		switch (st) {
		case 1:
			st = 0;
			if (c == IAC) { /* IAC IAC : un octet 255 (sans toucher au CR, comme l'asm) */
				out[w++] = c;
			} else if (c == T_IP) { /* interruption : Ctrl+C */
				out[w++] = 3;
			} else if (c == T_SB) {
				st = 3;
				s->sbn = 0;
			} else if (c >= T_WILL && c <= T_DONT) {
				st = 2; /* WILL / WONT / DO / DONT <option> */
			} /* NOP, GA, AYT... */
			continue;
		case 2:
			st = 0;
			continue;
		case 3:
			if (c == IAC) {
				st = 4;
				continue;
			}
			goto sbput;
		case 4:
			st = 3;
			if (c == IAC) /* IAC IAC : 255 dans la sous-négociation */
				goto sbput;
			if (c != T_SE)
				continue;
			st = 0; /* fin : NAWS ? largeur, hauteur (16 bits) */
			if (s->sbn >= 5 && s->sb[0] == TO_NAWS) {
				s->cols = s->sb[1] << 8 | s->sb[2];
				s->rows = s->sb[3] << 8 | s->sb[4];
				s->naws = 1;
			}
			continue;
		}
		if (c == IAC) { /* données */
			st = 1;
			continue;
		}
		if (cr) {
			cr = 0; /* après un CR : LF ou NUL avalés */
			if (c == 10 || c == 0)
				continue;
		}
		if (c == 13) /* CR (ICRNL du pty en fait LF ; un programme en brut voit CR) */
			cr = 1;
		out[w++] = c;
		continue;
	sbput:
		if (s->sbn < SB_MAX) /* gardée (16 octets au plus) */
			s->sb[s->sbn++] = c;
	}
	s->state = st;
	s->cr = cr;
	return w;
}

/* ---- mot de passe : verrou de rp_pw (rp_read, rp_equal : un thread à la fois) */

static void rp_take(void)
{
	long z = 0;
	while (!__atomic_compare_exchange_n(&rp_lock, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		z = 0;
		korin_sys0(SYS_YIELD);
	}
}

static void rp_give(void)
{
	__atomic_store_n(&rp_lock, 0, __ATOMIC_RELEASE);
}

/* -> longueur du mot de passe (0 : aucun), sous le verrou */
static long rp_check_empty(void)
{
	rp_take();
	long n = rp_read();
	rp_give();
	return n;
}

/* ---- threads */

/* un de ses threads s'arrête (le dernier libère la place) */
static void thread_end(struct session *s)
{
	__atomic_fetch_sub(&s->used, 1, __ATOMIC_ACQ_REL);
}

/* (thread) sortie de sh (bout maître) -> client (IAC doublé ; CR LF : ONLCR de vterm) */
static void to_client(struct session *s)
{
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	u8 out[1024];
	for (;;) {
		long h = s->master;
		if (!h)
			break;
		MW(m, 0) = VT_READ;
		MW(m, MSG_A) = h;
		MW(m, MSG_B) = FS_DATA_MAX;
		MW(m, MSG_C) = 0;
		ML(m) = 0;
		long r = vt_call(m);
		if (r == E_AGAIN) /* redemander */
			continue;
		if (r <= 0) /* sh a fini (ou le maître est rendu) */
			break;
		u64 w = 0;
		for (long i = 0; i < r; i++) {
			u8 c = m[MSG_DATA + i];
			if (c == IAC)
				out[w++] = c;
			out[w++] = c;
		}
		send(s->sock, out, w); /* (client parti : to_sh le voit) */
	}
	master_close(s);
	net_close(s->sock); /* fin de la connexion : to_sh sort de net_recv */
	ak_puts(m_ended);
	thread_end(s);
	thread_exit();
}

/* (thread) client -> entrée de sh */
NORETURN static void to_sh(struct session *s)
{
	u8 in[512], out[512];
	for (;;) {
		long n = net_recv(s->sock, (long)in, 512, 300000); /* 5 min de silence : un NOP */
		if (n == E_AGAIN) {
			u8 nop[2] = {IAC, T_NOP}; /* le client est-il là ? */
			if (send(s->sock, nop, 2) >= 0)
				continue;
			break;
		}
		if (n <= 0)
			break;
		u64 k = telnet_in(in, (u64)n, out, s); /* analyse telnet */
		if (s->naws) /* une taille donnée par le client */
			naws_pty(s);
		if (k && to_master(s, out, k) < 0)
			break;
	}
	master_close(s); /* fin : SIGHUP, fin de l'entrée de sh (to_client ferme la connexion) */
	thread_end(s);
	thread_exit();
}

/* (thread) le mot de passe, puis sh et les deux threads de copie (celui-ci
 * devient to_sh) */
static void auth(struct session *s)
{
	u8 buf[512], pend[512], line[128];
	long tries = TRIES;
	u64 rd = 0, end = 0; /* octets en attente dans pend */
	if (!rp_check_empty()) { /* aucun mot de passe : refus */
		send_tr(s, m_none);
		goto refuse;
	}
	for (;;) {
		send_str(s, lang_tr(m_pw)); /* « Mot de passe : », écho coupé */
		send_echo(s, T_WILL);
		u64 len = 0; /* longueur de la ligne */
		for (;;) {
			if (rd >= end) {
				long n = net_recv(s->sock, (long)buf, 512, 120000); /* 2 min pour taper */
				if (n <= 0)
					goto refuse; /* parti (E_AGAIN compris) */
				end = telnet_in(buf, (u64)n, pend, s);
				rd = 0;
				continue;
			}
			u8 c = pend[rd++];
			if (c == 10 || c == 13) /* (Entrée : CR, telnet_in le garde) */
				break;
			if (c == 8 || c == 127) { /* (client en mode caractère) */
				if (len)
					len--;
				continue;
			}
			if (c < 32) /* Ctrl+C, NUL... ignorés */
				continue;
			if (len < 128)
				line[len] = c;
			if (len < 200) /* (trop long : jamais le bon) */
				len++;
		}
		send_echo(s, T_WONT); /* écho rendu au client */
		send(s->sock, s_crlf, 2);
		rp_take(); /* (rp_pw commun aux threads) */
		if (!rp_read()) {
			rp_give(); /* retiré entre-temps */
			send_tr(s, m_none);
			goto refuse;
		}
		int ok = rp_equal(line, len);
		rp_give();
		memset(line, 0, sizeof line); /* la saisie effacée */
		if (ok)
			break;
		send_tr(s, m_wrong);
		korin_sys1(SYS_SLEEP, 2000);
		if (!--tries)
			goto refuse;
	}
	if (start_sh(s) < 0)
		goto refuse;
	send(s->sock, s_mode, 9); /* mode caractère : WILL ECHO, WILL SGA, DO NAWS */
	if (rd < end) /* tapé d'avance : à sh */
		to_master(s, pend + rd, end - rd);
	s->used = 2; /* deux threads : celui-ci, to_client */
	u8 *top = (u8 *)(((u64)s->stk2 + TSTACK) & ~15UL);
	if (korin_sys3(SYS_THREAD_CREATE, to_client, top, s) >= 0)
		to_sh(s);
	master_close(s); /* (sh voit la fin de son entrée) */
refuse:
	net_close(s->sock);
	ak_puts(m_refused);
	__atomic_store_n(&s->used, 0, __ATOMIC_RELEASE); /* place libre */
	thread_exit();
}

/* socket de la connexion : une session (place, sh, threads) */
static void session(long sock)
{
	int i;
	for (i = 0; i < SESS_MAX; i++) /* une place libre (libérée en fin de thread) */
		if (!__atomic_load_n(&sess[i].used, __ATOMIC_ACQUIRE))
			break;
	if (i == SESS_MAX) { /* plus de place */
		const char *t = lang_tr(m_full);
		send(sock, t, (u64)str_len(t));
		send(sock, s_crlf, 2);
		net_close(sock);
		return;
	}
	struct session *s = &sess[i];
	memset(s, 0, sizeof *s);
	s->sock = sock;
	s->used = 1; /* place prise (le thread auth) */
	/* les deux piles de cette place : allouées à sa première session, reprises
	 * par les suivantes (un refus ne perd rien) ; malloc ici, pas depuis les threads */
	for (int k = 0; k < 2; k++)
		if (!stacks[i][k] && !(stacks[i][k] = malloc(TSTACK)))
			goto ko;
	s->stk2 = stacks[i][1];
	u8 *top = (u8 *)(((u64)stacks[i][0] + TSTACK) & ~15UL);
	if (korin_sys3(SYS_THREAD_CREATE, auth, top, s) >= 0)
		return;
ko:
	net_close(sock);
	s->used = 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	lang_get(); /* (dictionnaires lus ici : malloc, avant les threads) */
	for (;;) {
		long sd = net_socket(SOCK_TCP);
		if (sd >= 0) { /* (netd pas encore là : attendre) */
			if (net_bind(sd, PORT) >= 0 && net_listen(sd, SESS_MAX) >= 0) {
				ak_puts(m_ready);
				for (;;) {
					long c = net_accept(sd, 3600000);
					if (c == E_AGAIN) /* encore */
						continue;
					if (c < 0) {
						if (c == E_PIPE) /* netd relancé : tout reprendre */
							break;
						continue;
					}
					session(c);
				}
			}
			net_close(sd);
		}
		korin_sys1(SYS_SLEEP, 2000);
	}
}
