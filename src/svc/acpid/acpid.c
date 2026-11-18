/* acpid - le service ACPI du PC : batterie, secteur, profil d'énergie (docs/etude/BATTERIE-PC.md).
 *
 * Lancé par init2 (skel/pc/etc/services.txt), avec les droits de /sys. ACPICA (acpi.c, osl.c,
 * tools/acpica/build.sh) lit les méthodes de l'AML (_BST, _BIX, _PSR) ; toutes les `poll` secondes
 * (10 par défaut) :
 *   * pourcentage (capacité restante / capacité à la dernière charge complète) et temps restant : la
 *     puissance de _BST lissée (moyenne glissante ~1 min), ou, si le micrologiciel ne la donne pas, la
 *     pente de la capacité ; en charge, le temps jusqu'à la charge complète ;
 *   * sujet du bus « korin.power.battery » (gardé) : « <pct> <min> <secteur> <état> <profil> <auto> », pct et min
 *     à -1 quand inconnus, secteur 0/1, état n (pas de batterie) d (décharge) c (charge) f (pleine)
 *     i (branché, ne charge pas), profil 0 performance 1 équilibré 2 économie -1 aucun, puis 1 si le
 *     profil est automatique (barre du haut : src/sys/menubar) ;
 *   * alertes (notifyd), une fois par décharge : `warn` % « batterie faible », `critical` % « très
 *     faible » ; à `shutdown` % (0 : jamais), extinction propre par init2 après `delay` secondes si le
 *     secteur n'est toujours pas branché ;
 *   * profil d'énergie des cœurs (SYS_POWER POWER_PROFILE), en automatique : `ac` sur secteur, `battery`
 *     sur batterie, `low` sous `saver` % ; ou un profil fixe (`profile = performance|balanced|saver`).
 * Réglages : /etc/power.ini (skel/pc/etc/power.ini), relu sur ACPID_OP_RELOAD.
 *
 * Requêtes (service « acpid ») :
 *   ACPID_OP_GET 1 -> données : lignes « clé=valeur » (pct, min, ac, state, rate_mw, remain_mwh,
 *                     full_mwh, design_mwh, cycles, voltage_mv, model, oem, tech, profile, auto, warn,
 *                     critical, shutdown, batteries, adapters ; ce qui est découvert : profiles,
 *                     lids, power_button, sleep_button, ec, events)
 *   ACPID_OP_PROFILE 2 (a : -1 automatique, 0..2 profil fixe) -> 0 ; écrit dans /etc/power.ini
 *   ACPID_OP_RELOAD 3 -> 0 : relit /etc/power.ini
 * Événements (SCI lié au canal du service, events = 1) : Notify de la batterie ou du secteur -> relecture
 * aussitôt ; couvercle -> « korin.power.lid » ; bouton d'alimentation -> « korin.power.button » (le panel
 * ouvre le dialogue d'extinction ; power_button = dialog|shutdown|nothing) ; requêtes de l'EC dans acpi.c. */
#include "sysfront.h"

#include "acpid.h"

#define ACPID_OP_GET 1
#define ACPID_OP_PROFILE 2
#define ACPID_OP_RELOAD 3
#define SVC_OP_SIGNAL 0xFFFD
#define INIT_OP_REBOOT 3 /* (init2) flags bit 1 : éteindre */
#define POWER_PROFILE_OP 3
#define CONF "/etc/power.ini"

#define M64(m, o) (*(u64 *)((u8 *)(m) + (o)))

void acpid_log(const char *line)
{
	slog("%s\n", line);
}

/* ---- réglages */
static struct conf {
	int warn, critical, shutdown, delay, poll, saver;
	int prof_ac, prof_bat, prof_low; /* -2 : celui du démarrage */
	int fixed;                       /* -1 automatique, sinon le profil */
	int init_all;
	int events;      /* 1 : mode ACPI, SCI, GPE (0 : sondage seul, le mode n'est pas changé) */
	int power_btn;   /* BTN_* du bouton d'alimentation */
	int sleep_btn;   /* BTN_* du bouton de mise en veille */
} cf;

enum { BTN_NOTHING, BTN_DIALOG, BTN_SHUTDOWN };

static int parse_btn(const char *v, int dflt)
{
	return !strcmp(v, "nothing") ? BTN_NOTHING : !strcmp(v, "dialog") ? BTN_DIALOG
	     : !strcmp(v, "shutdown") ? BTN_SHUTDOWN : dflt;
}

static const char *const prof_name[3] = {"performance", "balanced", "saver"};
static const char *const prof_fr[3] = {"performance", "équilibré", "économie"};

static int parse_prof(const char *v, int dflt)
{
	for (int i = 0; i < 3; i++)
		if (!strcmp(v, prof_name[i]))
			return i;
	if (!strcmp(v, "boot"))
		return -2;
	if (!strcmp(v, "auto"))
		return -1;
	return dflt;
}

static char conf_buf[4096];

static void conf_load(void)
{
	cf.warn = 20;
	cf.critical = 10;
	cf.shutdown = 5;
	cf.delay = 60;
	cf.poll = 10;
	cf.saver = 20;
	cf.prof_ac = -2;
	cf.prof_bat = 1;
	cf.prof_low = 2;
	cf.fixed = -1;
	cf.init_all = 0;
	cf.events = 1;
	cf.power_btn = BTN_DIALOG;
	cf.sleep_btn = BTN_NOTHING;
	long fd = sys_open(CONF, O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, conf_buf, sizeof conf_buf - 1);
	sys_close(fd);
	if (n <= 0)
		return;
	conf_buf[n] = 0;
	for (char *l = conf_buf; *l;) {
		char *e = l;
		while (*e && *e != '\n')
			e++;
		char *next = *e ? e + 1 : e;
		*e = 0;
		char *h = l;
		while (*h && *h != '#')
			h++;
		*h = 0;
		char *eq = l;
		while (*eq && *eq != '=')
			eq++;
		if (*eq) {
			*eq = 0;
			char *k = l, *v = eq + 1;
			while (*k == ' ' || *k == '\t')
				k++;
			for (char *t = eq - 1; t >= k && (*t == ' ' || *t == '\t' || *t == '\r'); t--)
				*t = 0;
			while (*v == ' ' || *v == '\t')
				v++;
			for (char *t = v + strlen(v) - 1; t >= v && (*t == ' ' || *t == '\t' || *t == '\r'); t--)
				*t = 0;
			int num = atoi(v);
			if (!strcmp(k, "warn"))
				cf.warn = num;
			else if (!strcmp(k, "critical"))
				cf.critical = num;
			else if (!strcmp(k, "shutdown"))
				cf.shutdown = num;
			else if (!strcmp(k, "delay"))
				cf.delay = num < 10 ? 10 : num;
			else if (!strcmp(k, "poll"))
				cf.poll = num < 2 ? 2 : num > 120 ? 120 : num;
			else if (!strcmp(k, "saver"))
				cf.saver = num;
			else if (!strcmp(k, "ac"))
				cf.prof_ac = parse_prof(v, cf.prof_ac);
			else if (!strcmp(k, "battery"))
				cf.prof_bat = parse_prof(v, cf.prof_bat);
			else if (!strcmp(k, "low"))
				cf.prof_low = parse_prof(v, cf.prof_low);
			else if (!strcmp(k, "profile"))
				cf.fixed = parse_prof(v, -1) < 0 ? -1 : parse_prof(v, -1);
			else if (!strcmp(k, "init"))
				cf.init_all = !strcmp(v, "all");
			else if (!strcmp(k, "events"))
				cf.events = num != 0;
			else if (!strcmp(k, "power_button"))
				cf.power_btn = parse_btn(v, cf.power_btn);
			else if (!strcmp(k, "sleep_button"))
				cf.sleep_btn = parse_btn(v, cf.sleep_btn);
		}
		l = next;
	}
}

/* « profile = … » remplacé (ou ajouté) dans /etc/power.ini, le reste gardé */
static void conf_save_profile(int fixed)
{
	static char in[4096], out[4400];
	long n = 0, fd = sys_open(CONF, O_READ);
	if (fd >= 0) {
		n = sys_read(fd, in, sizeof in - 1);
		sys_close(fd);
	}
	if (n < 0)
		n = 0;
	in[n] = 0;
	const char *val = fixed < 0 ? "auto" : prof_name[fixed];
	char *o = out;
	int done = 0;
	for (char *l = in; *l;) {
		char *e = l;
		while (*e && *e != '\n')
			e++;
		char *k = l;
		while (*k == ' ' || *k == '\t')
			k++;
		if (!strncmp(k, "profile", 7) && (k[7] == ' ' || k[7] == '=' || k[7] == '\t')) {
			o += snprintf(o, out + sizeof out - o, "profile = %s\n", val);
			done = 1;
		} else {
			memcpy(o, l, (u64)(e - l));
			o += e - l;
			*o++ = '\n';
		}
		l = *e ? e + 1 : e;
	}
	if (!done)
		o += snprintf(o, out + sizeof out - o, "profile = %s\n", val);
	fd = sys_open(CONF, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0) {
		slog("%s : écriture impossible (%ld)\n", CONF, fd);
		return;
	}
	sys_write(fd, out, (u64)(o - out));
	sys_close(fd);
}

/* ---- état */
static struct st {
	struct power_raw raw;
	int pct, min, ac;
	char state;
	long ema_mw;          /* puissance lissée */
	char last_state;
	long slope_t0, slope_c0; /* (ms, mWh) début de la pente de capacité */
	int profile, boot_profile;
	int warned, crit, shut_said;
	long shut_at;         /* ms de l'extinction prévue, 0 : aucune */
	int nbat, nac;
	char bus[64];
} s = {.profile = -1, .boot_profile = -1};

static long now_ms(void)
{
	return sys_time_ms();
}

static void fmt_time(char *b, int n, int min)
{
	if (min < 0)
		snprintf(b, n, "durée inconnue");
	else if (min < 60)
		snprintf(b, n, "%d min", min);
	else
		snprintf(b, n, "%d h %02d", min / 60, min % 60);
}

static void set_profile(int p, const char *why)
{
	if (p < 0 || p > 2 || p == s.profile)
		return;
	long r = korin_sys2(SYS_POWER, POWER_PROFILE_OP, p);
	if (r < 0) {
		static int said;
		if (!said++)
			slog("profil d'énergie impossible (%ld) : pstate=off ou processeur sans HWP ni EIST\n", r);
		return;
	}
	s.profile = (int)r;
	slog("profil d'énergie : %s (%s)\n", prof_fr[p], why);
}

static void power_off_now(void)
{
	slog("batterie à %d %% sans secteur : extinction\n", s.pct);
	long ch = korin_sys2(SYS_SVC_OPEN, "init2", 1000);
	if (ch < 0) {
		slog("init2 absent (%ld) : extinction impossible\n", ch);
		return;
	}
	u64 m[MSG_SIZE / 8] __attribute__((aligned(16)));
	memset(m, 0, sizeof m);
	m[0] = INIT_OP_REBOOT;
	m[1] = 2; /* éteindre */
	korin_sys2(SYS_SVC_CALL, ch, m);
}

static void policy(void)
{
	int on_battery = s.raw.batteries && s.ac == 0;
	char t[48], txt[200];
	if (!on_battery) {
		if (s.shut_at)
			notify_send("Secteur branché", "L'extinction est annulée.", 0);
		s.warned = s.crit = s.shut_said = 0;
		s.shut_at = 0;
	} else if (s.pct >= 0) {
		fmt_time(t, sizeof t, s.min);
		if (s.pct > cf.warn + 3) /* (une batterie changée, rechargée sans secteur…) */
			s.warned = s.crit = 0;
		/* (une alerte n'est faite que si notifyd l'a prise : avant la session, elle attend la lecture suivante) */
		if (s.pct <= cf.critical && !s.crit) {
			snprintf(txt, sizeof txt, "Il reste %d %% (%s). Branchez le secteur maintenant.", s.pct, t);
			if (notify_send("Batterie très faible", txt, 0) >= 0)
				s.crit = s.warned = 1;
		} else if (s.pct <= cf.warn && !s.warned) {
			snprintf(txt, sizeof txt, "Il reste %d %% (%s). Pensez à brancher le secteur.", s.pct, t);
			if (notify_send("Batterie faible", txt, 0) >= 0)
				s.warned = 1;
		}
		if (cf.shutdown > 0 && s.pct <= cf.shutdown && !s.shut_at) {
			s.shut_at = now_ms() + cf.delay * 1000L;
			slog("batterie à %d %% : extinction dans %d s sans secteur\n", s.pct, cf.delay);
		}
		if (s.shut_at && !s.shut_said) {
			snprintf(txt, sizeof txt,
				 "Batterie à %d %% : Korin s'éteindra proprement dans %d s si le secteur n'est pas branché.",
				 s.pct, (int)((s.shut_at - now_ms() + 999) / 1000));
			if (notify_send("Extinction imminente", txt, 0) >= 0)
				s.shut_said = 1;
		}
		if (s.shut_at && now_ms() >= s.shut_at) {
			s.shut_at = now_ms() + 30000; /* (si init2 ne fait rien, on réessaie) */
			power_off_now();
		}
	}
	/* profil */
	int want;
	const char *why;
	if (cf.fixed >= 0) {
		want = cf.fixed;
		why = "choisi";
	} else if (!on_battery) {
		want = cf.prof_ac == -2 ? s.boot_profile : cf.prof_ac;
		why = "secteur";
	} else if (s.pct >= 0 && s.pct <= cf.saver) {
		want = cf.prof_low == -2 ? s.boot_profile : cf.prof_low;
		why = "batterie faible";
	} else {
		want = cf.prof_bat == -2 ? s.boot_profile : cf.prof_bat;
		why = "batterie";
	}
	int before = s.profile;
	set_profile(want, why);
	if (on_battery && s.profile == 2 && before != 2 && before >= 0 && cf.fixed < 0)
		notify_send("Économie d'énergie", "Le processeur passe en économie pour faire durer la batterie.", 0);
}

/* un sujet du bus (retain : gardé) -> 0, < 0 si busd n'est pas (encore) là */
static long bus_publish(const char *topic, const char *v, int retain)
{
	static long bus_ch;
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	for (int tries = 0; tries < 2; tries++) {
		if (!bus_ch) {
			long c = korin_sys2(SYS_SVC_OPEN, "bus", 0);
			if (c < 0)
				return c;
			bus_ch = c + 1;
		}
		memset(m, 0, MSG_DATA);
		M64(m, 0) = 1; /* BUS_OP_PUBLISH */
		M64(m, MSG_A) = retain ? 1 : 0; /* BUS_RETAIN */
		u64 tl = strlen(topic) + 1, len = strlen(v);
		memcpy(m + MSG_DATA, topic, tl);
		memcpy(m + MSG_DATA + tl, v, len + 1);
		M64(m, MSG_B) = len;
		long r = korin_sys2(SYS_SVC_CALL, bus_ch - 1, m);
		if (r >= 0)
			return 0;
		bus_ch = 0; /* busd relancé : rouvrir */
	}
	return -1;
}

static void publish(void)
{
	char v[64];
	snprintf(v, sizeof v, "%d %d %d %c %d %d", s.pct, s.min, s.ac, s.state, s.profile, cf.fixed < 0);
	if (strcmp(v, s.bus) && bus_publish("korin.power.battery", v, 1) == 0)
		memcpy(s.bus, v, sizeof s.bus);
}

/* ---- événements : boutons, couvercle, batterie */
static int lid_state = -2; /* -2 : pas encore lu */
static unsigned button_seq;

static void button(const char *which, int action)
{
	char v[32];
	snprintf(v, sizeof v, "%s %u", which, ++button_seq);
	slog("bouton : %s\n", which);
	if (action == BTN_NOTHING)
		return;
	/* (la barre du bas ouvre le dialogue d'extinction, src/sys/panel ; sans bureau ou en « shutdown » :
	 * extinction propre) */
	if (action == BTN_DIALOG && bus_publish("korin.power.button", v, 0) == 0 &&
	    korin_sys2(SYS_SVC_OPEN, "notify", 0) >= 0)
		return;
	power_off_now();
}

static void lid_check(void)
{
	int l = acpi_lid();
	if (l == lid_state)
		return;
	if (lid_state != -2 || l >= 0)
		slog("couvercle : %s\n", l < 0 ? "absent" : l ? "ouvert" : "fermé");
	lid_state = l;
	if (l >= 0)
		bus_publish("korin.power.lid", l ? "open" : "closed", 1);
}

static void sample(void)
{
	acpi_read(&s.raw);
	acpi_run_deferred();
	struct power_raw *r = &s.raw;
	s.pct = -1;
	if (r->batteries && r->full_mwh > 0 && r->remain_mwh >= 0) {
		long p = (r->remain_mwh * 100 + r->full_mwh / 2) / r->full_mwh;
		s.pct = p > 100 ? 100 : (int)p;
	}
	s.ac = r->ac;
	if (s.ac < 0) /* pas d'adaptateur décrit : déduit de l'état de la batterie */
		s.ac = r->batteries && (r->state & 1) ? 0 : 1;
	if (!r->batteries)
		s.state = 'n';
	else if (r->state & 2)
		s.state = 'c';
	else if ((r->state & 1) || s.ac == 0)
		s.state = 'd';
	else
		s.state = s.pct >= 95 ? 'f' : 'i';
	if (s.state != s.last_state) { /* nouvelle phase : moyenne et pente oubliées */
		s.ema_mw = 0;
		s.slope_t0 = now_ms();
		s.slope_c0 = r->remain_mwh;
		s.last_state = s.state;
	}
	long rate = r->rate_mw;
	if (rate <= 0 && s.state == 'd' && r->remain_mwh >= 0 && s.slope_c0 > r->remain_mwh) {
		long dt = now_ms() - s.slope_t0; /* sans puissance donnée : la pente (après 2 min) */
		if (dt > 120000)
			rate = (s.slope_c0 - r->remain_mwh) * 3600000 / dt;
	}
	if (rate > 0)
		s.ema_mw = s.ema_mw ? (s.ema_mw * 5 + rate + 3) / 6 : rate;
	s.min = -1;
	if (s.ema_mw > 0 && r->remain_mwh >= 0) {
		long m = -1;
		if (s.state == 'd')
			m = r->remain_mwh * 60 / s.ema_mw;
		else if (s.state == 'c' && r->full_mwh > r->remain_mwh)
			m = (r->full_mwh - r->remain_mwh) * 60 / s.ema_mw;
		s.min = m > 5999 ? 5999 : (int)m;
	}
	policy();
	publish();
}

/* ---- requêtes */
static int put(char *b, int at, int max, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static int put(char *b, int at, int max, const char *fmt, ...)
{
	if (at >= max)
		return at;
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b + at, max - at, fmt, ap);
	va_end(ap);
	at += n;
	return at < max ? at : max - 1;
}

static int acpi_ok; /* acpi_start a réussi */

static void answer_get(u8 *m)
{
	char *b = (char *)m + MSG_DATA;
	int max = MSG_SIZE - MSG_DATA, n = 0;
	struct power_raw *r = &s.raw;
	n = put(b, n, max, "pct=%d\nmin=%d\nac=%d\nstate=%c\n", s.pct, s.min, s.ac, s.state);
	n = put(b, n, max, "rate_mw=%ld\nremain_mwh=%ld\nfull_mwh=%ld\ndesign_mwh=%ld\n", s.ema_mw ? s.ema_mw : r->rate_mw,
		r->remain_mwh, r->full_mwh, r->design_mwh);
	n = put(b, n, max, "cycles=%ld\nvoltage_mv=%ld\nmodel=%s\noem=%s\ntech=%s\n", r->cycles, r->voltage_mv, r->model,
		r->oem, r->tech);
	n = put(b, n, max, "profile=%d\nauto=%d\nwarn=%d\ncritical=%d\nshutdown=%d\nbatteries=%d\nadapters=%d\n",
		s.profile, cf.fixed < 0, cf.warn, cf.critical, cf.shutdown, s.nbat, s.nac);
	struct acpi_features f;
	memset(&f, 0, sizeof f);
	if (acpi_ok)
		acpi_features(&f);
	n = put(b, n, max, "profiles=%d\nlids=%d\npower_button=%d\nsleep_button=%d\nec=%d\nevents=%d\n",
		s.boot_profile >= 0, f.lids, f.power_button, f.sleep_button, f.ec, f.events);
	b[n] = 0;
	*(u32 *)(m + MSG_LEN) = (u32)n + 1;
	M64(m, 0) = 0;
}

/* les événements arrivés (SCI, ou Notify pendant une lecture) */
static void events(int polling)
{
	for (int guard = 0; guard < 4; guard++) {
		unsigned e = acpi_take_events();
		if (!e)
			return;
		if ((e & ACPI_EV_BATTERY) && polling)
			sample(); /* (secteur branché, batterie changée : tout de suite) */
		if (e & ACPI_EV_LID)
			lid_check();
		if (e & ACPI_EV_POWER)
			button("power", cf.power_btn);
		if (e & ACPI_EV_SLEEP)
			button("sleep", cf.sleep_btn);
	}
}

#define SCI_BIT 2         /* (SVC_OP_SIGNAL) le SCI */
#define IRQ_SHARED_ (1UL << 63)

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	slog_name("acpid");
	if (!sysfront_rights()) {
		out_puts("acpid : service du système (lancé par init2) ; voir la commande battery\n");
		return 1;
	}
	conf_load();
	long canal = korin_sys2(SYS_SVC_REGISTER, "acpid", 0);
	if (canal < 0) {
		slog("service déjà là (%ld)\n", canal);
		return 0;
	}
	acpi_ok = acpi_start(cf.init_all, cf.events) >= 0;
	if (!acpi_ok) {
		slog("pas d'ACPI utilisable : batterie inconnue\n");
		s.state = 'n';
		s.pct = s.min = -1;
		s.ac = 1;
		publish();
	}
	acpi_devices(&s.nbat, &s.nac);
	s.boot_profile = (int)korin_sys2(SYS_POWER, POWER_PROFILE_OP, -1);
	if (s.boot_profile >= 0)
		s.profile = s.boot_profile;
	else
		s.boot_profile = -1;
	/* le SCI : lié au canal du service (SVC_OP_SIGNAL, bit SCI_BIT), masqué jusqu'à IRQ_ACK */
	long sci = osl_sci_line();
	if (sci >= 0) {
		long r = korin_sys3(SYS_IRQ_BIND, sci, canal, SCI_BIT | IRQ_SHARED_);
		if (r < 0) {
			slog("SCI : ligne %ld refusée (%ld) : batterie seulement sondée\n", sci, r);
			sci = -1;
		} else {
			slog("SCI : ligne %ld\n", sci);
			korin_sys1(SYS_IRQ_ACK, sci);
		}
	}
	lid_check();
	int polling = s.nbat > 0 || s.nac > 0;
	if (polling) {
		sample();
		char t[48];
		fmt_time(t, sizeof t, s.min);
		slog("prêt : batterie %d %%, %s, secteur %s, état %c\n", s.pct, t, s.ac ? "branché" : "débranché",
		     s.state);
	} else {
		slog("prêt : ni batterie ni adaptateur décrits (ordinateur de bureau)\n");
		s.state = 'n';
		s.pct = s.min = -1;
		s.ac = 1;
		publish();
	}
	long next = now_ms() + cf.poll * 1000L;
	long sci_t0 = now_ms(), sci_n = 0, storms = 0;
	u8 m[MSG_SIZE] __attribute__((aligned(16)));
	for (;;) {
		long wait = polling ? next - now_ms() : 60000;
		if (wait < 1)
			wait = 1;
		long req = korin_sys3(SYS_SVC_NEXT, canal, m, wait);
		if (req > 0) {
			u64 op = M64(m, 0);
			if (op == SVC_OP_SIGNAL) {
				if ((M64(m, MSG_A) & SCI_BIT) && sci >= 0) {
					osl_sci_fire();
					korin_sys1(SYS_IRQ_ACK, sci);
					events(polling);
					/* garde : plus de 500 SCI par seconde, deux fois de suite -> GPE coupées */
					sci_n++;
					if (now_ms() - sci_t0 >= 1000) {
						storms = sci_n > 500 ? storms + 1 : 0;
						if (storms == 2)
							acpi_quiet_gpes();
						sci_t0 = now_ms();
						sci_n = 0;
					}
				}
				continue;
			}
			long rc = 0;
			*(u32 *)(m + MSG_LEN) = 0;
			if (op == ACPID_OP_GET) {
				answer_get(m);
			} else if (op == ACPID_OP_PROFILE) {
				long p = (long)M64(m, MSG_A);
				if (p < -1 || p > 2)
					rc = E_INVAL;
				else {
					cf.fixed = (int)p;
					conf_save_profile(cf.fixed);
					if (polling)
						sample();
					else if (p >= 0)
						set_profile((int)p, "choisi");
				}
				M64(m, 0) = (u64)rc;
			} else if (op == ACPID_OP_RELOAD) {
				conf_load();
				if (polling)
					sample();
				M64(m, 0) = 0;
			} else
				M64(m, 0) = (u64)E_INVAL;
			korin_sys2(SYS_SVC_REPLY, req, m);
		} else if (req < 0)
			sys_sleep(100);
		if (polling && now_ms() >= next) {
			sample();
			lid_check(); /* (sans SCI : sondé) */
			next = now_ms() + cf.poll * 1000L;
		}
		events(polling);
	}
}
