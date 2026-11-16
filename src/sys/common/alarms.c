/* alarms.c : les alarmes de l'Horloge (alarms.h), traduit ligne à ligne de src/inc/alarms.inc.
 * Le programme importe buskit (al_publish) et audiokit (gamesnd.c, pour les sons). */
#include "alarms.h"
#include "buskit_import.h"

u64 al_n;
u64 al_next_id = 1;
u64 al_snz = AL_SNOOZE_DEF;
struct alarm al_tab[AL_MAX];
static char al_pubval[24];
static char al_fbuf[AL_FBUF];

_Static_assert(sizeof(struct alarm) == 128, "AL_SIZE");
_Static_assert(__builtin_offsetof(struct alarm, date) == 16, "AL_DATE");
_Static_assert(__builtin_offsetof(struct alarm, label) == 40, "AL_LABEL");

const char al_path[] = "HOME:/.config/korin/alarms.txt"; /* (lu par conf_path : sinon /etc/alarms.txt) */
const char al_topic[] = "korin.clock.alarms";
static const char al_s_head[] =
	"# Horloge : les alarmes (ecrites par l'Horloge et clockd, sonnees par clockd).\n"
	"# snooze : minutes du report. Un bloc [alarm] par alarme.\n";
static const char al_s_eq[] = " = ";
static const char al_k_alarm[] = "[alarm]";
static const char al_k_snooze[] = "snooze";
/* les clés d'un bloc, dans l'ordre d'al_key */
static const char *const al_keys[] = { "id", "time", "label", "on", "days", "date", "sound", "snooze",
				       "missed" };
enum { K_ID, K_TIME, K_LABEL, K_ON, K_DAYS, K_DATE, K_SOUND, K_SNOOZE, K_MISSED, K_N };
static const char al_day_tok[7][4] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
static const char *const al_snd_tok[AL_SOUNDS] = { "chimes", "beeps", "marimba" };

/* ---- dates (heure locale ; une minute = jours * 1440 + h * 60 + m) */

u64 al_days(u64 y, u64 m, u64 d)
{
	y -= m <= 2;
	u64 era = y / 400;
	u64 yoe = y - era * 400;
	u64 doy = (153 * (m + (m > 2 ? (u64)-3 : 9)) + 2) / 5 + d - 1;
	u64 doe = yoe * 365 + (yoe >> 2) - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void al_civil(u64 days, u64 *py, u64 *pm, u64 *pd)
{
	u64 z = days + 719468;
	u64 era = z / 146097;
	u64 doe = z - era * 146097;
	u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	u64 y = era * 400 + yoe;
	u64 doy = doe - (yoe * 365 + (yoe >> 2) - yoe / 100);
	u64 mp = (doy * 5 + 2) / 153;
	u64 d = doy - (mp * 153 + 2) / 5 + 1;
	u64 m = mp + (mp >= 10 ? (u64)-9 : 3);
	if (m <= 2)
		y++;
	*py = y;
	*pm = m;
	*pd = d;
}

u64 al_wday(u64 days)
{
	return (days + 3) % 7; /* (le 1/1/1970 était un jeudi) */
}

i64 al_now(u64 *secs)
{
	u64 s = (u64)sys_clock(2);
	if (secs)
		*secs = s;
	return (i64)(s / 60);
}

i64 al_next(const struct alarm *a, i64 from)
{
	if (!a->valid)
		return -1;
	i64 hm = (i64)a->hh * 60 + a->mm;
	u32 days = a->days & AL_DAYS_ALL;
	if (!days) { /* une fois : son jour */
		if (a->date < 0)
			return -1;
		i64 m = a->date * 1440 + hm;
		return m >= from ? m : -1;
	}
	u64 start = (u64)(from > 0 ? from : 0) / 1440; /* le jour de départ */
	for (u64 k = 0; k <= 8; k++) {
		u64 d = start + k;
		if (!((days >> ((d + 3) % 7)) & 1)) /* son jour de la semaine */
			continue;
		i64 m = (i64)d * 1440 + hm;
		if (m >= from)
			return m;
	}
	return -1;
}

i64 al_next_all(i64 now, i64 *idx)
{
	i64 best = -1, bi = -1;
	for (u64 i = 0; i < al_n; i++) {
		const struct alarm *a = &al_tab[i];
		if (!a->on || !a->valid)
			continue;
		i64 m = al_next(a, now + 1);
		i64 s = a->snooze; /* un report à venir compte */
		if (s > now && (m < 0 || s < m))
			m = s;
		if (m < 0)
			continue;
		if (best < 0 || m < best) {
			best = m;
			bi = (i64)i;
		}
	}
	if (idx)
		*idx = bi;
	return best;
}

int al_passed(const struct alarm *a, i64 now)
{
	if (a->days & AL_DAYS_ALL)
		return 0;
	if (!a->valid)
		return 0;
	if (a->snooze >= now)
		return 0;
	if (a->date < 0)
		return 0;
	i64 m = a->date * 1440 + (i64)a->hh * 60 + a->mm;
	return m < now;
}

u64 al_once_day(u64 hh, u64 mm, u64 now)
{
	u64 today = now / 1440;
	u64 t = hh * 60 + mm;
	return t <= now % 1440 ? today + 1 : today;
}

struct alarm *al_find(u32 id)
{
	for (u64 i = 0; i < al_n; i++)
		if (al_tab[i].id == id)
			return &al_tab[i];
	return 0;
}

void al_blank(struct alarm *a)
{
	a->id = 0;
	a->hh = 7;
	a->mm = 0;
	a->on = 1;
	a->valid = 0;
	a->days = 0;
	a->sound = 0;
	a->date = -1;
	a->snooze = -1;
	a->missed = -1;
	a->label[0] = 0;
}

int al_remove(u32 id)
{
	struct alarm *a = al_find(id);
	if (!a)
		return 0;
	al_n--; /* les suivantes remontent */
	mem_move(a, a + 1, (u64)((u8 *)&al_tab[al_n] - (u8 *)a));
	return 1;
}

/* ---- lecture */

/* texte, chiffres -> valeur, -1 : pas des chiffres ou trop long (le texte doit finir juste
 * après) */
static i64 al_digits(const char *s, u64 n)
{
	u64 v = 0;
	for (; n; n--) {
		u32 c = (u8)*s++ - (u32)'0';
		if (c > 9)
			return -1;
		v = v * 10 + c;
	}
	if (*s)
		return -1;
	return (i64)v;
}

int al_parse_hm(const char *s, u64 *hh, u64 *mm)
{
	u64 h = 0, nd = 0;
	for (;;) {
		u8 c = (u8)*s++;
		if (c == ':')
			break;
		u32 dg = c - (u32)'0';
		if (dg > 9)
			goto no;
		h = h * 10 + dg;
		if (++nd > 2)
			goto no;
	}
	if (!nd || h > 23)
		goto no;
	if (s[0] && s[1] && s[2]) /* (« HH:MMx » : refusé) */
		goto no;
	{
		u32 d1 = (u8)s[0] - (u32)'0', d2;
		if (d1 > 5)
			goto no;
		d2 = (u8)s[1] - (u32)'0';
		if (d2 > 9)
			goto no;
		*hh = h;
		*mm = d1 * 10 + d2;
		return 1;
	}
no:
	*hh = 0;
	*mm = 0;
	return 0;
}

i64 al_parse_day(const char *s)
{
	i64 v = al_digits(s, 8);
	if (v < 0)
		return v;
	u64 y = (u64)v / 10000, r = (u64)v % 10000;
	u64 m = r / 100, d = r % 100;
	if (y < 1970 || m - 1 > 11 || d - 1 > 30)
		return -1;
	return (i64)al_days(y, m, d);
}

i64 al_parse_stamp(char *s)
{
	if (str_len(s) != 12)
		return -1;
	i64 v = al_digits(s + 8, 4); /* HHMM */
	if (v < 0)
		return -1;
	u64 hh = (u64)v / 100, mm = (u64)v % 100;
	if (hh > 23 || mm > 59)
		return -1;
	char c = s[8]; /* le jour seul */
	s[8] = 0;
	i64 d = al_parse_day(s);
	s[8] = c;
	if (d < 0)
		return d;
	return d * 1440 + (i64)(hh * 60 + mm);
}

u64 al_parse_days(const char *s)
{
	u64 bits = 0;
	while (*s) {
		/* les trois lettres, en minuscules (l'asm lisait s[2] même après la fin : sans
		   effet, un mot d'une lettre ne correspond à rien) */
		u32 c0 = (u8)s[0] | 0x20, c1 = (u8)s[1] | 0x20, c2 = (s[1] ? (u8)s[2] : 0) | 0x20;
		for (u64 i = 0; i < 7; i++)
			if ((u8)al_day_tok[i][0] == c0 && (u8)al_day_tok[i][1] == c1 &&
			    (u8)al_day_tok[i][2] == c2) {
				bits |= 1ull << i;
				break;
			}
		for (;;) { /* la fin du mot */
			char c = *s;
			if (!c)
				return bits;
			if (c == ' ' || c == ',' || c == 9)
				break;
			s++;
		}
		while (*s == ' ' || *s == ',' || *s == 9) /* puis les séparateurs */
			s++;
	}
	return bits;
}

void al_label_copy(char *d, const char *s)
{
	u64 i = 0;
	for (;; i++) {
		u8 c = (u8)s[i];
		if (!c)
			break;
		if (c == 9)
			c = ' ';
		if ((c & 0xC0) != 0x80) { /* un caractère commence : tient-il ? */
			u64 n = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
			if (i + n > AL_LABEL_MAX)
				break;
		} else if (i >= AL_LABEL_MAX)
			break; /* (écart voulu : l'asm recopiait sans borne une suite d'octets de
				  continuation isolés, au-delà de la place) */
		d[i] = (char)c;
	}
	d[i] = 0;
}

/* clé, valeur, alarme du bloc (0 : aucune), 1 après un bloc : une ligne « clé = valeur » */
static void al_key(const char *key, char *val, struct alarm *a, int after_block)
{
	if (!a) {
		if (after_block) /* (un autre bloc : ignoré) */
			return;
		if (!str_eq(key, al_k_snooze)) /* avant tout bloc : le report */
			return;
		u64 v = parse_dec(val);
		if (v - 1 > 29)
			return;
		al_snz = v;
		return;
	}
	int k;
	for (k = 0; k < K_N; k++) /* la clé : son numéro */
		if (str_eq(key, al_keys[k]))
			break;
	u64 hh, mm;
	switch (k) {
	case K_ID:
		a->id = (u32)parse_dec(val);
		break;
	case K_TIME:
		a->valid = (u8)al_parse_hm(val, &hh, &mm);
		a->hh = (u8)hh;
		a->mm = (u8)mm;
		break;
	case K_LABEL:
		al_label_copy(a->label, val);
		break;
	case K_ON: /* « 0 » seul : 0, sinon 1 */
		a->on = !(val[0] == '0' && val[1] == 0);
		break;
	case K_DAYS:
		a->days = (u8)al_parse_days(val);
		break;
	case K_DATE:
		a->date = al_parse_day(val);
		break;
	case K_SOUND: {
		u64 s;
		for (s = 0; s < AL_SOUNDS; s++)
			if (str_eq(val, al_snd_tok[s]))
				break;
		a->sound = (u8)(s < AL_SOUNDS ? s : 0);
		break;
	}
	case K_SNOOZE:
		a->snooze = al_parse_stamp(val);
		break;
	case K_MISSED:
		a->missed = al_parse_stamp(val);
		break;
	}
}

/* les numéros : uniques, positifs ; al_next_id au-dessus de tous */
static void al_fix_ids(void)
{
	i64 next = 1;
	for (u64 i = 0; i < al_n; i++) {
		i64 id = (i32)al_tab[i].id;
		if (id >= next)
			next = id + 1;
	}
	for (u64 i = 0; i < al_n; i++) { /* un numéro absent ou déjà pris : un neuf */
		i64 id = (i32)al_tab[i].id;
		int fresh = id <= 0;
		for (u64 j = 0; !fresh && j < i; j++)
			if (al_tab[j].id == (u32)id)
				fresh = 1;
		if (fresh)
			al_tab[i].id = (u32)next++;
	}
	al_next_id = (u64)next;
}

u64 al_load(void)
{
	long n = 0;
	long fd = sys_open(conf_path("alarms.txt", 0), O_READ);
	if (fd >= 0) {
		n = sys_read(fd, al_fbuf, AL_FBUF - 1);
		if (n < 0)
			n = 0;
		sys_close(fd);
	}
	al_n = 0;
	al_snz = AL_SNOOZE_DEF;
	char *p = al_fbuf, *end = al_fbuf + n;
	*end = 0;
	struct alarm *a = 0; /* l'alarme du bloc */
	int block = 0;       /* un bloc vu */
	while (p < end) {
		if (*p == ' ' || *p == 9) { /* les blancs du début */
			p++;
			continue;
		}
		char *le = p; /* la fin de la ligne */
		while (le < end && *le != 10)
			le++;
		char *t = le; /* sans les blancs de la fin */
		while (t > p && (t[-1] == 13 || t[-1] == ' ' || t[-1] == 9))
			t--;
		*t = 0;
		if (!*p || *p == '#' || *p == ';')
			goto next;
		if (*p == '[') { /* un bloc : [alarm] ou un autre */
			block = 1;
			a = 0;
			if (!str_eq(p, al_k_alarm) || al_n >= AL_MAX)
				goto next;
			a = &al_tab[al_n++];
			al_blank(a);
			goto next;
		}
		{
			char *q = p; /* « clé = valeur » */
			while (*q && *q != '=')
				q++;
			if (!*q)
				goto next;
			*q = 0;
			for (char *k = q; k > p && (k[-1] == ' ' || k[-1] == 9);)
				*--k = 0;
			char *v = q + 1;
			while (*v == ' ' || *v == 9)
				v++;
			al_key(p, v, a, block);
		}
	next:
		p = le + 1;
	}
	al_fix_ids();
	return al_n;
}

/* ---- écriture */

char *al_2d(char *d, u64 v)
{
	*d++ = (char)('0' + v / 10);
	*d++ = (char)('0' + v % 10);
	*d = 0;
	return d;
}

char *al_fmt_hm(char *d, const struct alarm *a)
{
	d = al_2d(d, a->hh);
	*d++ = ':';
	return al_2d(d, a->mm);
}

char *al_fmt_min_hm(char *d, u64 min)
{
	min %= 1440;
	d = al_2d(d, min / 60);
	*d++ = ':';
	return al_2d(d, min % 60);
}

char *al_fmt_day(char *d, i64 days)
{
	*d = 0;
	if (days < 0)
		return d;
	u64 y, m, dd;
	al_civil((u64)days, &y, &m, &dd);
	d = fmt_dec(y, d);
	d = al_2d(d, m);
	return al_2d(d, dd);
}

char *al_fmt_stamp(char *d, i64 min)
{
	*d = 0;
	if (min < 0)
		return d;
	d = al_fmt_day(d, min / 1440);
	u64 r = (u64)min % 1440; /* minute du jour */
	d = al_2d(d, r / 60);
	return al_2d(d, r % 60);
}

char *al_fmt_days(char *d, u64 bits)
{
	int word = 0; /* un mot déjà écrit */
	for (u64 i = 0; i < 7; i++) {
		if (!((bits >> i) & 1))
			continue;
		if (word)
			*d++ = ' ';
		*d++ = al_day_tok[i][0];
		*d++ = al_day_tok[i][1];
		*d++ = al_day_tok[i][2];
		word = 1;
	}
	*d = 0;
	return d;
}

/* fin du texte, clé -> après « clé = » */
static char *al_put_key(char *d, const char *k)
{
	return str_cat(str_cat(d, k), al_s_eq);
}

static char *al_put_nl(char *d)
{
	*d++ = 10;
	*d = 0;
	return d;
}

long al_save(void)
{
	char *d = str_cat(al_fbuf, al_s_head);
	d = al_put_key(d, al_k_snooze);
	d = al_put_nl(fmt_dec(al_snz, d));
	for (u64 i = 0; i < al_n; i++) {
		const struct alarm *a = &al_tab[i];
		d = al_put_nl(str_cat(al_put_nl(d), al_k_alarm));
		d = al_put_key(d, al_keys[K_ID]);
		d = al_put_nl(fmt_dec(a->id, d));
		d = al_put_key(d, al_keys[K_TIME]);
		if (a->valid)
			d = al_fmt_hm(d, a);
		d = al_put_nl(d);
		d = al_put_key(d, al_keys[K_LABEL]);
		d = al_put_nl(str_cat(d, a->label));
		d = al_put_key(d, al_keys[K_ON]);
		*d++ = (char)('0' + a->on);
		d = al_put_nl(d);
		d = al_put_key(d, al_keys[K_DAYS]);
		d = al_put_nl(al_fmt_days(d, a->days & AL_DAYS_ALL));
		d = al_put_key(d, al_keys[K_DATE]);
		if (!(a->days & AL_DAYS_ALL))
			d = al_fmt_day(d, a->date);
		d = al_put_nl(d);
		d = al_put_key(d, al_keys[K_SOUND]);
		d = al_put_nl(str_cat(d, al_snd_tok[a->sound < AL_SOUNDS ? a->sound : 0]));
		d = al_put_key(d, al_keys[K_SNOOZE]);
		d = al_put_nl(al_fmt_stamp(d, a->snooze));
		if (a->missed >= 0) {
			d = al_put_key(d, al_keys[K_MISSED]);
			d = al_put_nl(al_fmt_stamp(d, a->missed));
		}
	}
	u64 n = (u64)(d - al_fbuf); /* octets */
	long fd = sys_open(conf_path("alarms.txt", 1), O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return -1;
	long w = sys_write(fd, al_fbuf, n);
	sys_close(fd);
	return w == (long)n ? 0 : -1;
}

void al_publish(void)
{
	*fmt_dec((u64)sys_time_ms(), al_pubval) = 0; /* (l'asm ne posait pas le 0 : la valeur ne
							raccourcit jamais, même effet) */
	bus_publish((long)al_topic, (long)al_pubval, -1, 0);
}

/* ---- les sons (ceux d'Onyx : sounds.h), sur les voix FM d'AudioKit (gamesnd.c) */

static const struct gs_ent al_snd_chimes[] = { /* un arpège montant, des cloches */
	GS_ENT(0, 1047, 900, AKW_SINE, 170),
	GS_ENT(250, 1319, 900, AKW_SINE, 170),
	GS_ENT(500, 1568, 900, AKW_SINE, 170),
	GS_ENT(750, 2093, 1200, AKW_SINE, 170),
	GS_END,
};
static const struct gs_ent al_snd_beeps[] = { /* des bips aigus et courts */
	GS_ENT(0, 2093, 90, AKW_SQUARE, 70),
	GS_ENT(180, 2093, 90, AKW_SQUARE, 70),
	GS_ENT(360, 2093, 90, AKW_SQUARE, 70),
	GS_ENT(540, 2093, 90, AKW_SQUARE, 70),
	GS_END,
};
static const struct gs_ent al_snd_marimba[] = { /* une phrase douce, du bois */
	GS_ENT(0, 784, 400, AKW_TRIANGLE, 200),
	GS_ENT(200, 1047, 400, AKW_TRIANGLE, 200),
	GS_ENT(400, 1319, 400, AKW_TRIANGLE, 200),
	GS_ENT(600, 1047, 400, AKW_TRIANGLE, 200),
	GS_ENT(1000, 1568, 700, AKW_TRIANGLE, 200),
	GS_END,
};
static const struct gs_ent *const al_snd_tabs[AL_SOUNDS] = { al_snd_chimes, al_snd_beeps, al_snd_marimba };
static const u32 al_snd_round[AL_SOUNDS] = { 2200, 1200, 2000 };

const struct gs_ent *al_sound(u64 n, u64 *round)
{
	if (n >= AL_SOUNDS)
		n = 0;
	*round = al_snd_round[n];
	return al_snd_tabs[n];
}
