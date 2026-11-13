/*
 * acp.c -- libacp : client ACP (acp.h).
 */
#include "acp.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROTOCOL_VERSION 1
#define AUTH_METHOD "browser-auth-delegated"

typedef struct pending {
	long id;
	acp_reply_fn fn;
	void *data;
} pending;

typedef struct tool {
	char *id, *title, *kind, *status, *text, *diff_path, *diff_old, *diff_new;
} tool;

typedef struct perm {
	long req;
	char *id_json;		/* l'identifiant JSON-RPC de la requête de l'agent, tel qu'écrit */
} perm;

struct acp {
	pid_t pid;
	int in, out;		/* vers l'agent (son stdin), depuis l'agent (son stdout) */
	jbuf rd;		/* ligne en cours de lecture */
	long next_id;
	pending *pend;
	int npend, cappend;
	tool *tools;
	int ntools, captools;
	perm *perms;
	int nperms, capperms;
	long next_perm;
	acp_events ev;
	void *ud;
	int state;
	char *cwd, *mode_wanted, *load, *client, *log;
	int sign_in;
	char *session, *mode, *modes, *model;
	char *attempt;		/* connexion en cours */
	double retry_at;	/* reprise de authenticate complete ; 0 : rien */
	double auth_deadline;
};

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (double) t.tv_sec + t.tv_nsec / 1e9;
}

static char *dup0 (const char *s) { return s ? strdup (s) : NULL; }
static void set (char **dst, const char *s) { char *n = dup0 (s); free (*dst); *dst = n; }

static void status (acp_t *a, const char *t) { if (a->ev.status) a->ev.status (a->ud, t); }

static void error_ev (acp_t *a, const char *what, const json_t *err)
{
	char msg[600];
	if (err) snprintf (msg, sizeof msg, "%s : %s (%lld)", what, json_gets (err, "message"), json_int (json_get (err, "code"), 0));
	else snprintf (msg, sizeof msg, "%s", what);
	if (a->ev.error) a->ev.error (a->ud, msg);
}

/* --- le process et les tubes --------------------------------------------------------------------------------------- */

static int write_all (int fd, const char *p, size_t n)
{
	while (n)
	{
		ssize_t w = write (fd, p, n);
		if (w < 0)
		{
			if (errno == EINTR) continue;
			return -1;
		}
		p += w;
		n -= (size_t) w;
	}
	return 0;
}

static int send_line (acp_t *a, jbuf *b)
{
	if (a->in < 0) return -1;
	jb_putc (b, '\n');
	int r = write_all (a->in, b->p, b->n);
	jb_free (b);
	return r;
}

long acp_rpc_request (acp_t *a, const char *method, const char *params, acp_reply_fn fn, void *data)
{
	if (a->state == ACP_DEAD) return -1;
	long id = a->next_id++;
	jbuf b;
	jb_init (&b);
	jb_printf (&b, "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"method\":", id);
	jb_str (&b, method);
	jb_puts (&b, ",\"params\":");
	jb_puts (&b, params ? params : "{}");
	jb_putc (&b, '}');
	if (a->npend == a->cappend)
	{
		a->cappend = a->cappend ? a->cappend * 2 : 8;
		a->pend = realloc (a->pend, (size_t) a->cappend * sizeof *a->pend);
	}
	a->pend[a->npend++] = (pending) { id, fn, data };
	if (send_line (a, &b) < 0) { a->npend--; return -1; }
	return id;
}

int acp_rpc_notify (acp_t *a, const char *method, const char *params)
{
	jbuf b;
	jb_init (&b);
	jb_puts (&b, "{\"jsonrpc\":\"2.0\",\"method\":");
	jb_str (&b, method);
	jb_puts (&b, ",\"params\":");
	jb_puts (&b, params ? params : "{}");
	jb_putc (&b, '}');
	return send_line (a, &b);
}

static int respond_raw (acp_t *a, const char *id_json, const char *result)
{
	jbuf b;
	jb_init (&b);
	jb_printf (&b, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}", id_json, result ? result : "{}");
	return send_line (a, &b);
}

int acp_rpc_respond (acp_t *a, const json_t *id, const char *result)
{
	jbuf i;
	jb_init (&i);
	jb_json (&i, id);
	int r = respond_raw (a, jb_cstr (&i), result);
	jb_free (&i);
	return r;
}

int acp_rpc_error (acp_t *a, const json_t *id, int code, const char *message)
{
	jbuf b;
	jb_init (&b);
	jb_puts (&b, "{\"jsonrpc\":\"2.0\",\"id\":");
	jb_json (&b, id);
	jb_printf (&b, ",\"error\":{\"code\":%d,\"message\":", code);
	jb_str (&b, message);
	jb_puts (&b, "}}");
	return send_line (a, &b);
}

static int spawn (acp_t *a, char *const *argv)
{
	int in[2], out[2];
	if (pipe (in) < 0) return -1;
	if (pipe (out) < 0) { close (in[0]); close (in[1]); return -1; }
	pid_t pid = fork ();
	if (pid < 0) return -1;
	if (pid == 0)
	{
		setpgid (0, 0);		/* hors du groupe du terminal : Ctrl+C ne le vise pas */
		dup2 (in[0], 0);
		dup2 (out[1], 1);
		int e = open (a->log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (e >= 0) { dup2 (e, 2); close (e); }
		close (in[0]); close (in[1]); close (out[0]); close (out[1]);
		if (a->cwd && chdir (a->cwd) < 0) _exit (126);
		execvp (argv[0], argv);
		_exit (127);
	}
	close (in[0]);
	close (out[1]);
	a->pid = pid;
	a->in = in[1];
	a->out = out[0];
	return 0;
}

/* --- les outils connus ----------------------------------------------------------------------------------------------- */

static tool *find_tool (acp_t *a, const char *id, int create)
{
	for (int i = 0; i < a->ntools; i++) if (!strcmp (a->tools[i].id, id)) return &a->tools[i];
	if (!create) return NULL;
	if (a->ntools == a->captools)
	{
		a->captools = a->captools ? a->captools * 2 : 16;
		a->tools = realloc (a->tools, (size_t) a->captools * sizeof *a->tools);
	}
	tool *t = &a->tools[a->ntools++];
	memset (t, 0, sizeof *t);
	t->id = strdup (id);
	return t;
}

/* un tool_call, un tool_call_update ou le toolCall d'une approbation : fusionne dans la table */
static tool *merge_tool (acp_t *a, const json_t *u, int *is_new)
{
	const char *id = json_gets (u, "toolCallId");
	*is_new = find_tool (a, id, 0) == NULL;
	tool *t = find_tool (a, id, 1);
	const json_t *v;
	if ((v = json_get (u, "title")) && v->kind == JSON_STR) set (&t->title, v->s);
	if ((v = json_get (u, "kind")) && v->kind == JSON_STR) set (&t->kind, v->s);
	if ((v = json_get (u, "status")) && v->kind == JSON_STR) set (&t->status, v->s);
	const json_t *content = json_get (u, "content");
	if (content && content->kind == JSON_ARR)
	{
		jbuf text;
		jb_init (&text);
		for (int i = 0; i < content->n; i++)
		{
			const json_t *c = &content->items[i];
			const char *type = json_gets (c, "type");
			if (!strcmp (type, "content"))
			{
				const json_t *in = json_get (c, "content");
				if (!strcmp (json_gets (in, "type"), "text"))
				{
					if (text.n) jb_putc (&text, '\n');
					jb_puts (&text, json_gets (in, "text"));
				}
			}
			else if (!strcmp (type, "diff"))
			{
				set (&t->diff_path, json_gets (c, "path"));
				set (&t->diff_old, json_gets (c, "oldText"));
				set (&t->diff_new, json_gets (c, "newText"));
			}
		}
		if (text.n) set (&t->text, jb_cstr (&text));
		jb_free (&text);
	}
	return t;
}

static acp_tool view (const tool *t, int is_new)
{
	acp_tool v;
	v.id = t->id;
	v.title = t->title ? t->title : t->id;
	v.kind = t->kind ? t->kind : "other";
	v.status = t->status ? t->status : "pending";
	v.text = t->text ? t->text : "";
	v.diff_path = t->diff_path;
	v.diff_old = t->diff_old ? t->diff_old : "";
	v.diff_new = t->diff_new ? t->diff_new : "";
	v.is_new = is_new;
	return v;
}

/* --- la séance ------------------------------------------------------------------------------------------------------ */

static void open_session (acp_t *a);

/* configOptions : le modèle (l'option d'id « model », sa valeur courante, et son nom si la liste le donne) */
static void read_options (acp_t *a, const json_t *opts)
{
	for (int i = 0; i < json_len (opts); i++)
	{
		const json_t *o = json_at (opts, i);
		if (strcmp (json_gets (o, "id"), "model")) continue;
		const char *v = json_gets (o, "currentValue");
		const json_t *list = json_get (o, "options");
		for (int k = 0; k < json_len (list); k++)
			if (!strcmp (json_gets (json_at (list, k), "value"), v) && *json_gets (json_at (list, k), "name"))
				v = json_gets (json_at (list, k), "name");
		set (&a->model, v);
	}
}

static void opened (acp_t *a, const json_t *result, int loaded)
{
	read_options (a, json_get (result, "configOptions"));
	const json_t *modes = json_get (result, "modes");
	set (&a->mode, json_gets (modes, "currentModeId"));
	jbuf m;
	jb_init (&m);
	const json_t *av = json_get (modes, "availableModes");
	for (int i = 0; i < json_len (av); i++)
	{
		if (m.n) jb_putc (&m, ' ');
		jb_puts (&m, json_gets (json_at (av, i), "id"));
	}
	set (&a->modes, jb_cstr (&m));
	jb_free (&m);
	a->state = ACP_IDLE;
	if (a->ev.session) a->ev.session (a->ud, a->session, a->mode, loaded);
	if (a->mode_wanted && *a->mode_wanted && strcmp (a->mode_wanted, a->mode ? a->mode : ""))
		acp_set_mode (a, a->mode_wanted);
}

static void on_loaded (acp_t *a, void *data, const json_t *result, const json_t *err)
{
	(void) data;
	if (err)
	{
		error_ev (a, "session/load", err);
		a->state = ACP_IDLE;
		free (a->load);
		a->load = NULL;		/* une séance neuve à la place */
		open_session (a);
		return;
	}
	opened (a, result, 1);
}

static void auth_complete (acp_t *a);

static void on_auth_complete (acp_t *a, void *data, const json_t *result, const json_t *err)
{
	(void) data;
	if (err)
	{
		/* « Browser sign-in status could not be retrieved » : la première fois, à reprendre */
		if (now () < a->auth_deadline) { a->retry_at = now () + 5; return; }
		error_ev (a, "authenticate", err);
		if (a->ev.signed_in) a->ev.signed_in (a->ud, 0, "expired");
		return;
	}
	const char *st = json_text (json_path (result, "_meta." AUTH_METHOD ".status"));
	int ok = !strcmp (st, "completed");
	if (a->ev.signed_in) a->ev.signed_in (a->ud, ok, st);
	if (ok) { a->sign_in = 0; open_session (a); }
}

static void auth_complete (acp_t *a)
{
	jbuf p;
	jb_init (&p);
	jb_puts (&p, "{\"methodId\":\"" AUTH_METHOD "\",\"_meta\":{\"action\":\"complete\",\"attemptId\":");
	jb_str (&p, a->attempt);
	jb_puts (&p, "}}");
	acp_rpc_request (a, "authenticate", jb_cstr (&p), on_auth_complete, NULL);
	jb_free (&p);
}

static void on_auth_start (acp_t *a, void *data, const json_t *result, const json_t *err)
{
	(void) data;
	if (err) { error_ev (a, "authenticate", err); return; }
	const json_t *m = json_path (result, "_meta." AUTH_METHOD);
	const char *url = json_gets (m, "signInUrl");
	if (!*url) { a->sign_in = 0; open_session (a); return; }	/* déjà connecté */
	set (&a->attempt, json_gets (m, "attemptId"));
	a->auth_deadline = now () + 900;
	if (a->ev.sign_in) a->ev.sign_in (a->ud, url, json_gets (m, "expiresAt"));
	auth_complete (a);
}

static void start_sign_in (acp_t *a)
{
	a->state = ACP_SIGNING_IN;
	acp_rpc_request (a, "authenticate", "{\"methodId\":\"" AUTH_METHOD "\",\"_meta\":{\"action\":\"start\"}}",
		on_auth_start, NULL);
}

static void on_new (acp_t *a, void *data, const json_t *result, const json_t *err)
{
	(void) data;
	if (err)
	{
		/* -32000 Missing API key : pas de clé ; la connexion au compte */
		if (json_int (json_get (err, "code"), 0) == -32000 && a->sign_in) { start_sign_in (a); return; }
		error_ev (a, "session/new", err);
		a->state = ACP_IDLE;
		return;
	}
	set (&a->session, json_gets (result, "sessionId"));
	opened (a, result, 0);
}

static void load_id (acp_t *a, const char *id)
{
	set (&a->session, id);
	jbuf p;
	jb_init (&p);
	jb_puts (&p, "{\"sessionId\":");
	jb_str (&p, id);
	jb_puts (&p, ",\"cwd\":");
	jb_str (&p, a->cwd);
	jb_puts (&p, ",\"mcpServers\":[]}");
	acp_rpc_request (a, "session/load", jb_cstr (&p), on_loaded, NULL);
	jb_free (&p);
}

static void on_list (acp_t *a, void *data, const json_t *result, const json_t *err)
{
	(void) data;
	const json_t *s = json_get (result, "sessions");
	if (err || json_len (s) == 0)
	{
		if (err) error_ev (a, "session/list", err);
		free (a->load);
		a->load = NULL;
		open_session (a);
		return;
	}
	/* la plus récente : celle dont updatedAt est le plus grand (texte ISO 8601, comparable) */
	const json_t *best = json_at (s, 0);
	for (int i = 1; i < s->n; i++)
		if (strcmp (json_gets (&s->items[i], "updatedAt"), json_gets (best, "updatedAt")) > 0) best = &s->items[i];
	load_id (a, json_gets (best, "sessionId"));
}

static void open_session (acp_t *a)
{
	a->state = ACP_OPENING;
	jbuf p;
	jb_init (&p);
	if (a->load && !strcmp (a->load, "last"))
	{
		jb_puts (&p, "{\"cwd\":");
		jb_str (&p, a->cwd);
		jb_putc (&p, '}');
		acp_rpc_request (a, "session/list", jb_cstr (&p), on_list, NULL);
	}
	else if (a->load) load_id (a, a->load);
	else
	{
		jb_puts (&p, "{\"cwd\":");
		jb_str (&p, a->cwd);
		jb_puts (&p, ",\"mcpServers\":[]}");
		acp_rpc_request (a, "session/new", jb_cstr (&p), on_new, NULL);
	}
	jb_free (&p);
}

static void on_init (acp_t *a, void *data, const json_t *result, const json_t *err)
{
	(void) data;
	if (err) { error_ev (a, "initialize", err); return; }
	const json_t *info = json_get (result, "agentInfo");
	if (a->ev.ready) a->ev.ready (a->ud, json_gets (info, "name"), json_gets (info, "version"));
	open_session (a);
}

/* --- messages de l'agent -------------------------------------------------------------------------------------------- */

static void on_update (acp_t *a, const json_t *params)
{
	const json_t *u = json_get (params, "update");
	const char *kind = json_gets (u, "sessionUpdate");
	if (!strcmp (kind, "agent_message_chunk") || !strcmp (kind, "agent_thought_chunk") || !strcmp (kind, "user_message_chunk"))
	{
		const json_t *c = json_get (u, "content");
		if (strcmp (json_gets (c, "type"), "text") || !a->ev.text) return;
		int k = kind[0] == 'u' ? ACP_USER_TEXT : kind[6] == 't' ? ACP_THOUGHT_TEXT : ACP_AGENT_TEXT;
		a->ev.text (a->ud, k, json_gets (c, "text"));
	}
	else if (!strcmp (kind, "tool_call") || !strcmp (kind, "tool_call_update"))
	{
		int is_new;
		tool *t = merge_tool (a, u, &is_new);
		acp_tool v = view (t, is_new);
		if (a->ev.tool) a->ev.tool (a->ud, &v);
	}
	else if (!strcmp (kind, "current_mode_update"))
	{
		set (&a->mode, json_gets (u, "currentModeId"));
		if (a->ev.mode) a->ev.mode (a->ud, a->mode);
	}
	else if (!strcmp (kind, "usage_update"))
	{
		if (a->ev.usage) a->ev.usage (a->ud, (long) json_int (json_get (u, "used"), 0), (long) json_int (json_get (u, "size"), 0));
	}
	else if (!strcmp (kind, "config_option_update"))
		read_options (a, json_get (u, "configOptions"));
	else if (!strcmp (kind, "plan"))
	{
		if (a->ev.plan) a->ev.plan (a->ud, json_get (u, "entries"));
	}
}

static void on_request (acp_t *a, const json_t *msg)
{
	const char *method = json_gets (msg, "method");
	const json_t *id = json_get (msg, "id");
	const json_t *params = json_get (msg, "params");
	if (strcmp (method, "session/request_permission"))
	{
		/* fs/..., terminal/... : pas annoncés par initialize */
		acp_rpc_error (a, id, -32601, "method not found");
		return;
	}
	int is_new;
	tool *t = merge_tool (a, json_get (params, "toolCall"), &is_new);
	acp_tool v = view (t, is_new);
	const json_t *opts = json_get (params, "options");
	int n = json_len (opts);
	acp_option *o = calloc ((size_t) (n ? n : 1), sizeof *o);
	for (int i = 0; i < n; i++)
	{
		o[i].id = json_gets (&opts->items[i], "optionId");
		o[i].name = json_gets (&opts->items[i], "name");
		o[i].kind = json_gets (&opts->items[i], "kind");
	}
	if (a->nperms == a->capperms)
	{
		a->capperms = a->capperms ? a->capperms * 2 : 4;
		a->perms = realloc (a->perms, (size_t) a->capperms * sizeof *a->perms);
	}
	jbuf ij;
	jb_init (&ij);
	jb_json (&ij, id);
	long req = ++a->next_perm;
	a->perms[a->nperms++] = (perm) { req, strdup (jb_cstr (&ij)) };
	jb_free (&ij);
	if (a->ev.permission) a->ev.permission (a->ud, req, &v, n, o);
	else acp_permit (a, req, NULL);
	free (o);
}

static void on_response (acp_t *a, const json_t *msg)
{
	long id = (long) json_int (json_get (msg, "id"), -1);
	for (int i = 0; i < a->npend; i++)
	{
		if (a->pend[i].id != id) continue;
		pending p = a->pend[i];
		memmove (&a->pend[i], &a->pend[i + 1], (size_t) (a->npend - i - 1) * sizeof *a->pend);
		a->npend--;
		const json_t *err = json_get (msg, "error");
		if (p.fn) p.fn (a, p.data, json_get (msg, "result"), err);
		return;
	}
}

static void dispatch (acp_t *a, const char *line, size_t n)
{
	json_t *msg = json_parse (line, n);
	if (!msg) return;	/* pas du JSON (l'agent n'écrit que du JSON-RPC sur stdout) */
	int has_id = json_get (msg, "id") != NULL, has_method = json_get (msg, "method") != NULL;
	if (has_method && has_id) on_request (a, msg);
	else if (has_method)
	{
		if (!strcmp (json_gets (msg, "method"), "session/update")) on_update (a, json_get (msg, "params"));
	}
	else if (has_id) on_response (a, msg);
	json_free (msg);
}

static void died (acp_t *a)
{
	if (a->state == ACP_DEAD) return;
	a->state = ACP_DEAD;
	if (a->out >= 0) close (a->out);
	if (a->in >= 0) close (a->in);
	a->out = a->in = -1;
	/* les requêtes en vol ne recevront rien */
	json_t gone = { 0 };
	(void) gone;
	a->npend = 0;
	if (a->ev.exited) a->ev.exited (a->ud);
}

int acp_pump (acp_t *a, int timeout_ms)
{
	if (a->state == ACP_DEAD) return -1;
	if (a->retry_at)
	{
		double d = a->retry_at - now ();
		if (d <= 0) { a->retry_at = 0; auth_complete (a); }
		else if (timeout_ms < 0 || d * 1000 < timeout_ms) timeout_ms = (int) (d * 1000) + 1;
	}
	struct pollfd p = { a->out, POLLIN, 0 };
	int r = poll (&p, 1, timeout_ms);
	if (r <= 0) return 0;
	char buf[65536];
	ssize_t n = read (a->out, buf, sizeof buf);
	if (n < 0 && errno == EINTR) return 0;
	if (n <= 0) { died (a); return -1; }
	/* chaque ligne complète */
	size_t start = 0;
	for (size_t i = 0; i < (size_t) n; i++)
	{
		if (buf[i] != '\n') continue;
		if (a->rd.n)
		{
			jb_putn (&a->rd, buf + start, i - start);
			dispatch (a, a->rd.p, a->rd.n);
			a->rd.n = 0;
		}
		else dispatch (a, buf + start, i - start);
		start = i + 1;
		if (a->state == ACP_DEAD) return -1;
	}
	if (start < (size_t) n) jb_putn (&a->rd, buf + start, (size_t) n - start);
	return 1;
}

/* --- ce que fait le client ------------------------------------------------------------------------------------------- */

static void on_prompt (acp_t *a, void *data, const json_t *result, const json_t *err)
{
	(void) data;
	if (a->state == ACP_TURN) a->state = ACP_IDLE;
	if (err) { error_ev (a, "session/prompt", err); if (a->ev.turn_end) a->ev.turn_end (a->ud, "error"); return; }
	if (a->ev.turn_end) a->ev.turn_end (a->ud, json_gets (result, "stopReason"));
}

int acp_prompt (acp_t *a, const char *text)
{
	if (a->state != ACP_IDLE || !a->session) return 0;
	jbuf p;
	jb_init (&p);
	jb_puts (&p, "{\"sessionId\":");
	jb_str (&p, a->session);
	jb_puts (&p, ",\"prompt\":[{\"type\":\"text\",\"text\":");
	jb_str (&p, text);
	jb_puts (&p, "}]}");
	long id = acp_rpc_request (a, "session/prompt", jb_cstr (&p), on_prompt, NULL);
	jb_free (&p);
	if (id < 0) return 0;
	a->state = ACP_TURN;
	return 1;
}

int acp_cancel (acp_t *a)
{
	if (a->state != ACP_TURN) return 0;
	/* les approbations en attente sont annulées avec le tour (ACP : « cancelled ») */
	while (a->nperms) acp_permit (a, a->perms[0].req, NULL);
	jbuf p;
	jb_init (&p);
	jb_puts (&p, "{\"sessionId\":");
	jb_str (&p, a->session);
	jb_putc (&p, '}');
	int r = acp_rpc_notify (a, "session/cancel", jb_cstr (&p));
	jb_free (&p);
	return r == 0;
}

static void on_mode (acp_t *a, void *data, const json_t *result, const json_t *err)
{
	(void) result;
	char *mode = data;
	if (err) error_ev (a, "session/set_mode", err);
	else
	{
		set (&a->mode, mode);
		if (a->ev.mode) a->ev.mode (a->ud, a->mode);
	}
	free (mode);
}

int acp_set_mode (acp_t *a, const char *mode)
{
	if (!a->session || a->state == ACP_DEAD) return 0;
	jbuf p;
	jb_init (&p);
	jb_puts (&p, "{\"sessionId\":");
	jb_str (&p, a->session);
	jb_puts (&p, ",\"modeId\":");
	jb_str (&p, mode);
	jb_putc (&p, '}');
	long id = acp_rpc_request (a, "session/set_mode", jb_cstr (&p), on_mode, strdup (mode));
	jb_free (&p);
	return id >= 0;
}

int acp_permit (acp_t *a, long req, const char *option_id)
{
	for (int i = 0; i < a->nperms; i++)
	{
		if (a->perms[i].req != req) continue;
		jbuf r;
		jb_init (&r);
		if (option_id)
		{
			jb_puts (&r, "{\"outcome\":{\"outcome\":\"selected\",\"optionId\":");
			jb_str (&r, option_id);
			jb_puts (&r, "}}");
		}
		else jb_puts (&r, "{\"outcome\":{\"outcome\":\"cancelled\"}}");
		int ok = respond_raw (a, a->perms[i].id_json, jb_cstr (&r)) == 0;
		jb_free (&r);
		free (a->perms[i].id_json);
		memmove (&a->perms[i], &a->perms[i + 1], (size_t) (a->nperms - i - 1) * sizeof *a->perms);
		a->nperms--;
		return ok;
	}
	return 0;
}

int acp_open (acp_t *a, const char *load, const char *cwd)
{
	if (a->state != ACP_IDLE) return 0;
	set (&a->load, load);
	if (cwd) set (&a->cwd, cwd);
	free (a->session);
	a->session = NULL;
	open_session (a);
	return 1;
}

const char *acp_cwd (acp_t *a) { return a->cwd; }
int acp_pending_permission (acp_t *a) { return a->nperms > 0; }
int acp_fd (acp_t *a) { return a->state == ACP_DEAD ? -1 : a->out; }
int acp_state (acp_t *a) { return a->state; }
const char *acp_session_id (acp_t *a) { return a->session; }
const char *acp_mode (acp_t *a) { return a->mode ? a->mode : ""; }
const char *acp_modes (acp_t *a) { return a->modes ? a->modes : ""; }
const char *acp_model (acp_t *a) { return a->model ? a->model : ""; }

acp_t *acp_start (const acp_options *o, const acp_events *ev, void *ud)
{
	static char *const def[] = { "vibe-acp", NULL };
	acp_t *a = calloc (1, sizeof *a);
	a->ev = *ev;
	a->ud = ud;
	a->next_id = 1;
	a->in = a->out = -1;
	char cwd[1024];
	a->cwd = strdup (o->cwd ? o->cwd : getcwd (cwd, sizeof cwd) ? cwd : "/");
	a->mode_wanted = dup0 (o->mode);
	a->load = dup0 (o->load);
	a->client = strdup (o->client ? o->client : "korin-acp");
	a->log = strdup (o->log ? o->log : "/tmp/acp-agent.log");
	a->sign_in = o->sign_in;
	signal (SIGPIPE, SIG_IGN);
	status (a, "démarrage de l'agent");
	if (spawn (a, o->argv ? o->argv : def) < 0) { a->state = ACP_DEAD; return a; }
	a->state = ACP_STARTING;
	jbuf p;
	jb_init (&p);
	jb_printf (&p, "{\"protocolVersion\":%d,\"clientCapabilities\":{\"fs\":{\"readTextFile\":false,"
		"\"writeTextFile\":false},\"terminal\":false,\"_meta\":{\"" AUTH_METHOD "\":true}},\"clientInfo\":{\"name\":",
		PROTOCOL_VERSION);
	jb_str (&p, a->client);
	jb_puts (&p, ",\"version\":\"1.0\"}}");
	acp_rpc_request (a, "initialize", jb_cstr (&p), on_init, NULL);
	jb_free (&p);
	return a;
}

void acp_close (acp_t *a)
{
	if (!a) return;
	if (a->in >= 0) close (a->in);
	if (a->out >= 0) close (a->out);
	if (a->pid > 0)
	{
		int st;
		for (int i = 0; i < 20 && waitpid (a->pid, &st, WNOHANG) == 0; i++) usleep (100000);
		if (waitpid (a->pid, &st, WNOHANG) == 0)
		{
			kill (a->pid, SIGTERM);
			usleep (200000);
			if (waitpid (a->pid, &st, WNOHANG) == 0) { kill (a->pid, SIGKILL); waitpid (a->pid, &st, 0); }
		}
	}
	for (int i = 0; i < a->ntools; i++)
	{
		tool *t = &a->tools[i];
		free (t->id); free (t->title); free (t->kind); free (t->status); free (t->text);
		free (t->diff_path); free (t->diff_old); free (t->diff_new);
	}
	for (int i = 0; i < a->nperms; i++) free (a->perms[i].id_json);
	free (a->tools); free (a->perms); free (a->pend);
	free (a->cwd); free (a->mode_wanted); free (a->load); free (a->client); free (a->log);
	free (a->session); free (a->mode); free (a->modes); free (a->model); free (a->attempt);
	jb_free (&a->rd);
	free (a);
}
