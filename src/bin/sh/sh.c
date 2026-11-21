/* sh - interpréteur de commandes (docs/SHELL.md), comme sh.asm (et sh_word.inc, sh_run.inc,
 * sh_cmds.inc : sh_word.c, sh_run.c, sh_cmds.c) : sous-ensemble de POSIX, sans fork.
 *   sh                     interactif : lit des lignes sur son entrée standard (un bout de
 *                          pseudo-terminal de vterm : le Terminal ; ou un tube de vfs), écrit sur
 *                          sa sortie standard ; Ctrl+C arrête le programme
 * Sur un pty (docs/VTERM.md) : la discipline de ligne de vterm fait l'écho et l'édition ; sh se
 * met dans son groupe, ignore SIGINT, SIGQUIT, SIGTSTP, donne le terminal à ses fils (entrée) et
 * met chaque commande dans un groupe à elle, au premier plan (Ctrl+C : SIGINT à ce groupe), puis
 * se remet au premier plan. Sur un tube : Ctrl+C (octet 3) lu par sh, SYS_KILL du fils.
 *   sh -c "commandes" [nom [args]]
 *   sh script [args]
 * Variables, guillemets, $(...), tubes, redirections, && || ; if for while until case, { } ( ).
 * Tout autre nom est cherché dans PATH, puis /apps/<nom>.app. Les anciennes commandes du
 * terminal (ls, cat, cp, mkdir, rm, ps, kill, mem) servent si PATH n'a pas de programme de ce nom.
 *
 * Pas d'arbre : le texte est analysé en s'exécutant (P, le x28 de l'asm, est le curseur). Tout ce
 * qui demande un autre process (tube, ( ... ), $(...), composé redirigé) relance /bin/sh -c sur
 * sa tranche de texte. La traduction garde les bizarreries de l'asm (« comme l'asm » dans les
 * commentaires) : les mêmes tampons sans contrôle de taille (arènes de 1 à 16 Mo), les mêmes
 * messages, les mêmes codes (2 : erreur de syntaxe, 127 : introuvable). */
#include "sh.h"

/* ---- état (segment de données de l'asm) */
char *P;
long noexec, cond, jump, syntax, incomplete, chain_ok, subst_code, opt_e, opt_x;
long in_pipe, pipe_in, pipe_out, pipe_pid, reds_n, red_dn, last_code;
long out_fd = 1, err_fd = 2;
long interactive, tty, vfs, vtc, my_pid, job_pg, in_h, pend_n, pend_i, obuf_n, last_c;
u64 wst;
char **arg_v, **pos_v;
long arg_n, pos_n, vars_n;
char *txt, *ar_p, *varm_p, *cap_p, *slice_p;
char **at_p;
struct redir reds[RED_MAX];
long red_d[RED_MAX];
struct var vars[VARS_MAX];
char cwd[256], path_tmp[256], tmp_num[32], inbuf[IN_MAX + 1], line_buf[4096];
struct dirent dirent_buf;
u8 iobuf[520], pend[PEND_MAX], obuf[OBUF], msg[512] __attribute__((aligned(8)));
u8 iobuf2[64 * 64] __attribute__((aligned(8)));
struct job jobs[JOBS_MAX];

static const char *const env_default[] = { "HOME=/home", "PATH=/bin:/sbin:/devtools/gcc/bin:/devtools/rust/bin", "TMPDIR=/tmp",
					   "LANG=C.UTF-8", "TERM=dumb", 0 };

#define MSGQ(off) (*(u64 *)(msg + (off)))

/* ---------------------------------------------------------------------------
 *  Démarrage
 * --------------------------------------------------------------------------- */

static void mem_init(void)
{
	txt = sys_vm_map(TXT_MAX + 8);
	ar_p = sys_vm_map(AR_MAX);
	at_p = sys_vm_map(AT_MAX);
	varm_p = sys_vm_map(VAR_MAX);
	cap_p = sys_vm_map(CAP_MAX);
	slice_p = sys_vm_map(SLICE_MAX);
}

/* argv (arg_v) et l'environnement, donnés par le père si c'est un programme C (ou un sh), sinon
 * la ligne coupée et les valeurs par défaut (args_init) */
static void args_init(const char *line)
{
	if (krt_env) { /* (crt0 a pris l'objet du père : f_start_blob) */
		arg_v = at_p;
		arg_n = krt_argc;
		for (int i = 0; i < krt_argc; i++)
			at_push(krt_argv[i]);
		at_push(0);
		char *s = krt_env;
		for (long i = 0; i < krt_envc; i++) {
			var_set_kv(s, 1);
			s += str_len(s) + 1;
		}
		return;
	}
	for (const char *const *e = env_default; *e; e++) /* valeurs par défaut */
		var_set_kv(*e, 1);
	arg_v = at_p;
	at_push("sh");
	long n = 1;
	if (line) {
		u64 len = (u64)str_len(line) + 1; /* une copie à soi, coupée aux blancs */
		char *s = ar_p;
		ar_p += len;
		mem_move(s, line, len);
		for (;;) {
			while (*s == ' ')
				s++;
			if (!*s)
				break;
			if (*s != '"') {
				at_push(s);
				n++;
				while (*s && *s != ' ')
					s++;
				if (!*s)
					break;
				*s++ = 0;
				continue;
			}
			s++; /* entre guillemets : \" et \\ */
			at_push(s);
			n++;
			char *o = s;
			int end = 0;
			for (;;) {
				char c = *s;
				if (!c) {
					end = 1;
					break;
				}
				s++;
				if (c == '"')
					break;
				if (c == '\\' && (*s == '"' || *s == '\\'))
					c = *s++;
				*o++ = c;
			}
			*o = 0;
			if (end) /* (fin de la ligne dans les guillemets) */
				break;
		}
	}
	arg_n = n;
	at_push(0);
}

/* les options -> 1 : interactif ; 0 : le texte a été exécuté (options) */
static int options(void)
{
	char **a = arg_v + 1;
	long c = 0; /* le texte de -c */
	pos_v = arg_v;
	for (;;) {
		char *o = *a;
		if (!o || o[0] != '-' || !o[1])
			break;
		a++;
		if (o[1] == '-')
			break;
		for (o++; *o; o++) {
			if (*o == 'c')
				c = 1;
			else if (*o == 'e')
				opt_e = 1;
			else if (*o == 'x')
				opt_x = 1;
		}
		if (c)
			break;
	}
	if (c) { /* sh -c texte [nom [args]] */
		char *t = *a;
		if (!t)
			return 0;
		a++;
		if (*a) {
			long n = 0;
			while (a[n])
				n++;
			pos_v = a;
			pos_n = n - 1;
		} else {
			pos_v = arg_v;
			pos_n = 0;
		}
		run_text(t);
		return 0;
	}
	char *script = *a;
	if (!script)
		return 1; /* interactif */
	long n = 0; /* $0 : le script, $1... : ses arguments */
	while (a[n])
		n++;
	pos_v = a;
	pos_n = n - 1;
	long fd = sys_open(script, O_READ);
	if (fd < 0) {
		err_nofile(script);
		last_code = 127;
		return 0;
	}
	char *e = txt;
	for (;;) {
		long r = sys_read(fd, e, 4096);
		if (r <= 0)
			break;
		e += r;
		if ((u64)(e - txt) >= (u64)(TXT_MAX - 4096))
			break;
	}
	*e = 0;
	sys_close(fd);
	run_text(txt);
	return 0;
}

/* fin du shell, avec le code de la dernière commande (sh_sortie) */
NORETURN void sh_exit(void)
{
	flush();
	_exit((int)shell_code(last_code));
}

/* texte : vérifié, puis exécuté -> 1 si le texte est incomplet (rien n'a été lancé) (run_texte) */
long run_text(char *t)
{
	syntax = 0;
	incomplete = 0;
	P = t;
	noexec_inc();
	do_list();
	noexec_dec();
	if (*P)
		syntax_error();
	if (incomplete) {
		if (interactive)
			return 1;
		sh_err_say(S_SYNTAX);
		last_code = 2;
		return 0;
	}
	if (syntax) {
		sh_err_say(S_SYNTAX);
		last_code = 2;
		return 0;
	}
	P = t;
	do_list();
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Interactif
 * --------------------------------------------------------------------------- */

static NORETURN void quit(void)
{
	flush();
	_exit(0);
}

/* une ligne de l'entrée dans inbuf (sans fin de ligne) -> 0, < 0 à la fin de l'entrée (-2 :
 * Ctrl+C sur le pty). Octets en avance gardés (pend, pend_n) (read_line) */
static long read_line(void)
{
	long n = 0;
	for (;;) {
		if ((u64)pend_i >= (u64)pend_n) { /* lire (attente) */
			long r = in_read(464, 0);
			if (r == E_AGAIN)
				continue;
			if (r == E_INTR)
				return -2;
			if (r <= 0)
				return -1;
			pend_n = r;
			pend_i = 0;
			mem_move(pend, msg + MSG_DATA, (u64)pend_n); /* (copie : msg resert) */
			continue;
		}
		u8 c = pend[pend_i++];
		if (c == 10) {
			inbuf[n] = 0;
			last_c = 10; /* (la ligne tapée finit la ligne) */
			return 0;
		}
		if (c == 13)
			continue;
		if (c == 3) { /* Ctrl+C : la ligne est abandonnée */
			n = 0;
			term_puts("^C\n");
			flush();
			continue;
		}
		if (n >= IN_MAX)
			continue;
		inbuf[n++] = (char)c;
	}
}

static void ign(long sig)
{
	korin_sys6(SYS_SIG_ACTION, sig, 1, 0, 0, 0, 0);
}

/* l'entrée est un pty : vterm, son groupe à soi au premier plan, signaux du clavier et du
 * contrôle des tâches ignorés (SIGTTOU : sh reprend le premier plan après une commande), TERM
 * d'un xterm (tty_init) */
static void tty_init(void)
{
	long ch = korin_sys2(SYS_SVC_OPEN, "vterm", 5000);
	if (ch < 0)
		return; /* (pas de vterm : rien ne marchera) */
	vtc = ch;
	tty = 1;
	korin_sys2(SYS_SETPGID, 0, 0); /* un groupe à soi */
	my_pid = sys_getpid();
	ign(2); /* SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU : ignorés */
	ign(3);
	ign(18);
	ign(21);
	ign(22);
	tty_foreground();
	const char *t = var_value("TERM", 4); /* TERM : absent ou dumb -> xterm-256color */
	if (t && !str_eq(t, "dumb"))
		return;
	var_set_kv("TERM=xterm-256color", 1);
	var_set_kv("COLORTERM=truecolor", 1);
}

static NORETURN void sh_interactive(void)
{
	long ch = korin_sys2(SYS_SVC_OPEN, "vfs", 5000); /* l'entrée standard : un tube de vfs */
	if (ch < 0)
		quit();
	vfs = ch;
	MSGQ(0) = V_STDIO;
	*(u32 *)(msg + 40) = 0;
	if (korin_sys2(SYS_SVC_CALL, ch, msg) < 0)
		quit();
	in_h = (long)MSGQ(8);
	if (!in_h)
		quit(); /* (console : rien à lire) */
	interactive = 1;
	if (in_h & VT_TAG) /* un pty : la discipline de vterm */
		tty_init();
	term_puts(lang_tr("Korin OS - sh. Tapez help pour la liste des commandes.\n"));
	for (;;) {
		update_cwd(); /* invite */
		term_puts(cwd);
		term_puts("> ");
		flush();
		char *d = txt; /* le texte, ligne après ligne s'il est incomplet */
		for (;;) {
			long r = read_line();
			if (r == -2) { /* Ctrl+C (pty) : la ligne est abandonnée */
				term_putb(10);
				goto next;
			}
			if (r < 0)
				quit(); /* fin de l'entrée : fin de la session */
			const char *s = inbuf;
			while ((*d++ = *s++))
				;
			if (!run_text(txt))
				break;
			d[-1] = 10; /* une ligne de plus */
			term_puts("... ");
			flush();
		}
		term_ensure_nl();
	next:;
	}
}

/* octets voulus (<= 464), 1 sans attente -> octets (dans msg + 48), 0 fin, E_AGAIN, E_INTR (pty :
 * Ctrl+C) (in_read) */
long in_read(long n, long nowait)
{
	MSGQ(0) = tty ? VT_READ : V_PIPE_READ; /* (mêmes champs : a, b, c bit 0) */
	MSGQ(8) = (u64)in_h;
	MSGQ(16) = (u64)n;
	MSGQ(24) = (u64)nowait;
	*(u32 *)(msg + 40) = 0;
	return korin_sys2(SYS_SVC_CALL, tty ? vtc : vfs, msg);
}

/* groupe : au premier plan du pty (VT_SPGRP) */
void tty_spgrp(long g)
{
	MSGQ(0) = VT_SPGRP;
	MSGQ(MSG_B) = (u64)g;
	MSGQ(MSG_A) = (u64)in_h;
	*(u32 *)(msg + MSG_LEN) = 0;
	korin_sys2(SYS_SVC_CALL, vtc, msg);
}

/* sh au premier plan (après une commande) (tty_premier_plan) */
void tty_foreground(void)
{
	tty_spgrp(my_pid);
}

/* pid d'un fils lancé pour une commande (pty seulement) : il entre dans le groupe de la commande
 * (le sien pour le premier : job_pg), au premier plan */
void job_fg(long pid)
{
	if (!tty || pid < 0)
		return;
	if (!job_pg)
		job_pg = pid;
	long g = job_pg;
	korin_sys2(SYS_SETPGID, pid, g);
	tty_spgrp(g);
}

/* fin d'une commande (pty) : sh revient au premier plan (job_fin) */
void job_end(void)
{
	if (!tty || !job_pg)
		return;
	job_pg = 0;
	tty_foreground();
}

/* --- tâches arrêtées (pty ; docs/SHELL.md) : une commande arrêtée (Ctrl+Z : SIGTSTP à son
 * groupe) entre dans la table jobs : son groupe, le pid attendu (le dernier du tube), l'état, le
 * nom. jobs, fg, bg : commandes intégrées. */

/* numéro de la tâche (0..) : « [n] » + état + nom, fin de ligne (job_ligne) */
static void job_line(long i)
{
	struct job *j = &jobs[i];
	term_ensure_nl();
	term_putb('[');
	term_dec(i + 1);
	term_puts("] ");
	const char *s = j->state == JOB_STOPPED ? "Arrêté" : j->state == JOB_RUNNING ? "En cours" : "Fini";
	term_puts(lang_tr(s));
	term_puts("  ");
	term_puts(j->name);
	term_putb(10);
}

/* pid d'un fils arrêté, signal : sa tâche (groupe job_pg) entre dans la table, ou y repasse
 * arrêtée ; message ; last_code = 128 + signal (job_arrete) */
void job_stop(long pid, long sig)
{
	last_code = sig + 128;
	long g = job_pg; /* (sans groupe à elle : un sous-shell, un $(...), dans le groupe de sh) */
	if (!g)
		g = my_pid;
	long i;
	struct job *j;
	for (i = 0; i < JOBS_MAX; i++) /* déjà là (même groupe) ? */
		if (jobs[i].pg == g)
			goto set;
	for (i = 0; i < JOBS_MAX; i++) /* une place libre */
		if (!jobs[i].pg)
			break;
	if (i == JOBS_MAX)
		return; /* (table pleine) */
	j = &jobs[i];
	j->pg = g;
	j->state = 0;
	j->name[0] = 0;
	if (!korin_sys2(SYS_PROC_INFO, pid, iobuf2)) { /* son nom (SYS_PROC_INFO : +24) */
		const char *s = (const char *)iobuf2 + 24;
		long k = 0;
		for (;;) {
			char c = s[k];
			j->name[k] = c;
			if (!c)
				break;
			if (++k >= 31) {
				j->name[k] = 0;
				break;
			}
		}
	}
set:
	j = &jobs[i];
	j->pid = pid;
	if (j->state == JOB_STOPPED)
		return;
	j->state = JOB_STOPPED;
	term_putb(10); /* (après le ^Z de vterm) */
	job_line(i);
}

/* argument (0 : il manque) -> la tâche, 0 : aucune ; « %n » ou « n » ; sans argument, la plus
 * haute de la table (job_choisi) */
static struct job *job_pick(const char *a)
{
	if (!a) {
		for (long i = JOBS_MAX - 1; i >= 0; i--)
			if (jobs[i].pg)
				return &jobs[i];
	} else {
		if (*a == '%')
			a++;
		u64 i = (u64)(signed_dec(a) - 1);
		if (i < JOBS_MAX && jobs[i].pg)
			return &jobs[i];
	}
	term_puts(lang_tr("sh: pas de tâche\n"));
	return 0;
}

/* son état mis à jour (WAIT étendu, sans attendre) (job_maj) */
static void job_update(struct job *j)
{
	long r = korin_sys3(SYS_WAIT, j->pid, WAIT_EX_TAG | WEX_NOHANG | WEX_UNTRACED | WEX_CONTINUED, &wst);
	if (r == WR_STOPPED)
		j->state = JOB_STOPPED;
	else if (r == WR_CONTINUED)
		j->state = JOB_RUNNING;
	else if (r != WR_NONE)
		j->state = JOB_DONE; /* fini (ou pid inconnu) */
}

/* jobs : les tâches ; une tâche finie quitte la table après un dernier message */
long cmd_jobs(char **argv)
{
	(void)argv;
	for (long i = 0; i < JOBS_MAX; i++) {
		struct job *j = &jobs[i];
		if (!j->pg)
			continue;
		job_update(j);
		job_line(i);
		if (j->state == JOB_DONE)
			j->pg = 0;
	}
	return 0;
}

/* fg [%n] : la tâche au premier plan, SIGCONT à son groupe, attendue */
long cmd_fg(char **argv)
{
	struct job *j = job_pick(argv[1]);
	if (!j)
		return 1;
	term_puts(j->name);
	term_putb(10);
	job_pg = j->pg;
	tty_spgrp(j->pg);
	korin_sys3(SYS_SIG_SEND, -j->pg, 0, SIGCONT);
	j->state = JOB_RUNNING;
	wait_pid(j->pid);
	if (j->state != JOB_STOPPED) /* pas arrêtée de nouveau : finie */
		j->pg = 0;
	job_end();
	return last_code;
}

/* bg [%n] : SIGCONT au groupe de la tâche, qui reste en arrière-plan */
long cmd_bg(char **argv)
{
	struct job *j = job_pick(argv[1]);
	if (!j)
		return 1;
	korin_sys3(SYS_SIG_SEND, -j->pg, 0, SIGCONT);
	j->state = JOB_RUNNING;
	job_line(j - jobs);
	return 0;
}

/* --- sortie (tampon de OBUF octets) */

void flush(void)
{
	u64 n = (u64)obuf_n;
	if (!n)
		return;
	obuf_n = 0;
	if (out_fd < 0)
		return; /* (/dev/null) */
	sys_write(out_fd, obuf, n);
}

void term_putb(int c)
{
	if (obuf_n >= OBUF)
		flush();
	obuf[obuf_n++] = (u8)c;
	last_c = (u8)c;
}

void term_puts(const char *s)
{
	while (*s)
		term_putb((u8)*s++);
}

void term_write(const void *b, u64 n)
{
	const u8 *s = b;
	while (n--)
		term_putb(*s++);
}

/* valeur signée */
void term_dec(long v)
{
	char t[32];
	if (v < 0) {
		v = -v;
		term_putb('-');
	}
	fmt_dec((u64)v, t);
	term_puts(t);
}

void term_ensure_nl(void)
{
	if (last_c == 10)
		return;
	if (!obuf_n && !last_c)
		return; /* (rien encore écrit : pas de saut) */
	term_putb(10);
}

long count_cols(const char *s)
{
	long n = 0;
	for (; *s; s++)
		if (((u8)*s & 0xC0) != 0x80)
			n++;
	return n;
}

void update_cwd(void)
{
	sys_getcwd(cwd, 256);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	mem_init();
	args_init(krt_line);
	if (options())
		sh_interactive();
	sh_exit();
}
