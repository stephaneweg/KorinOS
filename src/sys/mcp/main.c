/*
 * korin-mcp -- un serveur MCP (Model Context Protocol) pour Korin OS (docs/MCP-KORIN.md) : les agents de code
 * (Claude Code, Vibe) lisent l'état du système par des outils typés qui rendent du JSON (process, processeur,
 * journal du noyau, fichiers, volumes, paquets, services, GPU, réseau, heure, fenêtres), une capture d'écran en
 * PNG, et peuvent envoyer un signal ou lancer un programme, au lieu de lancer des commandes et d'en lire la sortie.
 *
 *   korin-mcp              le serveur, sur stdio : JSON-RPC 2.0, un message par ligne (transport stdio de MCP,
 *                          versions 2024-11-05 à 2025-11-25) ; lancé par le client (Claude Code : /bin/claude
 *                          lui passe /etc/claude-code/mcp.json)
 *   korin-mcp --list       les outils (nom : titre)
 *   korin-mcp --call OUTIL [JSON]   un outil, une fois, le résultat sur la sortie (essais à la main)
 *
 * Programme de /bin, sur libposix, sans les droits de /sys : il lit le système par les appels système (SYS_*),
 * NetKit et les services (vfs, fs.*, intelgpu), jamais en analysant la sortie d'une commande. Il n'écrit rien,
 * sauf les journaux des programmes qu'il lance (/tmp/korin-mcp).
 */
#include "json.h"
#include "korin.h"
#include "tools.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VERSION "1.0.0"

/* ---- NetKit par sa table (rangs : korin_consts.h, tirés de netkit.abi) ---- */
void *netkit_fn(unsigned rank)
{
	static void *kit;
	static int tried;
	if (!tried) {
		tried = 1;
		kit = armpi_kit_open("netkit", NK_VERSION_NEEDED);
	}
	return kit ? armpi_kit_fn(kit, rank) : 0;
}

/* ---- sortie : un message par ligne ---- */
static void send_line(struct jb *b)
{
	jb_raw(b, "\n", 1);
	if (b->oom) {
		static const char e[] = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"mémoire\"}}\n";
		fwrite(e, 1, sizeof e - 1, stdout);
	} else
		fwrite(b->p, 1, b->n, stdout);
	fflush(stdout);
}

static void reply_head(struct jb *b, const struct jv *id)
{
	jb_init(b);
	jb_obj(b);
	jb_kstr(b, "jsonrpc", "2.0");
	jb_key(b, "id");
	if (id)
		jb_json(b, id->raw, id->rawlen);
	else
		jb_null(b);
}

static void reply_error(const struct jv *id, int code, const char *msg)
{
	struct jb b;
	reply_head(&b, id);
	jb_key(&b, "error");
	jb_obj(&b);
	jb_kint(&b, "code", code);
	jb_kstr(&b, "message", msg);
	jb_obj_end(&b);
	jb_obj_end(&b);
	send_line(&b);
	jb_free(&b);
}

/* ---- les versions du protocole connues (la plus récente d'abord) ---- */
static const char *const versions[] = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05", 0 };
static char proto[16] = "2025-11-25";

static const char instructions[] =
	"Korin OS (pas Linux) : ces outils lisent l'état du système en JSON (process, processeur, journal du noyau, "
	"fichiers, volumes, paquets, services, GPU, réseau, heure, fenêtres) et capturent l'écran. Préférez-les aux "
	"commandes de /bin (sysstat, cpuwatch, journal, df, netstat, pkg list, screenshot), dont la sortie est faite "
	"pour un humain. Un seul arbre : / (système), /tmp, /media/usb0, /media/sd1 (données de l'utilisateur : ne "
	"jamais y écrire), HOME:/. launch et send_signal agissent : seulement à la demande de l'utilisateur.";

static void on_initialize(const struct jv *id, const struct jv *params)
{
	const char *want = json_str(params, "protocolVersion", 0);
	snprintf(proto, sizeof proto, "%s", versions[0]);
	for (int i = 0; want && versions[i]; i++)
		if (!strcmp(want, versions[i]))
			snprintf(proto, sizeof proto, "%s", want);
	struct jb b;
	reply_head(&b, id);
	jb_key(&b, "result");
	jb_obj(&b);
	jb_kstr(&b, "protocolVersion", proto);
	jb_key(&b, "capabilities");
	jb_obj(&b);
	jb_key(&b, "tools");
	jb_obj(&b);
	jb_kbool(&b, "listChanged", 0);
	jb_obj_end(&b);
	jb_obj_end(&b);
	jb_key(&b, "serverInfo");
	jb_obj(&b);
	jb_kstr(&b, "name", "korin-mcp");
	jb_kstr(&b, "title", "Korin OS");
	jb_kstr(&b, "version", VERSION);
	jb_obj_end(&b);
	jb_kstr(&b, "instructions", instructions);
	jb_obj_end(&b);
	jb_obj_end(&b);
	send_line(&b);
	jb_free(&b);
}

static void on_tools_list(const struct jv *id)
{
	struct jb b;
	reply_head(&b, id);
	jb_key(&b, "result");
	jb_obj(&b);
	jb_key(&b, "tools");
	jb_arr(&b);
	int titles = strcmp(proto, "2025-06-18") >= 0; /* (title : depuis 2025-06-18) */
	for (int i = 0; i < ntools; i++) {
		const struct tool *t = &tools[i];
		jb_obj(&b);
		jb_kstr(&b, "name", t->name);
		if (titles)
			jb_kstr(&b, "title", t->title);
		jb_kstr(&b, "description", t->description);
		jb_key(&b, "inputSchema");
		jb_json(&b, t->schema, strlen(t->schema));
		jb_key(&b, "annotations");
		jb_obj(&b);
		jb_kstr(&b, "title", t->title);
		jb_kbool(&b, "readOnlyHint", t->read_only);
		if (!t->read_only)
			jb_kbool(&b, "destructiveHint", t->destructive);
		jb_kbool(&b, "openWorldHint", 0);
		jb_obj_end(&b);
		jb_obj_end(&b);
	}
	jb_arr_end(&b);
	jb_obj_end(&b);
	jb_obj_end(&b);
	send_line(&b);
	jb_free(&b);
}

static const struct tool *find_tool(const char *name)
{
	for (int i = 0; name && i < ntools; i++)
		if (!strcmp(tools[i].name, name))
			return &tools[i];
	return 0;
}

static void run_tool(const struct tool *t, const struct jv *args, struct result *r)
{
	memset(r, 0, sizeof *r);
	jb_init(&r->j);
	static const struct jv empty = { .t = J_OBJ };
	t->run(args && args->t == J_OBJ ? args : &empty, r);
	if (!r->err[0] && r->j.oom)
		snprintf(r->err, sizeof r->err, "mémoire (réponse trop grande)");
}

static void on_tools_call(const struct jv *id, const struct jv *params)
{
	const char *name = json_str(params, "name", 0);
	const struct tool *t = find_tool(name);
	if (!t) {
		char m[160];
		snprintf(m, sizeof m, "outil inconnu : %s", name ? name : "(sans nom)");
		reply_error(id, -32602, m);
		return;
	}
	struct result r;
	run_tool(t, json_get(params, "arguments"), &r);
	struct jb b;
	reply_head(&b, id);
	jb_key(&b, "result");
	jb_obj(&b);
	jb_key(&b, "content");
	jb_arr(&b);
	if (r.png && !r.err[0]) {
		jb_obj(&b);
		jb_kstr(&b, "type", "image");
		jb_kstr(&b, "mimeType", "image/png");
		jb_key(&b, "data");
		jb_base64(&b, r.png, r.png_len);
		jb_obj_end(&b);
	}
	jb_obj(&b);
	jb_kstr(&b, "type", "text");
	jb_key(&b, "text");
	if (r.err[0])
		jb_str(&b, r.err);
	else
		jb_strn(&b, r.j.p ? r.j.p : "{}", r.j.p ? r.j.n : 2);
	jb_obj_end(&b);
	jb_arr_end(&b);
	jb_kbool(&b, "isError", r.err[0] != 0);
	jb_obj_end(&b);
	jb_obj_end(&b);
	send_line(&b);
	jb_free(&b);
	jb_free(&r.j);
	free(r.png);
}

static void handle(const char *line, size_t len)
{
	struct jv *m = json_parse(line, len);
	if (!m || m->t != J_OBJ) {
		reply_error(0, -32700, "JSON illisible");
		json_free(m);
		return;
	}
	const struct jv *id = json_get(m, "id");
	const char *method = json_str(m, "method", 0);
	const struct jv *params = json_get(m, "params");
	if (!method) { /* une réponse du client (rien ne lui est demandé) : ignorée */
		json_free(m);
		return;
	}
	if (!id) { /* une notification (initialized, cancelled…) : rien à rendre */
		json_free(m);
		return;
	}
	if (!strcmp(method, "initialize"))
		on_initialize(id, params);
	else if (!strcmp(method, "ping")) {
		struct jb b;
		reply_head(&b, id);
		jb_key(&b, "result");
		jb_obj(&b);
		jb_obj_end(&b);
		jb_obj_end(&b);
		send_line(&b);
		jb_free(&b);
	} else if (!strcmp(method, "tools/list"))
		on_tools_list(id);
	else if (!strcmp(method, "tools/call"))
		on_tools_call(id, params);
	else /* (server/discover du protocole sans état, resources, prompts : inconnus, le client se replie) */
		reply_error(id, -32601, "méthode inconnue");
	json_free(m);
}

/* ---- essais à la main ---- */
static int cli(int argc, char **argv)
{
	if (!strcmp(argv[1], "--list")) {
		for (int i = 0; i < ntools; i++)
			printf("%-12s %s\n", tools[i].name, tools[i].title);
		return 0;
	}
	if (!strcmp(argv[1], "--call") && argc >= 3) {
		const struct tool *t = find_tool(argv[2]);
		if (!t) {
			fprintf(stderr, "korin-mcp : outil inconnu : %s (--list)\n", argv[2]);
			return 2;
		}
		struct jv *args = argc >= 4 ? json_parse(argv[3], strlen(argv[3])) : 0;
		if (argc >= 4 && !args) {
			fprintf(stderr, "korin-mcp : arguments : JSON illisible\n");
			return 2;
		}
		struct result r;
		run_tool(t, args, &r);
		json_free(args);
		if (r.err[0]) {
			fprintf(stderr, "korin-mcp : %s\n", r.err);
			return 1;
		}
		fwrite(r.j.p ? r.j.p : "{}", 1, r.j.p ? r.j.n : 2, stdout);
		printf("\n");
		if (r.png) {
			FILE *f = fopen("/tmp/korin-mcp.png", "wb");
			if (f) {
				fwrite(r.png, 1, r.png_len, f);
				fclose(f);
				printf("(image : /tmp/korin-mcp.png, %zu octets)\n", r.png_len);
			}
		}
		return 0;
	}
	fprintf(stderr, "usage : korin-mcp            (serveur MCP sur stdio)\n"
			"        korin-mcp --list\n"
			"        korin-mcp --call OUTIL ['{\"argument\": valeur}']\n");
	return 2;
}

int main(int argc, char **argv)
{
	if (argc >= 2)
		return cli(argc, argv);
	signal(SIGPIPE, SIG_IGN);
	size_t cap = 1 << 16, n = 0;
	char *buf = malloc(cap);
	if (!buf)
		return 1;
	/* les lignes de stdin, une requête chacune (une ligne vide est ignorée) ; fin de stdin : fin du serveur */
	for (;;) {
		char *nl = memchr(buf, '\n', n);
		if (nl) {
			size_t l = (size_t)(nl - buf);
			size_t s = 0;
			while (s < l && (buf[s] == ' ' || buf[s] == '\r' || buf[s] == '\t'))
				s++;
			size_t e = l;
			while (e > s && (buf[e - 1] == '\r' || buf[e - 1] == ' '))
				e--;
			if (e > s)
				handle(buf + s, e - s);
			memmove(buf, nl + 1, n - l - 1);
			n -= l + 1;
			continue;
		}
		if (n + 4096 > cap) {
			if (cap >= (64u << 20)) { /* (une ligne de 64 Mo : pas une requête) */
				n = 0;
				continue;
			}
			char *nb = realloc(buf, cap *= 2);
			if (!nb)
				return 1;
			buf = nb;
		}
		ssize_t k = read(0, buf + n, cap - n);
		if (k <= 0) {
			if (n)
				handle(buf, n);
			break;
		}
		n += (size_t)k;
	}
	free(buf);
	return 0;
}
