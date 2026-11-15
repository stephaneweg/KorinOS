/* sh_cmds.c - sh : commandes intégrées (docs/SHELL.md), comme sh_cmds.inc et testcmd.inc.
 *
 * Une commande : argv -> code de sortie. Deux tables : les commandes du shell passent avant
 * PATH ; les anciennes commandes du terminal (ls, cat...) seulement si PATH n'a rien. */
#include "sh.h"

static long cmd_true(char **argv), cmd_false(char **argv), cmd_echo(char **argv);
static long cmd_cd(char **argv), cmd_pwd(char **argv), cmd_exit(char **argv);
static long cmd_export(char **argv), cmd_unset(char **argv), cmd_set(char **argv);
static long cmd_shift(char **argv), cmd_test(char **argv), cmd_break(char **argv);
static long cmd_continue(char **argv), cmd_read(char **argv), cmd_exec(char **argv);
static long cmd_help(char **argv), cmd_clear(char **argv), cmd_mkdir(char **argv);
static long cmd_rm(char **argv), cmd_kill(char **argv), cmd_ls(char **argv), cmd_cat(char **argv);
static long cmd_cp(char **argv), cmd_ps(char **argv), cmd_mem(char **argv);

const struct builtin builtins[] = { /* integrees */
	{ "echo", cmd_echo }, { "cd", cmd_cd }, { "pwd", cmd_pwd }, { "exit", cmd_exit },
	{ "export", cmd_export }, { "unset", cmd_unset }, { "set", cmd_set }, { "shift", cmd_shift },
	{ "test", cmd_test }, { "[", cmd_test }, { "true", cmd_true }, { ":", cmd_true },
	{ "false", cmd_false }, { "break", cmd_break }, { "continue", cmd_continue },
	{ "read", cmd_read }, { "exec", cmd_exec }, { "help", cmd_help },
	{ "jobs", cmd_jobs }, { "fg", cmd_fg }, { "bg", cmd_bg },
	{ 0, 0 },
};
const struct builtin legacy[] = { /* anciennes */
	{ "clear", cmd_clear }, { "mkdir", cmd_mkdir }, { "rm", cmd_rm }, { "kill", cmd_kill },
	{ "ls", cmd_ls }, { "dir", cmd_ls }, { "cat", cmd_cat }, { "cp", cmd_cp },
	{ "ps", cmd_ps }, { "mem", cmd_mem }, { "free", cmd_mem },
	{ 0, 0 },
};

/* nom, table -> entrée du nom, 0 si absent (table_cherche) */
static const struct builtin *table_find(const char *name, const struct builtin *t)
{
	for (; t->name; t++)
		if (str_eq(t->name, name))
			return t;
	return 0;
}

const struct builtin *builtin_find(const char *name)
{
	return table_find(name, builtins);
}

const struct builtin *legacy_find(const char *name)
{
	return table_find(name, legacy);
}

/* entrée, argv : lancée avec ses redirections ; last_code (integree_lance). Comme l'asm :
 * l'entrée n'est jamais redirigée, « n>&m » fait seulement 1>&2 ou 2>&1 (m n'est pas lu), un seul
 * fichier ouvert est refermé à la fin (le dernier). */
void builtin_run(const struct builtin *b, char **argv)
{
	long r;
	flush();
	long so = out_fd, se = err_fd;
	long opened = -1; /* fichier ouvert pour cette commande */
	struct redir *d = reds;
	for (long n = reds_n; n; n--, d++) {
		long fd = d->fd;
		if (d->type == R_DUP) {
			if (fd == 1) /* 1>&2 */
				out_fd = err_fd;
			else if (fd == 2) /* 2>&1 */
				err_fd = out_fd;
			continue;
		}
		if (d->type == R_IN || (u64)fd > 2 || !fd)
			continue;
		long h = -1;
		if (!str_eq(d->target, "/dev/null")) {
			h = sys_open(d->target, d->type == R_OUT ? O_WRITE | O_CREATE | O_TRUNC
							       : O_WRITE | O_CREATE | O_APPEND);
			if (h < 0) {
				err_nofile(d->target);
				r = 1;
				goto fin;
			}
			opened = h;
		}
		if (fd == 2)
			err_fd = h;
		else
			out_fd = h;
	}
	r = b->fn(argv);
fin:
	last_code = r;
	flush();
	out_fd = so;
	err_fd = se;
	if (opened >= 0)
		sys_close(opened);
}

/* --- messages */

/* chaîne, sur l'erreur standard (err_puts) */
void sh_err_puts(const char *s)
{
	flush();
	u64 n = (u64)str_len(s);
	if (err_fd < 0)
		return;
	sys_write(err_fd, s, n);
}

/* texte du shell : traduit, sur l'erreur standard (err_dire) */
void sh_err_say(const char *s)
{
	sh_err_puts(lang_tr(s));
}

/* nom, texte : « sh: <texte> <nom> » (err_nom) */
static void err_name(const char *name, const char *text)
{
	sh_err_puts("sh: ");
	sh_err_say(text);
	sh_err_puts(name);
	sh_err_puts("\n");
}

void err_nofile(const char *name)
{
	err_name(name, "fichier inaccessible : ");
}

void err_notfound(const char *name)
{
	err_name(name, "commande introuvable : ");
}

/* code d'erreur du système : écrit ; last_code = 1 (erreur_code) */
void error_code(long e)
{
	char t[32];
	fmt_dec((u64)-e, t);
	sh_err_say("sh: erreur ");
	sh_err_puts(t);
	sh_err_puts("\n");
	last_code = 1;
}

/* argv : « + cmd args » (set -x) */
void trace(char **argv)
{
	sh_err_puts("+ ");
	for (; *argv; argv++) {
		sh_err_puts(*argv);
		if (!argv[1])
			break;
		sh_err_puts(" ");
	}
	sh_err_puts("\n");
}

/* --- commandes du shell */

static long cmd_true(char **argv)
{
	(void)argv;
	return 0;
}

static long cmd_false(char **argv)
{
	(void)argv;
	return 1;
}

/* echo [-n] mots */
static long cmd_echo(char **argv)
{
	char **a = argv + 1;
	int nonl = 0; /* 1 : pas de fin de ligne */
	if (*a && str_eq(*a, "-n")) {
		nonl = 1;
		a++;
	}
	for (; *a; a++) {
		term_puts(*a);
		if (!a[1])
			break;
		term_putb(' ');
	}
	if (!nonl)
		term_putb(10);
	return 0;
}

static long cmd_pwd(char **argv)
{
	(void)argv;
	update_cwd();
	term_puts(cwd);
	term_putb(10);
	return 0;
}

static long cmd_cd(char **argv)
{
	const char *p = argv[1];
	if (!p)
		p = var_value("HOME", 4);
	if (!p)
		p = "/";
	if (!sys_chdir(p))
		return 0;
	err_nofile(p);
	return 1;
}

/* exit [code] */
static long cmd_exit(char **argv)
{
	if (argv[1])
		last_code = signed_dec(argv[1]);
	sh_exit();
}

/* export NOM[=VALEUR]... */
static long cmd_export(char **argv)
{
	for (char **a = argv + 1; *a; a++) {
		const char *s = *a;
		long n = 0;
		while (s[n] && s[n] != '=')
			n++;
		if (s[n] == '=') {
			var_set_kv(s, 1);
			continue;
		}
		struct var *e = var_find(s, n);
		if (e)
			e->flag = 1;
		else
			var_set(s, str_len(s), "", 1);
	}
	return 0;
}

static long cmd_unset(char **argv)
{
	for (char **a = argv + 1; *a; a++)
		var_unset(*a);
	return 0;
}

/* tableau de chaînes terminé par 0 : deviennent $1, $2... ($0 gardé) (pos_copier) */
static void pos_copy(char **a)
{
	char *v0 = pos_v[0];
	char **base = (char **)(((u64)varm_p + 7) & ~(u64)7); /* (tableau et textes dans varm) */
	pos_v = base;
	base[0] = v0;
	long n = 0;
	while (a[n])
		n++;
	pos_n = n;
	char *t = (char *)(base + n + 2); /* textes après le tableau */
	for (long i = 0; i < n; i++) {
		const char *s = a[i];
		base[i + 1] = t;
		while ((*t++ = *s++))
			;
	}
	varm_p = t;
}

/* set [-e] [-x] [+e] [+x] [-- args] ; comme l'asm : « set -- » seul ne vide pas $1... */
static long cmd_set(char **argv)
{
	char **a = argv + 1;
	for (;;) {
		char *s = *a;
		if (!s)
			return 0;
		if (s[0] != '-' && s[0] != '+')
			break;
		long on = s[0] == '-'; /* 1 : activer */
		if (s[1] == '-') {
			a++;
			break;
		}
		for (s++; *s; s++) {
			if (*s == 'e')
				opt_e = on;
			else if (*s == 'x')
				opt_x = on;
		}
		a++;
	}
	if (*a) /* nouveaux $1... (copie permanente) */
		pos_copy(a);
	return 0;
}

/* shift [n] */
static long cmd_shift(char **argv)
{
	long n = 1;
	if (argv[1])
		n = signed_dec(argv[1]);
	if ((u64)n > (u64)pos_n)
		return 1;
	pos_n -= n;
	char *v0 = pos_v[0]; /* $0 reste devant */
	pos_v += n;
	pos_v[0] = v0;
	return 0;
}

static long cmd_break(char **argv)
{
	(void)argv;
	jump = 2;
	return 0;
}

static long cmd_continue(char **argv)
{
	(void)argv;
	jump = 1;
	return 0;
}

/* read NOM : une ligne de l'entrée standard (celle du shell : jamais redirigée, comme l'asm) ->
 * 1 à la fin ; 4 000 octets gardés au plus */
static long cmd_read(char **argv)
{
	const char *name = argv[1];
	if (!name)
		name = "REPLY";
	char *p = line_buf;
	for (;;) {
		char c;
		if (sys_read(0, &c, 1) != 1) {
			if (p == line_buf)
				return 1;
			break;
		}
		if (c == 10)
			break;
		if (p >= line_buf + 4000)
			continue;
		*p++ = c;
	}
	*p = 0;
	var_set(name, str_len(name), line_buf, 0);
	return 0;
}

/* exec commande : lancée, puis fin du shell avec son code */
static long cmd_exec(char **argv)
{
	if (!argv[1])
		return 0;
	launch_external(argv + 1, 0, 0);
	sh_exit();
}

/* --- test, [ (testcmd.inc) : -e -f -d -s -z -n, = != == , -eq -ne -lt -le -gt -ge, ! , un mot
 * seul ; x -> 0 si vrai */

/* texte -> nombre signé (dec_signe) */
long signed_dec(const char *s)
{
	long v = 0;
	int neg = 0;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	for (;;) {
		u64 d = (u8)*s++ - (u64)'0';
		if (d > 9)
			break;
		v = (long)((u64)v * 10 + d);
	}
	return neg ? -v : v;
}

/* lettre, texte -> 0 si vrai (test_unaire) */
static long test_unary(int op, const char *s)
{
	long st[2];
	if (op == 'z')
		return *s != 0;
	if (op == 'n')
		return *s == 0;
	if (sys_stat(s, st) < 0)
		return 1;
	if (op == 'd')
		return !(st[1] & ATTR_DIR);
	if (op == 'f')
		return (st[1] & ATTR_DIR) != 0;
	if (op == 's')
		return st[0] == 0;
	return 0; /* -e, -r, -w, -x */
}

/* gauche, opérateur, droite -> 0 si vrai (test_binaire) */
static long test_binary(const char *a, const char *op, const char *b)
{
	if (op[0] == '=')
		return !str_eq(a, b);
	if (op[0] == '!')
		return str_eq(a, b) != 0;
	long x = signed_dec(a), y = signed_dec(b);
	if (op[1] == 'e')
		return x != y;
	if (op[1] == 'n')
		return x == y;
	if (op[1] == 'l')
		return op[2] == 't' ? x >= y : x > y;
	return op[2] == 't' ? x <= y : x < y;
}

static long cmd_test(char **argv)
{
	long n = 0;
	while (argv[n + 1])
		n++;
	if (argv[0][0] == '[') { /* [ ... ] */
		if (!n || !str_eq(argv[n], "]"))
			return 1;
		n--;
	}
	char **a = argv + 1;
	long neg = 0; /* 1 : « ! » */
	for (;;) {
		if (!n)
			return 1 ^ neg;
		if (!str_eq(a[0], "!"))
			break;
		neg ^= 1;
		a++;
		n--;
	}
	long r;
	if (n == 1)
		r = a[0][0] == 0;
	else if (n == 2) {
		const volatile char *p = a[0]; /* (comme l'asm : l'octet après le premier) */
		r = test_unary(p[1], a[1]);
	} else if (n == 3)
		r = test_binary(a[0], a[1], a[2]);
	else
		return 1;
	return r ^ neg;
}

static long cmd_help(char **argv)
{
	(void)argv;
	term_puts(lang_tr("Le shell : variables, guillemets, $(...), tubes |, redirections < > >> 2>&1,\n"
			  "&& || ; if for while until case { } ( ), sh -c, scripts (docs/SHELL.md).\n"
			  "Commandes intégrées : echo cd pwd exit export unset set shift test [ read\n"
			  "exec true false break continue jobs fg bg, et si PATH n'a pas ces programmes :\n"
			  "ls cat cp mkdir rm ps kill mem.\n"
			  "Tout autre nom est cherché dans PATH puis /apps/<nom>.app\n"
			  "(Ctrl+C l'interrompt, Ctrl+Z l'arrête : fg, bg).\n"));
	return 0;
}

/* --- anciennes commandes du terminal */

static long cmd_clear(char **argv) /* (écran, curseur, historique : xterm) */
{
	(void)argv;
	term_puts("\033[H\033[2J\033[3J");
	return 0;
}

/* chemin : chaque niveau créé au besoin -> 0 (mkdir_p) ; coupé à 250 octets */
static long mkdir_p(const char *path)
{
	long n = 0;
	for (;;) {
		char c = path[n];
		path_tmp[n++] = c;
		if (!c)
			break;
		if (n >= 250) {
			path_tmp[n] = 0;
			break;
		}
	}
	for (char *q = path_tmp + 1; *q; q++)
		if (*q == '/' && q[-1] != ':') { /* pas la racine d'un volume */
			*q = 0;
			sys_mkdir(path_tmp);
			*q = '/';
		}
	sys_mkdir(path_tmp);
	return 0;
}

/* mkdir [-p] dossiers */
static long cmd_mkdir(char **argv)
{
	char **a = argv + 1;
	long p = 0, code = 0;
	if (!*a)
		return 0;
	if (str_eq(*a, "-p")) {
		p = 1;
		a++;
	}
	for (; *a; a++) {
		if (p) {
			code |= mkdir_p(*a);
			continue;
		}
		if (!sys_mkdir(*a))
			continue;
		err_nofile(*a);
		code |= 1;
	}
	return code;
}

/* rm [-f] [-r] chemins ; comme l'asm : toute option vaut -f */
static long cmd_rm(char **argv)
{
	char **a = argv + 1;
	long f = 0, code = 0;
	while (*a && (*a)[0] == '-') {
		f = 1;
		a++;
	}
	for (; *a; a++) {
		if (!sys_unlink(*a) || f)
			continue;
		err_nofile(*a);
		code = 1;
	}
	return code;
}

static long cmd_kill(char **argv)
{
	if (!argv[1])
		return 1;
	long r = korin_sys1(SYS_KILL, signed_dec(argv[1]));
	if (!r)
		return 0;
	error_code(r);
	return 1;
}

static long cmd_ls(char **argv)
{
	const char *d = argv[1];
	if (!d)
		d = ".";
	long fd = sys_open(d, O_READ);
	if (fd < 0) {
		error_code(fd);
		return 1;
	}
	while (sys_readdir(fd, &dirent_buf) == 1) {
		term_puts(dirent_buf.name);
		if (dirent_buf.attr & ATTR_DIR)
			term_putb('/');
		else { /* aligner la taille en colonne 40 */
			for (long c = count_cols(dirent_buf.name); c < 38; c++)
				term_putb(' ');
			term_putb(' ');
			term_dec((long)dirent_buf.size);
			term_puts(lang_tr(" o"));
		}
		term_putb(10);
	}
	sys_close(fd);
	return 0;
}

/* cat fichiers */
static long cmd_cat(char **argv)
{
	long code = 0;
	for (char **a = argv + 1; *a; a++) {
		long fd = sys_open(*a, O_READ);
		if (fd < 0) {
			err_nofile(*a);
			code = 1;
			continue;
		}
		for (;;) {
			long r = sys_read(fd, iobuf, 512);
			if (r <= 0)
				break;
			term_write(iobuf, (u64)r);
		}
		sys_close(fd);
	}
	return code;
}

/* cp source destination */
static long cmd_cp(char **argv)
{
	if (!argv[1] || !argv[2]) {
		sh_err_say("usage : cp source destination\n");
		return 1;
	}
	long in = sys_open(argv[1], O_READ);
	if (in < 0) {
		error_code(in);
		return 1;
	}
	long out = sys_open(argv[2], O_WRITE | O_CREATE | O_TRUNC);
	if (out < 0) {
		sys_close(in);
		error_code(out);
		return 1;
	}
	for (;;) {
		long r = sys_read(in, iobuf, 512);
		if (r <= 0)
			break;
		sys_write(out, iobuf, (u64)r);
	}
	sys_close(out);
	sys_close(in);
	return 0;
}

/* valeur : écrite sur 6 colonnes alignées à droite, puis 2 espaces (term_dec_pad6) */
static void term_dec_pad6(u64 v)
{
	char t[32];
	fmt_dec(v, t);
	for (long n = str_len(t); n < 6; n++)
		term_putb(' ');
	term_puts(t);
	term_putb(' ');
	term_putb(' ');
}

static long cmd_ps(char **argv)
{
	(void)argv;
	term_puts(lang_tr("   PID  THREADS  TAS Ko  NOM\n"));
	long from = 0; /* emplacement de départ (SYS_PROCLIST_FROM) */
	for (;;) {
		long n = korin_sys3(SYS_PROCLIST_FROM, iobuf2, 64, from);
		if (n < 0) {
			error_code(n);
			return 0;
		}
		u8 *e = iobuf2;
		for (long k = n; k; k--, e += 64) {
			term_dec_pad6(*(u64 *)e);
			term_dec_pad6(*(u64 *)(e + 8));
			term_dec_pad6(*(u64 *)(e + 16));
			term_putb(' ');
			term_puts((char *)e + 24);
			term_putb(10);
			from = (long)(*(u64 *)e & 0xFFFF); /* (la suite : après ce process) */
		}
		if (n != 64) /* morceau plein : la suite */
			return 0;
	}
}

static long cmd_mem(char **argv)
{
	(void)argv;
	long r = korin_sys1(SYS_SYSINFO, iobuf2);
	if (r < 0) {
		error_code(r);
		return 0;
	}
	const long *v = (const long *)iobuf2;
	term_puts(lang_tr("Coeurs       : "));
	term_dec(v[0]);
	term_puts(lang_tr("\nMémoire libre : "));
	term_dec(v[1]);
	term_puts(lang_tr(" Ko sur "));
	term_dec(v[2]);
	term_puts(lang_tr(" Ko\nEn marche    : "));
	term_dec((long)((u64)v[3] / 1000));
	term_puts(lang_tr(" s\nProcess      : "));
	term_dec(v[4]);
	term_puts(lang_tr("\nCharge       : "));
	for (int i = 0; i < 4; i++) {
		term_dec(v[5 + i]);
		term_puts("% ");
	}
	term_putb(10);
	return 0;
}
