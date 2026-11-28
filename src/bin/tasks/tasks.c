/* tasks - les tâches planifiées en ligne de commande (docs/TACHES.md ; le service taskd, src/svc/taskd).
 *
 *	tasks [list]                    les tâches (les miennes, puis celles du système)
 *	tasks show <id>                 une tâche : son fichier et son état
 *	tasks run <id>                  la lancer tout de suite (par taskd)
 *	tasks log <id>                  la sortie de son dernier passage
 *	tasks enable|disable <id>       l'activer, la désactiver
 *	tasks add <id> "<quand>" <commande...>   une tâche nouvelle (une ligne de bash)
 *	tasks remove <id>               la retirer (son état et sa sortie avec)
 * -s (avant la commande) : les tâches du système (/etc/tasks ; écrire demande l'uid 0 : sudo tasks -s ...).
 * Les tâches de l'utilisateur sont dans HOME:/etc/tasks. Exemples de <quand> : « daily 03:00 », « every 2 h »,
 * « weekly mon,fri 18:30 », « startup », « event korin.net.state up* » (taskfile.h). */
#include "korinrt.h"
#include "buskit_import.h"
#include "../../svc/taskd/taskfile.c"

static int sys_level;
static char text[TK_FILE + 1];

static const char *root(void) { return sys_level ? "" : "HOME:"; }

static void path_of(char *out, long cap, const char *dir, const char *id, const char *ext)
{
	snprintf(out, (size_t)cap, "%s/%s/tasks/%s%s", root(), dir, id, ext);
}

static long read_file(const char *path, char *buf, long cap)
{
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return -1;
	long n = sys_read(fd, buf, (u64)(cap - 1));
	sys_close(fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	return n;
}

static long write_file(const char *path, const char *buf, long n)
{
	long fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long w = sys_write(fd, buf, (u64)n);
	sys_close(fd);
	return w == n ? 0 : -1;
}

static long state_val(const char *s, const char *key)
{
	long kl = str_len(key);
	for (const char *l = s; *l;) {
		while (*l == ' ')
			l++;
		if (!strncmp(l, key, (size_t)kl) && (l[kl] == ' ' || l[kl] == '=')) {
			const char *v = l + kl;
			while (*v == ' ' || *v == '=')
				v++;
			return strtol(v, 0, 10);
		}
		while (*l && *l != '\n')
			l++;
		if (*l)
			l++;
	}
	return 0;
}

static void put_date(long utc)
{
	if (utc <= 0) {
		out_puts("-");
		return;
	}
	struct tk_tm t;
	tk_civil(utc + sys_clock(1), &t);
	printf("%04d-%02d-%02d %02d:%02d", t.year, t.month, t.day, t.hour, t.min);
}

static int load(const char *id, struct task *t)
{
	char p[200];
	path_of(p, sizeof p, "etc", id, ".task");
	memset(t, 0, sizeof *t);
	tk_copy(t->id, id, TK_ID);
	if (read_file(p, text, sizeof text) < 0) {
		eprintf("tasks: %s : pas de telle tâche\n", id);
		return -1;
	}
	if (tk_parse(t, text) < 0) {
		eprintf("tasks: %s : fichier illisible\n", p);
		return -1;
	}
	return 0;
}

static int save(const struct task *t)
{
	char p[200], d[200];
	snprintf(d, sizeof d, "%s/etc", root());
	sys_mkdir(d);
	snprintf(d, sizeof d, "%s/etc/tasks", root());
	sys_mkdir(d);
	path_of(p, sizeof p, "etc", t->id, ".task");
	long n = tk_format(t, text, sizeof text);
	if (write_file(p, text, n) < 0) {
		eprintf(sys_level ? "tasks: %s : écriture refusée (sudo tasks -s ...)\n" : "tasks: %s : écriture refusée\n", p);
		return 1;
	}
	if (bus_available() == 1)
		bus_publish((long)"korin.tasks.changed", (long)t->id, -1, 0);
	return 0;
}

static void list_level(int level)
{
	sys_level = level;
	char dir[200];
	snprintf(dir, sizeof dir, "%s/etc/tasks", root());
	long fd = sys_open(dir, O_READ);
	if (fd < 0)
		return;
	printf("%s :\n", level ? "Tâches du système (/etc/tasks)" : "Mes tâches (HOME:/etc/tasks)");
	struct dirent de;
	struct task t;
	while (sys_readdir(fd, &de) == 1) {
		if (!str_ends(de.name, ".task"))
			continue;
		char id[TK_ID];
		long n = str_len(de.name) - 5;
		if (n <= 0 || n >= TK_ID)
			continue;
		memcpy(id, de.name, (size_t)n);
		id[n] = 0;
		char p[260];
		snprintf(p, sizeof p, "%s/%s", dir, de.name);
		memset(&t, 0, sizeof t);
		if (read_file(p, text, sizeof text) < 0 || tk_parse(&t, text) < 0) {
			printf("  %-20s (illisible)\n", id);
			continue;
		}
		char w[200] = "-";
		if (t.nwhen)
			tk_when_format(&t.when[0], w, sizeof w);
		char sp[200], st[400];
		path_of(sp, sizeof sp, "var", id, ".state");
		if (read_file(sp, st, sizeof st) < 0)
			st[0] = 0;
		printf("  %-20s %-3s %-28s ", id, t.enabled ? "oui" : "non", w);
		if (state_val(st, "running"))
			out_puts("en cours");
		else {
			out_puts("dernier ");
			put_date(state_val(st, "last"));
			if (state_val(st, "last"))
				printf(" (code %ld)", state_val(st, "code"));
		}
		long nx = state_val(st, "next");
		if (nx > 0) {
			out_puts(", prochain ");
			put_date(nx);
		}
		out_nl();
	}
	sys_close(fd);
}

static int usage(void)
{
	eprintf("usage : tasks [-s] [list | show ID | run ID | log ID | enable ID | disable ID | remove ID |\n"
		"               add ID \"QUAND\" COMMANDE...]\n");
	return 2;
}

int main(int argc, char **argv)
{
	int a = 1;
	if (a < argc && tk_eq(argv[a], "-s")) {
		sys_level = 1;
		a++;
	}
	const char *cmd = a < argc ? argv[a++] : "list";
	const char *id = a < argc ? argv[a++] : 0;
	if (tk_eq(cmd, "list")) {
		int s = sys_level;
		if (!s)
			list_level(0);
		list_level(1);
		return 0;
	}
	if (!id)
		return usage();
	if (!tk_id_ok(id)) {
		eprintf("tasks: %s : nom de tâche invalide (a-z, 0-9, - et _)\n", id);
		return 2;
	}
	struct task t;
	if (tk_eq(cmd, "show")) {
		char p[200], st[400];
		path_of(p, sizeof p, "etc", id, ".task");
		if (read_file(p, text, sizeof text) < 0)
			return load(id, &t) < 0;
		out_puts(text);
		path_of(p, sizeof p, "var", id, ".state");
		if (read_file(p, st, sizeof st) >= 0) {
			out_puts("--- etat\n");
			out_puts(st);
		}
		return 0;
	}
	if (tk_eq(cmd, "log")) {
		char p[200];
		path_of(p, sizeof p, "var", id, ".log");
		long fd = sys_open(p, O_READ);
		if (fd < 0) {
			eprintf("tasks: %s : pas encore de sortie\n", id);
			return 1;
		}
		long n;
		while ((n = sys_read(fd, text, sizeof text)) > 0)
			out_write(text, (u64)n);
		sys_close(fd);
		return 0;
	}
	if (tk_eq(cmd, "run")) {
		if (load(id, &t) < 0)
			return 1;
		char v[80];
		snprintf(v, sizeof v, "%s %s", sys_level ? "system" : "user", id);
		if (bus_available() != 1 || bus_publish((long)"korin.tasks.run", (long)v, -1, 0) < 0) {
			eprintf("tasks: le bus ne répond pas\n");
			return 1;
		}
		printf("%s : demandée à taskd (tasks log %s pour sa sortie)\n", id, id);
		return 0;
	}
	if (tk_eq(cmd, "enable") || tk_eq(cmd, "disable")) {
		if (load(id, &t) < 0)
			return 1;
		t.enabled = tk_eq(cmd, "enable");
		return save(&t);
	}
	if (tk_eq(cmd, "remove")) {
		char p[200];
		path_of(p, sizeof p, "etc", id, ".task");
		if (sys_unlink(p) < 0) {
			eprintf("tasks: %s : pas retirée\n", id);
			return 1;
		}
		path_of(p, sizeof p, "var", id, ".state");
		sys_unlink(p);
		path_of(p, sizeof p, "var", id, ".log");
		sys_unlink(p);
		if (bus_available() == 1)
			bus_publish((long)"korin.tasks.changed", (long)id, -1, 0);
		return 0;
	}
	if (tk_eq(cmd, "add")) {
		memset(&t, 0, sizeof t);
		tk_copy(t.id, id, TK_ID);
		tk_defaults(&t);
		tk_copy(t.name, id, TK_NAME);
		if (a >= argc || !tk_when_parse(&t.when[0], argv[a])) {
			eprintf("tasks: quand ? « daily 03:00 », « every 2 h », « startup »... (docs/TACHES.md)\n");
			return 2;
		}
		t.nwhen = 1;
		a++;
		if (a >= argc)
			return usage();
		t.kind = TK_SHELL;
		long n = 0;
		for (; a < argc; a++) {
			long k = str_len(argv[a]);
			if (n + k + 2 >= TK_RUN)
				break;
			if (n)
				t.run[n++] = ' ';
			memcpy(t.run + n, argv[a], (size_t)k);
			n += k;
		}
		t.run[n] = 0;
		char p[200];
		path_of(p, sizeof p, "etc", id, ".task");
		long st[2];
		if (sys_stat(p, st) >= 0) {
			eprintf("tasks: %s : existe déjà\n", id);
			return 1;
		}
		return save(&t);
	}
	return usage();
}
