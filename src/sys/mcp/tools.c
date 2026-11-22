/* tools.c -- les outils du système de korin-mcp : process, processeur, journal du noyau, services, GPU, réseau,
 * heure, écran, signaux, lancement ; et la table de tous les outils (tools[]). docs/MCP-KORIN.md */
#define _GNU_SOURCE
#include "tools.h"
#include "korin.h"
#include "png.h"

#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

void fail(struct result *r, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(r->err, sizeof r->err, fmt, ap);
	va_end(ap);
}

/* ---- les process ---- */
#define PMAX 512
#define TMAX 128

struct proc {
	long pid;
	char name[33];
	long threads, heap_kb;
	int zombie;
	long long calls0, calls1; /* appels système : avant, après la mesure */
	long long cpu0, cpu1;     /* ms des threads */
	long long own, shared;    /* pages (VM_STATS) */
	int sys;                  /* programme de /sys */
};

static struct proc procs[PMAX];
static int nprocs;
static uint8_t thl[TMAX * THL_SIZE];

/* le temps de processeur d'un process : la somme de ses threads (ms) */
static long long proc_cpu(long pid, long *nth)
{
	long n = ksys3(SYS_THREAD_LIST, pid, thl, TMAX);
	if (nth)
		*nth = n;
	if (n <= 0)
		return 0;
	long long s = 0;
	for (long i = 0; i < n; i++)
		s += *(int64_t *)(thl + i * THL_SIZE + THL_RUNTIME);
	return s;
}

static long long proc_calls(long pid)
{
	uint64_t one;
	return ksys3(SYS_PROC_STATS, pid, &one, 1) < 0 ? -1 : (long long)one;
}

static void entry(struct proc *p, const uint8_t *e)
{
	p->pid = (long)*(const uint64_t *)e;
	p->threads = (long)*(const uint64_t *)(e + 8);
	p->heap_kb = (long)*(const uint64_t *)(e + 16);
	memcpy(p->name, e + 24, 32);
	p->name[32] = 0;
	p->zombie = *(const uint64_t *)(e + 56) == 3;
}

/* tous les process -> procs (SYS_PROCLIST_FROM, 16 à la fois) */
static void collect(void)
{
	static uint8_t lot[16 * 64];
	long from = 0;
	nprocs = 0;
	for (;;) {
		long n = ksys3(SYS_PROCLIST_FROM, lot, 16, from);
		if (n <= 0)
			return;
		for (long i = 0; i < n && nprocs < PMAX; i++) {
			struct proc *p = &procs[nprocs++];
			memset(p, 0, sizeof *p);
			entry(p, lot + i * 64);
			from = p->pid & 0xFFFF;
		}
		if (n < 16 || nprocs >= PMAX)
			return;
	}
}

/* la mesure : compteurs, sleep, compteurs (sample_ms 0 : une seule lecture) */
static long sample(const struct jv *a, long def)
{
	long ms = (long)json_int(a, "sample_ms", def);
	if (ms < 0)
		ms = 0;
	if (ms > 10000)
		ms = 10000;
	return ms;
}

static void measure(long ms)
{
	for (int i = 0; i < nprocs; i++) {
		procs[i].calls0 = proc_calls(procs[i].pid);
		procs[i].cpu0 = proc_cpu(procs[i].pid, 0);
	}
	if (ms)
		usleep((useconds_t)ms * 1000);
	for (int i = 0; i < nprocs; i++) {
		struct proc *p = &procs[i];
		p->calls1 = proc_calls(p->pid);
		long nth;
		p->cpu1 = proc_cpu(p->pid, &nth);
		if (nth >= 0)
			p->threads = nth;
		uint64_t vs[8];
		if (ksys2(SYS_VM_STATS, p->pid, vs) >= 0) {
			p->own = (long long)vs[0];
			p->shared = (long long)vs[1];
		}
		p->sys = ksys1(SYS_PROC_RIGHTS, p->pid) == 1;
	}
}

static void proc_json(struct jb *j, const struct proc *p, long ms)
{
	jb_kint(j, "pid", p->pid);
	jb_kstr(j, "name", p->name);
	jb_kstr(j, "state", p->zombie ? "zombie" : p->calls1 < 0 ? "gone" : "running");
	jb_kint(j, "threads", p->threads);
	jb_kint(j, "mem_kb", p->own * 4);
	jb_kint(j, "shared_kb", p->shared * 4);
	jb_kint(j, "heap_kb", p->heap_kb);
	if (p->calls1 >= 0) {
		jb_kint(j, "syscalls", p->calls1);
		if (ms)
			jb_kint(j, "syscalls_per_s", (p->calls1 - p->calls0) * 1000 / ms);
	}
	jb_kint(j, "cpu_ms", p->cpu1);
	if (ms)
		jb_knum(j, "cpu_percent", 100.0 * (double)(p->cpu1 - p->cpu0) / (double)ms, 1);
	jb_kbool(j, "system", p->sys);
}

static int by_cpu(const void *a, const void *b)
{
	const struct proc *x = a, *y = b;
	long long dx = x->cpu1 - x->cpu0, dy = y->cpu1 - y->cpu0;
	if (dx != dy)
		return dx < dy ? 1 : -1;
	long long cx = x->calls1 - x->calls0, cy = y->calls1 - y->calls0;
	if (cx != cy)
		return cx < cy ? 1 : -1;
	return x->pid < y->pid ? -1 : 1;
}

static void t_processes(const struct jv *a, struct result *r)
{
	long ms = sample(a, 1000);
	const char *want = json_str(a, "name", 0);
	const char *sort = json_str(a, "sort", "pid");
	collect();
	measure(ms);
	if (!strcmp(sort, "cpu"))
		qsort(procs, (size_t)nprocs, sizeof procs[0], by_cpu);
	struct jb *j = &r->j;
	jb_obj(j);
	jb_kint(j, "sample_ms", ms);
	jb_key(j, "processes");
	jb_arr(j);
	int n = 0;
	for (int i = 0; i < nprocs; i++) {
		if (want && !strcasestr(procs[i].name, want))
			continue;
		jb_obj(j);
		proc_json(j, &procs[i], ms);
		jb_obj_end(j);
		n++;
	}
	jb_arr_end(j);
	jb_kint(j, "count", n);
	jb_obj_end(j);
}

/* les noms des appels système (le tableau de SYS_PROC_STATS) : korin_sysnames.h, tiré de syscalls.inc */
static const struct {
	int nr;
	const char *name;
} sysnames[] = {
#include "korin_sysnames.h"
};

static const char *sys_name(int nr)
{
	for (size_t i = 0; i < sizeof sysnames / sizeof sysnames[0]; i++)
		if (sysnames[i].nr == nr)
			return sysnames[i].name;
	return 0;
}

/* les process lancés par launch (leur fin se lit par waitpid) */
#define LMAX 64
static struct launched {
	long pid;
	char log[96], cmd[200];
	int done, code, sig;
} launched[LMAX];
static int nlaunched;

static struct launched *launched_of(long pid)
{
	for (int i = 0; i < nlaunched; i++)
		if (launched[i].pid == pid) {
			if (!launched[i].done) {
				int st;
				pid_t w = waitpid((pid_t)pid, &st, WNOHANG);
				if (w == (pid_t)pid) {
					launched[i].done = 1;
					if (WIFEXITED(st))
						launched[i].code = WEXITSTATUS(st);
					else if (WIFSIGNALED(st))
						launched[i].sig = WTERMSIG(st);
				}
			}
			return &launched[i];
		}
	return 0;
}

static void t_process(const struct jv *a, struct result *r)
{
	long pid = (long)json_int(a, "pid", 0);
	const char *want = json_str(a, "name", 0);
	long ms = sample(a, 1000);
	collect();
	int i = 0;
	for (; i < nprocs; i++)
		if (pid ? procs[i].pid == pid : want && !strcasecmp(procs[i].name, want))
			break;
	if (i == nprocs && !pid && want) /* (sinon un morceau du nom) */
		for (i = 0; i < nprocs; i++)
			if (strcasestr(procs[i].name, want))
				break;
	struct launched *l = pid ? launched_of(pid) : 0;
	if (i == nprocs) {
		if (l) { /* fini : ce qu'on sait de lui */
			struct jb *j = &r->j;
			jb_obj(j);
			jb_kint(j, "pid", pid);
			jb_kstr(j, "state", "exited");
			jb_kstr(j, "command", l->cmd);
			if (l->sig)
				jb_kint(j, "signal", l->sig);
			else
				jb_kint(j, "exit_code", l->code);
			jb_kstr(j, "log", l->log);
			jb_obj_end(j);
			return;
		}
		if (want)
			fail(r, "pas de process « %s »", want);
		else
			fail(r, "pas de process %ld", pid);
		return;
	}
	struct proc p = procs[i];
	nprocs = 1;
	procs[0] = p;
	static uint64_t st0[129], st1[129];
	long w0 = ksys3(SYS_PROC_STATS, p.pid, st0, 129);
	measure(ms);
	long w1 = ksys3(SYS_PROC_STATS, p.pid, st1, 129);
	p = procs[0];
	struct jb *j = &r->j;
	jb_obj(j);
	proc_json(j, &p, ms);
	if (l) {
		jb_kstr(j, "command", l->cmd);
		jb_kstr(j, "log", l->log);
	}
	uint64_t vs[8];
	if (ksys2(SYS_VM_STATS, p.pid, vs) >= 0) {
		jb_key(j, "vm");
		jb_obj(j);
		jb_kint(j, "own_kb", (long long)vs[0] * 4);
		jb_kint(j, "shared_kb", (long long)vs[1] * 4);
		jb_kint(j, "lazy_reserved_kb", (long long)(vs[2] / 1024));
		jb_kint(j, "page_faults", (long long)vs[3]);
		jb_kint(j, "cow_copies", (long long)vs[4]);
		jb_kint(j, "page_tables", (long long)vs[5]);
		jb_obj_end(j);
	}
	long n = ksys3(SYS_THREAD_LIST, p.pid, thl, TMAX);
	static const char *const tst[] = { "ready", "sleeping", "blocked", "dead" };
	jb_key(j, "threads");
	jb_arr(j);
	for (long k = 0; k < n; k++) {
		const uint8_t *t = thl + k * THL_SIZE;
		uint64_t s = *(const uint64_t *)(t + THL_STATE);
		char nm[25];
		memcpy(nm, t + THL_NAME, 24);
		nm[24] = 0;
		jb_obj(j);
		jb_kint(j, "tid", *(const int64_t *)(t + THL_TID));
		jb_kstr(j, "name", nm);
		jb_kstr(j, "state", s < 4 ? tst[s] : "?");
		jb_kint(j, "priority", *(const int64_t *)(t + THL_PRIO));
		jb_kint(j, "core", *(const int64_t *)(t + THL_CPU));
		jb_kint(j, "runtime_ms", *(const int64_t *)(t + THL_RUNTIME));
		jb_obj_end(j);
	}
	jb_arr_end(j);
	if (w0 > 1 && w1 > 1) { /* les appels les plus fréquents, pendant la mesure (sinon depuis le début) */
		jb_key(j, ms ? "top_syscalls_during_sample" : "top_syscalls");
		jb_arr(j);
		for (int rank = 0; rank < 12; rank++) {
			long best = 0;
			uint64_t val = 0;
			for (long k = 1; k < w1; k++) {
				uint64_t d = ms && k < w0 ? st1[k] - st0[k] : st1[k];
				if (d > val) {
					val = d;
					best = k;
				}
			}
			if (!val)
				break;
			st1[best] = ms ? st0[best] : 0;
			const char *nm = sys_name((int)best - 1);
			char hx[16];
			snprintf(hx, sizeof hx, "0x%02lx", best - 1);
			jb_obj(j);
			jb_kstr(j, "syscall", nm ? nm : hx);
			jb_kint(j, "calls", (long long)val);
			jb_obj_end(j);
		}
		jb_arr_end(j);
	}
	jb_obj_end(j);
}

/* ---- le processeur ---- */
struct th {
	long pid, tid, core;
	long long rt0, rt1;
	char name[25], proc[33];
};

static int th_cmp(const void *a, const void *b)
{
	const struct th *x = a, *y = b;
	long long dx = x->rt1 - x->rt0, dy = y->rt1 - y->rt0;
	return dx == dy ? (x->rt1 < y->rt1) - (x->rt1 > y->rt1) : (dx < dy) - (dx > dy);
}

static void t_cpu(const struct jv *a, struct result *r)
{
	long ms = sample(a, 1000);
	long top = (long)json_int(a, "top", 15);
	if (top < 0)
		top = 0;
	if (top > 200)
		top = 200;
	collect();
	static struct th ths[1024];
	int nt = 0;
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < nprocs; i++) {
			long n = ksys3(SYS_THREAD_LIST, procs[i].pid, thl, TMAX);
			for (long k = 0; k < n; k++) {
				const uint8_t *t = thl + k * THL_SIZE;
				long tid = (long)*(const int64_t *)(t + THL_TID);
				long long rt = *(const int64_t *)(t + THL_RUNTIME);
				int f = 0;
				if (pass) /* (le même thread au premier passage) */
					for (; f < nt; f++)
						if (ths[f].tid == tid && ths[f].pid == procs[i].pid)
							break;
				if (!pass || f == nt) {
					if (nt == 1024)
						continue;
					f = nt++;
					memset(&ths[f], 0, sizeof ths[f]);
					ths[f].pid = procs[i].pid;
					ths[f].tid = tid;
					ths[f].rt0 = rt;
					memcpy(ths[f].name, t + THL_NAME, 24);
					snprintf(ths[f].proc, sizeof ths[f].proc, "%s", procs[i].name);
				}
				ths[f].rt1 = rt;
				ths[f].core = (long)*(const int64_t *)(t + THL_CPU);
			}
		}
		if (!pass && ms)
			usleep((useconds_t)ms * 1000);
		if (!ms)
			break;
	}
	uint64_t info[10];
	memset(info, 0, sizeof info);
	ksys1(SYS_SYSINFO, info);
	struct jb *j = &r->j;
	jb_obj(j);
	jb_kint(j, "sample_ms", ms);
	jb_kint(j, "cores", (long long)info[0]);
	static uint64_t cl[64 * 4];
	long nc = ksys2(SYS_CPU_LOAD, cl, 64L | (1L << 32)); /* PC : 32 o par cœur, avec les MHz */
	jb_key(j, "per_core");
	jb_arr(j);
	if (nc > 0) {
		for (long c = 0; c < nc && c < 64; c++) {
			jb_obj(j);
			jb_kint(j, "core", c);
			jb_kint(j, "load_percent", (long long)cl[c * 4]);
			jb_kint(j, "busy_ms", (long long)cl[c * 4 + 1]);
			if (cl[c * 4 + 2])
				jb_kint(j, "mhz", (long long)cl[c * 4 + 2]);
			if (cl[c * 4 + 3])
				jb_kint(j, "mhz_max_requested", (long long)cl[c * 4 + 3]);
			jb_obj_end(j);
		}
	} else /* (Pi : SYSINFO, les 4 premiers) */
		for (uint64_t c = 0; c < info[0] && c < 4; c++) {
			jb_obj(j);
			jb_kint(j, "core", (long long)c);
			jb_kint(j, "load_percent", (long long)info[5 + c]);
			jb_obj_end(j);
		}
	jb_arr_end(j);
	jb_key(j, "memory");
	jb_obj(j);
	jb_kint(j, "total_kb", (long long)info[2]);
	jb_kint(j, "free_kb", (long long)info[1]);
	jb_kint(j, "used_kb", (long long)(info[2] - info[1]));
	jb_obj_end(j);
	jb_kint(j, "uptime_ms", (long long)info[3]);
	jb_kint(j, "processes", (long long)info[4]);
	jb_kint(j, "context_switches", (long long)info[9]);
	if (top) {
		qsort(ths, (size_t)nt, sizeof ths[0], th_cmp);
		jb_key(j, ms ? "top_threads" : "top_threads_since_boot");
		jb_arr(j);
		for (int i = 0; i < nt && i < top; i++) {
			jb_obj(j);
			jb_kint(j, "pid", ths[i].pid);
			jb_kstr(j, "process", ths[i].proc);
			jb_kint(j, "tid", ths[i].tid);
			jb_kstr(j, "thread", ths[i].name);
			jb_kint(j, "core", ths[i].core);
			if (ms)
				jb_knum(j, "cpu_percent", 100.0 * (double)(ths[i].rt1 - ths[i].rt0) / (double)ms, 1);
			jb_kint(j, "runtime_ms", ths[i].rt1);
			jb_obj_end(j);
		}
		jb_arr_end(j);
	}
	jb_obj_end(j);
}

/* ---- le journal du noyau (SYS_KLOG : 64 Ko, « [   12.345] source: texte ») ---- */
#define KLOG_MAX 65536

static void t_kernel_log(const struct jv *a, struct result *r)
{
	static char buf[KLOG_MAX + 1];
	long n = ksys2(SYS_KLOG, buf, KLOG_MAX);
	if (n < 0) {
		fail(r, "SYS_KLOG refusé (%ld)", n);
		return;
	}
	buf[n] = 0;
	long long want = json_int(a, "lines", 100);
	if (want < 1)
		want = 1;
	if (want > 2000)
		want = 2000;
	const char *pat = json_str(a, "pattern", 0), *src = json_str(a, "source", 0);
	int fold = !json_bool(a, "case_sensitive", 0);
	double since = (double)json_int(a, "since_ms", -1) / 1000.0;
	regex_t re;
	if (pat && regcomp(&re, pat, REG_EXTENDED | REG_NOSUB | (fold ? REG_ICASE : 0))) {
		fail(r, "pattern : expression régulière fausse (POSIX étendue)");
		return;
	}
	/* les lignes (la première peut être coupée par l'anneau : sautée si le journal est plein) */
	char *start = buf;
	if (n == KLOG_MAX) {
		char *nl = strchr(buf, '\n');
		if (nl)
			start = nl + 1;
	}
	static char *lines[8192];
	int nl = 0, total = 0;
	for (char *s = start; *s && nl < 8192;) {
		char *e = strchr(s, '\n');
		if (e)
			*e = 0;
		total++;
		int ok = 1;
		double t = -1;
		const char *text = s;
		if (s[0] == '[') { /* l'heure */
			char *close = strchr(s, ']');
			if (close) {
				t = strtod(s + 1, 0);
				text = close + 1 + (close[1] == ' ');
			}
		}
		if (since >= 0 && t >= 0 && t * 1000 < since * 1000)
			ok = 0;
		if (ok && src) {
			size_t l = strlen(src);
			ok = (fold ? !strncasecmp(text, src, l) : !strncmp(text, src, l)) &&
			     (text[l] == ':' || text[l] == ' ' || text[l] == '[' || text[l] == 0 || text[l] == '.' || text[l] == '(');
		}
		if (ok && pat)
			ok = regexec(&re, s, 0, 0, 0) == 0;
		if (ok)
			lines[nl++] = s;
		if (!e)
			break;
		s = e + 1;
	}
	if (pat)
		regfree(&re);
	struct jb *j = &r->j;
	jb_obj(j);
	jb_kint(j, "log_bytes", n);
	jb_kbool(j, "ring_full", n == KLOG_MAX);
	jb_kint(j, "total_lines", total);
	jb_kint(j, "matched", nl);
	jb_kint(j, "uptime_ms", ksys0(SYS_TIME_MS));
	int from = nl > want ? nl - (int)want : 0;
	jb_kint(j, "returned", nl - from);
	jb_key(j, "lines");
	jb_arr(j);
	for (int i = from; i < nl; i++)
		jb_str(j, lines[i]);
	jb_arr_end(j);
	jb_obj_end(j);
}

/* ---- les services ---- */
static int running_pid(const char *name)
{
	for (int i = 0; i < nprocs; i++)
		if (!procs[i].zombie && !strncmp(procs[i].name, name, 32))
			return (int)procs[i].pid;
	return 0;
}

static void conf_list(struct jb *j, const char *key, const char *path)
{
	jb_key(j, key);
	jb_arr(j);
	FILE *f = fopen(path, "r");
	char line[512];
	while (f && fgets(line, sizeof line, f)) {
		char *s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		s[strcspn(s, "\r\n")] = 0;
		int off = *s == '#';
		if (off) { /* une ligne en commentaire qui est un programme : un service arrêté */
			s++;
			while (*s == ' ')
				s++;
			if (*s != '/')
				continue;
		}
		if (*s != '/')
			continue;
		char prog[256];
		snprintf(prog, sizeof prog, "%.*s", (int)strcspn(s, " \t"), s);
		const char *base = strrchr(prog, '/');
		base = base ? base + 1 : prog;
		jb_obj(j);
		jb_kstr(j, "line", s);
		jb_kstr(j, "program", prog);
		jb_kbool(j, "enabled", !off);
		int pid = running_pid(base);
		jb_kbool(j, "running", pid != 0);
		if (pid)
			jb_kint(j, "pid", pid);
		jb_obj_end(j);
	}
	if (f)
		fclose(f);
	jb_arr_end(j);
}

static void t_services(const struct jv *a, struct result *r)
{
	(void)a;
	collect();
	for (int i = 0; i < nprocs; i++)
		procs[i].sys = ksys1(SYS_PROC_RIGHTS, procs[i].pid) == 1;
	struct jb *j = &r->j;
	jb_obj(j);
	conf_list(j, "services_txt", "/etc/services.txt"); /* lancés et relancés par init2 */
	conf_list(j, "startup_txt", "/etc/startup.txt");   /* lancés au démarrage du bureau */
	jb_key(j, "system_processes"); /* tout ce qui a les droits de /sys : services, pilotes (hwmanager), init */
	jb_arr(j);
	for (int i = 0; i < nprocs; i++) {
		if (!procs[i].sys || procs[i].zombie)
			continue;
		jb_obj(j);
		jb_kint(j, "pid", procs[i].pid);
		jb_kstr(j, "name", procs[i].name);
		jb_kint(j, "threads", procs[i].threads);
		jb_obj_end(j);
	}
	jb_arr_end(j);
	static const char *const svcs[] = { "vfs", "net", "sound", "vterm", "hid", "intelgpu", "pkgd", 0 };
	jb_key(j, "registered"); /* quelques noms de service connus : enregistrés ou non (SVC_OPEN sans attente) */
	jb_obj(j);
	for (int i = 0; svcs[i]; i++)
		jb_kbool(j, svcs[i], ksys2(SYS_SVC_OPEN, svcs[i], 0) >= 0);
	jb_obj_end(j);
	jb_obj_end(j);
}

/* ---- intelgpu (src/svc/intelgpu/igproto.h) ---- */
#define IG_OP_INFO 1
#define IG_OP_STATUS 15
#define IG_OP_TIMES 16
struct ig_info {
	unsigned int version, state, pid, status_buf, device, revision, eu_total, slice_mask, dss_mask, eu_per_dss, ts_hz,
		resets;
	unsigned long long gtt_size;
};

static long ig_ask(long ch, long op, struct kmsg *m)
{
	memset(m, 0, sizeof *m);
	m->op = op;
	return ksys2(SYS_SVC_CALL, ch, m);
}

static void ig_text(struct jb *j, const char *key, const struct kmsg *m)
{
	size_t n = m->len < sizeof m->data ? m->len : sizeof m->data;
	jb_key(j, key);
	jb_strn(j, (const char *)m->data, strnlen((const char *)m->data, n));
}

static void t_gpu(const struct jv *a, struct result *r)
{
	(void)a;
	struct jb *j = &r->j;
	jb_obj(j);
	long ch = ksys2(SYS_SVC_OPEN, "intelgpu", 0);
	if (ch < 0) {
		jb_kstr(j, "intelgpu", "ne tourne pas");
		jb_kstr(j, "note", "sans service GPU, GlKit et GpuKit rendent au processeur (softpipe) ; sous QEMU il n'y a pas de GPU");
		jb_obj_end(j);
		return;
	}
	struct kmsg m;
	jb_kstr(j, "intelgpu", "tourne");
	if (ig_ask(ch, IG_OP_INFO, &m) >= 0 && m.op >= 0) {
		struct ig_info in;
		memcpy(&in, m.data, sizeof in);
		jb_kstr(j, "state", in.state == 1 ? "ready" : in.state == 2 ? "simulated" : "lost");
		char dev[16];
		snprintf(dev, sizeof dev, "8086:%04x", in.device);
		jb_kstr(j, "device", dev);
		jb_kint(j, "revision", in.revision);
		jb_kint(j, "eu_total", in.eu_total);
		jb_kint(j, "service_pid", in.pid);
		jb_kint(j, "resets", in.resets);
		jb_kint(j, "timestamp_hz", in.ts_hz);
	}
	if (ig_ask(ch, IG_OP_STATUS, &m) >= 0)
		ig_text(j, "status", &m);
	if (ig_ask(ch, IG_OP_TIMES, &m) >= 0)
		ig_text(j, "batch_times", &m);
	jb_obj_end(j);
}

/* ---- le réseau (NetKit : net_info, net_ifaces, net_active, net_sockets, net_time) ---- */
static void ip_str(uint32_t ip, char *t)
{
	snprintf(t, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

static void kip(struct jb *j, const char *k, uint32_t ip)
{
	char t[16];
	if (!ip) {
		jb_key(j, k);
		jb_null(j);
		return;
	}
	ip_str(ip, t);
	jb_kstr(j, k, t);
}

static void t_network(const struct jv *a, struct result *r)
{
	long (*net_info)(long) = (long (*)(long))netkit_fn(NK_net_info);
	long (*net_ifaces)(long, long) = (long (*)(long, long))netkit_fn(NK_net_ifaces);
	long (*net_sockets)(long, long) = (long (*)(long, long))netkit_fn(NK_net_sockets);
	if (!net_info || !net_ifaces) {
		fail(r, "NetKit absent");
		return;
	}
	int socks = json_bool(a, "sockets", 1);
	struct jb *j = &r->j;
	jb_obj(j);
	uint32_t ni[12];
	memset(ni, 0, sizeof ni);
	net_info((long)ni);
	jb_key(j, "config");
	jb_obj(j);
	kip(j, "address", ni[NI_IP / 4]);
	kip(j, "netmask", ni[NI_MASK / 4]);
	kip(j, "gateway", ni[NI_GW / 4]);
	kip(j, "dns", ni[NI_DNS / 4]);
	const uint8_t *mac = (const uint8_t *)ni + NI_MAC;
	char ms[20];
	snprintf(ms, sizeof ms, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	jb_kstr(j, "mac", ms);
	jb_kstr(j, "dhcp", ni[NI_DHCP / 4] == 4 ? "lease" : ni[NI_UP / 4] ? "waiting" : "no link");
	jb_obj_end(j);
	static uint8_t ifs[8 * NIF_ENT];
	long n = net_ifaces((long)ifs, 8);
	jb_key(j, "interfaces");
	jb_arr(j);
	for (long i = 0; i < n; i++) {
		const uint8_t *e = ifs + i * NIF_ENT;
		char name[9], ssid[33];
		memcpy(name, e + NIF_E_NAME, 8);
		name[8] = 0;
		uint32_t t = *(const uint32_t *)(e + NIF_E_TYPE);
		jb_obj(j);
		jb_kstr(j, "name", name);
		jb_kstr(j, "type", t == NIF_ETHERNET ? "ethernet" : t == NIF_WIFI ? "wifi" : t == NIF_USB ? "usb" : "?");
		jb_kbool(j, "link", *(const uint32_t *)(e + NIF_E_LINK) != 0);
		if (*(const uint32_t *)(e + NIF_E_SPEED))
			jb_kint(j, "speed_mbps", *(const uint32_t *)(e + NIF_E_SPEED));
		jb_kbool(j, "active", *(const uint32_t *)(e + NIF_E_ACTIVE) != 0);
		if (*(const uint32_t *)(e + NIF_E_IP))
			kip(j, "address", *(const uint32_t *)(e + NIF_E_IP));
		memcpy(ssid, e + NIF_E_SSID, 32);
		ssid[32] = 0;
		if (ssid[0])
			jb_kstr(j, "ssid", ssid);
		jb_obj_end(j);
	}
	jb_arr_end(j);
	if (socks && net_sockets) {
		static uint8_t sk[256 * NSK_ENT];
		static const char *const states[] = { "CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD", "ESTABLISHED", "FIN_WAIT1",
						      "FIN_WAIT2", "CLOSE_WAIT", "CLOSING", "LAST_ACK", "TIME_WAIT" };
		long ns = net_sockets((long)sk, 256);
		jb_key(j, "sockets");
		jb_arr(j);
		for (long i = 0; i < ns; i++) {
			const uint8_t *e = sk + i * NSK_ENT;
			uint32_t type = *(const uint32_t *)(e + NSK_TYPE), st = *(const uint32_t *)(e + NSK_STATE);
			jb_obj(j);
			jb_kstr(j, "proto", type == 1 ? "tcp" : "udp");
			if (type == 1)
				jb_kstr(j, "state", st <= 10 ? states[st] : "?");
			jb_kint(j, "local_port", *(const uint32_t *)(e + NSK_LPORT));
			uint32_t rip = *(const uint32_t *)(e + NSK_RIP);
			if (rip) {
				char t[24];
				ip_str(rip, t);
				snprintf(t + strlen(t), 8, ":%u", *(const uint32_t *)(e + NSK_RPORT));
				jb_kstr(j, "remote", t);
			}
			jb_kint(j, "rx_queue", *(const uint32_t *)(e + NSK_RXQ));
			jb_kint(j, "tx_queue", *(const uint32_t *)(e + NSK_TXQ));
			uint32_t owner = *(const uint32_t *)(e + NSK_OWNER);
			if (owner) {
				uint8_t info[64];
				jb_kint(j, "pid", owner);
				if (ksys2(SYS_PROC_INFO, owner, info) >= 0) {
					info[24 + 31] = 0;
					jb_kstr(j, "program", (char *)info + 24);
				}
			} else
				jb_kstr(j, "program", "netd");
			jb_obj_end(j);
		}
		jb_arr_end(j);
	}
	jb_obj_end(j);
}

/* ---- l'heure ---- */
static void t_time(const struct jv *a, struct result *r)
{
	(void)a;
	long long utc = ksys1(SYS_CLOCK_GET, 0), off = ksys1(SYS_CLOCK_GET, 1);
	struct jb *j = &r->j;
	jb_obj(j);
	char t[24];
	iso_time(utc, t);
	jb_kstr(j, "utc", t);
	jb_kint(j, "epoch", utc);
	if (off > -86400 && off < 86400) {
		jb_kint(j, "local_offset_s", off);
		time_t l = (time_t)(utc + off);
		struct tm tm;
		if (gmtime_r(&l, &tm)) {
			strftime(t, sizeof t, "%Y-%m-%dT%H:%M:%S", &tm);
			jb_kstr(j, "local", t);
		}
	}
	jb_kint(j, "uptime_ms", ksys0(SYS_TIME_MS));
	long (*net_time)(long) = (long (*)(long))netkit_fn(NK_net_time);
	if (net_time) {
		uint64_t nt[3] = { 0, 0, 0 };
		long u = net_time((long)nt);
		jb_kbool(j, "ntp_synced", u > 0 && nt[1] != 0);
	}
	jb_kstr(j, "note", "Korin est en UTC (un seul fuseau) ; local = UTC + local_offset_s");
	jb_obj_end(j);
}

/* ---- l'écran : les fenêtres (WS_OP_WIN_LIST) et la capture (WS_OP_SNAPSHOT) ---- */
struct win {
	uint32_t id, pid, flags, state;
	int16_t x, y;
	uint16_t w, h;
	char title[33];
};

static int windows(struct win *out, int max, uint32_t *focus)
{
	int n = 0;
	for (long first = 0;;) {
		uint8_t m[WS_MSG] __attribute__((aligned(8)));
		memset(m, 0, sizeof m);
		*(uint64_t *)(m + WM_OP) = WS_OP_WIN_LIST;
		*(uint64_t *)(m + WM_A) = (uint64_t)first;
		long total = ksys1(SYS_WS_CALL, m);
		if (total < 0)
			return n ? n : (int)total;
		if (focus)
			*focus = (uint32_t)*(uint64_t *)(m + WM_A);
		uint32_t len = *(uint32_t *)(m + WM_LEN);
		for (uint32_t k = 0; k + WE_SIZE <= len && n < max; k += WE_SIZE) {
			const uint8_t *e = m + WM_DATA + k;
			struct win *w = &out[n++];
			w->id = *(const uint32_t *)(e + WE_ID);
			w->pid = *(const uint32_t *)(e + WE_PID);
			w->x = *(const int16_t *)(e + WE_X);
			w->y = *(const int16_t *)(e + WE_Y);
			w->w = *(const uint16_t *)(e + WE_W);
			w->h = *(const uint16_t *)(e + WE_H);
			w->flags = *(const uint32_t *)(e + WE_FLAGS);
			w->state = *(const uint32_t *)(e + WE_STATE);
			memcpy(w->title, e + WE_TITLE, 32);
			w->title[32] = 0;
		}
		first += WE_PER_MSG;
		if (first >= total || n >= max)
			return n;
	}
}

static void t_windows(const struct jv *a, struct result *r)
{
	(void)a;
	static struct win ws[256];
	uint32_t focus = 0;
	int n = windows(ws, 256, &focus);
	if (n < 0) {
		fail(r, "Elegant ne répond pas (pas de bureau : console texte ?)");
		return;
	}
	collect();
	struct jb *j = &r->j;
	jb_obj(j);
	jb_key(j, "windows"); /* du fond vers l'avant */
	jb_arr(j);
	for (int i = 0; i < n; i++) {
		struct win *w = &ws[i];
		jb_obj(j);
		jb_kint(j, "id", w->id);
		jb_kstr(j, "title", w->title);
		jb_kint(j, "pid", w->pid);
		for (int k = 0; k < nprocs; k++)
			if (procs[k].pid == (long)w->pid) {
				jb_kstr(j, "process", procs[k].name);
				break;
			}
		jb_kint(j, "x", w->x);
		jb_kint(j, "y", w->y);
		jb_kint(j, "width", w->w);
		jb_kint(j, "height", w->h);
		jb_kbool(j, "focused", w->id == focus);
		jb_kbool(j, "hidden", (w->state & WES_MINI) != 0);
		jb_kstr(j, "layer", w->flags & WF_BACK ? "background" : w->flags & WF_TOP ? "top" : "normal");
		jb_obj_end(j);
	}
	jb_arr_end(j);
	jb_obj_end(j);
}

static void t_screenshot(const struct jv *a, struct result *r)
{
	static long buf_no = -1;
	static uint8_t *px;
	uint8_t m[WS_MSG] __attribute__((aligned(8)));
	memset(m, 0, sizeof m);
	*(uint64_t *)(m + WM_OP) = WS_OP_SNAPSHOT; /* l'écran entier, curseur compris (génération 0 : toujours) */
	if (ksys1(SYS_WS_CALL, m) < 0) {
		fail(r, "Elegant ne répond pas (pas de bureau : console texte ?)");
		return;
	}
	long sw = (long)*(uint64_t *)(m + WM_A), sh = (long)*(uint64_t *)(m + WM_B);
	long no = (long)*(uint64_t *)(m + WM_D);
	if (no != buf_no) { /* le tampon d'Elegant, mappé une fois */
		long p = ksys1(SYS_BUF_MAP, no);
		if (p < 0) {
			fail(r, "tampon de la capture refusé (%ld)", p);
			return;
		}
		px = (uint8_t *)p;
		buf_no = no;
	}
	if (sw <= 0 || sh <= 0) {
		fail(r, "écran vide (%ldx%ld)", sw, sh);
		return;
	}
	/* la partie : une fenêtre (id ou morceau du titre), un rectangle, ou tout */
	long x0 = 0, y0 = 0, x1 = sw, y1 = sh;
	const char *wt = json_str(a, "window", 0);
	long wid = (long)json_int(a, "window_id", 0);
	if (!wt && json_get(a, "window") && json_get(a, "window")->t == J_NUM)
		wid = (long)json_get(a, "window")->num;
	char wname[40] = "";
	if (wt || wid) {
		static struct win ws[256];
		int n = windows(ws, 256, 0), f = -1;
		for (int i = n - 1; i >= 0; i--) /* (du premier plan vers le fond) */
			if (wid ? (long)ws[i].id == wid : strcasestr(ws[i].title, wt) != 0) {
				f = i;
				break;
			}
		if (f < 0) {
			if (wt)
				fail(r, "pas de fenêtre « %s » (outil windows : la liste)", wt);
			else
				fail(r, "pas de fenêtre %ld (outil windows : la liste)", wid);
			return;
		}
		int frame = json_bool(a, "frame", 1) && !(ws[f].flags & 1);
		long fx = frame ? 4 : 0, ftop = frame ? 30 : 0; /* le cadre d'UIKit autour du contenu (à peu près) */
		x0 = ws[f].x - fx;
		y0 = ws[f].y - ftop;
		x1 = ws[f].x + ws[f].w + fx;
		y1 = ws[f].y + ws[f].h + fx;
		snprintf(wname, sizeof wname, "%s", ws[f].title);
	}
	const struct jv *rg = json_get(a, "region");
	if (rg && rg->t == J_OBJ) {
		x0 = (long)json_int(rg, "x", 0);
		y0 = (long)json_int(rg, "y", 0);
		x1 = x0 + (long)json_int(rg, "width", sw);
		y1 = y0 + (long)json_int(rg, "height", sh);
	}
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > sw)
		x1 = sw;
	if (y1 > sh)
		y1 = sh;
	if (x1 <= x0 || y1 <= y0) {
		fail(r, "partie vide (hors de l'écran %ldx%ld)", sw, sh);
		return;
	}
	long cw = x1 - x0, chh = y1 - y0;
	long maxw = (long)json_int(a, "max_width", 1280);
	if (maxw < 64)
		maxw = 64;
	/* réduction par un facteur entier (moyenne des pixels : le texte reste lisible) */
	long f = 1;
	while (cw / f > maxw)
		f++;
	long ow = cw / f, oh = chh / f;
	uint8_t *rgb = malloc((size_t)ow * (size_t)oh * 3);
	if (!rgb) {
		fail(r, "mémoire");
		return;
	}
	for (long y = 0; y < oh; y++)
		for (long x = 0; x < ow; x++) {
			unsigned sr = 0, sg = 0, sb = 0;
			for (long dy = 0; dy < f; dy++) {
				const uint32_t *row = (const uint32_t *)(px + ((size_t)(y0 + y * f + dy) * (size_t)sw + (size_t)(x0 + x * f)) * 4);
				for (long dx = 0; dx < f; dx++) {
					uint32_t p = row[dx];
					sr += (p >> 16) & 255;
					sg += (p >> 8) & 255;
					sb += p & 255;
				}
			}
			unsigned d = (unsigned)(f * f);
			uint8_t *o = rgb + ((size_t)y * (size_t)ow + (size_t)x) * 3;
			o[0] = (uint8_t)(sr / d);
			o[1] = (uint8_t)(sg / d);
			o[2] = (uint8_t)(sb / d);
		}
	long long t0 = ksys0(SYS_TIME_MS);
	r->png = png_encode(rgb, (int)ow, (int)oh, &r->png_len);
	free(rgb);
	if (!r->png) {
		fail(r, "mémoire (PNG)");
		return;
	}
	struct jb *j = &r->j;
	jb_obj(j);
	jb_kint(j, "screen_width", sw);
	jb_kint(j, "screen_height", sh);
	if (wname[0])
		jb_kstr(j, "window", wname);
	jb_kint(j, "x", x0);
	jb_kint(j, "y", y0);
	jb_kint(j, "width", cw);
	jb_kint(j, "height", chh);
	jb_kint(j, "scale_divisor", f);
	jb_kint(j, "image_width", ow);
	jb_kint(j, "image_height", oh);
	jb_kint(j, "png_bytes", (long long)r->png_len);
	jb_kint(j, "encode_ms", ksys0(SYS_TIME_MS) - t0);
	if (f > 1)
		jb_kstr(j, "note", "image réduite : un pixel de l'image vaut scale_divisor pixels de l'écran (x, y : coin en pixels de l'écran)");
	jb_obj_end(j);
}

/* ---- signaux ---- */
static const struct {
	const char *name;
	int sig;
} signames[] = {
	{ "TERM", SIGTERM }, { "INT", SIGINT }, { "HUP", SIGHUP }, { "KILL", SIGKILL }, { "STOP", SIGSTOP },
	{ "CONT", SIGCONT }, { "USR1", SIGUSR1 }, { "USR2", SIGUSR2 }, { "QUIT", SIGQUIT }, { "WINCH", SIGWINCH },
};

static void t_send_signal(const struct jv *a, struct result *r)
{
	long pid = (long)json_int(a, "pid", 0);
	if (pid <= 0) {
		fail(r, "pid manque");
		return;
	}
	const struct jv *sv = json_get(a, "signal");
	int sig = SIGTERM;
	const char *sname = "TERM";
	if (sv && sv->t == J_NUM) {
		sig = (int)sv->num;
		sname = 0;
	} else if (sv && sv->t == J_STR) {
		const char *s = sv->s;
		if (!strncasecmp(s, "SIG", 3))
			s += 3;
		sig = 0;
		for (size_t i = 0; i < sizeof signames / sizeof signames[0]; i++)
			if (!strcasecmp(s, signames[i].name)) {
				sig = signames[i].sig;
				sname = signames[i].name;
			}
		if (!sig) {
			fail(r, "signal inconnu : %s (TERM, INT, HUP, KILL, STOP, CONT, USR1, USR2, QUIT, WINCH)", sv->s);
			return;
		}
	}
	uint8_t info[64];
	if (ksys2(SYS_PROC_INFO, pid, info) < 0) {
		fail(r, "pas de process %ld", pid);
		return;
	}
	info[24 + 31] = 0;
	char name[33];
	snprintf(name, sizeof name, "%s", (char *)info + 24);
	if (pid == getpid()) {
		fail(r, "c'est korin-mcp lui-même");
		return;
	}
	int sys = ksys1(SYS_PROC_RIGHTS, pid) == 1;
	if (sys && !json_bool(a, "allow_system", 0)) {
		fail(r, "%s (pid %ld) est un programme de /sys (service, pilote) : refusé sans allow_system (et l'accord de "
			"l'utilisateur : un service arrêté peut figer le système)", name, pid);
		return;
	}
	long rc = sig == SIGKILL ? ksys1(SYS_KILL, pid) : kill((pid_t)pid, sig) == 0 ? 0 : -errno;
	if (rc < 0) {
		fail(r, "%s (pid %ld) : refusé (%s)", name, pid, strerror((int)-rc));
		return;
	}
	struct jb *j = &r->j;
	jb_obj(j);
	jb_kint(j, "pid", pid);
	jb_kstr(j, "process", name);
	if (sname)
		jb_kstr(j, "signal", sname);
	else
		jb_kint(j, "signal", sig);
	jb_kbool(j, "sent", 1);
	jb_obj_end(j);
}

/* ---- lancer un programme ou une app, en arrière-plan ---- */
static const char *const no_launch[] = { "mkfs", "gpt", "osinstall", "reboot", "shutdown", "rm", "rmdir", "mv", "dd",
					 "format", "useradd", "userdel", "usermod", "passwd", "sudo", "mount", "eject", 0 };

static void t_launch(const struct jv *a, struct result *r)
{
	const char *prog = json_str(a, "program", 0);
	if (!prog || !prog[0]) {
		fail(r, "program manque (un nom de /bin, un chemin, ou une app : « terminal », « /apps/files.app »)");
		return;
	}
	char path[512];
	snprintf(path, sizeof path, "%s", prog);
	size_t pl = strlen(path);
	if (pl > 4 && !strcmp(path + pl - 4, ".app")) /* une app : son main */
		snprintf(path + pl, sizeof path - pl, "/main");
	else if (pl > 5 && !strcmp(path + pl - 5, ".app/"))
		snprintf(path + pl, sizeof path - pl, "main");
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	if (!strncmp(path, "/sys/", 5) || !strncmp(path, "/sbin/", 6) || !strncmp(path, "/boot", 5)) {
		fail(r, "%s : les programmes de /sys, /sbin et /boot ne se lancent pas par korin-mcp", path);
		return;
	}
	for (int i = 0; no_launch[i]; i++)
		if (!strcmp(base, no_launch[i])) {
			fail(r, "%s : commande qui change le système ou les fichiers, refusée par korin-mcp (l'outil Bash de "
				"l'agent, avec l'accord de l'utilisateur)", base);
			return;
		}
	/* les arguments */
	char *argv[66];
	int argc = 0;
	argv[argc++] = path;
	const struct jv *av = json_get(a, "args");
	for (const struct jv *k = av && av->t == J_ARR ? av->kid : 0; k && argc < 64; k = k->next) {
		if (k->t != J_STR)
			continue;
		if (strstr(k->s, "/media/sd1")) {
			fail(r, "/media/sd1 est la partition personnelle de l'utilisateur : refusé");
			return;
		}
		argv[argc++] = k->s;
	}
	argv[argc] = 0;
	mkdir("/tmp/korin-mcp", 0777);
	static int seq;
	char log[96];
	snprintf(log, sizeof log, "/tmp/korin-mcp/%d-%d.log", (int)getpid(), ++seq);
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	int out = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out >= 0) {
		posix_spawn_file_actions_adddup2(&fa, out, 1);
		posix_spawn_file_actions_adddup2(&fa, out, 2);
	}
	const char *cwd = json_str(a, "cwd", 0);
	char oldcwd[512] = "";
	if (cwd && (!getcwd(oldcwd, sizeof oldcwd) || chdir(cwd) != 0)) {
		if (out >= 0)
			close(out);
		posix_spawn_file_actions_destroy(&fa);
		fail(r, "%s : dossier impossible", cwd);
		return;
	}
	pid_t pid = -1;
	int rc = posix_spawnp(&pid, path, &fa, 0, argv, environ);
	if (cwd && oldcwd[0])
		chdir(oldcwd);
	posix_spawn_file_actions_destroy(&fa);
	if (out >= 0)
		close(out);
	if (rc != 0) {
		fail(r, "%s : lancement refusé (%s)", path, strerror(rc));
		return;
	}
	struct launched *l = &launched[nlaunched < LMAX ? nlaunched++ : (nlaunched = LMAX, LMAX - 1)];
	memset(l, 0, sizeof *l);
	l->pid = pid;
	snprintf(l->log, sizeof l->log, "%s", log);
	size_t n = 0;
	for (int i = 0; i < argc && n < sizeof l->cmd - 1; i++)
		n += (size_t)snprintf(l->cmd + n, sizeof l->cmd - n, "%s%s", i ? " " : "", argv[i]);
	/* un court délai : un programme qui échoue tout de suite le dit ici */
	long wait = (long)json_int(a, "wait_ms", 500);
	if (wait > 0)
		usleep((useconds_t)(wait > 10000 ? 10000 : wait) * 1000);
	launched_of(pid);
	struct jb *j = &r->j;
	jb_obj(j);
	jb_kint(j, "pid", pid);
	jb_kstr(j, "command", l->cmd);
	jb_kstr(j, "log", out >= 0 ? log : "");
	jb_kstr(j, "state", l->done ? "exited" : "running");
	if (l->done) {
		if (l->sig)
			jb_kint(j, "signal", l->sig);
		else
			jb_kint(j, "exit_code", l->code);
	}
	jb_kstr(j, "note", "sortie et erreurs dans log (read_file) ; process (pid) : son état ; send_signal : l'arrêter");
	jb_obj_end(j);
}

/* ---- la table ---- */
#define SAMPLE "\"sample_ms\":{\"type\":\"integer\",\"description\":\"durée de la mesure en ms (0 : aucune, les compteurs seuls), 1000 par défaut\"}"

const struct tool tools[] = {
	{ "processes", "Process",
	  "Les process de Korin : pid, nom, état, threads, mémoire (Ko à lui, partagés), appels système (en tout et par "
	  "seconde), temps de processeur (ms, % pendant la mesure), programme de /sys ou non. Ce que donnent sysstat et "
	  "cpuwatch, en JSON.",
	  "{\"type\":\"object\",\"properties\":{" SAMPLE ",\"name\":{\"type\":\"string\",\"description\":\"seulement les "
	  "noms qui contiennent ce texte\"},\"sort\":{\"type\":\"string\",\"enum\":[\"pid\",\"cpu\"]}}}",
	  1, 0, t_processes },
	{ "process", "Un process",
	  "Le détail d'un process (par pid ou par nom) : mémoire (pages, fautes, copies), chaque thread (état, priorité, "
	  "cœur, temps), ses appels système les plus fréquents par nom ; pour un process lancé par launch : sa commande, "
	  "son journal, son code de sortie.",
	  "{\"type\":\"object\",\"properties\":{\"pid\":{\"type\":\"integer\"},\"name\":{\"type\":\"string\"}," SAMPLE "}}",
	  1, 0, t_process },
	{ "cpu", "Processeur et mémoire",
	  "L'occupation du processeur : charge et fréquence de chaque cœur, mémoire totale et libre, temps depuis le "
	  "démarrage, commutations, et les threads qui consomment le plus pendant la mesure (comme cpuwatch).",
	  "{\"type\":\"object\",\"properties\":{" SAMPLE ",\"top\":{\"type\":\"integer\",\"description\":\"nombre de "
	  "threads rendus (15)\"}}}",
	  1, 0, t_cpu },
	{ "kernel_log", "Journal du noyau",
	  "Les dernières lignes du journal du noyau (SYS_KLOG, anneau de 64 Ko ; chaque ligne « [secondes] source: "
	  "texte ») : filtrées par source (le premier mot : netd, intelgpu, usbd, vfs…), par motif (regex POSIX étendue) "
	  "ou depuis un instant.",
	  "{\"type\":\"object\",\"properties\":{\"lines\":{\"type\":\"integer\",\"description\":\"au plus (100 ; 2000 au "
	  "plus)\"},\"pattern\":{\"type\":\"string\",\"description\":\"regex POSIX étendue\"},\"source\":{\"type\":"
	  "\"string\",\"description\":\"le premier mot de la ligne\"},\"since_ms\":{\"type\":\"integer\",\"description\":"
	  "\"seulement après ce temps depuis le démarrage (uptime_ms d'un appel précédent)\"},\"case_sensitive\":"
	  "{\"type\":\"boolean\"}}}",
	  1, 0, t_kernel_log },
	{ "list_dir", "Lister un dossier",
	  "Le contenu d'un dossier : nom, type, taille, date (UTC), droits. Un seul arbre : / (système), /tmp, "
	  "/media/usb0, /media/sd1 (données de l'utilisateur, Pi), /net/…, HOME:/ ou ~ (dossier personnel).",
	  "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"hidden\":{\"type\":\"boolean\"},"
	  "\"limit\":{\"type\":\"integer\"}},\"required\":[\"path\"]}",
	  1, 0, t_list_dir },
	{ "read_file", "Lire un fichier",
	  "Lire un fichier, ou un morceau : par octets (offset, length : 64 Ko par défaut, 512 Ko au plus) ou par lignes "
	  "(start_line, max_lines). Texte UTF-8 tel quel, sinon base64. Les fichiers de mots de passe et de clés sont "
	  "refusés.",
	  "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"offset\":{\"type\":\"integer\"},"
	  "\"length\":{\"type\":\"integer\"},\"start_line\":{\"type\":\"integer\"},\"max_lines\":{\"type\":\"integer\"},"
	  "\"encoding\":{\"type\":\"string\",\"enum\":[\"auto\",\"text\",\"base64\"]}},\"required\":[\"path\"]}",
	  1, 0, t_read_file },
	{ "find_files", "Chercher des fichiers",
	  "Chercher des fichiers par nom sous un dossier : motif (name : *.c, main.?, sans casse par défaut) ou morceau "
	  "du nom (contains) ; type file ou dir ; profondeur, nombre et délai limités. /net n'est parcouru que si root "
	  "est dedans.",
	  "{\"type\":\"object\",\"properties\":{\"root\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},"
	  "\"contains\":{\"type\":\"string\"},\"type\":{\"type\":\"string\",\"enum\":[\"any\",\"file\",\"dir\"]},"
	  "\"max_depth\":{\"type\":\"integer\"},\"max_results\":{\"type\":\"integer\"},\"case_sensitive\":{\"type\":"
	  "\"boolean\"},\"timeout_ms\":{\"type\":\"integer\"}}}",
	  1, 0, t_find_files },
	{ "volumes", "Volumes",
	  "Les volumes montés : nom interne, point de montage, système de fichiers, taille, place prise et libre, "
	  "étiquette.",
	  "{\"type\":\"object\",\"properties\":{}}", 1, 0, t_volumes },
	{ "packages", "Paquets",
	  "Les paquets (pkg) : installed (installés, version, version du dépôt), updates (les mises à jour), available "
	  "(tout le dépôt), info (un paquet : [package] de sa base, nombre de fichiers, entrée du dépôt). D'après le "
	  "dernier index vérifié (/var/pkg/index.txt) ; installer ou mettre à jour : pkg, avec l'accord de l'utilisateur.",
	  "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"installed\",\"updates\","
	  "\"available\",\"info\"]},\"name\":{\"type\":\"string\"}}}",
	  1, 0, t_packages },
	{ "services", "Services",
	  "Les services : chaque ligne de /etc/services.txt (lancés et relancés par init2) et de /etc/startup.txt, "
	  "active ou en commentaire, en marche ou non (pid) ; tous les process qui ont les droits de /sys (services, "
	  "pilotes lancés par hwmanager) ; quelques services enregistrés (vfs, net, sound, hid, intelgpu…).",
	  "{\"type\":\"object\",\"properties\":{}}", 1, 0, t_services },
	{ "gpu", "GPU Intel",
	  "L'état du service intelgpu (comme intelgpu status) : prêt, simulé ou perdu, identité du GPU, EU, remises à "
	  "zéro, temps des lots ; ou qu'il ne tourne pas (QEMU, Pi).",
	  "{\"type\":\"object\",\"properties\":{}}", 1, 0, t_gpu },
	{ "network", "Réseau",
	  "Le réseau (netd, NetKit) : adresse IPv4, masque, passerelle, DNS, MAC, DHCP ; les interfaces (lien, débit, "
	  "Wi-Fi) ; les sockets ouverts de tout le système (TCP et UDP, état, ports, adresse distante, programme).",
	  "{\"type\":\"object\",\"properties\":{\"sockets\":{\"type\":\"boolean\"}}}", 1, 0, t_network },
	{ "time", "Heure",
	  "L'heure du système (UTC et locale), le temps depuis le démarrage, et si l'heure vient du réseau (NTP).",
	  "{\"type\":\"object\",\"properties\":{}}", 1, 0, t_time },
	{ "windows", "Fenêtres",
	  "Les fenêtres du bureau Elegant, du fond vers l'avant : id, titre, process, position et taille du contenu (pixels "
	  "de l'écran), au clavier, cachée, couche.",
	  "{\"type\":\"object\",\"properties\":{}}", 1, 0, t_windows },
	{ "screenshot", "Capture d'écran",
	  "Une capture de l'écran (tel qu'affiché, curseur compris), en image PNG : tout l'écran, une fenêtre (window : "
	  "un morceau du titre, ou window_id ; frame : avec son cadre) ou un rectangle (region). Réduite d'un facteur "
	  "entier sous max_width (1280).",
	  "{\"type\":\"object\",\"properties\":{\"window\":{\"type\":\"string\"},\"window_id\":{\"type\":\"integer\"},"
	  "\"frame\":{\"type\":\"boolean\"},\"region\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\"},"
	  "\"y\":{\"type\":\"integer\"},\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"}}},"
	  "\"max_width\":{\"type\":\"integer\"}}}",
	  1, 0, t_screenshot },
	{ "send_signal", "Envoyer un signal",
	  "Envoyer un signal à un process (TERM par défaut ; INT, HUP, KILL, STOP, CONT, USR1, USR2…). Refusé pour "
	  "korin-mcp lui-même, et pour un programme de /sys (service, pilote) sans allow_system.",
	  "{\"type\":\"object\",\"properties\":{\"pid\":{\"type\":\"integer\"},\"signal\":{\"type\":[\"string\","
	  "\"integer\"]},\"allow_system\":{\"type\":\"boolean\"}},\"required\":[\"pid\"]}",
	  0, 1, t_send_signal },
	{ "launch", "Lancer",
	  "Lancer un programme ou une app en arrière-plan (sans attendre sa fin) : une commande de /bin (nom ou chemin), "
	  "une app (« terminal », « /apps/files.app »), avec ses arguments et son dossier ; sortie et erreurs dans un "
	  "journal de /tmp/korin-mcp. Pas de programme de /sys ni de commande qui efface, formate ou installe.",
	  "{\"type\":\"object\",\"properties\":{\"program\":{\"type\":\"string\"},\"args\":{\"type\":\"array\","
	  "\"items\":{\"type\":\"string\"}},\"cwd\":{\"type\":\"string\"},\"wait_ms\":{\"type\":\"integer\","
	  "\"description\":\"attendre ce temps avant de répondre (500) : un échec immédiat se voit\"}},"
	  "\"required\":[\"program\"]}",
	  0, 0, t_launch },
};

const int ntools = (int)(sizeof tools / sizeof tools[0]);
