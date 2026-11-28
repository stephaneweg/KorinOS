/*
 * applet.cpp -- l'applet « Tâches planifiées » de Réglages, installé avec le service taskd (docs/TACHES.md) :
 * /sys/applets/tasks.so, chargé par Réglages (src/apps/control/control.h, « les applets installées »).
 *
 *   * en tête : Mes tâches | Tâches du système (celles-ci s'écrivent avec l'élévation) ;
 *   * la liste : nom, quand, dernier passage (réussi, échec, en cours), prochain ;
 *   * Nouvelle, Modifier, Exécuter maintenant, Activer / Désactiver, Sortie (le journal du dernier passage),
 *     Supprimer ;
 *   * le dialogue d'une tâche : quoi lancer (une application, un programme ou un script, une ligne de bash), quand
 *     (listes déroulantes : toutes les n minutes, chaque jour, certains jours, chaque mois, une fois, au démarrage,
 *     à l'extinction, à un événement choisi dans une liste), les options, et une phrase qui dit ce qui va se passer.
 * Les fichiers : <racine>/etc/tasks/<id>.task (taskfile.h) ; l'état et la sortie, écrits par taskd :
 * <racine>/var/tasks/<id>.state, .log. Après un changement, korin.tasks.changed : taskd relit tout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <buskit_abi.h>
#include <armpi/syscall.h>
#include "control.h"
#include "../taskfile.h"

enum { TB_BTN = 28, TB_CT = 24, TB_TOPS = 20, TMAX = 64, BTN_COLW = 150 };

#define TASKD "/sys/svc/taskd"

struct item {
	struct task t;
	long last, code, next, running;
};

static long tops[TB_TOPS];
static int ntop;
static long w_level, w_list, w_status, w_toggle, w_run, w_edit, w_del, w_log;
static int level;		/* 0 : mes tâches, 1 : le système */
static struct item items[TMAX];
static int nitems;
static char sig[TMAX * 48];	/* l'état montré (le tick ne refait la liste que s'il change) */
static int task_off_warned;

static const char *root (void) { return level ? "" : "HOME:"; }

static void path_of (char *out, size_t cap, const char *dir, const char *id, const char *ext)
{
	snprintf (out, cap, "%s/%s/tasks/%s%s", root (), dir, id, ext);
}

/* ---- les widgets ---- */

static long top (long w)
{
	if (ntop < TB_TOPS)
		tops[ntop++] = w;
	return w;
}

static void tb_close (void)
{
	for (int i = 0; i < ntop; i++)
		ui_w_destroy (tops[i]);
	ntop = 0;
}

static void tb_draw (void) { ui_w_draw (0); }

static void status (const char *text)
{
	ui_w_set_text (w_status, (long) text);
	ui_w_draw (w_status);
}

/* ---- les textes ---- */

static const char *const day_short[7] = { "dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam." };

static void fmt_hm (char *out, size_t cap, int h, int m) { snprintf (out, cap, "%02d:%02d", h, m); }

/* les déclencheurs d'événement proposés : le texte, la ligne when */
static const struct { const char *label, *when; } presets[] = {
	{ "Le réseau est connecté", "event korin.net.state up*" },
	{ "Le réseau est coupé", "event korin.net.state down*" },
	{ "Un volume est monté (clé USB, disque)", "event korin.storage.volume.*" },
	{ "Un volume est retiré", "removed korin.storage.volume.*" },
	{ "Un périphérique USB est branché", "event korin.usb.device.*" },
	{ "Le secteur est branché", "power ac" },
	{ "L'ordinateur passe sur batterie", "power battery" },
	{ "La batterie descend sous un seuil", "battery 20" },
	{ "Le couvercle est fermé", "event korin.power.lid closed" },
	{ "Le couvercle est ouvert", "event korin.power.lid open" },
	{ "Le thème change", "event korin.theme" },
	{ "Une application est installée ou retirée", "event korin.apps.changed" },
	{ "Un autre sujet du bus…", 0 },
};
enum { NPRESET = sizeof presets / sizeof presets[0], P_BATTERY = 7, P_CUSTOM = NPRESET - 1 };

/* le préréglage d'un déclencheur d'événement (P_CUSTOM : aucun) */
static int preset_of (const struct tk_when *w)
{
	if (w->type == TW_BATTERY)
		return P_BATTERY;
	char a[TK_TOPIC + TK_VALUE + 16];
	tk_when_format (w, a, sizeof a);
	for (int i = 0; i < P_CUSTOM; i++)
		if (!strcmp (a, presets[i].when))
			return i;
	return P_CUSTOM;
}

/* un déclencheur en clair : « chaque jour à 03:00 » */
static void describe (const struct tk_when *w, int lv, char *out, size_t cap)
{
	char hm[8];
	fmt_hm (hm, sizeof hm, w->hour, w->min);
	switch (w->type) {
	case TW_EVERY:
		if (w->n == 1)
			snprintf (out, cap, "%s", TR (w->unit == 60 ? "chaque minute" : w->unit == 3600 ? "toutes les heures"
				: "une fois par jour"));
		else
			snprintf (out, cap, "%s %ld %s", TR ("toutes les"), w->n,
				  TR (w->unit == 60 ? "minutes" : w->unit == 3600 ? "heures" : "jours"));
		break;
	case TW_DAILY:
		snprintf (out, cap, "%s %s", TR ("chaque jour à"), hm);
		break;
	case TW_WEEKLY: {
		char d[80] = "";
		if (w->days == 0x3E)
			snprintf (d, sizeof d, "%s", TR ("du lundi au vendredi"));
		else if (w->days == 0x41)
			snprintf (d, sizeof d, "%s", TR ("le week-end"));
		else if (w->days == 0x7F)
			snprintf (d, sizeof d, "%s", TR ("chaque jour"));
		else
			for (int i = 1; i <= 7; i++)
				if (w->days & (1 << (i % 7))) {
					size_t n = strlen (d);
					snprintf (d + n, sizeof d - n, "%s%s", n ? " " : "", TR (day_short[i % 7]));
				}
		snprintf (out, cap, "%s %s %s", d, TR ("à"), hm);
		break;
	}
	case TW_MONTHLY:
		if (w->mday >= 28)
			snprintf (out, cap, "%s %d %s %s %s", TR ("le"), w->mday, TR ("de chaque mois (ou le dernier jour)"), TR ("à"), hm);
		else
			snprintf (out, cap, "%s %d %s %s %s", TR ("le"), w->mday, TR ("de chaque mois"), TR ("à"), hm);
		break;
	case TW_ONCE:
		snprintf (out, cap, "%s %02d/%02d/%04d %s %s", TR ("le"), w->day, w->month, w->year, TR ("à"), hm);
		break;
	case TW_STARTUP:
		snprintf (out, cap, "%s", TR (lv ? "au démarrage de Korin" : "à l'ouverture de ma session"));
		break;
	case TW_SHUTDOWN:
		snprintf (out, cap, "%s", TR (lv ? "à l'extinction de Korin" : "à la fermeture de ma session"));
		break;
	default: {
		int p = preset_of (w);
		if (p == P_BATTERY)
			snprintf (out, cap, "%s %ld %%", TR ("quand la batterie descend sous"), w->n);
		else if (p != P_CUSTOM)
			snprintf (out, cap, "%s %s", TR ("quand :"), TR (presets[p].label));
		else if (w->type == TW_REMOVED)
			snprintf (out, cap, "%s %s", TR ("quand ce sujet est retiré :"), w->topic);
		else
			snprintf (out, cap, "%s %s%s%s", TR ("quand ce sujet change :"), w->topic, w->value[0] ? " = " : "", w->value);
	}
	}
}


/* le décalage de l'heure locale (SYS_CLOCK_GET 1), l'heure UTC (0) */
static long clock_get (long which) { return armpi_syscall (0x1B, which, 0, 0, 0, 0, 0); }

static void date_short (long utc, char *out, size_t cap)
{
	struct tk_tm t, n;
	tk_civil (utc + clock_get (1), &t);
	tk_civil (clock_get (0) + clock_get (1), &n);
	if (t.year == n.year && t.month == n.month && t.day == n.day)
		snprintf (out, cap, "%s %02d:%02d", TR ("aujourd'hui"), t.hour, t.min);
	else
		snprintf (out, cap, "%02d/%02d %02d:%02d", t.day, t.month, t.hour, t.min);
}

/* ---- les fichiers ---- */

static long read_text (const char *path, char *buf, size_t cap)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return -1;
	size_t n = fread (buf, 1, cap - 1, f);
	fclose (f);
	buf[n] = 0;
	return (long) n;
}

static long state_val (const char *s, const char *key)
{
	size_t kl = strlen (key);
	for (const char *l = s; *l;) {
		while (*l == ' ')
			l++;
		if (!strncmp (l, key, kl) && (l[kl] == ' ' || l[kl] == '=')) {
			const char *v = l + kl;
			while (*v == ' ' || *v == '=')
				v++;
			return strtol (v, 0, 10);
		}
		while (*l && *l != '\n')
			l++;
		if (*l)
			l++;
	}
	return 0;
}

static void state_read (struct item *it)
{
	char p[200], b[400];
	path_of (p, sizeof p, "var", it->t.id, ".state");
	if (read_text (p, b, sizeof b) < 0)
		b[0] = 0;
	it->last = state_val (b, "last");
	it->code = state_val (b, "code");
	it->next = state_val (b, "next");
	it->running = state_val (b, "running");
}

static int item_cmp (const void *a, const void *b)
{
	return strcasecmp (((const struct item *) a)->t.name, ((const struct item *) b)->t.name);
}

static void items_load (void)
{
	nitems = 0;
	char dir[160];
	snprintf (dir, sizeof dir, "%s/etc/tasks", root ());
	DIR *d = opendir (dir);
	if (!d)
		return;
	static char text[TK_FILE + 1];
	for (struct dirent *e; nitems < TMAX && (e = readdir (d));) {
		size_t l = strlen (e->d_name);
		if (l <= 5 || l - 5 >= TK_ID || strcmp (e->d_name + l - 5, ".task"))
			continue;
		struct item *it = &items[nitems];
		memset (it, 0, sizeof *it);
		memcpy (it->t.id, e->d_name, l - 5);
		if (!tk_id_ok (it->t.id))
			continue;
		char p[260];
		snprintf (p, sizeof p, "%s/%s", dir, e->d_name);
		if (read_text (p, text, sizeof text) < 0 || tk_parse (&it->t, text) < 0)
			continue;
		if (!it->t.name[0])
			tk_copy (it->t.name, it->t.id, sizeof it->t.name);
		state_read (it);
		nitems++;
	}
	closedir (d);
	qsort (items, (size_t) nitems, sizeof items[0], item_cmp);
}

/* l'élévation le temps d'écrire dans /etc (les tâches du système ; docs/etude/MULTI-UTILISATEUR.md §2.6) */
static int elevated (int (*fn) (void *), void *arg)
{
	if (!level)
		return fn (arg);
	unsigned me;
	if (ap_elevate (TR ("Réglages veut modifier les tâches planifiées du système."), &me)) {
		status (TR ("Autorisation refusée."));
		return -2;
	}
	int r = fn (arg);
	ap_unelevate (me);
	return r;
}

static void notify_taskd (const char *topic, const char *value)
{
	bus_publish ((long) topic, (long) value, -1, 0);
}

static int save_fn (void *a)
{
	const struct task *t = (const struct task *) a;
	char p[200];
	snprintf (p, sizeof p, "%s/etc", root ());
	mkdir (p, 0755);
	snprintf (p, sizeof p, "%s/etc/tasks", root ());
	mkdir (p, 0755);
	path_of (p, sizeof p, "etc", t->id, ".task");
	static char text[TK_FILE];
	long n = tk_format (t, text, sizeof text);
	FILE *f = fopen (p, "wb");
	if (!f)
		return -1;
	size_t w = fwrite (text, 1, (size_t) n, f);
	return fclose (f) == 0 && w == (size_t) n ? 0 : -1;
}

/* -> 0, -2 refusé (message déjà dit), -1 écriture impossible */
static int task_save (struct task *t)
{
	int r = elevated (save_fn, t);
	if (r == 0)
		notify_taskd ("korin.tasks.changed", t->id);
	else if (r == -1)
		status (TR (level ? "/etc/tasks ne peut pas être écrit." : "Le dossier des tâches ne peut pas être écrit."));
	return r;
}

static int remove_fn (void *a)
{
	const char *id = (const char *) a;
	char p[200];
	path_of (p, sizeof p, "etc", id, ".task");
	int r = unlink (p);
	path_of (p, sizeof p, "var", id, ".state");
	unlink (p);
	path_of (p, sizeof p, "var", id, ".log");
	unlink (p);
	return r == 0 ? 0 : -1;
}

/* ---- la liste ---- */

static void when_text (const struct item *it, char *out, size_t cap)
{
	if (!it->t.nwhen) {
		snprintf (out, cap, "%s", TR ("jamais (à la demande)"));
		return;
	}
	describe (&it->t.when[0], level, out, cap);
	if (it->t.nwhen > 1) {
		size_t n = strlen (out);
		snprintf (out + n, cap - n, " (+%d)", it->t.nwhen - 1);
	}
	if (out[0] >= 'a' && out[0] <= 'z')
		out[0] = (char) (out[0] - 32);
}

static void last_text (const struct item *it, char *out, size_t cap)
{
	if (it->running) {
		snprintf (out, cap, "%s", TR ("en cours…"));
		return;
	}
	if (!it->last) {
		snprintf (out, cap, "%s", TR ("jamais"));
		return;
	}
	char d[32];
	date_short (it->last, d, sizeof d);
	if (it->code == 0)
		snprintf (out, cap, "%s · %s", d, TR ("réussie"));
	else if (it->code == -1001)
		snprintf (out, cap, "%s · %s", d, TR ("arrêtée (trop longue)"));
	else if (it->code == -1002)
		snprintf (out, cap, "%s · %s", d, TR ("pas lancée"));
	else if (it->code == -1000)
		snprintf (out, cap, "%s · %s", d, TR ("terminée"));
	else
		snprintf (out, cap, "%s · %s %ld", d, TR ("échec, code"), it->code);
}

static void next_text (const struct item *it, char *out, size_t cap)
{
	if (!it->t.enabled) {
		snprintf (out, cap, "%s", TR ("désactivée"));
		return;
	}
	if (it->next > 0) {
		date_short (it->next, out, cap);
		return;
	}
	int ev = 0, st = 0, sd = 0;
	for (int j = 0; j < it->t.nwhen; j++) {
		ev |= tk_is_event (it->t.when[j].type);
		st |= it->t.when[j].type == TW_STARTUP;
		sd |= it->t.when[j].type == TW_SHUTDOWN;
	}
	snprintf (out, cap, "%s", TR (ev ? "à l'événement" : st ? (level ? "au démarrage" : "à l'ouverture")
		: sd ? (level ? "à l'extinction" : "à la fermeture") : "—"));
}

static int sel (void)
{
	long v = ui_w_get_value (w_list);
	return v >= 0 && v < nitems ? (int) v : -1;
}

static void buttons_state (void)
{
	int i = sel ();
	ui_w_enable (w_edit, i >= 0);
	ui_w_enable (w_run, i >= 0);
	ui_w_enable (w_toggle, i >= 0);
	ui_w_enable (w_log, i >= 0);
	ui_w_enable (w_del, i >= 0);
	ui_w_set_text (w_toggle, (long) TR (i >= 0 && !items[i].t.enabled ? "Activer" : "Désactiver"));
}

static void row_text (const struct item *it, char *row, size_t cap)
{
	char w[160], l[80], n[80];
	when_text (it, w, sizeof w);
	last_text (it, l, sizeof l);
	next_text (it, n, sizeof n);
	snprintf (row, cap, "%s\t%s\t%s\t%s", it->t.name, w, l, n);
}

static void sig_make (char *out, size_t cap)
{
	size_t n = 0;
	out[0] = 0;
	for (int i = 0; i < nitems && n + 48 < cap; i++)
		n += (size_t) snprintf (out + n, cap - n, "%ld %ld %ld %ld|", items[i].last, items[i].code, items[i].next,
					items[i].running);
}

static void list_fill (const char *keep_id)
{
	items_load ();
	ui_list_clear (w_list);
	int keep = 0;
	for (int i = 0; i < nitems; i++) {
		char row[400];
		row_text (&items[i], row, sizeof row);
		ui_list_add (w_list, (long) row, i);
		if (keep_id && !strcmp (keep_id, items[i].t.id))
			keep = i;
	}
	ui_w_set_value (w_list, nitems ? keep : -1);
	sig_make (sig, sizeof sig);
	buttons_state ();
	ui_w_draw (0);
}

/* chaque seconde : l'état écrit par taskd (dernier passage, prochain) */
static int tb_tick (void)
{
	if (!nitems)
		return 0;
	for (int i = 0; i < nitems; i++)
		state_read (&items[i]);
	char s[sizeof sig];
	sig_make (s, sizeof s);
	if (!strcmp (s, sig))
		return 0;
	strcpy (sig, s);
	for (int i = 0; i < nitems; i++) {
		char row[400];
		row_text (&items[i], row, sizeof row);
		ui_list_set (w_list, i, (long) row);
	}
	ui_w_draw (w_list);
	return 0;
}

/* ---- le dialogue d'une tâche ---- */

static struct {
	long name, kind, app, path, browse, cmd;
	long when, n, unit, at, time, days[7], le, mday, dumois, date;
	long event, topic, value, pct, pctl;
	long catchup, ac, net, delay, timeout, notify, enabled, summary;
	int napps;
	char app_names[APP_MAX][APP_NAME];
} dl;

enum { K_APP, K_PROGRAM, K_SHELL };			/* les éléments de la liste « Lancer » */
enum { W_EVERY, W_DAILY, W_WEEKLY, W_MONTHLY, W_ONCE, W_STARTUP, W_SHUTDOWN, W_EVENT };
static const int day_order[7] = { 1, 2, 3, 4, 5, 6, 0 };	/* les cases : lundi d'abord */

static const char *txt (long w) { return (const char *) ui_w_get_text (w); }

/* le dialogue -> la tâche ; -> 0, ou le message de ce qui ne va pas */
static const char *dl_collect (struct task *t)
{
	snprintf (t->name, sizeof t->name, "%s", txt (dl.name));
	long k = ui_w_get_value (dl.kind);
	t->kind = k == K_APP ? TK_APP : k == K_SHELL ? TK_SHELL : TK_PROGRAM;
	if (k == K_APP) {
		long a = ui_w_get_value (dl.app);
		t->run[0] = 0;
		if (a >= 0 && a < dl.napps)
			snprintf (t->run, sizeof t->run, "%s", dl.app_names[a]);
	} else
		snprintf (t->run, sizeof t->run, "%s", txt (k == K_SHELL ? dl.cmd : dl.path));
	char line[TK_TOPIC + TK_VALUE + 40];
	const char *tm = txt (dl.time);
	switch (ui_w_get_value (dl.when)) {
	case W_EVERY: {
		static const char u[3] = { 'm', 'h', 'd' };
		long un = ui_w_get_value (dl.unit);
		snprintf (line, sizeof line, "every %s %c", txt (dl.n), u[un >= 0 && un < 3 ? un : 0]);
		break;
	}
	case W_DAILY:
		snprintf (line, sizeof line, "daily %s", tm);
		break;
	case W_WEEKLY: {
		static const char *const nm[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
		char d[40] = "";
		for (int i = 0; i < 7; i++)
			if (ui_w_get_value (dl.days[i])) {
				size_t n = strlen (d);
				snprintf (d + n, sizeof d - n, "%s%s", n ? "," : "", nm[day_order[i]]);
			}
		if (!d[0])
			return "Cochez au moins un jour.";
		snprintf (line, sizeof line, "weekly %s %s", d, tm);
		break;
	}
	case W_MONTHLY:
		snprintf (line, sizeof line, "monthly %s %s", txt (dl.mday), tm);
		break;
	case W_ONCE:
		snprintf (line, sizeof line, "once %s %s", txt (dl.date), tm);
		break;
	case W_STARTUP:
		snprintf (line, sizeof line, "startup");
		break;
	case W_SHUTDOWN:
		snprintf (line, sizeof line, "shutdown");
		break;
	default: {
		long p = ui_w_get_value (dl.event);
		if (p == P_BATTERY)
			snprintf (line, sizeof line, "battery %s", txt (dl.pct));
		else if (p >= 0 && p < P_CUSTOM)
			snprintf (line, sizeof line, "%s", presets[p].when);
		else {
			const char *v = txt (dl.value);
			snprintf (line, sizeof line, strchr (v, ' ') ? "event %s \"%s\"" : "event %s %s", txt (dl.topic), v);
		}
	}
	}
	struct tk_when w;
	int ok = tk_when_parse (&w, line);
	/* (le premier déclencheur est celui du dialogue ; les autres lignes when du fichier sont gardées) */
	if (t->nwhen == 0)
		t->nwhen = 1;
	if (ok)
		t->when[0] = w;
	t->catchup = ui_w_get_value (dl.catchup) != 0;
	t->ac_only = ui_w_get_value (dl.ac) != 0;
	t->net_only = ui_w_get_value (dl.net) != 0;
	t->delay = atol (txt (dl.delay));
	t->timeout = atol (txt (dl.timeout)) * 60;
	long nt = ui_w_get_value (dl.notify);
	t->notify = nt == 0 ? TN_NEVER : nt == 2 ? TN_ALWAYS : TN_FAIL;
	t->enabled = ui_w_get_value (dl.enabled) != 0;
	if (!ok) {
		switch (ui_w_get_value (dl.when)) {
		case W_EVERY: return "Donnez un nombre (1 ou plus).";
		case W_MONTHLY: return "Donnez un jour du mois (1 à 31) et une heure (HH:MM).";
		case W_ONCE: return "Donnez une date (AAAA-MM-JJ) et une heure (HH:MM).";
		case W_EVENT: return ui_w_get_value (dl.event) == P_BATTERY ? "Donnez un seuil de 1 à 100 %."
			: "Donnez le sujet du bus (korin.net.state, korin.usb.* …).";
		default: return "Donnez une heure (HH:MM, de 00:00 à 23:59).";
		}
	}
	return 0;
}

/* les widgets montrés selon les choix, et la phrase du résumé */
static void dl_update (void)
{
	long k = ui_w_get_value (dl.kind), w = ui_w_get_value (dl.when), p = ui_w_get_value (dl.event);
	ui_w_show (dl.app, k == K_APP);
	ui_w_show (dl.path, k == K_PROGRAM);
	ui_w_show (dl.browse, k == K_PROGRAM);
	ui_w_show (dl.cmd, k == K_SHELL);
	ui_w_show (dl.n, w == W_EVERY);
	ui_w_show (dl.unit, w == W_EVERY);
	int timed = w == W_DAILY || w == W_WEEKLY || w == W_MONTHLY || w == W_ONCE;
	ui_w_show (dl.time, timed);
	ui_w_show (dl.at, timed);
	for (int i = 0; i < 7; i++)
		ui_w_show (dl.days[i], w == W_WEEKLY);
	ui_w_show (dl.le, w == W_MONTHLY);
	ui_w_show (dl.mday, w == W_MONTHLY);
	ui_w_show (dl.dumois, w == W_MONTHLY);
	ui_w_show (dl.date, w == W_ONCE);
	ui_w_show (dl.event, w == W_EVENT);
	ui_w_show (dl.topic, w == W_EVENT && p == P_CUSTOM);
	ui_w_show (dl.value, w == W_EVENT && p == P_CUSTOM);
	ui_w_show (dl.pct, w == W_EVENT && p == P_BATTERY);
	ui_w_show (dl.pctl, w == W_EVENT && p == P_BATTERY);
	ui_w_enable (dl.catchup, timed || w == W_EVERY);

	struct task t;
	memset (&t, 0, sizeof t);
	const char *bad = dl_collect (&t);
	char s[600], what[200], when[200];
	if (k == K_APP) {
		long a = ui_w_get_value (dl.app);
		snprintf (what, sizeof what, "%s « %s »", TR ("Ouvrira l'application"), a >= 0 && a < apps_n ? apps[a].label : "?");
	} else if (k == K_SHELL)
		snprintf (what, sizeof what, "%s", TR ("Lancera la commande"));
	else {
		const char *r = t.run[0] ? strrchr (t.run, '/') : 0;
		char b[120];
		snprintf (b, sizeof b, "%s", r ? r + 1 : t.run[0] ? t.run : "…");
		char *sp = strchr (b, ' ');
		if (sp)
			*sp = 0;
		snprintf (what, sizeof what, "%s « %s »", TR ("Lancera"), b);
	}
	if (bad)
		snprintf (s, sizeof s, "%s", TR (bad));
	else {
		describe (&t.when[0], level, when, sizeof when);
		snprintf (s, sizeof s, "%s %s%s%s.", what, when, t.ac_only ? TR (", sur secteur seulement") : "",
			  t.net_only ? TR (", avec le réseau seulement") : "");
		if (!t.enabled)
			snprintf (s + strlen (s), sizeof s - strlen (s), " %s", TR ("(désactivée : elle ne partira pas seule)"));
	}
	ui_w_set_text (dl.summary, (long) s);
	ui_w_draw (0);
}

static long dl_fn (long d, long w, long code)
{
	if (w == dl.browse && code == UN_ACTIVATE) {
		static char p[TK_RUN];
		snprintf (p, sizeof p, "%s", txt (dl.path));
		char *sp = strchr (p, ' ');
		if (sp)
			*sp = 0;
		if (!p[0] || access (p, F_OK) != 0)
			strcpy (p, "HOME:/");
		if (ui_file_open ((long) p, (long) p, sizeof p) == 1)
			ui_w_set_text (dl.path, (long) p);
		dl_update ();
	} else if (code == UN_CHANGE)
		dl_update ();
	else if (code == UN_ACTIVATE && w != dl.browse && w != dl.cmd)
		ui_dialog_close (d, 1);
	return 0;
}

/* t : la tâche à remplir (ses valeurs de départ) -> 1 : Enregistrer, 2 : à reprendre (le message est dit), 0 : Annuler */
static int task_dialog (const char *title, struct task *t)
{
	enum { DW = 620, DH = 560, M = 16, LW = 120, CH = 30, RW = DW - 2 * M - LW };
	long d = ui_dialog_begin (DW, DH, (long) TR (title));
	if (!d)
		return 0;
	long x = M + LW, y = 16;
	ui_label (M, y + 6, LW, 20, (long) TR ("Nom"), 0);
	dl.name = ui_textbox (x, y, RW, CH, 0);
	ui_w_set_text (dl.name, (long) t->name);
	ui_textbox_hint (dl.name, (long) TR ("Sauvegarde des notes, Nettoyage…"));
	y += 42;

	/* quoi */
	ui_label (M, y + 6, LW, 20, (long) TR ("Lancer"), 0);
	char items_k[200];
	snprintf (items_k, sizeof items_k, "%s|%s|%s", TR ("Une application"), TR ("Un programme ou un script"),
		  TR ("Une commande (bash)"));
	dl.kind = ui_dropdown (x, y, 260, CH, (long) items_k);
	ui_w_set_value (dl.kind, t->kind == TK_APP ? K_APP : t->kind == TK_SHELL ? K_SHELL : K_PROGRAM);
	y += 38;
	applis_load ();
	static char app_items[APP_MAX * (APP_LABEL + 1)];
	app_items[0] = 0;
	dl.napps = 0;
	long app_sel = 0;
	for (long i = 0; i < apps_n && dl.napps < APP_MAX; i++) {
		size_t n = strlen (app_items);
		snprintf (app_items + n, sizeof app_items - n, "%s%s", n ? "|" : "", apps[i].label);
		snprintf (dl.app_names[dl.napps], APP_NAME, "%s", apps[i].name);
		if (t->kind == TK_APP && !strcmp (t->run, apps[i].name))
			app_sel = dl.napps;
		dl.napps++;
	}
	dl.app = ui_dropdown (x, y, RW, CH, (long) (app_items[0] ? app_items : "-"));
	ui_w_set_value (dl.app, app_sel);
	dl.path = ui_textbox (x, y, RW - 108, CH, 0);
	ui_textbox_hint (dl.path, (long) TR ("/home/moi/bin/sauvegarde.sh, ou un programme et ses arguments"));
	dl.browse = ui_pushbutton (DW - M - 100, y, 100, CH, (long) TR ("Choisir…"), 0);
	dl.cmd = ui_textbox (x, y, RW, CH, UTF_MONO);
	ui_textbox_hint (dl.cmd, (long) TR ("cp -r ~/documents /media/usb0/"));
	if (t->kind == TK_SHELL)
		ui_w_set_text (dl.cmd, (long) t->run);
	else if (t->kind == TK_PROGRAM)
		ui_w_set_text (dl.path, (long) t->run);
	y += 46;

	/* quand */
	const struct tk_when *w0 = t->nwhen ? &t->when[0] : 0;
	int wt = w0 ? w0->type : TW_DAILY;
	ui_label (M, y + 6, LW, 20, (long) TR ("Quand"), 0);
	char items_w[400];
	snprintf (items_w, sizeof items_w, "%s|%s|%s|%s|%s|%s|%s|%s", TR ("À intervalles réguliers"), TR ("Chaque jour"),
		  TR ("Certains jours de la semaine"), TR ("Chaque mois"), TR ("Une seule fois"),
		  TR (level ? "Au démarrage de Korin" : "À l'ouverture de ma session"),
		  TR (level ? "À l'extinction de Korin" : "À la fermeture de ma session"), TR ("Quand un événement arrive"));
	dl.when = ui_dropdown (x, y, 300, CH, (long) items_w);
	ui_w_set_value (dl.when, wt == TW_EVERY ? W_EVERY : wt == TW_WEEKLY ? W_WEEKLY : wt == TW_MONTHLY ? W_MONTHLY
		: wt == TW_ONCE ? W_ONCE : wt == TW_STARTUP ? W_STARTUP : wt == TW_SHUTDOWN ? W_SHUTDOWN
		: tk_is_event (wt) ? W_EVENT : W_DAILY);
	y += 38;
	char b[64];
	/* toutes les [n] [unité] */
	dl.n = ui_textbox (x, y, 70, CH, UTF_RIGHT);
	snprintf (b, sizeof b, "%ld", w0 && wt == TW_EVERY ? w0->n : 1L);
	ui_w_set_text (dl.n, (long) b);
	char items_u[100];
	snprintf (items_u, sizeof items_u, "%s|%s|%s", TR ("minutes"), TR ("heures"), TR ("jours"));
	dl.unit = ui_dropdown (x + 78, y, 150, CH, (long) items_u);
	ui_w_set_value (dl.unit, w0 && wt == TW_EVERY ? (w0->unit == 60 ? 0 : w0->unit == 3600 ? 1 : 2) : 1);
	/* les jours (lundi d'abord), le jour du mois, la date */
	static const char *const dn[7] = { "Lu", "Ma", "Me", "Je", "Ve", "Sa", "Di" };
	for (int i = 0; i < 7; i++) {
		int on = w0 && wt == TW_WEEKLY ? (w0->days >> day_order[i]) & 1 : i < 5;
		dl.days[i] = ui_checkbox (x + i * 50, y + 4, 48, 22, (long) TR (dn[i]), on);
	}
	dl.le = ui_label (x, y + 6, 24, 20, (long) TR ("le"), 0);
	dl.mday = ui_textbox (x + 26, y, 54, CH, UTF_RIGHT);
	snprintf (b, sizeof b, "%d", w0 && wt == TW_MONTHLY ? w0->mday : 1);
	ui_w_set_text (dl.mday, (long) b);
	dl.dumois = ui_label (x + 88, y + 6, 120, 20, (long) TR ("du mois"), ULB_DIM);
	dl.date = ui_textbox (x, y, 130, CH, 0);
	ui_textbox_hint (dl.date, (long) TR ("AAAA-MM-JJ"));
	if (w0 && wt == TW_ONCE) {
		snprintf (b, sizeof b, "%04d-%02d-%02d", w0->year, w0->month, w0->day);
		ui_w_set_text (dl.date, (long) b);
	} else {
		struct tk_tm n;
		tk_civil (clock_get (0) + clock_get (1) + 86400, &n);
		snprintf (b, sizeof b, "%04d-%02d-%02d", n.year, n.month, n.day);
		ui_w_set_text (dl.date, (long) b);
	}
	/* à HH:MM (à droite des jours) */
	dl.at = ui_label (x + 356, y + 6, 22, 20, (long) TR ("à"), 0);
	dl.time = ui_textbox (x + 380, y, 80, CH, 0);
	snprintf (b, sizeof b, "%02d:%02d", w0 && tk_is_calendar (wt) ? w0->hour : 3, w0 && tk_is_calendar (wt) ? w0->min : 0);
	ui_w_set_text (dl.time, (long) b);
	/* l'événement */
	static char items_e[1200];
	items_e[0] = 0;
	for (int i = 0; i < NPRESET; i++) {
		size_t n = strlen (items_e);
		snprintf (items_e + n, sizeof items_e - n, "%s%s", i ? "|" : "", TR (presets[i].label));
	}
	dl.event = ui_dropdown (x, y, RW, CH, (long) items_e);
	ui_w_set_value (dl.event, w0 && tk_is_event (wt) ? preset_of (w0) : 0);
	y += 38;
	dl.topic = ui_textbox (x, y, 240, CH, UTF_MONO);
	ui_textbox_hint (dl.topic, (long) TR ("korin.usb.device.*"));
	dl.value = ui_textbox (x + 248, y, RW - 248, CH, UTF_MONO);
	ui_textbox_hint (dl.value, (long) TR ("valeur (* et ? permis ; vide : toute valeur)"));
	if (w0 && tk_is_event (wt) && preset_of (w0) == P_CUSTOM) {
		ui_w_set_text (dl.topic, (long) w0->topic);
		ui_w_set_text (dl.value, (long) w0->value);
	}
	dl.pct = ui_textbox (x, y, 60, CH, UTF_RIGHT);
	snprintf (b, sizeof b, "%ld", w0 && wt == TW_BATTERY ? w0->n : 20L);
	ui_w_set_text (dl.pct, (long) b);
	dl.pctl = ui_label (x + 66, y + 6, 200, 20, (long) TR ("% de charge"), ULB_DIM);
	y += 44;

	/* les options */
	ui_label (M, y, DW - 2 * M, 20, (long) TR ("Options"), ULB_BOLD);
	y += 26;
	long y0 = y, cw = 300;
	dl.catchup = ui_checkbox (M, y, cw, 24, (long) TR ("Rattraper un passage manqué (Korin éteint)"), t->catchup);
	y += 30;
	dl.ac = ui_checkbox (M, y, cw, 24, (long) TR ("Seulement sur secteur"), t->ac_only);
	y += 30;
	dl.net = ui_checkbox (M, y, cw, 24, (long) TR ("Seulement avec le réseau"), t->net_only);
	y += 30;
	dl.enabled = ui_checkbox (M, y, cw, 24, (long) TR ("Activée"), t->enabled);
	long rx = M + cw + 10, rl = 130;
	y = y0;
	ui_label (rx, y + 6, rl, 20, (long) TR ("Attendre (s)"), 0);
	dl.delay = ui_textbox (rx + rl, y, 70, CH, UTF_RIGHT);
	snprintf (b, sizeof b, "%ld", t->delay);
	ui_w_set_text (dl.delay, (long) b);
	y += 36;
	ui_label (rx, y + 6, rl, 20, (long) TR ("Arrêter après (min)"), 0);
	dl.timeout = ui_textbox (rx + rl, y, 70, CH, UTF_RIGHT);
	snprintf (b, sizeof b, "%ld", t->timeout / 60);
	ui_w_set_text (dl.timeout, (long) b);
	ui_textbox_hint (dl.timeout, (long) "0");
	y += 36;
	ui_label (rx, y + 6, rl, 20, (long) TR ("Me prévenir"), 0);
	char items_n[160];
	snprintf (items_n, sizeof items_n, "%s|%s|%s", TR ("Jamais"), TR ("En cas d'échec"), TR ("À chaque fin"));
	dl.notify = ui_dropdown (rx + rl, y, DW - M - rx - rl, CH, (long) items_n);
	ui_w_set_value (dl.notify, t->notify == TN_NEVER ? 0 : t->notify == TN_ALWAYS ? 2 : 1);
	y = y0 + 4 * 30 + 8;
	dl.summary = ui_label (M, y, DW - 2 * M, 44, (long) "", ULB_DIM | ULB_WRAP);

	long bt = ui_dialog_button (d, 0, (long) TR ("Enregistrer"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, bt);
	ui_dialog_cancel (d, 0);
	dl_update ();
	ui_w_focus (dl.name);
	long r = ui_dialog_run (d, (long) dl_fn, 0);
	const char *bad = 0;
	if (r == 1) {
		bad = dl_collect (t);	/* (les valeurs gardées même fausses : le dialogue rouvert les remontre) */
		if (!bad && !t->name[0])
			bad = "Donnez un nom à la tâche.";
		if (!bad && !t->run[0])
			bad = t->kind == TK_APP ? "Choisissez une application." : t->kind == TK_SHELL ? "Tapez la commande à lancer."
				: "Choisissez le programme ou le script à lancer.";
		if (!bad && t->kind == TK_PROGRAM) {
			char p[TK_RUN];
			snprintf (p, sizeof p, "%s", t->run);
			char *sp = strchr (p, ' ');
			if (sp)
				*sp = 0;
			if (access (p, F_OK) != 0 && strncmp (p, "~/", 2))
				bad = "Ce programme n'existe pas.";
		}
	}
	ui_dialog_end (d);
	ui_w_draw (0);
	if (bad) {
		ui_message ((long) TR ("Tâches planifiées"), (long) TR (bad), UM_OK);
		return 2;
	}
	return r == 1;
}

/* ---- les actions ---- */

static void edit (int i)
{
	struct task t;
	if (i >= 0)
		t = items[i].t;
	else {
		if (nitems >= TMAX) {
			status (TR ("Trop de tâches."));
			return;
		}
		memset (&t, 0, sizeof t);
		tk_defaults (&t);
		t.kind = TK_PROGRAM;
		t.nwhen = 1;
		tk_when_parse (&t.when[0], "daily 03:00");
	}
	int r;
	while ((r = task_dialog (i < 0 ? "Nouvelle tâche" : "Modifier la tâche", &t)) == 2)
		;
	if (!r)
		return;
	if (i < 0) {	/* un id tiré du nom, sans doublon */
		char base[TK_ID];
		tk_id_from_name (t.name, base);
		base[TK_ID - 4] = 0;
		snprintf (t.id, sizeof t.id, "%s", base);
		for (int n = 2; n < 100; n++) {
			char p[200];
			path_of (p, sizeof p, "etc", t.id, ".task");
			if (access (p, F_OK) != 0)
				break;
			snprintf (t.id, sizeof t.id, "%s-%d", base, n);
		}
	}
	if (task_save (&t) == 0) {
		status (TR (i < 0 ? "Tâche ajoutée." : "Tâche enregistrée."));
		list_fill (t.id);
	}
}

static void on_new (long, long) { edit (-1); }

static void on_edit (long, long)
{
	int i = sel ();
	if (i >= 0)
		edit (i);
}

static void on_list (long, long code)
{
	if (code == UN_ACTIVATE)
		on_edit (0, 0);
	else
		buttons_state ();
}

static void on_run (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char v[80];
	snprintf (v, sizeof v, "%s %s", level ? "system" : "user", items[i].t.id);
	if (bus_publish ((long) "korin.tasks.run", (long) v, -1, 0) < 0) {
		status (TR ("Le bus ne répond pas : la tâche n'a pas été lancée."));
		return;
	}
	status (TR (proc_find ("taskd", 0) ? "Tâche lancée : son état s'affiche dans la liste."
		: "Le service des tâches (taskd) ne tourne pas."));
}

static void on_toggle (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	struct task t = items[i].t;
	t.enabled = !t.enabled;
	if (task_save (&t) == 0) {
		status (TR (t.enabled ? "Tâche activée." : "Tâche désactivée : elle ne partira plus seule."));
		list_fill (t.id);
	}
}

static void on_remove (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char q[300];
	snprintf (q, sizeof q, "%s « %s » ?", TR ("Supprimer la tâche"), items[i].t.name);
	if (!ui_message ((long) TR ("Tâches planifiées"), (long) q, UM_YESNO))
		return;
	char id[TK_ID];
	snprintf (id, sizeof id, "%s", items[i].t.id);
	int r = elevated (remove_fn, id);
	if (r == -2)
		return;
	if (r < 0) {
		status (TR ("La tâche n'a pas pu être supprimée."));
		return;
	}
	notify_taskd ("korin.tasks.changed", id);
	status (TR ("Tâche supprimée."));
	list_fill (0);
}

/* la sortie du dernier passage (<id>.log, écrit par taskd) */
static void on_log (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char p[200];
	path_of (p, sizeof p, "var", items[i].t.id, ".log");
	static char text[65536];
	if (read_text (p, text, sizeof text) < 0)
		snprintf (text, sizeof text, "%s", TR ("Pas encore de sortie : la tâche n'est pas encore passée."));
	else if (!text[0])
		snprintf (text, sizeof text, "%s", TR ("(rien d'écrit)"));
	enum { DW = 640, DH = 420 };
	char title[160];
	snprintf (title, sizeof title, "%s : %s", TR ("Sortie"), items[i].t.name);
	long d = ui_dialog_begin (DW, DH, (long) title);
	if (!d)
		return;
	long ta = ui_textarea (12, 12, DW - 24, DH - 70, UTF_READONLY | UTF_MONO | UTA_VSCROLL);
	ui_w_set_text (ta, (long) text);
	ui_dialog_button (d, 0, (long) TR ("Fermer"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_cancel (d, 0);
	ui_dialog_run (d, 0, 0);
	ui_dialog_end (d);
	ui_w_draw (0);
}

static void on_level (long w, long)
{
	level = ui_w_get_value (w) == 1;
	list_fill (0);
	status (TR (level ? "Lancées en administrateur, que quelqu'un soit connecté ou non ; les changer demande votre mot de passe."
		: "Lancées sous votre compte, pendant que votre session est ouverte."));
}

/* ---- l'applet ---- */

static void tb_open (void)
{
	ap.draw = tb_draw;
	ap.close = tb_close;
	ap.widgets = 1;
	ap.tick = tb_tick;
	ntop = 0;
	long x = SIDE_W + 10, y = AP_HEAD + 4;
	long pw = ui.w - SIDE_W - 20, ph = ui.h - AP_HEAD - 10;

	char seg[120];
	snprintf (seg, sizeof seg, "%s|%s", TR ("Mes tâches"), TR ("Tâches du système"));
	w_level = top (ui_segmented (x, y, 0, TB_BTN, (long) seg));
	ui_w_set_value (w_level, level);
	ui_w_on (w_level, (long) on_level, 0);
	y += 40;

	long lw = pw - BTN_COLW - 10, lh = ph - 40 - 30;
	w_list = top (ui_list (x, y, lw, lh, UL_HEADER | UL_NOSORT | UL_STRIPES));
	long c1 = lw * 26 / 100, c2 = lw * 34 / 100, c3 = lw * 22 / 100;
	ui_list_column (w_list, (long) TR ("Tâche"), c1, 0);
	ui_list_column (w_list, (long) TR ("Quand"), c2, 0);
	ui_list_column (w_list, (long) TR ("Dernier passage"), c3, 0);
	ui_list_column (w_list, (long) TR ("Prochain"), lw - c1 - c2 - c3 - 4, 0);
	ui_w_on (w_list, (long) on_list, 0);

	long bx = x + lw + 10, by = y;
	struct { long *w; const char *t; void (*fn) (long, long); } bt[] = {
		{ 0, "Nouvelle tâche…", on_new }, { &w_edit, "Modifier…", on_edit }, { &w_run, "Exécuter maintenant", on_run },
		{ &w_toggle, "Désactiver", on_toggle }, { &w_log, "Sortie…", on_log }, { &w_del, "Supprimer", on_remove },
	};
	for (unsigned i = 0; i < sizeof bt / sizeof bt[0]; i++) {
		long b = top (ui_pushbutton (bx, by, BTN_COLW, TB_BTN, (long) TR (bt[i].t), i == 0 ? UB_DEFAULT : 0));
		ui_w_on (b, (long) bt[i].fn, 0);
		if (bt[i].w)
			*bt[i].w = b;
		by += TB_BTN + (i == 0 || i == 3 ? 16 : 6);
	}

	w_status = top (ui_label (x, y + lh + 8, pw, 20, (long) "", ULB_DIM));
	list_fill (0);
	if (!proc_find ("taskd", 0) && !task_off_warned) {
		task_off_warned = 1;
		status (TR ("Le service des tâches (taskd) ne tourne pas : il démarrera avec Korin."));
	} else
		status (TR (nitems ? "Double-cliquez sur une tâche pour la modifier."
			: "Aucune tâche : « Nouvelle tâche… » lance un programme, un script ou une app au moment voulu."));
}

static void tb_apply (void) {}

extern "C" {
extern const int korin_applet_abi;
extern const Applet korin_applet;
const int korin_applet_abi = APPLET_ABI;
const Applet korin_applet = { "tasks", "Tâches planifiées", "Tâches planifiées", "clock", tb_open, tb_apply, TASKD };
}
