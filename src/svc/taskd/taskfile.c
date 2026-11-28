/* taskfile.c : une tâche planifiée, son fichier et ses déclencheurs (taskfile.h, docs/TACHES.md). Sans aucune
 * bibliothèque : compilé dans taskd et tasks (korinrt) et dans le volet de Réglages (C++, libc). */
#include "taskfile.h"

/* ---- chaînes */

long tk_len(const char *s)
{
	long n = 0;
	while (s[n])
		n++;
	return n;
}

void tk_copy(char *d, const char *s, long cap)
{
	long i = 0;
	if (cap <= 0)
		return;
	while (s[i] && i < cap - 1) {
		d[i] = s[i];
		i++;
	}
	d[i] = 0;
}

int tk_eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static void tk_zero(void *p, long n)
{
	char *c = (char *)p;
	while (n--)
		*c++ = 0;
}

static int is_blank(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	long n = tk_len(s);
	while (n && is_blank(s[n - 1]))
		s[--n] = 0;
	return s;
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* s commence-t-il par le mot w (suivi d'un blanc ou de la fin) ? -> après le mot et ses blancs, 0 sinon */
static const char *word(const char *s, const char *w)
{
	while (*w && lower(*s) == *w)
		s++, w++;
	if (*w || (*s && !is_blank(*s)))
		return 0;
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

/* un nombre décimal -> sa valeur, *ps après lui ; -1 : pas de chiffre */
static long number(const char **ps)
{
	const char *s = *ps;
	long v = 0;
	if (*s < '0' || *s > '9')
		return -1;
	while (*s >= '0' && *s <= '9' && v < 100000000)
		v = v * 10 + (*s++ - '0');
	*ps = s;
	return v;
}

/* « HH:MM » -> 1 */
static int hhmm(const char **ps, int *h, int *m)
{
	const char *s = *ps;
	long a = number(&s);
	if (a < 0 || a > 23 || *s != ':')
		return 0;
	s++;
	long b = number(&s);
	if (b < 0 || b > 59)
		return 0;
	*h = (int)a;
	*m = (int)b;
	while (*s == ' ' || *s == '\t')
		s++;
	*ps = s;
	return 1;
}

/* ---- écriture dans un tampon borné */

struct buf {
	char *p;
	long n, cap;
};

static void put(struct buf *b, const char *s)
{
	while (*s) {
		if (b->n < b->cap - 1)
			b->p[b->n++] = *s;
		s++;
	}
	if (b->cap)
		b->p[b->n < b->cap ? b->n : b->cap - 1] = 0;
}

static void put_num(struct buf *b, long v, int width)
{
	char t[24];
	int i = 0;
	int neg = v < 0;
	unsigned long u = neg ? (unsigned long)-v : (unsigned long)v;
	do {
		t[i++] = (char)('0' + u % 10);
		u /= 10;
	} while (u && i < 22);
	while (i < width && i < 22)
		t[i++] = '0';
	if (neg)
		t[i++] = '-';
	char o[24];
	int k = 0;
	while (i)
		o[k++] = t[--i];
	o[k] = 0;
	put(b, o);
}

/* ---- dates (jours civils : l'algorithme de Howard Hinnant) */

static long days_from_civil(long y, long m, long d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void tk_civil(long secs, struct tk_tm *t)
{
	long z = secs >= 0 ? secs / 86400 : (secs - 86399) / 86400;
	long rem = secs - z * 86400;
	t->hour = (int)(rem / 3600);
	t->min = (int)(rem / 60 % 60);
	t->sec = (int)(rem % 60);
	t->wday = (int)((z % 7 + 11) % 7); /* le 1/1/1970 est un jeudi (4) */
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	long doe = z - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long y = yoe + era * 400;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long mp = (5 * doy + 2) / 153;
	long d = doy - (153 * mp + 2) / 5 + 1;
	long m = mp + (mp < 10 ? 3 : -9);
	t->year = (int)(y + (m <= 2));
	t->month = (int)m;
	t->day = (int)d;
}

long tk_secs(int year, int month, int day, int hour, int min, int sec)
{
	return days_from_civil(year, month, day) * 86400 + hour * 3600L + min * 60L + sec;
}

int tk_mdays(int year, int month)
{
	static const char n[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month == 2 && ((year % 4 == 0 && year % 100) || year % 400 == 0))
		return 29;
	return month >= 1 && month <= 12 ? n[month - 1] : 30;
}

int tk_is_calendar(int type)
{
	return type == TW_DAILY || type == TW_WEEKLY || type == TW_MONTHLY || type == TW_ONCE;
}

int tk_is_event(int type)
{
	return type == TW_EVENT || type == TW_REMOVED || type == TW_POWER || type == TW_BATTERY;
}

long tk_next(const struct tk_when *w, long after)
{
	struct tk_tm t;
	tk_civil(after, &t);
	if (w->type == TW_ONCE) {
		long s = tk_secs(w->year, w->month, w->day, w->hour, w->min, 0);
		return s > after ? s : -1;
	}
	if (w->type == TW_DAILY || w->type == TW_WEEKLY) {
		long day0 = tk_secs(t.year, t.month, t.day, 0, 0, 0);
		for (int i = 0; i <= 7; i++) {
			long s = day0 + i * 86400L + w->hour * 3600L + w->min * 60L;
			int wd = (t.wday + i) % 7;
			if (s > after && (w->type == TW_DAILY || (w->days & (1 << wd))))
				return s;
		}
		return -1; /* (weekly sans aucun jour) */
	}
	if (w->type == TW_MONTHLY) {
		int y = t.year, m = t.month;
		for (int i = 0; i < 14; i++) {
			int d = w->mday < 1 ? 1 : w->mday;
			if (d > tk_mdays(y, m))
				d = tk_mdays(y, m);
			long s = tk_secs(y, m, d, w->hour, w->min, 0);
			if (s > after)
				return s;
			if (++m > 12)
				m = 1, y++;
		}
	}
	return -1;
}

/* ---- motifs */

int tk_topic_match(const char *pattern, const char *topic)
{
	for (; *pattern; pattern++, topic++) {
		if (*pattern == '*' && !pattern[1])
			return 1;
		if (*pattern != *topic)
			return 0;
	}
	return !*topic;
}

int tk_glob(const char *p, const char *s)
{
	const char *star = 0, *back = 0;
	if (!*p)
		return 1; /* (motif vide : toute valeur) */
	while (*s) {
		if (*p == '*') {
			star = ++p;
			back = s;
		} else if (*p && (*p == '?' || *p == *s)) {
			p++;
			s++;
		} else if (star) {
			p = star;
			s = ++back;
		} else
			return 0;
	}
	while (*p == '*')
		p++;
	return !*p;
}

const char *tk_when_topic(const struct tk_when *w)
{
	if (w->type == TW_POWER || w->type == TW_BATTERY)
		return "korin.power.battery";
	return w->topic;
}

/* le champ n (0 : le premier) d'une valeur séparée par des blancs, dans out (32) */
static void field(const char *v, int n, char *out)
{
	while (*v == ' ')
		v++;
	while (n-- > 0) {
		while (*v && *v != ' ')
			v++;
		while (*v == ' ')
			v++;
	}
	int i = 0;
	while (*v && *v != ' ' && i < 31)
		out[i++] = *v++;
	out[i] = 0;
}

int tk_when_holds(const struct tk_when *w, const char *value, long len)
{
	if (w->type == TW_REMOVED)
		return len < 0;
	if (len < 0)
		return 0;
	if (w->type == TW_EVENT)
		return tk_glob(w->value, value);
	/* korin.power.battery : « <pct> <min> <secteur> <état> ... » (docs/BUS.md) */
	char f[32];
	field(value, 3, f);
	if (f[0] == 'n' || !f[0])
		return 0; /* pas de batterie */
	field(value, 2, f);
	int ac = f[0] == '1';
	if (w->type == TW_POWER)
		return w->n ? ac : !ac;
	field(value, 0, f);
	const char *p = f;
	long pct = number(&p);
	return !ac && pct >= 0 && pct < w->n;
}

/* ---- les déclencheurs */

static const char *const day_names[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

int tk_when_parse(struct tk_when *w, const char *s)
{
	tk_zero(w, sizeof *w);
	while (*s == ' ' || *s == '\t')
		s++;
	const char *r;
	if ((r = word(s, "every"))) {
		long n = number(&r);
		while (*r == ' ')
			r++;
		long u = 0;
		if (*r == 'm' || *r == 'M')
			u = 60;
		else if (*r == 'h' || *r == 'H')
			u = 3600;
		else if (*r == 'd' || *r == 'D' || *r == 'j' || *r == 'J')
			u = 86400;
		if (n < 1 || !u)
			return 0;
		w->type = TW_EVERY;
		w->n = n;
		w->unit = u;
		return 1;
	}
	if ((r = word(s, "daily"))) {
		w->type = TW_DAILY;
		return hhmm(&r, &w->hour, &w->min);
	}
	if ((r = word(s, "weekly"))) {
		w->type = TW_WEEKLY;
		while (*r && *r != ' ') {
			int found = 0;
			for (int i = 0; i < 7; i++)
				if (lower(r[0]) == day_names[i][0] && lower(r[1]) == day_names[i][1] &&
				    lower(r[2]) == day_names[i][2]) {
					w->days |= 1 << i;
					found = 1;
				}
			if (!found)
				return 0;
			r += 3;
			while (*r && *r != ',' && *r != ' ')
				r++;
			if (*r == ',')
				r++;
		}
		while (*r == ' ')
			r++;
		return w->days && hhmm(&r, &w->hour, &w->min);
	}
	if ((r = word(s, "monthly"))) {
		long d = number(&r);
		while (*r == ' ')
			r++;
		w->type = TW_MONTHLY;
		w->mday = (int)d;
		return d >= 1 && d <= 31 && hhmm(&r, &w->hour, &w->min);
	}
	if ((r = word(s, "once"))) {
		long y = number(&r);
		if (*r++ != '-')
			return 0;
		long m = number(&r);
		if (*r++ != '-')
			return 0;
		long d = number(&r);
		while (*r == ' ')
			r++;
		w->type = TW_ONCE;
		w->year = (int)y;
		w->month = (int)m;
		w->day = (int)d;
		return y >= 1970 && m >= 1 && m <= 12 && d >= 1 && d <= tk_mdays((int)y, (int)m) &&
		       hhmm(&r, &w->hour, &w->min);
	}
	if (word(s, "startup") || word(s, "boot") || word(s, "login")) {
		w->type = TW_STARTUP;
		return 1;
	}
	if (word(s, "shutdown") || word(s, "logout")) {
		w->type = TW_SHUTDOWN;
		return 1;
	}
	if ((r = word(s, "event")) || (s[0] == 'r' && (r = word(s, "removed")))) {
		w->type = s[0] == 'r' ? TW_REMOVED : TW_EVENT;
		int i = 0;
		while (*r && !is_blank(*r) && i < TK_TOPIC - 1)
			w->topic[i++] = *r++;
		w->topic[i] = 0;
		while (*r == ' ' || *r == '\t')
			r++;
		if (w->type == TW_EVENT) {
			tk_copy(w->value, r, TK_VALUE);
			long n = tk_len(w->value);
			while (n && is_blank(w->value[n - 1]))
				w->value[--n] = 0;
			if (n >= 2 && w->value[0] == '"' && w->value[n - 1] == '"') { /* (« "up *" » : les blancs gardés) */
				w->value[n - 1] = 0;
				for (long k = 0; k < n - 1; k++)
					w->value[k] = w->value[k + 1];
			}
		}
		return i > 0;
	}
	if ((r = word(s, "power"))) {
		w->type = TW_POWER;
		if (word(r, "ac"))
			w->n = 1;
		else if (word(r, "battery"))
			w->n = 0;
		else
			return 0;
		return 1;
	}
	if ((r = word(s, "battery"))) {
		long n = number(&r);
		w->type = TW_BATTERY;
		w->n = n;
		return n >= 1 && n <= 100;
	}
	return 0;
}

static void put_hhmm(struct buf *b, int h, int m)
{
	put_num(b, h, 2);
	put(b, ":");
	put_num(b, m, 2);
}

long tk_when_format(const struct tk_when *w, char *out, long cap)
{
	struct buf b = {out, 0, cap};
	if (cap)
		out[0] = 0;
	switch (w->type) {
	case TW_EVERY:
		put(&b, "every ");
		put_num(&b, w->n, 1);
		put(&b, w->unit == 60 ? " m" : w->unit == 3600 ? " h" : " d");
		break;
	case TW_DAILY:
		put(&b, "daily ");
		put_hhmm(&b, w->hour, w->min);
		break;
	case TW_WEEKLY: {
		put(&b, "weekly ");
		int first = 1;
		for (int i = 1; i <= 7; i++) /* lundi d'abord */
			if (w->days & (1 << (i % 7))) {
				if (!first)
					put(&b, ",");
				put(&b, day_names[i % 7]);
				first = 0;
			}
		put(&b, " ");
		put_hhmm(&b, w->hour, w->min);
		break;
	}
	case TW_MONTHLY:
		put(&b, "monthly ");
		put_num(&b, w->mday, 1);
		put(&b, " ");
		put_hhmm(&b, w->hour, w->min);
		break;
	case TW_ONCE:
		put(&b, "once ");
		put_num(&b, w->year, 4);
		put(&b, "-");
		put_num(&b, w->month, 2);
		put(&b, "-");
		put_num(&b, w->day, 2);
		put(&b, " ");
		put_hhmm(&b, w->hour, w->min);
		break;
	case TW_STARTUP:
		put(&b, "startup");
		break;
	case TW_SHUTDOWN:
		put(&b, "shutdown");
		break;
	case TW_EVENT:
		put(&b, "event ");
		put(&b, w->topic);
		if (w->value[0]) {
			int q = 0;
			for (const char *c = w->value; *c; c++)
				q |= *c == ' ';
			put(&b, q ? " \"" : " ");
			put(&b, w->value);
			if (q)
				put(&b, "\"");
		}
		break;
	case TW_REMOVED:
		put(&b, "removed ");
		put(&b, w->topic);
		break;
	case TW_POWER:
		put(&b, w->n ? "power ac" : "power battery");
		break;
	case TW_BATTERY:
		put(&b, "battery ");
		put_num(&b, w->n, 1);
		break;
	}
	return b.n;
}

/* ---- le fichier */

void tk_defaults(struct task *t)
{
	char id[TK_ID];
	tk_copy(id, t->id, TK_ID);
	tk_zero(t, sizeof *t);
	tk_copy(t->id, id, TK_ID);
	t->enabled = 1;
	t->catchup = 1;
	t->notify = TN_FAIL;
}

static int yes(const char *v)
{
	return v[0] == '1' || lower(v[0]) == 'y' || lower(v[0]) == 'o' || (lower(v[0]) == 't' && lower(v[1]) == 'r');
}

int tk_parse(struct task *t, char *text)
{
	tk_defaults(t);
	char *l = text;
	int any = 0;
	while (*l) {
		char *e = l;
		while (*e && *e != '\n')
			e++;
		char *next = *e ? e + 1 : e;
		*e = 0;
		char *s = trim(l);
		char *eq = s;
		while (*eq && *eq != '=')
			eq++;
		if (*s && *s != '#' && *eq == '=') {
			*eq = 0;
			char *k = trim(s), *v = trim(eq + 1);
			for (char *c = k; *c; c++)
				*c = (char)lower(*c);
			any = 1;
			if (tk_eq(k, "name"))
				tk_copy(t->name, v, TK_NAME);
			else if (tk_eq(k, "enabled"))
				t->enabled = yes(v);
			else if (tk_eq(k, "kind"))
				t->kind = tk_eq(v, "app") ? TK_APP : tk_eq(v, "shell") ? TK_SHELL : TK_PROGRAM;
			else if (tk_eq(k, "run"))
				tk_copy(t->run, v, TK_RUN);
			else if (tk_eq(k, "when")) {
				if (t->nwhen < TK_WHEN_MAX && tk_when_parse(&t->when[t->nwhen], v))
					t->nwhen++;
			} else if (tk_eq(k, "catchup"))
				t->catchup = yes(v);
			else if (tk_eq(k, "delay")) {
				const char *p = v;
				long n = number(&p);
				t->delay = n < 0 ? 0 : n;
			} else if (tk_eq(k, "timeout")) {
				const char *p = v;
				long n = number(&p);
				t->timeout = n < 0 ? 0 : n;
			} else if (tk_eq(k, "ac_only"))
				t->ac_only = yes(v);
			else if (tk_eq(k, "net_only"))
				t->net_only = yes(v);
			else if (tk_eq(k, "notify"))
				t->notify = tk_eq(v, "never") ? TN_NEVER : tk_eq(v, "always") ? TN_ALWAYS : TN_FAIL;
		}
		l = next;
	}
	return any && t->run[0] ? 0 : -1;
}

long tk_format(const struct task *t, char *out, long cap)
{
	struct buf b = {out, 0, cap};
	static const char *const kinds[3] = {"program", "app", "shell"};
	static const char *const notes[3] = {"never", "fail", "always"};
	char w[TK_TOPIC + TK_VALUE + 32];
	put(&b, "# Tâche planifiée (Réglages › Tâches planifiées, docs/TACHES.md)\n");
	put(&b, "name = ");
	put(&b, t->name);
	put(&b, "\nenabled = ");
	put(&b, t->enabled ? "1" : "0");
	put(&b, "\nkind = ");
	put(&b, kinds[t->kind >= 0 && t->kind <= 2 ? t->kind : 0]);
	put(&b, "\nrun = ");
	put(&b, t->run);
	put(&b, "\n");
	for (int i = 0; i < t->nwhen; i++) {
		tk_when_format(&t->when[i], w, sizeof w);
		put(&b, "when = ");
		put(&b, w);
		put(&b, "\n");
	}
	put(&b, "catchup = ");
	put(&b, t->catchup ? "1" : "0");
	put(&b, "\ndelay = ");
	put_num(&b, t->delay, 1);
	put(&b, "\ntimeout = ");
	put_num(&b, t->timeout, 1);
	put(&b, "\nac_only = ");
	put(&b, t->ac_only ? "1" : "0");
	put(&b, "\nnet_only = ");
	put(&b, t->net_only ? "1" : "0");
	put(&b, "\nnotify = ");
	put(&b, notes[t->notify >= 0 && t->notify <= 2 ? t->notify : 1]);
	put(&b, "\n");
	return b.n;
}

/* ---- les noms */

int tk_id_ok(const char *id)
{
	long n = 0;
	for (; id[n]; n++) {
		char c = id[n];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return 0;
	}
	return n > 0 && n < TK_ID;
}

void tk_id_from_name(const char *name, char *out)
{
	long n = 0;
	int dash = 0;
	for (const unsigned char *s = (const unsigned char *)name; *s && n < TK_ID - 1; s++) {
		int c = lower(*s);
		if (*s == 0xC3 && s[1]) { /* (les lettres accentuées les plus courantes, en UTF-8) */
			unsigned char d = s[1] | 0x20;
			s++;
			c = d >= 0xA0 && d <= 0xA5 ? 'a' : d == 0xA7 ? 'c' : d >= 0xA8 && d <= 0xAB ? 'e'
			  : d >= 0xAC && d <= 0xAF ? 'i' : d >= 0xB2 && d <= 0xB6 ? 'o' : d >= 0xB9 && d <= 0xBC ? 'u' : '-';
		}
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			out[n++] = (char)c;
			dash = 0;
		} else if (n && !dash) {
			out[n++] = '-';
			dash = 1;
		}
	}
	while (n && out[n - 1] == '-')
		n--;
	out[n] = 0;
	if (!n)
		tk_copy(out, "tache", TK_ID);
}
