/* sh_run.c - sh : analyse et exécution (docs/SHELL.md), comme sh_run.inc.
 *
 * Pas d'arbre : le texte est analysé en s'exécutant, P est le curseur dans le texte. noexec > 0 :
 * on analyse sans rien lancer (branche non prise, tranche d'un tube, vérification avant
 * d'exécuter) ; une boucle revient en arrière en remettant P. Tout ce qui demande un autre
 * process sans fork (tube, ( ... ), $(...), composé redirigé) relance sh -c sur sa tranche. */
#include "sh.h"

static void do_command(void);

/* ---------------------------------------------------------------------------
 *  Lexique
 * --------------------------------------------------------------------------- */

/* espaces, suites de ligne, commentaire */
void skip_blank(void)
{
	for (;;) {
		char c = *P;
		if (c == ' ' || c == 9 || c == 13) {
			P++;
			continue;
		}
		if (c == '\\') {
			if (P[1] != 10)
				return;
			P += 2;
			continue;
		}
		if (c == '#')
			while (*P && *P != 10)
				P++;
		return;
	}
}

/* espaces, fins de ligne et « ; » (pas « ;; ») */
static void skip_seps(void)
{
	for (;;) {
		skip_blank();
		if (*P == 10 || (*P == ';' && P[1] != ';')) {
			P++;
			continue;
		}
		return;
	}
}

/* mot -> 1 si le texte en P est ce mot entier (est_mot) */
static int is_word(const char *w)
{
	long i;
	for (i = 0; w[i]; i++)
		if (w[i] != P[i])
			return 0;
	switch (P[i]) {
	case 0: case ' ': case 9: case 10: case 13: case ';': case '&': case '|': case ')':
		return 1;
	}
	return 0;
}

/* mot -> 1 et le mot passé, s'il est là (prend_mot) */
static int take_word(const char *w)
{
	if (!is_word(w))
		return 0;
	P += str_len(w);
	return 1;
}

/* mot obligatoire (attendre_mot) */
static void expect_word(const char *w)
{
	skip_seps();
	if (!take_word(w))
		syntax_error();
}

/* (erreur_syntaxe) fin du texte : une ligne de plus, en interactif */
void syntax_error(void)
{
	if (!*P)
		incomplete = 1;
	if (!syntax)
		syntax = 1;
}

/* -> 1 si une liste s'arrête ici (a_la_fin) */
static int at_end(void)
{
	if (!*P || *P == ')' || (*P == ';' && P[1] == ';'))
		return 1;
	return is_word("then") || is_word("elif") || is_word("else") || is_word("fi") || is_word("do") ||
	       is_word("done") || is_word("esac") || is_word("}");
}

void noexec_inc(void)
{
	noexec++;
}

void noexec_dec(void)
{
	noexec--;
}

/* ---------------------------------------------------------------------------
 *  Listes
 * --------------------------------------------------------------------------- */

static void do_andor(void);

/* une suite de commandes, jusqu'à un mot réservé, « ) », « ;; » ou la fin */
void do_list(void)
{
	for (;;) {
		skip_seps();
		if (at_end() || syntax)
			return;
		char *s = P;
		if (jump) { /* (break, continue : le reste est seulement lu) */
			noexec_inc();
			do_andor();
			noexec_dec();
		} else
			do_andor();
		skip_blank();
		if (*P == '&') /* (pas d'arrière-plan : comme « ; ») */
			P++;
		if (P == s) { /* rien de lu : un caractère inattendu */
			syntax_error();
			return;
		}
		if (noexec || cond || !opt_e || chain_ok || !last_code) /* set -e */
			continue;
		sh_exit();
	}
}

static void do_pipeline(void);

/* pipeline { && pipeline | || pipeline } */
static void do_andor(void)
{
	chain_ok = 0;
	do_pipeline();
	for (;;) {
		skip_blank();
		char c = P[0];
		if (c != P[1])
			return;
		long orr;
		if (c == '&')
			orr = 0;
		else if (c == '|')
			orr = 1;
		else
			return;
		P += 2;
		skip_seps();
		chain_ok = 1; /* (set -e : pas ce qui est à gauche) */
		if ((last_code != 0) == orr) {
			chain_ok = 0;
			do_pipeline();
		} else { /* à passer */
			noexec++;
			do_pipeline();
			noexec--;
		}
	}
}

/* P sur « | » seul ? (est_tube) */
static int is_pipe(void)
{
	return P[0] == '|' && P[1] != '|';
}

static long launch_member(const char *end);

/* [!] commande { | commande } */
static void do_pipeline(void)
{
	char *slices[TUBE_MAX][2];
	long pids[TUBE_MAX];
	long pp[2];
	skip_blank();
	int neg = 0; /* 1 : « ! » */
	if (P[0] == '!' && P[1] == ' ') {
		neg = 1;
		P++;
		skip_blank();
	}
	if (noexec) { /* analyse seule (comme l'asm : « ! » sans effet) */
		for (;;) {
			do_command();
			skip_blank();
			if (!is_pipe())
				return;
			P++;
			skip_seps();
		}
	}
	char *start = P; /* les tranches : début, fin */
	long n = 0;
	noexec_inc();
	for (;;) {
		char *s = P;
		do_command();
		skip_blank();
		if (n >= TUBE_MAX)
			break;
		slices[n][0] = s;
		slices[n][1] = P;
		n++;
		if (!is_pipe())
			break;
		P++;
		skip_seps();
	}
	noexec_dec();
	if (syntax)
		return;
	if (n == 1) { /* une seule : exécutée ici */
		char *save = P;
		P = start;
		do_command();
		P = save;
	} else {
		char *save = P;
		long prev = -1; /* bout de lecture du précédent */
		long i;
		for (i = 0; i < n; i++) {
			if (i < n - 1) {
				if (f_pipe(pp) < 0) {
					P = save;
					last_code = 1;
					goto negate;
				}
			} else { /* dernier : notre sortie */
				pp[0] = -1;
				pp[1] = 1;
			}
			P = slices[i][0];
			pipe_in = prev;
			pipe_out = pp[1];
			pipe_pid = 0;
			long pid = launch_member(slices[i][1]);
			pids[i] = pid;
			job_fg(pid); /* (pty : le groupe du tube) */
			if (prev >= 0)
				sys_close(prev);
			prev = pp[0];
			if ((u64)pp[1] >= 3)
				sys_close(pp[1]);
		}
		for (i = 0; i < n; i++) /* tous attendus, le dernier donne le code */
			if (pids[i] >= 0)
				wait_pid(pids[i]);
		job_end();
		P = save;
	}
negate:
	if (neg)
		last_code = last_code == 0;
}

static int is_compound(void);
static void do_simple(void);
static void fds_default(long *fds);
static long launch_slice(const char *a, const char *b, const long *fds);

/* P = début de la tranche d'un tube, end = sa fin : lancée sans attendre -> pid (une commande
 * simple directement ; sinon, un sh -c) (lance_membre). Comme l'asm : une commande simple qui
 * n'a pas pu être lancée (introuvable, redirection fausse) est relancée par sh -c. */
static long launch_member(const char *end)
{
	char *s = P;
	long r, fds[3];
	in_pipe = 1;
	skip_blank();
	if (!is_compound()) {
		do_simple();
		r = pipe_pid;
		if (r)
			goto fin;
	}
	fds_default(fds); /* composé, ou commande intégrée */
	r = launch_slice(s, end, fds);
fin:
	in_pipe = 0;
	return r;
}

/* ---------------------------------------------------------------------------
 *  Commandes
 * --------------------------------------------------------------------------- */

/* -> 1 si P est sur une commande composée (est_compose) */
static int is_compound(void)
{
	return *P == '(' || is_word("if") || is_word("while") || is_word("until") || is_word("for") ||
	       is_word("case") || is_word("{");
}

static void compound(void);
static int is_redir(void);
static void compound_apart(const char *a, const char *b);

/* une commande, simple ou composée */
static void do_command(void)
{
	skip_blank();
	if (!is_compound()) {
		do_simple();
		return;
	}
	if (noexec) {
		compound();
		return;
	}
	char *s = P; /* suivie d'une redirection, ou ( ... ) : dans un autre process */
	noexec_inc();
	compound();
	noexec_dec();
	char *e = P;
	skip_blank();
	if (is_redir() || *s == '(') {
		if (*s == '(') /* (l'intérieur des parenthèses) */
			compound_apart(s + 1, e - 1);
		else
			compound_apart(s, e);
		return;
	}
	P = s;
	compound();
}

/* (analyse seule, ou dans un process à part) ( liste ) */
static void do_sub(void)
{
	P++;
	do_list();
	if (*P != ')') {
		syntax_error();
		return;
	}
	P++;
}

static void do_brace(void)
{
	P++;
	do_list();
	expect_word("}");
}

/* if liste then liste { elif liste then liste } [ else liste ] fi */
static void do_if(void)
{
	P += 2;
	long taken = 0; /* 1 : une branche a été prise */
	for (;;) {
		if (taken)
			noexec_inc();
		cond++;
		do_list();
		cond--;
		if (taken)
			noexec_dec();
		expect_word("then");
		long run = !taken && !last_code; /* à exécuter ? */
		if (!run)
			noexec_inc();
		do_list();
		if (!run)
			noexec_dec();
		taken |= run;
		skip_seps();
		if (!take_word("elif"))
			break;
	}
	if (take_word("else")) {
		if (taken)
			noexec_inc();
		do_list();
		if (taken)
			noexec_dec();
		taken = 1;
	}
	expect_word("fi");
	if (!taken) /* aucune branche : 0 (comme l'asm, aussi pendant l'analyse) */
		last_code = 0;
}

/* while / until liste do liste done */
static void do_while(void)
{
	long until = *P == 'u'; /* 1 : until */
	P += 5;
	char *test = P;
	long code = 0; /* code du corps */
	for (;;) {
		P = test;
		cond++;
		do_list();
		cond--;
		expect_word("do");
		if (noexec)
			break;
		if ((last_code == 0) == until) /* (while échoue, until réussit) */
			break;
		do_list();
		code = last_code;
		if (!jump)
			continue;
		long j = jump;
		jump = 0;
		if (j != 1) /* break : 2, continue : 1 */
			break;
	}
	noexec_inc();
	do_list();
	noexec_dec();
	expect_word("done");
	if (noexec)
		return;
	last_code = code;
}

/* for NOM [ in mots ] ; do liste done */
static void do_for(void)
{
	P += 3;
	skip_blank();
	char *name = P; /* le nom */
	name_skip();
	long len = P - name;
	char *mark = ar_p; /* marques */
	char **first = at_p;
	skip_seps();
	if (take_word("in")) {
		for (;;) {
			skip_blank();
			if (word_end((u8)*P))
				break;
			if (noexec)
				word_skip();
			else
				word_expand(0);
		}
	} else /* sans « in » : "$@" */
		for (long i = 1; i <= pos_n; i++)
			at_push(pos_v[i]);
	char **last = at_p; /* fin de la liste */
	char **cur = first;
	expect_word("do");
	char *body = P; /* le corps */
	if (!noexec) {
		last_code = 0;
		while (cur < last) {
			var_set(name, len, *cur++, 0);
			P = body;
			do_list();
			if (!jump)
				continue;
			long j = jump;
			jump = 0;
			if (j != 1)
				break;
		}
	}
	P = body;
	noexec_inc();
	do_list();
	noexec_dec();
	expect_word("done");
	at_p = first;
	ar_p = mark;
}

/* case mot in motif { | motif } ) liste ;; ... esac */
static void do_case(void)
{
	P += 4;
	skip_blank();
	char *mark = ar_p;
	char *word = 0; /* le mot */
	if (!noexec)
		word = word_expand(1);
	else
		word_skip();
	skip_seps();
	if (!take_word("in")) {
		syntax_error();
		goto out;
	}
	long taken = 0; /* 1 : branche prise */
	for (;;) {
		skip_seps();
		if (take_word("esac"))
			goto out;
		if (!*P)
			goto err;
		if (*P == '(')
			P++;
		long match = 0; /* ce motif correspond ? */
		for (;;) {
			skip_blank();
			if (!noexec && word && !taken && !match)
				match = pattern_match(word_expand(2), word);
			else
				word_skip();
			skip_blank();
			if (*P != '|')
				break;
			P++;
		}
		if (*P != ')')
			goto err;
		P++;
		if (!match) {
			noexec_inc();
			do_list();
			noexec_dec();
		} else {
			taken = 1;
			do_list();
		}
		skip_seps();
		if (*P == ';')
			P += 2;
	}
err:
	syntax_error();
out:
	ar_p = mark;
}

static void compound(void)
{
	if (*P == '(')
		do_sub();
	else if (is_word("if"))
		do_if();
	else if (is_word("while") || is_word("until"))
		do_while();
	else if (is_word("for"))
		do_for();
	else if (is_word("case"))
		do_case();
	else
		do_brace();
}

static void read_redir(void);
static long fds_prepare(long *fds);
static void fds_close(void);

/* a, b = tranche du composé (P sur ses redirections) : un sh -c à part (compose_a_part) */
static void compound_apart(const char *a, const char *b)
{
	long fds[4];
	char *mark = ar_p;
	reds_n = 0;
	for (;;) {
		skip_blank();
		if (!is_redir())
			break;
		read_redir();
	}
	if (fds_prepare(fds) < 0)
		last_code = 1;
	else {
		long pid = launch_slice(a, b, fds);
		fds_close();
		if (pid < 0)
			error_code(pid);
		else {
			job_fg(pid);
			wait_pid(pid);
			job_end();
		}
	}
	ar_p = mark;
}

/* ---------------------------------------------------------------------------
 *  Redirections
 * --------------------------------------------------------------------------- */

/* -> 1 si P est sur une redirection (chiffres puis < ou >) (est_redir) */
static int is_redir(void)
{
	const char *s = P;
	while ((u8)(*s - '0') <= 9)
		s++;
	return *s == '<' || *s == '>';
}

/* P sur une redirection : ajoutée à reds (mot développé) (lire_redir) */
static void read_redir(void)
{
	long fd = 0, had = 0; /* 1 : un chiffre était là */
	for (;;) {
		u64 d = (u8)(*P - '0');
		if (d > 9)
			break;
		fd = fd * 10 + (long)d;
		had = 1;
		P++;
	}
	char c = *P++;
	long type = R_IN;
	if (c != '<') {
		type = R_OUT;
		if (*P == '>') {
			type = R_APP;
			P++;
		}
	}
	if (!had)
		fd = c == '>'; /* < : 0, > : 1 */
	if (*P == '&') {
		P++;
		type = R_DUP;
	}
	skip_blank();
	if (noexec) {
		word_skip();
		return;
	}
	if (word_end((u8)*P)) {
		syntax_error();
		return;
	}
	char *t = word_expand(1);
	if (reds_n >= RED_MAX)
		return;
	reds[reds_n].fd = fd;
	reds[reds_n].type = type;
	reds[reds_n].target = t;
	reds_n++;
}

/* fds[3] : entrée, sortie, erreur par défaut d'un fils (fds_defaut) ; interactif sur un tube : son
 * entrée est la nôtre, le fils n'y lit rien (-1) ; sur un pty le fils a le terminal */
static void fds_default(long *fds)
{
	long i = tty ? 0 : interactive;
	fds[0] = i ? -1 : 0;
	fds[1] = 1;
	fds[2] = 2;
	if (in_pipe) {
		if (pipe_in >= 0)
			fds[0] = pipe_in;
		fds[1] = pipe_out;
	}
}

/* fds[3] : remplis, redirections comprises -> 0, < 0 si un fichier a échoué (message écrit).
 * Les descripteurs ouverts sont dans red_d (fds_close les rend) (fds_preparer) */
static long fds_prepare(long *fds)
{
	fds_default(fds);
	red_dn = 0;
	struct redir *r = reds;
	for (long n = reds_n; n; n--, r++) {
		u64 fd = (u64)r->fd;
		if (fd > 2)
			continue;
		if (r->type == R_DUP) {
			u64 d = (u8)(r->target[0] - '0');
			if (d <= 2)
				fds[fd] = fds[d];
			continue;
		}
		if (r->type != R_IN && !str_eq(r->target, "/dev/null")) { /* créé (ou vidé) ici */
			long h = sys_open(r->target, r->type == R_OUT ? O_WRITE | O_CREATE | O_TRUNC : O_WRITE | O_CREATE);
			if (h < 0)
				goto err;
			sys_close(h);
		}
		long h = f_redir(r->target, r->type == R_IN ? O_READ : O_WRITE);
		if (h < 0)
			goto err;
		fds[fd] = h;
		red_d[red_dn++] = h;
		continue;
	err:
		err_nofile(r->target);
		fds_close();
		return -1;
	}
	return 0;
}

static void fds_close(void)
{
	long n = red_dn;
	red_dn = 0;
	while (n)
		sys_close(red_d[--n]);
}

/* ---------------------------------------------------------------------------
 *  Commande simple
 * --------------------------------------------------------------------------- */

/* -> longueur du nom si P est sur NOM=... , 0 sinon (est_affect) */
static long is_assign(void)
{
	for (long n = 0;; n++) {
		u8 c = (u8)P[n];
		if (c == '=')
			return n;
		if (c == '_')
			continue;
		if ((u8)(c - '0') <= 9) {
			if (n) /* (pas de chiffre en tête) */
				continue;
			return 0;
		}
		if ((u8)((c | 0x20) - 'a') > 25)
			return 0;
	}
}

static void do_simple(void)
{
	struct assign as[AS_MAX];
	char *mark_ar = ar_p;
	char **mark_at = at_p;
	long nas = 0; /* affectations */
	reds_n = 0;
	subst_code = 0;
	for (;;) {
		skip_blank();
		if (is_redir()) {
			read_redir();
			continue;
		}
		if (word_end((u8)*P))
			break;
		if (noexec) {
			word_skip();
			continue;
		}
		long l;
		if (at_p == mark_at && (l = is_assign()) && nas < AS_MAX) { /* (pas encore de commande) */
			char *nm = P;
			P += l + 1;
			char *v = word_expand(1);
			as[nas].name = nm;
			as[nas].len = l;
			as[nas].value = v;
			nas++;
			continue;
		}
		word_expand(0);
	}
	char *end = P; /* fin de la tranche */
	if (noexec || syntax)
		goto rest;
	if (at_p == mark_at) { /* pas de commande : les affectations */
		for (long i = 0; i < nas; i++)
			var_set(as[i].name, as[i].len, as[i].value, 0);
		last_code = subst_code;
		goto rest;
	}
	at_push(0); /* argv terminé */
	char **argv = mark_at;
	if (opt_x)
		trace(argv);
	const struct builtin *b = builtin_find(argv[0]);
	if (b) {
		if (in_pipe) /* (dans un tube : launch_member fait un sh -c) */
			goto rest;
		for (long i = 0; i < nas; i++) /* affectations : gardées */
			var_set(as[i].name, as[i].len, as[i].value, 0);
		builtin_run(b, argv);
		goto rest;
	}
	launch_external(argv, nas, as);
rest:
	P = end;
	ar_p = mark_ar;
	at_p = mark_at;
}

static char *prog_find(char *name);

/* argv : le programme, ses redirections et l'environnement ; nas affectations ; attendu, sauf dans
 * un tube (pipe_pid) (lance_externe) */
void launch_external(char **argv, long nas, struct assign *as)
{
	long fds[4];
	char *path = prog_find(argv[0]);
	if (!path) {
		const struct builtin *b = legacy_find(argv[0]); /* anciennes commandes du terminal */
		if (b) {
			if (!in_pipe)
				builtin_run(b, argv);
			return;
		}
		path = argv[0]; /* (SD0:/apps/<nom>.app : vfs) */
	}
	if (fds_prepare(fds) < 0) {
		last_code = 1;
		return;
	}
	char **env = at_p; /* l'environnement : affectations d'abord */
	for (long i = 0; i < nas; i++) {
		char *d = ar_p, *e = d;
		for (long k = 0; k < as[i].len; k++)
			*d++ = as[i].name[k];
		*d++ = '=';
		const char *v = as[i].value;
		while ((*d++ = *v++))
			;
		ar_p = d;
		at_push(e);
	}
	env_array(0);
	flush();
	long pid = f_spawn_env(path, argv, env, fds);
	fds_close();
	at_p = env;
	if (pid < 0) {
		if (pid == E_NOENT) {
			err_notfound(argv[0]);
			last_code = 127;
		} else
			error_code(pid);
		return;
	}
	if (in_pipe) {
		pipe_pid = pid;
		return;
	}
	job_fg(pid);
	wait_pid(pid);
	job_end();
}

/* nom -> chemin à lancer (PATH), 0 si aucun ; le nom seul s'il contient / ou : (prog_trouver) ;
 * comme l'asm : un dossier de PATH coupé à 200 octets, un chemin de 250 et plus écarté */
static char *prog_find(char *name)
{
	long st[2];
	for (const char *s = name; *s; s++)
		if (*s == '/' || *s == ':')
			return name;
	const char *p = var_value("PATH", 4);
	if (!p)
		return 0;
	for (;;) {
		if (!*p)
			return 0;
		long n = 0; /* dossier/nom */
		for (;;) {
			char c = *p;
			if (!c)
				break;
			p++;
			if (c == ':')
				break;
			if (n < 200)
				path_tmp[n++] = c;
		}
		if (!n)
			continue;
		path_tmp[n++] = '/';
		const char *s = name;
		int ok = 1;
		for (;;) {
			char c = *s++;
			path_tmp[n++] = c;
			if (n >= 250) {
				ok = 0;
				break;
			}
			if (!c)
				break;
		}
		if (!ok)
			continue;
		if (sys_stat(path_tmp, st) < 0 || (st[1] & ATTR_DIR))
			continue;
		return path_tmp;
	}
}

/* a, b = tranche de texte, fds[3] : lancée par sh -c, sans attendre -> pid, < 0 (lance_tranche) */
static long launch_slice(const char *a, const char *b, const long *fds)
{
	flush();
	char *d = slice_p; /* la tranche, terminée par 0 */
	while (a < b)
		*d++ = *a++;
	*d = 0;
	char **argv = at_p;
	at_push("sh");
	at_push(opt_e ? "-ec" : "-c");
	at_push(slice_p);
	for (long i = 0; i <= pos_n; i++) /* $0, $1... */
		at_push(pos_v[i]);
	at_push(0);
	char **env = env_array(1);
	long pid = f_spawn_env("SD0:/bin/sh", argv, env, fds);
	at_p = argv;
	return pid;
}

/* a, b = tranche, 1 : entre guillemets : sa sortie, sans les fins de ligne finales, posée dans le
 * mot en cours (capturer) ; comme l'asm : sans limite de taille (CAP_MAX) */
void capture(const char *a, const char *b, long quoted)
{
	long pp[2], fds[4];
	if (f_pipe(pp) < 0)
		return;
	fds_default(fds);
	fds[1] = pp[1]; /* sortie : le tube */
	long pid = launch_slice(a, b, fds);
	sys_close(pp[1]);
	char *s = cap_p, *e = cap_p; /* lue en entier */
	for (;;) {
		long r = sys_read(pp[0], e, 4096);
		if (r <= 0)
			break;
		e += r;
	}
	sys_close(pp[0]);
	while (e > s && e[-1] == 10)
		e--;
	*e = 0;
	if (pid >= 0) {
		wait_pid(pid);
		subst_code = last_code;
	}
	put_value(s, quoted);
}

/* octets reçus dans msg pendant qu'un programme tourne : gardés pour la suite (sh les lira
 * ensuite comme des commandes) -> 1 si un Ctrl+C s'y trouve (entree_garder) */
static int input_keep(long n)
{
	const u8 *s = msg + MSG_DATA;
	int ctrlc = 0;
	long k = pend_n;
	if ((u64)pend_i >= (u64)k) { /* tout lu : le tampon repart de zéro */
		k = 0;
		pend_i = 0;
	}
	for (long i = 0; i < n; i++) {
		u8 c = s[i];
		if (c == 3) {
			ctrlc = 1;
			continue;
		}
		if (k < PEND_MAX)
			pend[k++] = c;
	}
	pend_n = k;
	return ctrlc;
}

/* pid -> 1 s'il est encore dans la liste des process */
static int alive(long pid)
{
	if (korin_sys2(SYS_PROC_INFO, pid, iobuf2))
		return 0;
	return !*(u64 *)(iobuf2 + 56); /* (état : 1 en fin de vie) */
}

/* pid : sa fin (interactif : Ctrl+C sur notre entrée l'arrête) ; last_code (attendre) */
long wait_pid(long pid)
{
	long r;
	flush();
	if (interactive && !tty) {
		do {
			sys_sleep(50);
			r = in_read(464, 1); /* (sans attente) */
			if (r > 0 && input_keep(r)) {
				korin_sys1(SYS_KILL, pid);
				term_puts("^C\n");
			}
		} while (alive(pid));
	}
	if (tty) { /* pty : un arrêt (Ctrl+Z) rend aussi la main (job_stop) */
		r = korin_sys3(SYS_WAIT, pid, WAIT_EX_TAG | WEX_UNTRACED, &wst);
		if (r == WR_STOPPED) {
			job_stop(pid, (long)wst);
			return last_code;
		}
		if (r == WR_EXITED)
			r = (long)wst;
	} else
		r = korin_sys1(SYS_WAIT, pid);
	last_code = r;
	if (interactive && !in_pipe && r) {
		term_ensure_nl();
		term_puts(lang_tr("[code de sortie "));
		term_dec(r);
		term_puts("]\n");
	}
	return last_code;
}
