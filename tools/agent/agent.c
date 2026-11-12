/*
 * agent -- le client terminal de Vibe pour Korin (docs/PYTHON.md §5.3, docs/PYTHON-PORT.md « Étape 8 »).
 *
 *     agent [--cwd DIR] [--mode ask|accept-edits|auto-approve] [--load ID | --continue] [--agent CMD] [--log FICHIER]
 *
 * Il lance vibe-acp (libacp : ACP sur deux tubes), se connecte au compte Mistral par navigateur s'il le faut (l'URL
 * est affichée, à ouvrir sur cet appareil ou un autre), ouvre une séance en mode « ask » (les modifications et les
 * commandes demandent une approbation), puis : une invite d'une ligne, le texte du modèle au fil de l'eau, les appels
 * d'outils sur une ligne (» bash: git status), les diffs en couleur, les approbations par une question.
 *
 * Touches : Entrée envoie ; Échap ou Ctrl+C pendant un tour l'interrompt (session/cancel) ; Ctrl+C à l'invite efface
 * la ligne, deux fois de suite quitte ; Ctrl+D sur une ligne vide quitte ; Maj+Tab change de mode ; flèches, Début,
 * Fin, Suppr, historique (haut, bas). Commandes : /mode [nom], /session, /help, /quit.
 *
 * Le terminal est en mode brut (sans écho ni signaux) ; toute séquence d'échappement reçue est lue en entier et
 * avalée si elle n'est pas comprise ; le texte de l'agent est filtré (aucun caractère de commande n'en sort tel quel).
 */
#include "acp.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define C_DIM	"\x1b[2m"
#define C_BOLD	"\x1b[1m"
#define C_OFF	"\x1b[0m"
#define C_RED	"\x1b[31m"
#define C_GREEN	"\x1b[32m"
#define C_YEL	"\x1b[33m"
#define C_BLUE	"\x1b[34m"
#define C_CYAN	"\x1b[36m"

static acp_t *A;
static struct termios saved;
static int raw_on;
static volatile sig_atomic_t winch, term_sig;
static int cols = 80;
static double t_start;

/* --- sortie ---------------------------------------------------------------------------------------------------------- */

static jbuf ob;
static int at_bol = 1;		/* la sortie est en début de ligne */
static int status_shown;	/* la ligne d'état (tour en cours) est affichée */
static int input_shown;		/* l'invite est affichée */
static int input_row;		/* ligne du curseur dans l'invite (depuis sa première ligne) */

static void flush (void)
{
	size_t off = 0;
	while (off < ob.n)
	{
		ssize_t w = write (1, ob.p + off, ob.n - off);
		if (w < 0) { if (errno == EINTR) continue; break; }
		off += (size_t) w;
	}
	ob.n = 0;
}

static void out (const char *s) { jb_puts (&ob, s); }
static void outn (const char *s, size_t n) { jb_putn (&ob, s, n); }
static void outf (const char *fmt, ...)
{
	char tmp[2048];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (tmp, sizeof tmp, fmt, ap);
	va_end (ap);
	out (tmp);
}

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (double) t.tv_sec + t.tv_nsec / 1e9;
}

/* largeur affichée d'un texte UTF-8 (un caractère = une colonne) */
static int width (const char *s, size_t n)
{
	int w = 0;
	for (size_t i = 0; i < n; i++) if (((unsigned char) s[i] & 0xC0) != 0x80) w++;
	return w;
}

/* le texte de l'agent : sans caractères de commande (ni ESC, ni CSI de C1) ; \n et \t gardés */
static void out_clean (const char *s, size_t n)
{
	size_t run = 0;
	for (size_t i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) s[i];
		int bad = (c < 0x20 && c != '\n' && c != '\t') || c == 0x7F;
		/* C1 en UTF-8 : C2 80..9F */
		if (c == 0xC2 && i + 1 < n && (unsigned char) s[i + 1] >= 0x80 && (unsigned char) s[i + 1] <= 0x9F) bad = 2;
		if (!bad) continue;
		outn (s + run, i - run);
		if (bad == 2) i++;
		run = i + 1;
	}
	outn (s + run, n - run);
	if (n) at_bol = s[n - 1] == '\n';
}

/* --- l'invite ---------------------------------------------------------------------------------------------------------- */

static char line[8192];
static size_t len, cur;		/* octets, curseur en octets */
static char *hist[200];
static int nhist, hpos;
static const char *PROMPT_ASK = "› ";

static void hide_input (void)
{
	if (!input_shown) return;
	if (input_row) outf ("\x1b[%dA", input_row);
	out ("\r\x1b[J");
	input_shown = 0;
	input_row = 0;
}

static void hide_status (void)
{
	if (!status_shown) return;
	out ("\r\x1b[K");
	status_shown = 0;
}

/* avant d'écrire autre chose : l'invite et la ligne d'état s'effacent */
static void clear_ui (void)
{
	hide_status ();
	hide_input ();
}

static void newline_if_needed (void)
{
	if (!at_bol) { out ("\n"); at_bol = 1; }
}

static void show_input (void)
{
	hide_input ();
	char pr[64];
	snprintf (pr, sizeof pr, "%s", PROMPT_ASK);
	const char *mode = acp_mode (A);
	int plen = width (pr, strlen (pr));
	out (C_BOLD C_BLUE);
	out (pr);
	out (C_OFF);
	outn (line, len);
	int total = plen + width (line, len);
	if (!len)
	{
		/* l'aide, en gris, après l'invite vide */
		char hint[160];
		snprintf (hint, sizeof hint, "mode %s · Maj+Tab : mode · /help", *mode ? mode : "?");
		int room = cols - plen - 1;
		if (room > 8)
		{
			out (C_DIM);
			outn (hint, strlen (hint) < (size_t) room ? strlen (hint) : (size_t) room);
			out (C_OFF);
			outf ("\r\x1b[%dC", plen);
		}
		input_row = 0;
		input_shown = 1;
		return;
	}
	if (total % cols == 0) out (" \r");
	int erow = total / cols;
	int p = plen + width (line, cur);
	int trow = p / cols, tcol = p % cols;
	if (erow > trow) outf ("\x1b[%dA", erow - trow);
	out ("\r");
	if (tcol) outf ("\x1b[%dC", tcol);
	input_row = trow;
	input_shown = 1;
}

static const char spin[] = "|/-\\";
static int spin_i;
static double turn_t0;

static void show_status (void)
{
	if (!at_bol || status_shown) return;
	double dt = now () - turn_t0;
	char s[200];
	const char *what = acp_pending_permission (A) ? "approbation" : "Vibe travaille";
	snprintf (s, sizeof s, "%c %s… %.0f s · Échap : interrompre", spin[spin_i & 3], what, dt);
	out (C_DIM);
	outn (s, strlen (s) < (size_t) cols - 1 ? strlen (s) : (size_t) cols - 1);
	out (C_OFF);
	status_shown = 1;
}

/* --- le texte du modèle : un peu de Markdown -------------------------------------------------------------------- */

static int md_bold, md_code, md_block, md_star, md_tick, md_bol = 1, md_hash;

static void md_style (void)
{
	out (C_OFF);
	if (md_block) out (C_DIM);
	if (md_bold) out (C_BOLD);
	if (md_code) out (C_CYAN);
}

static void md_flush_ticks (void)
{
	if (md_tick == 1) { md_code = !md_code; md_style (); }
	else if (md_tick == 3) { md_block = !md_block; md_code = 0; md_style (); }
	else for (int i = 0; i < md_tick; i++) out ("`");
	md_tick = 0;
}

static void md_text (const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		char c = s[i];
		if (c != '`' && md_tick) md_flush_ticks ();
		if (c != '*' && md_star)
		{
			if (md_star == 2 && !md_block && !md_code) { md_bold = !md_bold; md_style (); }
			else for (int k = 0; k < md_star; k++) out ("*");
			md_star = 0;
		}
		if (c == '`') { md_tick++; continue; }
		if (c == '*' && !md_code && !md_block && md_star < 2) { md_star++; continue; }
		if (md_bol && c == '#' && !md_block) { md_hash = 1; continue; }
		if (md_hash && c == ' ') { md_hash = 2; md_bold = 1; md_style (); md_bol = 0; continue; }
		if (md_hash == 1) { out ("#"); }
		if (c == '\n')
		{
			if (md_hash == 2) { md_bold = 0; md_style (); }
			md_hash = 0;
			md_bol = 1;
			out_clean ("\n", 1);
			continue;
		}
		if (md_hash == 1) md_hash = 0;
		md_bol = 0;
		out_clean (&c, 1);
	}
}

static void md_end (void)
{
	if (md_tick) md_flush_ticks ();
	for (int k = 0; k < md_star; k++) out ("*");
	md_star = md_bold = md_code = md_block = md_hash = 0;
	md_bol = 1;
	out (C_OFF);
}

/* --- diffs -------------------------------------------------------------------------------------------------------------- */

typedef struct { const char *p; size_t n; } span;

static int split (const char *s, span **out_lines)
{
	int n = 0, cap = 64;
	span *v = malloc (cap * sizeof *v);
	const char *b = s;
	for (const char *p = s;; p++)
	{
		if (*p == '\n' || !*p)
		{
			if (*p || p > b)
			{
				if (n == cap) v = realloc (v, (cap *= 2) * sizeof *v);
				v[n++] = (span) { b, (size_t) (p - b) };
			}
			if (!*p) break;
			b = p + 1;
		}
	}
	*out_lines = v;
	return n;
}

static int same (span a, span b) { return a.n == b.n && !memcmp (a.p, b.p, a.n); }

static void show_diff (const char *path, const char *old, const char *new_)
{
	span *a, *b;
	int na = split (old, &a), nb = split (new_, &b);
	int pre = 0, suf = 0;
	while (pre < na && pre < nb && same (a[pre], b[pre])) pre++;
	while (suf < na - pre && suf < nb - pre && same (a[na - 1 - suf], b[nb - 1 - suf])) suf++;
	newline_if_needed ();
	outf (C_DIM "  ┄ %s" C_OFF "\n", path);
	int shown = 0, ctx0 = pre > 2 ? pre - 2 : 0;
	for (int i = ctx0; i < pre; i++) { out (C_DIM "    "); out_clean (a[i].p, a[i].n); out (C_OFF "\n"); }
	for (int i = pre; i < na - suf && shown < 40; i++, shown++)
	{ out (C_RED "  - "); out_clean (a[i].p, a[i].n); out (C_OFF "\n"); }
	for (int i = pre; i < nb - suf && shown < 40; i++, shown++)
	{ out (C_GREEN "  + "); out_clean (b[i].p, b[i].n); out (C_OFF "\n"); }
	if (shown >= 40) out (C_DIM "    …" C_OFF "\n");
	for (int i = nb - suf, k = 0; i < nb && k < 2; i++, k++) { out (C_DIM "    "); out_clean (b[i].p, b[i].n); out (C_OFF "\n"); }
	at_bol = 1;
	free (a);
	free (b);
}

/* --- les événements de libacp ---------------------------------------------------------------------------------------- */

static char *shown_ids[512];
static int nshown;

static int mark_shown (const char *id)
{
	for (int i = 0; i < nshown; i++) if (!strcmp (shown_ids[i], id)) return 0;
	if (nshown < 512) shown_ids[nshown++] = strdup (id);
	return 1;
}

static long perm_req = -1;
static int perm_n;
static char *perm_ids[16];
static char perm_keys[16];

static int text_kind = -1;	/* le genre du dernier texte écrit (agent, pensée, utilisateur rejoué) */

static void end_text (void)
{
	if (text_kind >= 0) { md_end (); text_kind = -1; }
}

static void ev_ready (void *ud, const char *agent, const char *version)
{
	(void) ud;
	clear_ui ();
	outf (C_DIM "%s %s prêt en %.1f s" C_OFF "\n", agent, version, now () - t_start);
	flush ();
}

static void ev_sign_in (void *ud, const char *url, const char *expires)
{
	(void) ud;
	clear_ui ();
	newline_if_needed ();
	out (C_BOLD "Connexion au compte Mistral." C_OFF " Ouvrez ce lien dans un navigateur (ici ou sur un autre appareil) :\n\n  ");
	out (C_CYAN);
	out_clean (url, strlen (url));
	out (C_OFF "\n\n");
	if (*expires) { out (C_DIM "Le lien expire à "); out_clean (expires, strlen (expires)); out (" (UTC). En attente…" C_OFF "\n"); }
	at_bol = 1;
	flush ();
}

static void ev_signed_in (void *ud, int ok, const char *st)
{
	(void) ud;
	clear_ui ();
	if (ok) out (C_GREEN "Connecté." C_OFF " La clé est rangée par Vibe dans $VIBE_HOME/.env.\n");
	else outf (C_RED "Connexion non faite (%s)." C_OFF "\n", st);
	flush ();
}

static void ev_session (void *ud, const char *id, const char *mode, int loaded)
{
	(void) ud;
	end_text ();
	clear_ui ();
	newline_if_needed ();
	outf (C_DIM "Séance %s %s, mode %s (%.1f s depuis le lancement)" C_OFF "\n",
		id, loaded ? "reprise" : "ouverte", mode, now () - t_start);
	if (!loaded) out (C_DIM "Le mode « ask » demande votre accord avant chaque modification et chaque commande." C_OFF "\n");
	show_input ();
	flush ();
}

static void ev_mode (void *ud, const char *mode)
{
	(void) ud;
	clear_ui ();
	newline_if_needed ();
	outf (C_DIM "Mode : %s" C_OFF "\n", mode);
	if (acp_state (A) == ACP_IDLE) show_input ();
	flush ();
}

static void ev_text (void *ud, int kind, const char *text)
{
	(void) ud;
	/* le texte de l'utilisateur : rejoué par session/load ; pendant un tour, Vibe renvoie la demande déjà affichée */
	if (kind == ACP_USER_TEXT && acp_state (A) == ACP_TURN) return;
	clear_ui ();
	if (kind != text_kind)
	{
		end_text ();
		newline_if_needed ();
		if (kind == ACP_USER_TEXT) out (C_BOLD C_BLUE "› " C_OFF);
		else if (kind == ACP_THOUGHT_TEXT) out (C_DIM);
		text_kind = kind;
		if (kind == ACP_THOUGHT_TEXT) md_style (), out (C_DIM);
	}
	if (kind == ACP_THOUGHT_TEXT) out_clean (text, strlen (text));
	else md_text (text, strlen (text));
	flush ();
}

static void ev_tool (void *ud, const acp_tool *t)
{
	(void) ud;
	end_text ();
	clear_ui ();
	if (t->is_new)
	{
		newline_if_needed ();
		out (C_YEL "» " C_OFF);
		out_clean (t->title, strlen (t->title));
		out ("\n");
		at_bol = 1;
	}
	int done = !strcmp (t->status, "completed"), failed = !strcmp (t->status, "failed");
	if ((done || failed) && mark_shown (t->id))
	{
		if (t->diff_path) show_diff (t->diff_path, t->diff_old, t->diff_new);
		else if (*t->text && (!strcmp (t->kind, "execute") || failed))
		{
			/* la sortie d'une commande : ses premières lignes */
			span *l;
			int n = split (t->text, &l);
			for (int i = 0; i < n && i < 12; i++)
			{
				out (failed ? C_RED "  │ " : C_DIM "  │ ");
				out_clean (l[i].p, l[i].n);
				out (C_OFF "\n");
			}
			if (n > 12) outf (C_DIM "  │ … (%d lignes)" C_OFF "\n", n);
			free (l);
			at_bol = 1;
		}
		if (failed) { out (C_RED "  échec" C_OFF "\n"); at_bol = 1; }
	}
	flush ();
}

static const char *option_label (const acp_option *o, char *key)
{
	if (!strcmp (o->id, "allow_once")) { *key = 'o'; return "oui, une fois"; }
	if (!strcmp (o->id, "allow_always")) { *key = 's'; return "oui, pour cette séance"; }
	if (!strcmp (o->id, "allow_always_permanent")) { *key = 't'; return "toujours"; }
	if (!strcmp (o->id, "reject_once")) { *key = 'n'; return "non"; }
	if (!strcmp (o->id, "allow_session_exact")) { *key = 's'; return "oui, ce chemin, pour cette séance"; }
	if (!strcmp (o->id, "allow_session_directory_recursive")) { *key = 'd'; return "oui, ce dossier, pour cette séance"; }
	if (!strcmp (o->id, "allow_permanent_exact")) { *key = 't'; return "toujours, ce chemin"; }
	*key = 0;
	return o->name;
}

static void ev_permission (void *ud, long req, const acp_tool *t, int n, const acp_option *o)
{
	(void) ud;
	end_text ();
	clear_ui ();
	newline_if_needed ();
	out (C_BOLD C_YEL "? " C_OFF C_BOLD "Vibe demande votre accord : " C_OFF);
	out_clean (t->title, strlen (t->title));
	out ("\n");
	at_bol = 1;
	if (t->diff_path && mark_shown (t->id)) show_diff (t->diff_path, t->diff_old, t->diff_new);
	for (int i = 0; i < perm_n; i++) free (perm_ids[i]);
	perm_n = 0;
	perm_req = req;
	out ("  ");
	for (int i = 0; i < n && i < 9; i++)
	{
		char k;
		const char *label = option_label (&o[i], &k);
		/* un raccourci par lettre : le premier qui la prend */
		for (int j = 0; j < perm_n; j++) if (perm_keys[j] == k) k = 0;
		perm_ids[perm_n] = strdup (o[i].id);
		perm_keys[perm_n++] = k;
		outf (C_BOLD "[%d%s%c]" C_OFF " ", i + 1, k ? "/" : "", k ? k : ' ');
		out_clean (label, strlen (label));
		out ("   ");
	}
	out ("\n" C_DIM "  Échap : refuser et interrompre le tour" C_OFF "\n");
	flush ();
}

static void ev_turn_end (void *ud, const char *reason)
{
	(void) ud;
	end_text ();
	clear_ui ();
	newline_if_needed ();
	perm_req = -1;
	const char *fr = !strcmp (reason, "end_turn") ? NULL : !strcmp (reason, "cancelled") ? "interrompu"
		: !strcmp (reason, "max_tokens") ? "réponse trop longue" : !strcmp (reason, "refusal") ? "refusé" : reason;
	if (fr) outf (C_DIM "(%s)" C_OFF "\n", fr);
	outf (C_DIM "%.1f s" C_OFF "\n", now () - turn_t0);
	show_input ();
	flush ();
}

static void ev_plan (void *ud, const json_t *entries)
{
	(void) ud;
	end_text ();
	clear_ui ();
	newline_if_needed ();
	out (C_DIM "Plan :" C_OFF "\n");
	for (int i = 0; i < json_len (entries); i++)
	{
		const json_t *e = json_at (entries, i);
		const char *st = json_gets (e, "status");
		const char *c = json_gets (e, "content");
		outf ("  %s ", !strcmp (st, "completed") ? C_GREEN "[x]" C_OFF : !strcmp (st, "in_progress") ? C_YEL "[>]" C_OFF : "[ ]");
		out_clean (c, strlen (c));
		out ("\n");
	}
	at_bol = 1;
	flush ();
}

static void ev_error (void *ud, const char *msg)
{
	(void) ud;
	end_text ();
	clear_ui ();
	newline_if_needed ();
	out (C_RED "! ");
	out_clean (msg, strlen (msg));
	out (C_OFF "\n");
	if (acp_state (A) == ACP_IDLE) show_input ();
	flush ();
}

static int quit;

static void ev_exited (void *ud)
{
	(void) ud;
	clear_ui ();
	newline_if_needed ();
	out (C_RED "L'agent s'est arrêté." C_OFF " (journal : /tmp/agent-vibe.log)\n");
	flush ();
	quit = 1;
}

static void ev_status (void *ud, const char *text)
{
	(void) ud;
	clear_ui ();
	outf (C_DIM "%s…" C_OFF "\n", text);
	flush ();
}

/* --- le clavier ---------------------------------------------------------------------------------------------------------- */

static void raw_mode (void)
{
	struct termios t;
	if (tcgetattr (0, &saved) < 0) return;
	t = saved;
	t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	t.c_iflag &= ~(IXON | ICRNL | INLCR);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr (0, TCSANOW, &t) == 0) raw_on = 1;
}

static void restore (void)
{
	if (raw_on) tcsetattr (0, TCSANOW, &saved);
	raw_on = 0;
}

static void get_size (void)
{
	struct winsize w;
	if (ioctl (1, TIOCGWINSZ, &w) == 0 && w.ws_col > 10) cols = w.ws_col;
}

static void on_winch (int s) { (void) s; winch = 1; }
static void on_term (int s) { term_sig = s; }

static void add_history (const char *s)
{
	if (!*s || (nhist && !strcmp (hist[nhist - 1], s))) return;
	if (nhist == 200) { free (hist[0]); memmove (hist, hist + 1, 199 * sizeof *hist); nhist--; }
	hist[nhist++] = strdup (s);
}

static void set_line (const char *s)
{
	len = strlen (s);
	if (len >= sizeof line) len = sizeof line - 1;
	memcpy (line, s, len);
	line[len] = 0;
	cur = len;
}

static void cycle_mode (void)
{
	const char *modes = acp_modes (A), *m = acp_mode (A);
	if (!*modes) return;
	const char *p = strstr (modes, m);
	const char *next = modes;
	if (*m && p)
	{
		p += strlen (m);
		while (*p == ' ') p++;
		if (*p) next = p;
	}
	char buf[64];
	size_t n = strcspn (next, " ");
	if (n >= sizeof buf) n = sizeof buf - 1;
	memcpy (buf, next, n);
	buf[n] = 0;
	acp_set_mode (A, buf);
}

static void help (void)
{
	out (C_DIM
		"Entrée : envoyer · Échap ou Ctrl+C : interrompre un tour · Ctrl+D : quitter · Maj+Tab : mode\n"
		"/mode [ask|accept-edits|auto-approve] · /session · /quit\n" C_OFF);
}

static void submit (void)
{
	line[len] = 0;
	char *text = line;
	while (*text == ' ') text++;
	clear_ui ();
	if (!*text) { show_input (); return; }
	add_history (text);
	hpos = nhist;
	/* l'invite reste, avec ce qui a été envoyé */
	out (C_BOLD C_BLUE "› " C_OFF);
	out_clean (text, strlen (text));
	out ("\n");
	at_bol = 1;
	if (text[0] == '/')
	{
		if (!strcmp (text, "/quit") || !strcmp (text, "/exit")) quit = 1;
		else if (!strcmp (text, "/help")) help ();
		else if (!strcmp (text, "/session")) outf (C_DIM "séance %s, mode %s, modes : %s" C_OFF "\n",
			acp_session_id (A) ? acp_session_id (A) : "-", acp_mode (A), acp_modes (A));
		else if (!strncmp (text, "/mode", 5))
		{
			const char *m = text + 5;
			while (*m == ' ') m++;
			if (*m) acp_set_mode (A, m);
			else outf (C_DIM "mode %s ; modes : %s" C_OFF "\n", acp_mode (A), acp_modes (A));
		}
		else outf (C_RED "commande inconnue : %s" C_OFF " (/help)\n", text);
		len = cur = 0;
		if (!quit) show_input ();
		return;
	}
	if (!acp_prompt (A, text))
	{
		out (C_RED "La séance n'est pas prête." C_OFF "\n");
		show_input ();
		return;
	}
	len = cur = 0;
	turn_t0 = now ();
	show_status ();
}

static void answer_permission (int i)
{
	if (perm_req < 0 || i < 0 || i >= perm_n) return;
	clear_ui ();
	outf (C_DIM "  → %s" C_OFF "\n", perm_ids[i]);
	at_bol = 1;
	acp_permit (A, perm_req, perm_ids[i]);
	perm_req = -1;
	show_status ();
}

static double last_ctrl_c;

static void interrupt (void)
{
	clear_ui ();
	newline_if_needed ();
	out (C_DIM "^C interruption du tour…" C_OFF "\n");
	at_bol = 1;
	perm_req = -1;
	acp_cancel (A);
}

/* une touche ou une séquence complète */
static void key (const unsigned char *k, size_t n)
{
	int st = acp_state (A);
	int lone_esc = n == 1 && k[0] == 0x1B;
	if (k[0] == 3)		/* Ctrl+C */
	{
		if (st == ACP_TURN) { interrupt (); return; }
		if (len) { len = cur = 0; show_input (); return; }
		if (now () - last_ctrl_c < 2) { quit = 1; return; }
		last_ctrl_c = now ();
		clear_ui ();
		out (C_DIM "(Ctrl+C encore, ou Ctrl+D, pour quitter)" C_OFF "\n");
		show_input ();
		return;
	}
	if (st == ACP_TURN)
	{
		if (lone_esc) { interrupt (); return; }
		if (perm_req >= 0 && n == 1)
		{
			if (k[0] >= '1' && k[0] <= '9') { answer_permission (k[0] - '1'); return; }
			for (int i = 0; i < perm_n; i++) if (perm_keys[i] && perm_keys[i] == (char) (k[0] | 0x20)) { answer_permission (i); return; }
		}
		return;		/* pendant un tour, le reste est ignoré */
	}
	if (st != ACP_IDLE)
	{
		return;
	}
	if (n >= 3 && k[0] == 0x1B && (k[1] == '[' || k[1] == 'O'))
	{
		char f = (char) k[n - 1];
		if (f == 'Z') { cycle_mode (); return; }			/* Maj+Tab */
		if (f == 'D' && cur) { do cur--; while (cur && ((unsigned char) line[cur] & 0xC0) == 0x80); }
		else if (f == 'C' && cur < len) { do cur++; while (cur < len && ((unsigned char) line[cur] & 0xC0) == 0x80); }
		else if (f == 'H' || (f == '~' && k[2] == '1')) cur = 0;
		else if (f == 'F' || (f == '~' && k[2] == '4')) cur = len;
		else if (f == '~' && k[2] == '3' && cur < len)		/* Suppr */
		{
			size_t e = cur + 1;
			while (e < len && ((unsigned char) line[e] & 0xC0) == 0x80) e++;
			memmove (line + cur, line + e, len - e);
			len -= e - cur;
		}
		else if (f == 'A' && hpos > 0) set_line (hist[--hpos]);
		else if (f == 'B' && hpos < nhist) { hpos++; set_line (hpos < nhist ? hist[hpos] : ""); }
		show_input ();
		return;
	}
	if (k[0] == 0x1B) return;	/* Échap seul, Alt+touche, autres séquences : avalés */
	if (k[0] == '\r' || k[0] == '\n') { submit (); return; }
	if (k[0] == 4) { if (!len) quit = 1; return; }		/* Ctrl+D */
	if (k[0] == 0x7F || k[0] == 8)
	{
		if (!cur) return;
		size_t b = cur;
		do b--; while (b && ((unsigned char) line[b] & 0xC0) == 0x80);
		memmove (line + b, line + cur, len - cur);
		len -= cur - b;
		cur = b;
		show_input ();
		return;
	}
	if (k[0] == 1) { cur = 0; show_input (); return; }	/* Ctrl+A */
	if (k[0] == 5) { cur = len; show_input (); return; }	/* Ctrl+E */
	if (k[0] == 21) { memmove (line, line + cur, len - cur); len -= cur; cur = 0; show_input (); return; }	/* Ctrl+U */
	if (k[0] == 12) { out ("\x1b[H\x1b[2J"); input_shown = 0; show_input (); return; }	/* Ctrl+L */
	if (k[0] < 0x20 && k[0] != '\t') return;
	if (len + n >= sizeof line - 1) return;
	memmove (line + cur + n, line + cur, len - cur);
	memcpy (line + cur, k, n);
	len += n;
	cur += n;
	show_input ();
}

/* découpe ce qui est lu en touches : séquences CSI et SS3 entières, caractères UTF-8 entiers */
static unsigned char kb[256];
static size_t kn;
static double esc_at;

static void keys (int final)
{
	size_t i = 0;
	while (i < kn)
	{
		unsigned char c = kb[i];
		size_t n = 1;
		if (c == 0x1B)
		{
			if (i + 1 >= kn)
			{
				if (!final) break;		/* attendre la suite ou le délai */
			}
			else if (kb[i + 1] == '[')
			{
				size_t j = i + 2;
				while (j < kn && !(kb[j] >= 0x40 && kb[j] <= 0x7E)) j++;
				if (j >= kn) { if (!final) break; j = kn - 1; }
				n = j - i + 1;
			}
			else if (kb[i + 1] == 'O') { if (i + 2 >= kn && !final) break; n = i + 2 < kn ? 3 : 2; }
			else n = 2;					/* Alt+touche */
		}
		else if (c >= 0xC0)
		{
			n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
			if (i + n > kn) { if (!final) break; n = kn - i; }
		}
		key (kb + i, n);
		i += n;
		if (quit) break;
	}
	memmove (kb, kb + i, kn - i);
	kn -= i;
	if (kn && kb[0] == 0x1B) esc_at = now ();
}

static void usage (void)
{
	fprintf (stderr, "agent [--cwd DIR] [--mode ask|accept-edits|auto-approve] [--load ID | --continue] [--agent CMD] [--log FICHIER]\n");
	exit (2);
}

int main (int argc, char **argv)
{
	acp_options o = { 0 };
	o.mode = "ask";
	o.sign_in = 1;
	o.client = "korin-agent";
	o.log = "/tmp/agent-vibe.log";
	char *agent_argv[16] = { "vibe-acp", NULL };
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (!strcmp (a, "--cwd") && i + 1 < argc) o.cwd = argv[++i];
		else if (!strcmp (a, "--mode") && i + 1 < argc) o.mode = argv[++i];
		else if (!strcmp (a, "--load") && i + 1 < argc) o.load = argv[++i];
		else if (!strcmp (a, "--continue")) o.load = "last";
		else if (!strcmp (a, "--log") && i + 1 < argc) o.log = argv[++i];
		else if (!strcmp (a, "--agent") && i + 1 < argc)
		{
			/* la commande, découpée aux espaces */
			char *s = strdup (argv[++i]);
			int n = 0;
			for (char *t = strtok (s, " "); t && n < 15; t = strtok (NULL, " ")) agent_argv[n++] = t;
			agent_argv[n] = NULL;
		}
		else usage ();
	}
	o.argv = agent_argv;
	if (!isatty (0)) { fprintf (stderr, "agent : il faut un terminal\n"); return 2; }
	t_start = now ();
	jb_init (&ob);
	get_size ();
	raw_mode ();
	atexit (restore);
	signal (SIGWINCH, on_winch);
	signal (SIGTERM, on_term);
	signal (SIGHUP, on_term);
	out (C_BOLD "agent" C_OFF C_DIM " · Mistral Vibe sous Korin · Ctrl+D pour quitter" C_OFF "\n");
	flush ();
	acp_events ev = { ev_ready, ev_sign_in, ev_signed_in, ev_session, ev_mode, ev_text, ev_tool, ev_permission,
		ev_turn_end, ev_plan, ev_error, ev_exited, ev_status, NULL };
	A = acp_start (&o, &ev, NULL);
	double last_spin = 0;
	while (!quit && !term_sig)
	{
		if (winch)
		{
			winch = 0;
			int had = input_shown;
			clear_ui ();
			get_size ();
			if (had) show_input ();
			else show_status ();
			flush ();
		}
		struct pollfd p[2] = { { 0, POLLIN, 0 }, { acp_fd (A), POLLIN, 0 } };
		int timeout = kn ? 60 : 250;
		int r = poll (p, acp_fd (A) >= 0 ? 2 : 1, timeout);
		if (r < 0 && errno != EINTR) break;
		if (r > 0 && (p[0].revents & POLLIN))
		{
			ssize_t n = read (0, kb + kn, sizeof kb - kn);
			if (n <= 0) break;
			kn += (size_t) n;
			keys (0);
		}
		/* Échap seul : plus rien derrière lui après 50 ms (un caractère UTF-8 coupé, lui, attend sa suite) */
		else if (kn && kb[0] == 0x1B && now () - esc_at > 0.05) keys (1);
		acp_pump (A, 0);		/* ce que l'agent a écrit, et la reprise de la connexion */
		if (acp_state (A) == ACP_TURN && now () - last_spin > 0.5)
		{
			last_spin = now ();
			if (status_shown) { hide_status (); spin_i++; show_status (); }
			else show_status ();
		}
		flush ();
	}
	clear_ui ();
	newline_if_needed ();
	flush ();
	restore ();
	acp_close (A);
	return 0;
}
