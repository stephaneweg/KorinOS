/* taskd - les tâches planifiées de Korin (docs/TACHES.md) : des programmes, des scripts ou des apps lancés à une
 * heure, à intervalles, au démarrage, à l'extinction ou à un événement du bus (docs/BUS.md). Deux niveaux :
 *  * le système : /etc/tasks/<id>.task, lancées en uid 0 ; état et sortie dans /var/tasks/<id>.state et .log ;
 *  * l'utilisateur : <dossier du compte>/etc/tasks/<id>.task, lancées sous le compte de la session, tant qu'elle
 *    est ouverte ; état et sortie dans <dossier du compte>/var/tasks/.
 * Le fichier d'une tâche et ses déclencheurs : taskfile.h. Le volet « Tâches planifiées » de Réglages
 * (applet/applet.cpp) et la commande tasks (src/bin/tasks) les écrivent.
 *
 * Lancé par init2 après les services ; sans sondage : il dort sur le bus (bus_wait) jusqu'à la prochaine échéance,
 * une heure au plus, 30 s sans busd, une seconde tant qu'une tâche tourne (sa fin, sa durée maximale). Sujets :
 *  * korin.tasks.changed (le volet, la commande : un fichier changé) : tout relu ;
 *  * korin.tasks.run « system <id> » ou « user <id> » : la tâche lancée tout de suite ;
 *  * korin.tasks.login (taskd phase login, par init2 à l'ouverture de la session) : les tâches de l'utilisateur
 *    relues, ses tâches « startup » lancées ;
 *  * korin.clock.offset : l'heure réglée (NTP, fuseau) ;
 *  * les sujets des déclencheurs « event », « removed », « power », « battery » ;
 *  * il publie korin.tasks.state « <system|user> <id> run|end <code> » (pas gardé) à chaque début et fin.
 *
 *	taskd                   le service
 *	taskd phase login       (init2) prévient le service que la session est ouverte
 *	taskd phase logout      (init2, avant de fermer la session) les tâches « shutdown » de l'utilisateur, attendues
 *	taskd phase shutdown    (init2, avant d'éteindre) celles de l'utilisateur puis celles du système, attendues
 * Une phase attend ses tâches PHASE_MS au plus en tout (chacune : sa durée maximale, sinon PHASE_TASK_MS). */
#include "../svclib/svclib.h"
#include "buskit_import.h"
#include "taskfile.h"

#define TASK_MAX 64
#define EDGE_MAX 8          /* sujets suivis par déclencheur d'événement */
#define WAIT_MAX 3600000    /* ms : une heure au plus sans regarder l'heure */
#define WAIT_NOBUS 30000    /* sans busd */
#define WAIT_RUN 500        /* une tâche tourne */
#define STALE_S 600         /* une échéance passée depuis plus longtemps : manquée (machine éteinte, saut d'heure) */
#define GRACE_MS 45000      /* après le démarrage, avant les échéances d'heure (l'heure n'est sûre qu'après NTP) */
#define CLOCK_OK 1000000000 /* secondes UTC : l'heure est connue */
#define PHASE_MS 60000
#define PHASE_TASK_MS 30000
#define BOOT_MARK "/tmp/taskd.boot"
#define LOGIN_MARK "/tmp/taskd.login" /* la dernière valeur de korin.tasks.login traitée */

/* src/kits/buskit/buskit.inc */
#define WEV_BUS 13
#define BEV_TOPIC 8
#define BEV_LEN 12
#define BUS_DELETED 0xFFFFFFFFu
#define BUS_NAME_MAX 64
#define BUS_VALUE_MAX 256

#define WAIT_EX_TAG (0x57414954L << 32) /* SYS_WAIT étendu (src/inc/syscalls.inc) */
#define WEX_NOHANG 1
#define WR_EXITED 1
#define CODE_UNKNOWN -1000 /* fin vue, code perdu */
#define CODE_TIMEOUT -1001 /* arrêtée : trop longue */
#define CODE_NOSTART -1002 /* pas lancée */

enum { L_SYSTEM = 0, L_USER = 1 };

struct edge {
	u32 tid;  /* le sujet du bus */
	u32 hash; /* sa dernière valeur (event : une valeur redite ne relance pas) */
	u8 holds;
	u8 used;
};

struct entry {
	struct task t;
	int level;
	long last;   /* UTC : le dernier lancement (0 : jamais) */
	long seen;   /* UTC : la première fois que taskd l'a vue */
	long code;   /* le dernier code de sortie */
	long runs;
	long dur;    /* ms */
	long check;  /* heure locale jusqu'où les échéances de calendrier ont été vues */
	long pid;    /* > 0 : tourne */
	long start_ms, deadline_ms;
	long pending_ms; /* un lancement retardé (delay) : à cette heure du compteur, 0 : aucun */
	long rebase;     /* UTC : every repart d'ici (un passage sauté : condition, déjà en cours, manqué) */
	struct edge edge[TK_WHEN_MAX][EDGE_MAX];
};

static struct entry tasks[TASK_MAX];
static int ntasks;
static long sess_uid = -1;  /* le compte dont les tâches sont chargées (-1 : aucun) */
static int sess_open;
static char user_root[160]; /* son dossier */
static int have_bus;
static u8 ev[32];
static char text[TK_FILE + 1];
static char value[BUS_VALUE_MAX + 1];
static char tname[BUS_NAME_MAX];

static const char s_title[] = "T\xC3\xA2" "ches planifi\xC3\xA9" "es";

/* ---- l'heure */

static long now_utc(void) { return sys_clock(0); }
static long now_local(void) { return sys_clock(0) + sys_clock(1); }
static int clock_known(void) { return now_utc() >= CLOCK_OK; }

/* ---- les chemins : <racine>/etc/tasks, <racine>/var/tasks */

static const char *root_of(int level) { return level == L_USER ? user_root : ""; }

static void path_of(char *out, long cap, int level, const char *dir, const char *id, const char *ext)
{
	snprintf(out, (size_t)cap, "%s/%s/tasks/%s%s", root_of(level), dir, id, ext);
}

static void mkdirs(int level)
{
	char p[200];
	snprintf(p, sizeof p, "%s/var", root_of(level));
	sys_mkdir(p);
	snprintf(p, sizeof p, "%s/var/tasks", root_of(level));
	sys_mkdir(p);
}

/* la session : /tmp/session.ini écrit par l'écran de connexion ; sans /sys/login (le Pi) : toujours ouverte */
static int session_is_open(void)
{
	long st[2];
	return sys_stat("/tmp/session.ini", st) >= 0 || sys_stat("/sys/login", st) < 0;
}

/* ---- l'état d'une tâche : <racine>/var/tasks/<id>.state */

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

static long next_utc(struct entry *e); /* (plus bas) */

static void state_save(struct entry *e)
{
	char p[220], b[400];
	path_of(p, sizeof p, e->level, "var", e->t.id, ".state");
	long nx = next_utc(e);
	int n = snprintf(b, sizeof b, "# taskd (docs/TACHES.md)\r\nseen = %ld\r\nlast = %ld\r\ncode = %ld\r\nruns = %ld\r\n"
			 "duration = %ld\r\nrunning = %d\r\nnext = %ld\r\n",
			 e->seen, e->last, e->code, e->runs, e->dur, e->pid > 0, nx > 0 ? nx : 0);
	mkdirs(e->level);
	write_file(p, b, n);
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

static void state_load(struct entry *e)
{
	char p[220], b[400];
	path_of(p, sizeof p, e->level, "var", e->t.id, ".state");
	if (read_file(p, b, sizeof b) < 0)
		return;
	e->seen = state_val(b, "seen");
	e->last = state_val(b, "last");
	e->code = state_val(b, "code");
	e->runs = state_val(b, "runs");
	e->dur = state_val(b, "duration");
}

/* ---- le bus */

static void publish_state(struct entry *e, const char *what, long code)
{
	if (!have_bus)
		return;
	char b[120];
	snprintf(b, sizeof b, "%s %s %s %ld", e->level == L_USER ? "user" : "system", e->t.id, what, code);
	bus_publish((long)"korin.tasks.state", (long)b, -1, 0);
}

static u32 fnv(const char *s, long n)
{
	u32 h = 2166136261u;
	for (long i = 0; i < n; i++)
		h = (h ^ (u8)s[i]) * 16777619u;
	return h;
}

/* ---- lancer */

static void say_task(struct entry *e, const char *what)
{
	char b[200];
	snprintf(b, sizeof b, "taskd : %s %s : %s\n", e->level == L_USER ? "user" : "system", e->t.id, what);
	say(b);
}

/* « HOME:/x », « ~/x » -> <dossier>/x pour une tâche de l'utilisateur (taskd, en uid 0, a un autre HOME:) */
static void home_path(struct entry *e, const char *in, char *out, long cap)
{
	if (e->level == L_USER && (!strncmp(in, "HOME:/", 6) || !strncmp(in, "~/", 2)))
		snprintf(out, (size_t)cap, "%s/%s", user_root, in + (in[0] == '~' ? 2 : 6));
	else
		tk_copy(out, in, cap);
}

/* la ligne de bash entre guillemets (relue par le fils : \" et \\) */
static void quote(const char *s, char *out, long cap)
{
	long n = 0;
	if (cap < 4)
		return;
	out[n++] = '"';
	for (; *s && n < cap - 3; s++) {
		if (*s == '"' || *s == '\\')
			out[n++] = '\\';
		out[n++] = *s;
	}
	out[n++] = '"';
	out[n] = 0;
}

/* les conditions (seulement sur secteur, seulement avec le réseau) -> 0 si elles tiennent, sinon la raison */
static const char *blocked(struct entry *e)
{
	if (e->level == L_USER && (!session_is_open() || sess_uid != user_session_uid()))
		return "session fermee";
	if (!have_bus)
		return 0;
	char v[BUS_VALUE_MAX + 1];
	if (e->t.ac_only) {
		long n = bus_get((long)"korin.power.battery", (long)v, BUS_VALUE_MAX);
		struct tk_when w;
		memset(&w, 0, sizeof w);
		w.type = TW_POWER;
		w.n = 0;
		if (n > 0) {
			v[n] = 0;
			if (tk_when_holds(&w, v, n))
				return "sur batterie";
		}
	}
	if (e->t.net_only) {
		long n = bus_get((long)"korin.net.state", (long)v, BUS_VALUE_MAX);
		if (n < 2 || v[0] != 'u' || v[1] != 'p')
			return "pas de reseau";
	}
	return 0;
}

/* -> pid, ou une erreur */
static long spawn_task(struct entry *e)
{
	static char path[TK_RUN + 200], args[TK_RUN * 2 + 16], q[TK_RUN * 2 + 8];
	char run[TK_RUN];
	home_path(e, e->t.run, run, sizeof run);
	const char *a = 0;
	if (e->t.kind == TK_SHELL) {
		long st[2];
		tk_copy(path, sys_stat("/bin/bash", st) >= 0 ? "/bin/bash" : "/bin/sh", sizeof path);
		quote(run, q, sizeof q);
		snprintf(args, sizeof args, "-c %s", q);
		a = args;
	} else if (e->t.kind == TK_APP) {
		long st[2];
		snprintf(path, sizeof path, "%s/apps/%s.app/main", e->level == L_USER ? user_root : "", run);
		if (e->level != L_USER || sys_stat(path, st) < 0)
			snprintf(path, sizeof path, "/apps/%s.app/main", run);
	} else {
		char *sp = strchr(run, ' ');
		if (sp) {
			*sp = 0;
			a = sp + 1;
			while (*a == ' ')
				a++;
			if (!*a)
				a = 0;
		}
		if (str_ends(run, ".sh")) { /* un script : par bash (ou sh) */
			long st[2];
			snprintf(args, sizeof args, "%s%s%s", run, a ? " " : "", a ? a : "");
			tk_copy(path, sys_stat("/bin/bash", st) >= 0 ? "/bin/bash" : "/bin/sh", sizeof path);
			a = args;
		} else {
			tk_copy(path, run, sizeof path);
			if (a) {
				tk_copy(args, a, sizeof args);
				a = args;
			}
		}
	}
	/* la sortie et les erreurs dans <id>.log (remplacé à chaque passage) */
	char logp[220], head[300];
	mkdirs(e->level);
	path_of(logp, sizeof logp, e->level, "var", e->t.id, ".log");
	struct tk_tm tm;
	tk_civil(now_local(), &tm);
	int hn = snprintf(head, sizeof head, "# %04d-%02d-%02d %02d:%02d:%02d : %s%s%s\n", tm.year, tm.month, tm.day,
			  tm.hour, tm.min, tm.sec, path, a ? " " : "", a ? a : "");
	write_file(logp, head, hn);
	long fds[3] = {0, 1, 2};
	long hin = f_redir("/dev/null", O_READ);
	long hout = f_redir(logp, O_WRITE);
	if (hin >= 0)
		fds[0] = hin;
	if (hout >= 0)
		fds[1] = fds[2] = hout;
	if (e->level == L_USER)
		sys_chdir(user_root); /* (le dossier du compte, le temps du lancement) */
	if (e->level == L_USER)
		spawn_session_next();
	long pid = f_spawn_stdio(path, a, fds);
	if (e->level == L_USER)
		sys_chdir("/");
	if (hin >= 0)
		sys_close(hin);
	if (hout >= 0)
		sys_close(hout);
	return pid;
}

static void start(struct entry *e)
{
	e->pending_ms = 0;
	if (e->pid > 0) {
		say_task(e, "tourne encore, pas relancee");
		e->rebase = now_utc();
		return;
	}
	const char *why = blocked(e);
	if (why) {
		say_task(e, why);
		e->rebase = now_utc(); /* (ce passage est sauté : every attend un intervalle) */
		return;
	}
	long pid = spawn_task(e);
	e->last = now_utc();
	e->runs++;
	e->start_ms = sys_time_ms();
	if (pid <= 0) {
		e->code = CODE_NOSTART;
		e->dur = 0;
		char b[80];
		snprintf(b, sizeof b, "pas lancee (%ld)", pid);
		say_task(e, b);
		state_save(e);
		if (e->t.notify != TN_NEVER) {
			char t[200];
			snprintf(t, sizeof t, "\xC2\xAB %s \xC2\xBB n'a pas pu \xC3\xAA" "tre lanc\xC3\xA9" "e.", e->t.name);
			notify_send(s_title, t, 0);
		}
		publish_state(e, "end", e->code);
		return;
	}
	e->pid = pid;
	e->deadline_ms = e->t.timeout > 0 ? e->start_ms + e->t.timeout * 1000 : 0;
	char b[48];
	snprintf(b, sizeof b, "lancee (pid %ld)", pid);
	say_task(e, b);
	state_save(e);
	publish_state(e, "run", 0);
}

/* un déclencheur est arrivé : lancée maintenant, ou dans delay secondes */
static void fire(struct entry *e)
{
	if (!e->t.enabled)
		return;
	if (e->t.delay > 0) {
		if (!e->pending_ms)
			e->pending_ms = sys_time_ms() + e->t.delay * 1000;
		return;
	}
	start(e);
}

static void finished(struct entry *e, long code)
{
	e->pid = 0;
	e->code = code;
	e->dur = sys_time_ms() - e->start_ms;
	char b[64];
	snprintf(b, sizeof b, "terminee (code %ld, %ld ms)", code, e->dur);
	say_task(e, b);
	state_save(e);
	publish_state(e, "end", code);
	if (e->t.notify == TN_ALWAYS || (e->t.notify == TN_FAIL && code != 0)) {
		char t[200];
		if (code == 0)
			snprintf(t, sizeof t, "\xC2\xAB %s \xC2\xBB est termin\xC3\xA9" "e.", e->t.name);
		else if (code == CODE_TIMEOUT)
			snprintf(t, sizeof t, "\xC2\xAB %s \xC2\xBB a \xC3\xA9t\xC3\xA9 arr\xC3\xAA" "t\xC3\xA9" "e : trop longue.",
				 e->t.name);
		else
			snprintf(t, sizeof t, "\xC2\xAB %s \xC2\xBB a \xC3\xA9" "chou\xC3\xA9 (code %ld).", e->t.name, code);
		notify_send(s_title, t, 0);
	}
}

/* une tâche qui tourne : finie ? trop longue ? -> 1 si elle ne tourne plus */
static int reap(struct entry *e)
{
	long out = 0;
	long r = korin_sys3(SYS_WAIT, e->pid, WAIT_EX_TAG | WEX_NOHANG, (long)&out);
	if (r == WR_EXITED) {
		finished(e, out);
		return 1;
	}
	if (r < 0) { /* (pas notre fils, ou SYS_WAIT sans la forme étendue) : sa fin par la liste des process */
		u8 info[64];
		if (korin_sys2(SYS_PROC_INFO, e->pid, (long)info)) {
			finished(e, CODE_UNKNOWN);
			return 1;
		}
	}
	if (e->deadline_ms && sys_time_ms() >= e->deadline_ms) {
		korin_sys1(SYS_KILL, e->pid);
		korin_sys3(SYS_WAIT, e->pid, WAIT_EX_TAG | WEX_NOHANG, (long)&out);
		finished(e, CODE_TIMEOUT);
		return 1;
	}
	return 0;
}

/* ---- charger les tâches */

static struct entry *find(int level, const char *id)
{
	for (int i = 0; i < ntasks; i++)
		if (tasks[i].level == level && tk_eq(tasks[i].t.id, id))
			return &tasks[i];
	return 0;
}

static struct entry old[TASK_MAX]; /* (le relevé d'avant : ce qui tourne, où en est le calendrier) */
static int nold;

static void load_level(int level)
{
	char dir[200];
	snprintf(dir, sizeof dir, "%s/etc/tasks", root_of(level));
	long fd = sys_open(dir, O_READ);
	if (fd < 0)
		return;
	struct dirent de;
	while (sys_readdir(fd, &de) == 1 && ntasks < TASK_MAX) {
		if ((de.attr & ATTR_DIR) || !str_ends(de.name, ".task"))
			continue;
		long n = str_len(de.name) - 5;
		if (n <= 0 || n >= TK_ID)
			continue;
		struct entry *e = &tasks[ntasks];
		memset(e, 0, sizeof *e);
		memcpy(e->t.id, de.name, (size_t)n);
		e->t.id[n] = 0;
		if (!tk_id_ok(e->t.id))
			continue;
		char p[260];
		snprintf(p, sizeof p, "%s/%s", dir, de.name);
		if (read_file(p, text, sizeof text) < 0 || tk_parse(&e->t, text) < 0) {
			say("taskd : illisible : ");
			say(p);
			say("\n");
			continue;
		}
		e->level = level;
		state_load(e);
		struct entry *o = 0;
		for (int i = 0; i < nold; i++)
			if (old[i].level == level && tk_eq(old[i].t.id, e->t.id))
				o = &old[i];
		if (o) { /* (relue : ce qui tourne et le calendrier suivent) */
			e->pid = o->pid;
			e->start_ms = o->start_ms;
			e->deadline_ms = e->t.timeout > 0 && o->pid > 0 ? o->start_ms + e->t.timeout * 1000 : 0;
			e->check = o->check;
			e->pending_ms = e->t.enabled ? o->pending_ms : 0;
			e->rebase = o->rebase;
			memcpy(e->edge, o->edge, sizeof e->edge);
		} else {
			long lt = now_local();
			e->check = lt;
			if (e->t.catchup && clock_known()) { /* un passage manqué depuis le dernier (machine éteinte) */
				long ref = e->last ? e->last : e->seen;
				if (ref)
					e->check = ref + sys_clock(1);
			}
		}
		if (!e->seen && clock_known()) {
			e->seen = now_utc();
			state_save(e);
		}
		ntasks++;
	}
	sys_close(fd);
}

static void subscribe_all(void);

static void reload(void)
{
	memcpy(old, tasks, sizeof(struct entry) * (size_t)ntasks);
	nold = ntasks;
	ntasks = 0;
	load_level(L_SYSTEM);
	sess_open = session_is_open();
	sess_uid = -1;
	if (sess_open) {
		sess_uid = user_session_uid();
		if (sess_uid <= 0 || !user_home(sess_uid, user_root, sizeof user_root))
			tk_copy(user_root, "/home", sizeof user_root); /* (pas de comptes : le dossier commun) */
		load_level(L_USER);
	}
	/* une tâche qui tournait et a disparu (retirée) : laissée finir, sans suivi */
	for (int i = 0; i < nold; i++)
		if (old[i].pid > 0 && !find(old[i].level, old[i].t.id))
			say_task(&old[i], "retiree pendant qu'elle tourne");
	subscribe_all();
	for (int i = 0; i < ntasks; i++)
		state_save(&tasks[i]); /* (la prochaine échéance, pour le volet) */
	char b[80];
	snprintf(b, sizeof b, "taskd : %d tache(s), session %ld\n", ntasks, sess_uid);
	say(b);
}

/* ---- le bus : les sujets des déclencheurs */

static void edge_set(struct edge *ed, u32 tid, int holds, u32 hash)
{
	struct edge *free_e = 0;
	for (int k = 0; k < EDGE_MAX; k++) {
		if (ed[k].used && ed[k].tid == tid) {
			ed[k].holds = (u8)holds;
			ed[k].hash = hash;
			return;
		}
		if (!ed[k].used && !free_e)
			free_e = &ed[k];
	}
	if (!free_e)
		free_e = &ed[EDGE_MAX - 1];
	free_e->used = 1;
	free_e->tid = tid;
	free_e->holds = (u8)holds;
	free_e->hash = hash;
}

static struct edge *edge_get(struct edge *ed, u32 tid)
{
	for (int k = 0; k < EDGE_MAX; k++)
		if (ed[k].used && ed[k].tid == tid)
			return &ed[k];
	return 0;
}

/* un sujet a changé (len < 0 : retiré) ; prime : l'état de départ, sans rien lancer */
static void topic_event(u32 tid, const char *name, const char *val, long len, int prime)
{
	u32 h = len >= 0 ? fnv(val, len) : 0;
	for (int i = 0; i < ntasks; i++) {
		struct entry *e = &tasks[i];
		for (int j = 0; j < e->t.nwhen; j++) {
			struct tk_when *w = &e->t.when[j];
			if (!tk_is_event(w->type) || !tk_topic_match(tk_when_topic(w), name))
				continue;
			int holds = tk_when_holds(w, val, len);
			struct edge *ed = edge_get(e->edge[j], tid);
			int go;
			if (w->type == TW_EVENT) /* chaque valeur nouvelle qui convient */
				go = holds && !(ed && ed->hash == h && ed->holds);
			else /* power, battery, removed : quand la condition devient vraie */
				go = holds && !(ed && ed->holds);
			edge_set(e->edge[j], tid, holds, h);
			if (go && !prime)
				fire(e);
		}
	}
}

static int sub_has(char pats[][BUS_NAME_MAX], int n, const char *p)
{
	for (int i = 0; i < n; i++)
		if (tk_eq(pats[i], p))
			return 1;
	return 0;
}

static void subscribe_all(void)
{
	if (!have_bus)
		return;
	static char pats[TASK_MAX * TK_WHEN_MAX][BUS_NAME_MAX];
	int np = 0;
	bus_unsubscribe(0);
	bus_subscribe((long)"korin.tasks.changed", 0);
	bus_subscribe((long)"korin.tasks.run", 0);
	bus_subscribe((long)"korin.tasks.login", 1); /* (BUS_CURRENT : une ouverture publiée avant l'abonnement) */
	bus_subscribe((long)"korin.clock.offset", 0);
	for (int i = 0; i < ntasks; i++)
		for (int j = 0; j < tasks[i].t.nwhen; j++) {
			const struct tk_when *w = &tasks[i].t.when[j];
			if (!tk_is_event(w->type) || !tasks[i].t.enabled)
				continue;
			const char *p = tk_when_topic(w);
			if (!p[0] || sub_has(pats, np, p))
				continue;
			tk_copy(pats[np++], p, BUS_NAME_MAX);
			if (bus_subscribe((long)p, 0) <= 0) {
				say("taskd : abonnement refuse : ");
				say(p);
				say("\n");
			}
		}
	if (!np)
		return;
	/* l'état de départ : les sujets déjà là (une clé déjà branchée ne lance rien) */
	long t = 0;
	while ((t = bus_list(t, (long)tname)) > 0) {
		int match = 0;
		for (int i = 0; i < np && !match; i++)
			match = tk_topic_match(pats[i], tname);
		if (!match)
			continue;
		long n = bus_get_id(t, (long)value, BUS_VALUE_MAX);
		if (n < 0)
			continue;
		value[n] = 0;
		topic_event((u32)t, tname, value, n, 1);
	}
}

/* korin.tasks.run : « system <id> » ou « user <id> » */
static void run_request(const char *v)
{
	int level = !strncmp(v, "user ", 5) ? L_USER : !strncmp(v, "system ", 7) ? L_SYSTEM : -1;
	if (level < 0)
		return;
	const char *id = v + (level == L_USER ? 5 : 7);
	struct entry *e = find(level, id);
	if (!e) {
		reload(); /* (une tâche toute neuve) */
		e = find(level, id);
	}
	if (e)
		start(e); /* (même désactivée : Exécuter maintenant) */
}

static void bus_event(void)
{
	u32 tid = *(u32 *)(ev + BEV_TOPIC);
	u32 len = *(u32 *)(ev + BEV_LEN);
	if (bus_name(tid, (long)tname) < 0)
		return;
	long n = 0;
	value[0] = 0;
	if (len != BUS_DELETED) {
		n = bus_get_id(tid, (long)value, BUS_VALUE_MAX);
		if (n < 0)
			n = 0;
		value[n] = 0;
	}
	if (tk_eq(tname, "korin.tasks.changed")) {
		reload();
		return;
	}
	if (tk_eq(tname, "korin.tasks.run")) {
		run_request(value);
		return;
	}
	if (tk_eq(tname, "korin.tasks.login")) {
		/* une fois par ouverture (gardée : relue à chaque abonnement, et par un taskd relancé) */
		char done[64];
		if (read_file(LOGIN_MARK, done, sizeof done) >= 0 && tk_eq(done, value))
			return;
		write_file(LOGIN_MARK, value, n);
		reload();
		for (int i = 0; i < ntasks; i++)
			for (int j = 0; j < tasks[i].t.nwhen; j++)
				if (tasks[i].level == L_USER && tasks[i].t.when[j].type == TW_STARTUP) {
					fire(&tasks[i]);
					break;
				}
		return;
	}
	if (tk_eq(tname, "korin.clock.offset"))
		return; /* (les échéances sont recalculées au tour suivant) */
	topic_event(tid, tname, value, len == BUS_DELETED ? -1 : n, 0);
}

/* ---- les échéances d'heure */

/* every : la prochaine échéance en UTC (0 : aucune) */
static long every_due(struct entry *e, const struct tk_when *w)
{
	long base = e->last > e->seen ? e->last : e->seen;
	if (e->rebase > base)
		base = e->rebase;
	return base ? base + w->n * w->unit : 0;
}

/* la prochaine échéance en UTC, toutes les lignes when (0 : aucune ; pour l'état et l'attente) */
static long next_utc(struct entry *e)
{
	if (!e->t.enabled || !clock_known())
		return 0;
	long best = 0, off = sys_clock(1), lt = now_local();
	for (int j = 0; j < e->t.nwhen; j++) {
		const struct tk_when *w = &e->t.when[j];
		long s = 0;
		if (w->type == TW_EVERY)
			s = every_due(e, w);
		else if (tk_is_calendar(w->type)) {
			long nx = tk_next(w, e->check < lt ? e->check : lt);
			s = nx > 0 ? nx - off : 0;
		}
		if (s > 0 && (!best || s < best))
			best = s;
	}
	return best;
}

static void time_tick(void)
{
	if (!clock_known() || sys_time_ms() < GRACE_MS)
		return;
	long ut = now_utc(), lt = now_local(), off = sys_clock(1);
	for (int i = 0; i < ntasks; i++) {
		struct entry *e = &tasks[i];
		if (!e->t.enabled)
			continue;
		int go = 0;
		for (int j = 0; j < e->t.nwhen; j++) {
			const struct tk_when *w = &e->t.when[j];
			if (w->type == TW_EVERY) {
				long due = every_due(e, w);
				if (due && due <= ut) {
					if (ut - due > STALE_S && !e->t.catchup) { /* manquée, sans rattrapage : l'intervalle repart */
						e->rebase = ut;
						continue;
					}
					go = 1;
				}
			} else if (tk_is_calendar(w->type)) {
				if (e->check > lt) /* (l'heure a reculé) */
					e->check = lt;
				long occ = tk_next(w, e->check);
				if (occ < 0 || occ > lt)
					continue;
				if (e->last && occ <= e->last + off)
					continue; /* (déjà lancée pour ce passage) */
				if (lt - occ > STALE_S && !e->t.catchup)
					continue; /* manquée, sans rattrapage */
				go = 1;
			}
		}
		e->check = lt;
		if (go && e->pid <= 0)
			fire(e);
	}
}

/* ms avant le prochain réveil */
static long wait_ms(void)
{
	long w = have_bus ? WAIT_MAX : WAIT_NOBUS;
	long ms = sys_time_ms();
	for (int i = 0; i < ntasks; i++) {
		struct entry *e = &tasks[i];
		if (e->pid > 0 && w > WAIT_RUN)
			w = WAIT_RUN;
		if (e->pending_ms) {
			long d = e->pending_ms - ms;
			if (d < w)
				w = d;
		}
		long nx = ms >= GRACE_MS && e->pid <= 0 ? next_utc(e) : 0; /* (pendant la grâce : seulement sa fin) */
		if (nx > 0) {
			long d = (nx - now_utc()) * 1000 + 200;
			if (d < 1000)
				d = 1000; /* (une échéance passée que time_tick n'a pas prise : pas d'attente active) */
			if (d < w)
				w = d;
		}
	}
	if (ms < GRACE_MS && GRACE_MS - ms < w)
		w = GRACE_MS - ms;
	return w < 50 ? 50 : w;
}

/* ---- les phases (init2) : login, logout, shutdown */

static int phase_run(void)
{
	long t0 = sys_time_ms();
	for (;;) {
		int running = 0;
		for (int i = 0; i < ntasks; i++)
			if (tasks[i].pid > 0 && !reap(&tasks[i]))
				running++;
		if (!running)
			return 0;
		if (sys_time_ms() - t0 >= PHASE_MS) {
			for (int i = 0; i < ntasks; i++)
				if (tasks[i].pid > 0) {
					korin_sys1(SYS_KILL, tasks[i].pid);
					finished(&tasks[i], CODE_TIMEOUT);
				}
			return 1;
		}
		sys_sleep(100);
	}
}

static int phase(const char *what)
{
	if (tk_eq(what, "login")) {
		char b[48];
		snprintf(b, sizeof b, "%ld %ld", user_session_uid(), sys_time_ms()); /* (chaque ouverture : une autre valeur) */
		if (bus_available() == 1)
			bus_publish((long)"korin.tasks.login", (long)b, -1, 1); /* (BUS_RETAIN) */
		return 0;
	}
	int shutdown = tk_eq(what, "shutdown");
	if (!shutdown && !tk_eq(what, "logout"))
		return 2;
	have_bus = bus_available() == 1;
	reload();
	int n = 0;
	for (int lv = L_USER; lv >= L_SYSTEM; lv--) /* l'utilisateur d'abord, puis le système */
		for (int i = 0; i < ntasks; i++) {
			struct entry *e = &tasks[i];
			if (e->level != lv || !e->t.enabled || (lv == L_SYSTEM && !shutdown))
				continue;
			for (int j = 0; j < e->t.nwhen; j++)
				if (e->t.when[j].type == TW_SHUTDOWN) {
					if (e->t.timeout <= 0 || e->t.timeout * 1000 > PHASE_TASK_MS)
						e->t.timeout = PHASE_TASK_MS / 1000;
					e->pid = 0;
					start(e);
					n++;
					break;
				}
		}
	if (n)
		phase_run();
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 3 && tk_eq(argv[1], "phase"))
		return phase(argv[2]);
	if (korin_sys2(SYS_SVC_REGISTER, "taskd", 0) < 0) /* un seul taskd */
		return 0;
	have_bus = bus_available() == 1;
	reload();
	/* au démarrage de la machine (pas à une relance de taskd) : les tâches « startup » du système */
	long st[2];
	if (sys_stat(BOOT_MARK, st) < 0) {
		long fd = sys_open(BOOT_MARK, O_WRITE | O_CREATE | O_TRUNC);
		if (fd >= 0 || sys_time_ms() < 15 * 60000) {
			if (fd >= 0)
				sys_close(fd);
			for (int i = 0; i < ntasks; i++)
				for (int j = 0; j < tasks[i].t.nwhen; j++)
					if (tasks[i].level == L_SYSTEM && tasks[i].t.when[j].type == TW_STARTUP) {
						fire(&tasks[i]);
						break;
					}
		}
	}
	for (;;) {
		long ms = sys_time_ms();
		for (int i = 0; i < ntasks; i++) {
			struct entry *e = &tasks[i];
			if (e->pid > 0)
				reap(e);
			if (e->pending_ms && ms >= e->pending_ms)
				start(e);
		}
		/* la session fermée (ses tâches oubliées) ou une autre ouverte : relire */
		if (session_is_open() != sess_open || (sess_open && user_session_uid() != sess_uid))
			reload();
		time_tick();
		long w = wait_ms();
		if (have_bus) {
			long r = bus_wait((long)ev, w);
			if (r == WEV_BUS)
				bus_event();
			else if (r < 0) {
				have_bus = bus_available() == 1; /* (busd relancé : les abonnements à refaire) */
				if (have_bus)
					subscribe_all();
				else
					sys_sleep(w < WAIT_NOBUS ? w : WAIT_NOBUS);
			}
		} else {
			sys_sleep(w);
			if (bus_available() == 1) {
				have_bus = 1;
				subscribe_all();
			}
		}
	}
}
