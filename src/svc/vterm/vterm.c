/* vterm - pseudo-terminaux (docs/VTERM.md ; docs/TERMINAL-ET-FORK.md 2.1),
 * traduction de vterm.asm (même protocole : src/inc/vterm.inc, mêmes tables,
 * mêmes bizarreries, notées « comme l'asm »). Lancé par init2
 * (/etc/services.txt), relancé s'il tombe (les pty ouverts sont perdus).
 *
 *  * Un pty : un bout maître (le Terminal, telnetd) et un bout esclave (le
 *    programme) ; deux anneaux de PTY_RING octets : entrée (maître -> esclave,
 *    après la discipline de ligne) et sortie (esclave -> maître, après OPOST) ;
 *    un état termios, une taille, un groupe de premier plan.
 *  * Bouts à plusieurs propriétaires, comme les tubes de vfs : couples
 *    (poignée, pid) ; un bout compte tant qu'il a un propriétaire. Avis de
 *    naissance : le fils devient propriétaire des bouts de vterm que le père
 *    lui donne en entrée / sortie (0 : ceux du père, gardés ici par pid) ;
 *    avis de fork : tous les bouts du père ; avis de fin : le pid les quitte.
 *    Plus de maître : l'esclave lit la fin, SIGHUP au groupe de premier plan.
 *    Plus d'esclave : le maître lit la fin.
 *  * Lectures : ce qui est là ; sinon réponse différée (WAIT_MS au plus, puis
 *    E_AGAIN : le client redemande), VMIN / VTIME en mode brut. Un signal de la
 *    discipline (ISIG) rend E_INTR aux lectures en attente de l'esclave.
 *  * Discipline (entrée, écrite par le maître) : ISTRIP, IGNCR, ICRNL, INLCR ;
 *    ISIG ; ICANON : ligne éditée (VERASE et 0x08, VWERASE, VKILL, VREPRINT,
 *    VLNEXT), rendue à la fin de ligne ou à VEOF ; ECHO, ECHOE, ECHOK, ECHOKE,
 *    ECHONL, ECHOCTL. Sortie (écrite par l'esclave) : OPOST, ONLCR, OCRNL.
 *  * Contrôle des tâches (côté esclave, s'il y a un groupe de premier plan) :
 *    SIGTTIN, SIGTTOU (SIG_SEND | SIGF_TTY : le noyau décide), seulement pour
 *    la session du pty (PT_SID, TIOCSCTTY). */
#include "korinrt.h"

/* src/inc/vterm.inc */
#define VT_TAG 0x40000000
#define VT_OPEN 1
#define VT_READ 2
#define VT_WRITE 3
#define VT_CLOSE 4
#define VT_TCGETS 5
#define VT_TCSETS 6
#define VT_GWINSZ 7
#define VT_SWINSZ 8
#define VT_GPGRP 9
#define VT_SPGRP 10
#define VT_INFO 11
#define VT_FLUSH 12
#define VT_SLAVE 13
#define VT_SCTTY 14
#define VT_NOTTY 15
#define VT_GSID 16
#define VT_NOWAIT 1
#define VT_E_IO -5
#define VT_E_INTR -4
#define VT_E_NOTTY -25
#define VT_E_PERM -1
#define VTT_SIZE 48

#define VTC_INTR 0
#define VTC_QUIT 1
#define VTC_ERASE 2
#define VTC_KILL 3
#define VTC_EOF 4
#define VTC_TIME 5
#define VTC_MIN 6
#define VTC_SUSP 10
#define VTC_EOL 11
#define VTC_REPRINT 12
#define VTC_WERASE 14
#define VTC_LNEXT 15
#define VTI_ISTRIP 0x20
#define VTI_INLCR 0x40
#define VTI_IGNCR 0x80
#define VTI_ICRNL 0x100
#define VTI_IUTF8 0x4000
#define VTO_OPOST 1
#define VTO_ONLCR 4
#define VTO_OCRNL 8
#define VTL_ISIG 1
#define VTL_ICANON 2
#define VTL_ECHO 8
#define VTL_ECHOE 0x10
#define VTL_ECHOK 0x20
#define VTL_ECHONL 0x40
#define VTL_NOFLSH 0x80
#define VTL_TOSTOP 0x100
#define VTL_ECHOCTL 0x200
#define VTL_ECHOKE 0x800
#define VTL_IEXTEN 0x8000

/* src/inc/armpi.inc, syscalls.inc, fs.inc */
#define SVF_ENDS 1
#define SVF_BIRTHS 2
#define SVC_OP_END 0xFFFF
#define SVC_OP_BIRTH 0xFFFE
#define SVC_OP_SIGNAL 0xFFFD
#define SVC_OP_FORK 0xFFFC
#define SIGF_TTY_MASK 0x10000
#define GETPGID_SID_MASK (1L << 62)
#define FS_DATA_MAX 464

#define PTY_MAX 16
#define PTY_RING 4096 /* (puissance de 2) */
#define LINE_MAX 256  /* ligne en édition (mode canonique) */
#define OWN_MAX 512   /* couples (poignée, pid) */
#define PS_MAX 128    /* entrées / sorties de vterm par pid */
#define WAIT_MAX 32
#define WAIT_MS 1000  /* (court : le client regarde ses signaux entre deux essais) */
#define EMFILE -24

#define SIGHUP_ 1
#define SIGINT_ 2
#define SIGQUIT_ 3
#define SIGTSTP_ 18
#define SIGCONT_ 19
#define SIGTTIN_ 21
#define SIGTTOU_ 22
#define SIGWINCH_ 28

struct termios {
	u32 iflag, oflag, cflag, lflag;
	u8 cc[32];
};

struct pty {          /* 128 octets, comme PT_* */
	u32 used;
	u32 n[2];     /* propriétaires du maître, de l'esclave (PT_NM, PT_NS) */
	u32 pgrp;     /* groupe de premier plan (0 : aucun) */
	struct termios t;
	u32 rows, cols, xpix, ypix;
	u32 ih, it;   /* anneau d'entrée : tête, queue */
	u32 oh, ot;   /* anneau de sortie */
	u32 ln;       /* octets de la ligne en édition */
	u32 mark;     /* position d'une marque dans l'entrée */
	u32 markf;    /* 0 aucune, 1 fin (VEOF), 2 limite */
	u32 lnext;    /* 1 : le prochain octet est pris tel quel */
	u32 sid;      /* session dont c'est le terminal de contrôle (0 : aucune) */
	u32 pad[3];
};

struct owner {
	u32 h, pid;
};

struct stdio_ent { /* PS_SIZE = 32 : pid, poignées 0, 1, 2 */
	u64 pid;
	u64 h[3];
};

struct wait {
	u64 req, h, bytes, limit, pid, vtime; /* vtime 1 : échéance de VTIME (rendre ce qui est là) */
};

static long canal;
static long current_req;
static u32 msg_len;
static u8 msg[MSG_SIZE] __attribute__((aligned(16)));
static u8 rmsg[MSG_SIZE] __attribute__((aligned(16)));
static struct pty ptys[PTY_MAX];
static struct owner owners[OWN_MAX];
static struct stdio_ent stdios[PS_MAX];
static struct wait waits[WAIT_MAX];
static u8 lines[PTY_MAX][LINE_MAX];
static u8 in_data[PTY_MAX][PTY_RING];
static u8 out_data[PTY_MAX][PTY_RING];

/* termios par défaut : ICRNL IUTF8 ; OPOST ONLCR ; CS8 CREAD ; ISIG ICANON
 * ECHO ECHOE ECHOK ECHOCTL ECHOKE IEXTEN ; ^C ^\ DEL ^U ^D, VMIN 1, ^Z, ^R ^W ^V */
static const struct termios termios_default = {
	VTI_ICRNL | VTI_IUTF8, VTO_OPOST | VTO_ONLCR, 0xB0,
	VTL_ISIG | VTL_ICANON | VTL_ECHO | VTL_ECHOE | VTL_ECHOK | VTL_ECHOCTL | VTL_ECHOKE | VTL_IEXTEN,
	{3, 0x1C, 0x7F, 0x15, 4, 0, 1, 0, 0x11, 0x13, 0x1A, 0, 0x12, 0x0F, 0x17, 0x16},
};

#define M64(off) (*(u64 *)(msg + (off)))
#define M32(off) (*(u32 *)(msg + (off)))

/* ---- poignées, propriétaires */

/* poignée -> pty (0 : invalide ou libre), *num, *side (0 maître, 1 esclave) */
static struct pty *h_pty(u64 h, u64 *num, u64 *side)
{
	u64 x = h - (VT_TAG + 1);
	if (x >= PTY_MAX * 2)
		return 0;
	struct pty *p = &ptys[x >> 1];
	if (!p->used)
		return 0;
	*num = x >> 1;
	*side = x & 1;
	return p;
}

static u64 pty_h(u64 num, u64 side)
{
	return num * 2 + side + VT_TAG + 1;
}

static struct owner *own_find(u32 h, u32 pid)
{
	for (int i = 0; i < OWN_MAX; i++)
		if (owners[i].h == h && owners[i].pid == pid)
			return &owners[i];
	return 0;
}

/* un propriétaire de plus (rien s'il l'est déjà) -> 1, 0 si la table est pleine */
static int own_add(u64 h, u64 pid)
{
	if (own_find((u32)h, (u32)pid))
		return 1;
	for (int i = 0; i < OWN_MAX; i++) {
		if (owners[i].h)
			continue;
		owners[i].h = (u32)h;
		owners[i].pid = (u32)pid;
		u64 x = h - (VT_TAG + 1); /* compteur du côté */
		ptys[(x >> 1) & (PTY_MAX - 1)].n[x & 1]++;
		return 1;
	}
	return 0;
}

/* pty s'il est à ce pid (0 sinon) */
static struct pty *h_check(u64 h, u64 pid, u64 *num, u64 *side)
{
	struct pty *p = h_pty(h, num, side);
	if (p && !own_find((u32)h, (u32)pid))
		return 0;
	return p;
}

static struct stdio_ent *ps_find(u64 pid)
{
	for (int i = 0; i < PS_MAX; i++)
		if (stdios[i].pid == pid)
			return &stdios[i];
	return 0;
}

/* son entrée, créée (0 : table pleine) */
static struct stdio_ent *ps_get(u64 pid)
{
	struct stdio_ent *e = ps_find(pid);
	if (e)
		return e;
	e = ps_find(0); /* une place libre (pid 0) */
	if (e) {
		e->pid = pid;
		e->h[0] = e->h[1] = e->h[2] = 0;
	}
	return e;
}

static void pty_serve(u64 num);

static void wait_reply(struct wait *w, long res, u32 n)
{
	*(u64 *)rmsg = (u64)res;
	*(u32 *)(rmsg + MSG_LEN) = n;
	u64 req = w->req;
	w->req = 0;
	korin_sys2(SYS_SVC_REPLY, req, rmsg);
}

/* les lectures en attente sur les bouts du pty num reçoivent code ; both :
 * les deux côtés, sinon l'esclave seulement */
static void pty_fail_side(u64 num, long code, int both)
{
	u64 m = pty_h(num, 0), s = m + 1;
	for (int i = 0; i < WAIT_MAX; i++) {
		struct wait *w = &waits[i];
		if (!w->req)
			continue;
		if (w->h == s || (both && w->h == m))
			wait_reply(w, code, 0);
	}
}

/* au groupe de premier plan (rien s'il n'y en a pas) */
static void pty_signal(struct pty *p, long sig)
{
	if (p->pgrp)
		korin_sys3(SYS_SIG_SEND, -(long)p->pgrp, 0, sig);
}

/* côté qui n'a plus personne : l'autre côté lit la fin ; plus de maître :
 * SIGHUP au groupe ; plus personne : pty libéré */
static void side_closed(struct pty *p, u64 num, u64 side)
{
	if (side == 0 && p->n[1])
		pty_signal(p, SIGHUP_);
	pty_serve(num);
	if (p->n[0] | p->n[1])
		return;
	pty_fail_side(num, E_BADF, 1);
	memset(p, 0, sizeof *p);
}

/* le pid quitte le bout ; ses lectures en attente dessus : E_BADF ; le dernier
 * parti, ce côté est fermé */
static void own_drop(u64 h, u64 pid)
{
	struct owner *o = own_find((u32)h, (u32)pid);
	if (!o)
		return;
	o->h = 0;
	o->pid = 0;
	for (int i = 0; i < WAIT_MAX; i++) {
		struct wait *w = &waits[i];
		if (w->req && w->h == h && w->pid == pid)
			wait_reply(w, E_BADF, 0);
	}
	struct stdio_ent *e = ps_find(pid); /* plus dans ses entrées / sorties */
	if (e)
		for (int i = 0; i < 3; i++)
			if (e->h[i] == h)
				e->h[i] = 0;
	u64 num, side;
	struct pty *p = h_pty(h, &num, &side);
	if (!p)
		return;
	if (p->n[side])
		p->n[side]--;
	if (p->n[side])
		return;
	side_closed(p, num, side);
}

/* ---- avis du noyau */

static void pid_gone(u64 pid)
{
	for (int i = 0; i < OWN_MAX; i++)
		if (owners[i].h && owners[i].pid == (u32)pid)
			own_drop(owners[i].h, pid);
	struct stdio_ent *e = ps_find(pid);
	if (e)
		memset(e, 0, sizeof *e);
}

/* fils, père (msg : c, d, données = entrée, sortie, erreur données ; 0 : celle
 * du père, -1 : aucune) : le fils devient propriétaire des bouts de vterm qu'il reçoit */
static void birth(u64 child, u64 parent)
{
	for (int i = 0; i < 3; i++) {
		u64 h = i < 2 ? M64(MSG_C + i * 8) : M64(MSG_DATA);
		if (h == (u64)-1)
			continue;
		if (!h) { /* 0 : celle du père */
			struct stdio_ent *e = ps_find(parent);
			if (!e)
				continue;
			h = e->h[i];
			if (!h)
				continue;
		}
		if (!(h & VT_TAG))
			continue;
		u64 num, side;
		if (!h_check(h, parent, &num, &side)) /* encore au père ? */
			continue;
		if (!own_add(h, child))
			continue;
		struct stdio_ent *e = ps_get(child);
		if (!e)
			continue;
		e->h[i] = h;
	}
}

/* fork : le fils tient tous les bouts du père (mêmes poignées) et ses entrées / sorties */
static void fork_copy(u64 child, u64 parent)
{
	for (int i = 0; i < OWN_MAX; i++)
		if (owners[i].h && owners[i].pid == (u32)parent)
			own_add(owners[i].h, child);
	struct stdio_ent *pe = ps_find(parent);
	if (!pe)
		return;
	struct stdio_ent *ce = ps_get(child);
	if (!ce)
		return;
	ce->h[0] = pe->h[0];
	ce->h[1] = pe->h[1];
	ce->h[2] = pe->h[2];
}

/* ---- anneaux */

static u32 in_count(struct pty *p)
{
	return (p->ih - p->it) & (PTY_RING - 1);
}

static u32 out_count(struct pty *p)
{
	return (p->oh - p->ot) & (PTY_RING - 1);
}

static int in_put(struct pty *p, u64 num, u8 b)
{
	u32 nh = (p->ih + 1) & (PTY_RING - 1);
	if (nh == p->it)
		return 0;
	in_data[num][p->ih] = b;
	p->ih = nh;
	return 1;
}

static int out_put(struct pty *p, u64 num, u8 b)
{
	u32 nh = (p->oh + 1) & (PTY_RING - 1);
	if (nh == p->ot)
		return 0;
	out_data[num][p->oh] = b;
	p->oh = nh;
	return 1;
}

/* sortie avec OPOST (ONLCR, OCRNL) -> 1, 0 si la place manque (rien écrit) */
static int opost_put(struct pty *p, u64 num, u8 b)
{
	u32 room = PTY_RING - 1 - out_count(p);
	u32 of = p->t.oflag;
	if (of & VTO_OPOST) {
		if (b == 10) {
			if (of & VTO_ONLCR) { /* LF -> CR LF */
				if (room < 2)
					return 0;
				out_put(p, num, 13);
				return out_put(p, num, b);
			}
		} else if (b == 13 && (of & VTO_OCRNL)) {
			b = 10; /* CR -> LF */
		}
	}
	if (!room)
		return 0;
	return out_put(p, num, b);
}

static void in_flush(struct pty *p)
{
	p->ih = p->it = 0;
	p->ln = 0;
	p->markf = 0;
	p->lnext = 0;
}

/* ---- discipline de ligne : un octet écrit par le maître */

/* à l'écho : ECHOCTL (^X) pour un octet de contrôle autre que TAB, LF */
static void echo_b(struct pty *p, u64 num, u8 b)
{
	if ((p->t.lflag & VTL_ECHOCTL) && (b == 0x7F || (b < 0x20 && b != 9 && b != 10))) {
		opost_put(p, num, '^');
		b ^= 0x40;
	}
	opost_put(p, num, b);
}

static void echo_s(struct pty *p, u64 num, const char *s)
{
	while (*s)
		opost_put(p, num, (u8)*s++);
}

/* efface le dernier caractère de la ligne (UTF-8) ; visual : ECHOE (BS espace
 * BS, deux fois pour un ^X) -> 1 s'il y en avait un */
static int erase_one(struct pty *p, u64 num, int visual)
{
	u32 ln = p->ln;
	if (!ln)
		return 0;
	u8 c;
	for (;;) {
		ln--;
		c = lines[num][ln];
		if (!ln || (c & 0xC0) != 0x80)
			break;
	}
	p->ln = ln;
	if (visual) {
		echo_s(p, num, "\b \b");
		if ((p->t.lflag & VTL_ECHOCTL) && c < 0x20 && c != 9) /* ^X tient deux colonnes */
			echo_s(p, num, "\b \b");
	}
	return 1;
}

/* la ligne en édition passe dans l'anneau d'entrée -> 1, 0 si la place manque */
static int line_push(struct pty *p, u64 num)
{
	u32 room = PTY_RING - 1 - in_count(p);
	if (room < p->ln)
		return 0;
	for (u32 i = 0; i < p->ln; i++)
		in_put(p, num, lines[num][i]);
	p->ln = 0;
	return 1;
}

/* octet ajouté à la ligne (s'il y a la place : sinon perdu) ; écho */
static void line_add(struct pty *p, u64 num, u8 b)
{
	if (p->ln >= LINE_MAX - 1)
		return;
	lines[num][p->ln++] = b;
	if (p->t.lflag & VTL_ECHO)
		echo_b(p, num, b);
}

static int is_blank(u8 c)
{
	return c == ' ' || c == 9;
}

/* octet écrit par le maître : la discipline -> 1 pris, 0 : pas de place */
static int ld_input(struct pty *p, u64 num, u8 b)
{
	u32 ifl = p->t.iflag, lf = p->t.lflag;
	const u8 *cc = p->t.cc;
	if (in_count(p) >= PTY_RING - LINE_MAX - 8) /* place : une ligne entière au moins */
		return 0;
	if (p->lnext) { /* après VLNEXT : tel quel */
		p->lnext = 0;
		if (!(lf & VTL_ICANON))
			goto raw;
		if (p->ln && (lf & VTL_ECHO)) /* (le ^V affiché : effacé) */
			echo_s(p, num, "\b \b");
		line_add(p, num, b);
		return 1;
	}
	if (ifl & VTI_ISTRIP)
		b &= 0x7F;
	if (b == 13) {
		if (ifl & VTI_IGNCR)
			return 1;
		if (ifl & VTI_ICRNL)
			b = 10;
	} else if (b == 10 && (ifl & VTI_INLCR)) {
		b = 13;
	}
	if (lf & VTL_ISIG) {
		long sig = 0;
		if (cc[VTC_INTR] && cc[VTC_INTR] == b)
			sig = SIGINT_;
		else if (cc[VTC_QUIT] && cc[VTC_QUIT] == b)
			sig = SIGQUIT_;
		else if (cc[VTC_SUSP] && cc[VTC_SUSP] == b)
			sig = SIGTSTP_;
		if (sig) {
			if (!(lf & VTL_NOFLSH))
				in_flush(p);
			if (lf & VTL_ECHO) /* ^C */
				echo_b(p, num, b);
			pty_signal(p, sig);
			/* lecteurs de l'esclave : E_INTR ; E_AGAIN pour SIGTSTP (arrêtés,
			 * ils redemandent une fois repris) */
			pty_fail_side(num, sig == SIGTSTP_ ? E_AGAIN : VT_E_INTR, 0);
			return 1;
		}
	}
	if (!(lf & VTL_ICANON))
		goto raw;
	if ((lf & VTL_IEXTEN) && cc[VTC_LNEXT] && cc[VTC_LNEXT] == b) {
		p->lnext = 1;
		if (lf & VTL_ECHO)
			echo_s(p, num, "^\b"); /* ^ puis BS, comme Linux */
		return 1;
	}
	if (cc[VTC_ERASE] == b || b == 8) { /* (Retour arrière en ^H aussi) */
		erase_one(p, num, (lf & VTL_ECHO) && (lf & VTL_ECHOE));
		return 1;
	}
	if ((lf & VTL_IEXTEN) && cc[VTC_WERASE] && cc[VTC_WERASE] == b) {
		while (p->ln && is_blank(lines[num][p->ln - 1])) /* les blancs, puis le mot */
			erase_one(p, num, (lf & VTL_ECHO) != 0);
		while (p->ln && !is_blank(lines[num][p->ln - 1]))
			erase_one(p, num, (lf & VTL_ECHO) != 0);
		return 1;
	}
	if (cc[VTC_KILL] && cc[VTC_KILL] == b) {
		if (lf & VTL_ECHO) {
			if (lf & VTL_ECHOKE) { /* ECHOKE : tout effacé à l'écran */
				while (erase_one(p, num, 1))
					;
				return 1;
			}
			echo_b(p, num, b); /* sinon : ^U, puis LF (ECHOK) */
			if (lf & VTL_ECHOK)
				echo_s(p, num, "\n");
		}
		p->ln = 0;
		return 1;
	}
	if ((lf & VTL_IEXTEN) && cc[VTC_REPRINT] && cc[VTC_REPRINT] == b) {
		if (!(lf & VTL_ECHO))
			return 1;
		echo_b(p, num, b); /* ^R, LF, la ligne */
		echo_s(p, num, "\n");
		for (u32 i = 0; i < p->ln; i++)
			echo_b(p, num, lines[num][i]);
		return 1;
	}
	if (cc[VTC_EOF] && cc[VTC_EOF] == b) {
		u32 n = p->ln;
		if (!line_push(p, num))
			return 0;
		p->mark = p->ih; /* marque : fin (ligne vide) ou limite */
		p->markf = n ? 2 : 1;
		return 1;
	}
	if (b == 10 || (cc[VTC_EOL] && cc[VTC_EOL] == b)) {
		u32 l = p->ln; /* fin de ligne : ajoutée, la ligne part */
		if (l >= LINE_MAX - 1)
			l = LINE_MAX - 2; /* (pleine : la fin de ligne remplace le dernier octet) */
		lines[num][l] = b;
		p->ln = l + 1;
		if ((lf & VTL_ECHO) || (b == 10 && (lf & VTL_ECHONL)))
			echo_b(p, num, b);
		line_push(p, num); /* (pas de place : la ligne reste, l'octet compte pris, comme l'asm) */
		return 1;
	}
	line_add(p, num, b);
	return 1;
raw: /* mode brut : tel quel */
	if (!in_put(p, num, b))
		return 0;
	if (lf & VTL_ECHO)
		opost_put(p, num, b);
	return 1;
}

/* ---- lectures */

/* -> octets, 0 fin, -1 : pas prêt (attendre) ; deadline : échéance de VTIME */
static long pty_take(struct pty *p, u64 num, u64 side, u8 *dst, u64 want, int deadline)
{
	if (!side) { /* maître : la sortie du programme */
		u32 n = out_count(p);
		if (!n)
			return p->n[1] ? -1 : 0; /* plus d'esclave : fin */
		if (n < want)
			want = n;
		u32 t = p->ot;
		for (u64 i = 0; i < want; i++) {
			dst[i] = out_data[num][t];
			t = (t + 1) & (PTY_RING - 1);
		}
		p->ot = t;
		return (long)want;
	}
	u32 lf = p->t.lflag;
	if (lf & VTL_ICANON) {
		if (p->markf && p->mark == p->it) { /* une marque au début ? */
			u32 f = p->markf;
			p->markf = 0;
			if (f == 1)
				return 0; /* VEOF sur une ligne vide */
		}
		if (!in_count(p))
			return p->n[0] ? -1 : 0; /* plus de maître : fin */
		/* jusqu'à la fin de ligne, ou la marque */
		u32 t = p->it, h = p->ih, mf = p->markf, mk = p->mark;
		u8 eol = p->t.cc[VTC_EOL];
		u64 i = 0;
		while (i < want && t != h) {
			if (mf && t == mk) {
				if (mf == 2) /* limite (VEOF) : effacée, on s'arrête */
					p->markf = 0;
				break;
			}
			u8 c = in_data[num][t];
			dst[i++] = c;
			t = (t + 1) & (PTY_RING - 1);
			if (c == 10 || (eol && c == eol))
				break;
		}
		p->it = t;
		if (mf == 2 && t == mk) /* marque atteinte et passée ? */
			p->markf = 0;
		return (long)i;
	}
	u32 n = in_count(p); /* VMIN, VTIME */
	u64 vmin = p->t.cc[VTC_MIN];
	u8 vtime = p->t.cc[VTC_TIME];
	if (!n) {
		if (!p->n[0] || deadline)
			return 0;
		if (!(vmin | vtime))
			return 0; /* VMIN 0, VTIME 0 : 0 tout de suite */
		return -1;
	}
	if (vmin && !deadline) {
		if (vmin > want) /* VMIN au plus ce qui est demandé */
			vmin = want;
		if (n < vmin)
			return -1;
	}
	if (n < want)
		want = n;
	u32 t = p->it;
	for (u64 i = 0; i < want; i++) {
		dst[i] = in_data[num][t];
		t = (t + 1) & (PTY_RING - 1);
	}
	p->it = t;
	return (long)want;
}

/* sert la lecture si elle est prête (1 : répondue) ; deadline : échéance */
static int wait_try(struct wait *w, int deadline)
{
	u64 num, side;
	struct pty *p = h_pty(w->h, &num, &side);
	if (!p) {
		wait_reply(w, E_BADF, 0);
		return 1;
	}
	long n = pty_take(p, num, side, rmsg + MSG_DATA, w->bytes, (int)(w->vtime & (u64)deadline));
	if (n < 0)
		return 0;
	wait_reply(w, n, (u32)n);
	return 1;
}

/* sert les lectures qui attendent le pty num */
static void pty_serve(u64 num)
{
	u64 m = pty_h(num, 0); /* maître, esclave = m + 1 */
	for (int i = 0; i < WAIT_MAX; i++)
		if (waits[i].req && waits[i].h - m <= 1)
			wait_try(&waits[i], 0);
}

/* échéances : VTIME (rendre ce qui est là), sinon E_AGAIN (le client redemande) */
static void waits_tick(void)
{
	u64 now = (u64)korin_sys0(SYS_TIME_MS);
	for (int i = 0; i < WAIT_MAX; i++) {
		struct wait *w = &waits[i];
		if (!w->req || w->limit > now)
			continue;
		if (!wait_try(w, 1))
			wait_reply(w, E_AGAIN, 0);
	}
}

static int waits_any(void)
{
	for (int i = 0; i < WAIT_MAX; i++)
		if (waits[i].req)
			return 1;
	return 0;
}

/* ---- contrôle des tâches */

/* l'appelant (msg) est-il en arrière-plan dans la session du pty ? -> 0 :
 * continuer ; 1 : signal envoyé à son groupe (rendre E_INTR) ; 2 : rendre E_IO
 * (SIGTTIN ignoré ou masqué, groupe orphelin) */
static int bg_check(struct pty *p, long sig)
{
	u64 pid = M32(MSG_PID);
	u32 pg = p->pgrp;
	if (!pg)
		return 0;
	long g = korin_sys1(SYS_GETPGID, pid);
	if (g < 0 || (u32)g == pg)
		return 0;
	u32 sid = p->sid;
	if (sid) { /* terminal d'une autre session : rien */
		long s = korin_sys1(SYS_GETPGID, pid | GETPGID_SID_MASK);
		if (s != (long)sid)
			return 0;
	}
	long r = korin_sys3(SYS_SIG_SEND, pid, 0, SIGF_TTY_MASK | sig);
	if (r == 0)
		return 1;
	if (r == 2)
		return 2;
	if (sig == SIGTTIN_) /* ignoré ou masqué : SIGTTOU laisse faire, SIGTTIN rend E_IO */
		return 2;
	return 0;
}

/* changement par l'esclave : SIGTTOU s'il est en arrière-plan */
static int bg_ttou(struct pty *p, u64 side)
{
	return side ? bg_check(p, SIGTTOU_) : 0;
}

static long bg_result(int r)
{
	return r == 1 ? VT_E_INTR : VT_E_IO;
}

/* ---- requêtes (msg, au nom de MSG_PID) -> résultat ; *deferred = 1 : différée */

static long op_open(u64 pid)
{
	int num;
	for (num = 0; num < PTY_MAX; num++) /* un pty libre */
		if (!ptys[num].used)
			break;
	if (num == PTY_MAX)
		return EMFILE;
	struct pty *p = &ptys[num];
	memset(p, 0, sizeof *p);
	p->used = 1;
	p->t = termios_default;
	p->rows = 24;
	p->cols = 80;
	u64 m = pty_h(num, 0);
	if (!own_add(m, pid)) {
		p->used = 0;
		return EMFILE;
	}
	if (!own_add(m + 1, pid)) {
		struct owner *o = own_find((u32)m, (u32)pid);
		if (o) { /* (le compteur du maître n'est pas rendu, comme l'asm) */
			o->h = 0;
			o->pid = 0;
		}
		p->used = 0;
		return EMFILE;
	}
	M64(MSG_A) = m;
	M64(MSG_B) = m + 1;
	M64(MSG_C) = (u64)num;
	return 0;
}

static long op_slave(u64 pid)
{
	u64 num = M64(MSG_B); /* open de /dev/pts/N */
	if (num >= PTY_MAX || !ptys[num].used || !ptys[num].n[0]) /* (sans maître : il ne revivra pas) */
		return E_NOENT;
	u64 h = pty_h(num, 1);
	if (!own_add(h, pid))
		return EMFILE;
	M64(MSG_A) = h;
	return 0;
}

static long ops(int *deferred)
{
	u64 op = M64(0);
	u64 pid = M32(MSG_PID);
	msg_len = M32(MSG_LEN); /* (gardée : la réponse la remet à 0) */
	M32(MSG_LEN) = 0;
	*deferred = 0;
	if (op == VT_OPEN)
		return op_open(pid);
	if (op == VT_SLAVE)
		return op_slave(pid);
	u64 h = M64(MSG_A); /* les autres : un bout de l'appelant */
	u64 num, side;
	struct pty *p = h_check(h, pid, &num, &side);
	if (!p)
		return E_BADF;
	int r;
	switch (op) {
	case VT_READ: {
		if (side) { /* esclave : en arrière-plan ? */
			r = bg_check(p, SIGTTIN_);
			if (r == 1) /* (SIGTTIN envoyé : E_AGAIN, le client redemande une fois repris) */
				return E_AGAIN;
			if (r)
				return bg_result(r);
		}
		u64 want = M64(MSG_B);
		if (want > FS_DATA_MAX)
			want = FS_DATA_MAX;
		if (!want)
			return 0;
		long n = pty_take(p, num, side, msg + MSG_DATA, want, 0);
		if (n >= 0) {
			M32(MSG_LEN) = (u32)n;
			return n;
		}
		if (M64(MSG_C) & VT_NOWAIT)
			return E_AGAIN;
		struct wait *w = 0;
		for (int i = 0; i < WAIT_MAX; i++)
			if (!waits[i].req) {
				w = &waits[i];
				break;
			}
		if (!w)
			return E_AGAIN; /* (table pleine : le client redemande) */
		w->req = (u64)current_req;
		w->h = h;
		w->bytes = want;
		w->pid = pid;
		w->vtime = 0;
		u64 now = (u64)korin_sys0(SYS_TIME_MS);
		w->limit = now + WAIT_MS;
		/* esclave en brut avec VTIME : son échéance */
		if (side && !(p->t.lflag & VTL_ICANON) && p->t.cc[VTC_TIME]) {
			w->limit = now + (u64)p->t.cc[VTC_TIME] * 100;
			w->vtime = 1;
		}
		*deferred = 1;
		return 0;
	}
	case VT_WRITE: {
		u64 n = msg_len, i;
		if (n > FS_DATA_MAX)
			n = FS_DATA_MAX;
		if (!side) { /* maître : la discipline, octet par octet */
			for (i = 0; i < n; i++)
				if (!ld_input(p, num, msg[MSG_DATA + i]))
					break;
			pty_serve(num);
			return (long)i;
		}
		if (!p->n[0]) /* esclave : plus de maître, E_IO */
			return VT_E_IO;
		if (p->t.lflag & VTL_TOSTOP) { /* TOSTOP : en arrière-plan ? */
			r = bg_check(p, SIGTTOU_);
			if (r)
				return bg_result(r);
		}
		for (i = 0; i < n; i++)
			if (!opost_put(p, num, msg[MSG_DATA + i]))
				break;
		pty_serve(num);
		return (long)i;
	}
	case VT_CLOSE:
		own_drop(h, pid);
		return 0;
	case VT_TCGETS:
		memcpy(msg + MSG_DATA, &p->t, VTT_SIZE);
		M32(MSG_LEN) = VTT_SIZE;
		return 0;
	case VT_TCSETS: {
		r = bg_ttou(p, side);
		if (r)
			return bg_result(r);
		if (msg_len < VTT_SIZE)
			return E_INVAL;
		u32 old = p->t.lflag; /* ancien mode */
		memcpy(&p->t, msg + MSG_DATA, VTT_SIZE);
		if (M64(MSG_B) == 2) /* TCSAFLUSH : entrée vidée */
			in_flush(p);
		if ((old & VTL_ICANON) && !(p->t.lflag & VTL_ICANON)) {
			line_push(p, num); /* canonique -> brut : la ligne en édition passe à l'anneau */
			p->markf = 0;
		}
		pty_serve(num);
		return 0;
	}
	case VT_GWINSZ:
		M64(MSG_A) = p->rows;
		M64(MSG_B) = p->cols;
		M64(MSG_C) = p->xpix;
		M64(MSG_D) = p->ypix;
		return 0;
	case VT_SWINSZ: {
		u32 rows = (u32)M64(MSG_B), cols = (u32)M64(MSG_C);
		u64 d = M64(MSG_D);
		u32 orows = p->rows, ocols = p->cols;
		p->rows = rows;
		p->cols = cols;
		p->xpix = d & 0xFFFF;
		p->ypix = (d >> 16) & 0xFFFF;
		if (rows != orows || cols != ocols)
			pty_signal(p, SIGWINCH_);
		return 0;
	}
	case VT_GPGRP:
		M64(MSG_A) = p->pgrp;
		return 0;
	case VT_SPGRP:
		r = bg_ttou(p, side);
		if (r)
			return bg_result(r);
		if ((long)M64(MSG_B) <= 0)
			return E_INVAL;
		p->pgrp = (u32)M64(MSG_B);
		return 0;
	case VT_SCTTY: { /* TIOCSCTTY : meneur de session seulement ; le pty ne doit pas être à une autre session */
		long s = korin_sys1(SYS_GETPGID, pid | GETPGID_SID_MASK);
		if (s != (long)pid)
			return VT_E_PERM;
		if (p->sid && p->sid != (u32)pid)
			return VT_E_PERM;
		p->sid = (u32)pid;
		long g = korin_sys1(SYS_GETPGID, pid); /* son groupe au premier plan */
		if (g >= 0)
			p->pgrp = (u32)g;
		return 0;
	}
	case VT_NOTTY: { /* TIOCNOTTY : le meneur rend le terminal */
		long s = korin_sys1(SYS_GETPGID, pid | GETPGID_SID_MASK);
		if (!p->sid || (u32)s != p->sid)
			return VT_E_NOTTY;
		if (s != (long)pid)
			return 0;
		pty_signal(p, SIGHUP_); /* (comme Linux : SIGHUP, SIGCONT au groupe de premier plan) */
		pty_signal(p, SIGCONT_);
		p->sid = 0;
		p->pgrp = 0;
		return 0;
	}
	case VT_GSID:
		M64(MSG_A) = p->sid;
		return 0;
	case VT_INFO: {
		M64(MSG_A) = side + 1;
		M64(MSG_B) = num;
		u32 count, other;
		if (!side) {
			count = out_count(p);
			other = p->n[1];
		} else {
			count = in_count(p);
			other = p->n[0];
			if ((p->t.lflag & VTL_ICANON) && p->markf == 1) /* (fin marquée par VEOF : vue aussi) */
				other = 0;
		}
		M64(MSG_C) = count;
		M64(MSG_D) = other == 0;
		return 0;
	}
	case VT_FLUSH: {
		r = bg_ttou(p, side);
		if (r)
			return bg_result(r);
		u64 mode = M64(MSG_B);
		if (mode != 1) {
			in_flush(p);
			if (mode == 0)
				return 0;
		}
		p->oh = p->ot = 0;
		return 0;
	}
	}
	return E_INVAL;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	canal = korin_sys2(SYS_SVC_REGISTER, "vterm", SVF_ENDS | SVF_BIRTHS);
	if (canal < 0)
		exit(1);
	ak_puts("vterm : pret\n");
	for (;;) {
		waits_tick();
		long wait = waits_any() ? 20 : 60000; /* (VTIME : 1/10 s) */
		long req = korin_sys3(SYS_SVC_NEXT, canal, msg, wait);
		if (req < 0)
			exit(1);
		if (!req)
			continue;
		u64 op = M64(0);
		if (op == SVC_OP_END) {
			pid_gone(M64(MSG_A));
			continue;
		}
		if (op == SVC_OP_BIRTH) {
			birth(M64(MSG_A), M64(MSG_B));
			continue;
		}
		if (op == SVC_OP_FORK) {
			fork_copy(M64(MSG_A), M64(MSG_B));
			continue;
		}
		if (op == SVC_OP_SIGNAL)
			continue;
		current_req = req;
		int deferred;
		long r = ops(&deferred);
		if (deferred)
			continue;
		M64(0) = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, msg);
	}
}
