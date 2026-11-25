/* agenda - l'applet du bureau (apps/agenda d'Onyx), lancé par la session (/etc/session) : les
 * prochains rendez-vous du Calendrier, aujourd'hui d'abord, sur le fond d'écran, sous toutes les
 * fenêtres. Il lit les fichiers que le Calendrier écrit (src/apps/calendar/agendafiles.h, ceux
 * d'Onyx) :
 *   /apps/calendar.app/agenda.txt	« AAAAMMJJ|[H:MM ]titre » (Latin-1)
 *   /apps/calendar.app/reminders.txt	« AAAAMMJJHHMM|texte » : les rappels, envoyés à notifyd
 *					quand leur minute vient
 * Service « agenda » : le Calendrier sait qu'il n'a pas à envoyer les rappels lui-même ; op 1 : ses
 * fichiers ont changé, relire. Relu aussi à chaque minute. Un clic sur une ligne ouvre le
 * Calendrier à ce jour. Sa place est fixe, en haut à gauche, sous la barre de menus.
 *
 * Traduit ligne à ligne de agenda.asm (le PC n'avait pas l'applet). */
#include "sys_consts.h"
#include "uikit_import.h"

#define CW 344
#define HDR 34 /* le titre, son trait */
#define ROW 24
#define NROWS 6
#define CH (HDR + 4 + NROWS * ROW + 8)
#define MAXEV 64
#define NOTE 152 /* titre converti en UTF-8 */
#define NOTE_X 112
#define FBUF 16384
#define ALPHA 0x28000000u /* le fond laisse voir un peu le bureau */
#define APPLET_X 16       /* place fixe : sous la barre de menus (src/inc/applet.inc) */
#define APPLET_Y 44

static struct krt_ui ui;
static long g_svc = -1;
static i64 g_nev, g_hot = -1, g_last = -1;
static u64 g_today, g_sig, g_minute;
static u8 msg[WS_MSG];
static char textbuf[320];
static char fbuf[FBUF];
static struct ev {
	u64 key;
	char note[NOTE];
} g_ev[MAXEV];

static const char s_title[] = "Prochains rendez-vous";
static const char s_cal[] = "Calendrier";
/* les fichiers de Calendrier : HOME:/.settings/calendar/ (depuis le 25/11), sinon l'ancienne place */
static const char s_agenda[] = "/apps/calendar.app/agenda.txt";
static const char s_remind[] = "/apps/calendar.app/reminders.txt";
static const char s_agenda_u[] = "HOME:/.settings/calendar/agenda.txt";
static const char s_remind_u[] = "HOME:/.settings/calendar/reminders.txt";

static const char *pick(const char *mine, const char *old)
{
	long st[2];
	return sys_stat(mine, st) >= 0 ? mine : old;
}
static const char p_calendar[] = "/apps/calendar.app/main";
static const char *const s_days[7] = { "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim" };
static const char *const s_months[12] = { "janv.", "f\xC3\xA9vr.", "mars", "avr.", "mai", "juin",
					   "juil.", "ao\xC3\xBBt", "sept.", "oct.", "nov.",
					   "d\xC3\xA9" "c." };

static u32 token(long off)
{
	return *(const u32 *)((const u8 *)ui_tokens() + off);
}

/* ---- dates */

/* année, mois, jour -> jours depuis 1970 (H. Hinnant) */
static i64 days_from_civil(i64 y, u64 m, u64 d)
{
	y -= m <= 2;
	i64 era = (y >= 0 ? y : y - 399) / 400;
	u64 yoe = (u64)(y - era * 400);
	u64 doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	u64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (i64)doe - 719468;
}

/* jours depuis 1970 -> année, mois, jour */
static void civil_from_days(i64 z, u64 *y, u64 *m, u64 *d)
{
	z += 719468;
	i64 era = (z >= 0 ? z : z - 146096) / 146097;
	u64 doe = (u64)(z - era * 146097);
	u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	i64 yy = (i64)yoe + era * 400;
	u64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	u64 mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp < 10 ? mp + 3 : mp - 9;
	*y = (u64)(yy + (*m <= 2));
}

/* -> aujourd'hui : AAAAMMJJ */
static u64 today_key(void)
{
	u64 y, m, d;
	civil_from_days((i64)((u64)sys_clock(2) / 86400), &y, &m, &d);
	return y * 10000 + m * 100 + d;
}

/* n chiffres -> valeur (-1 : pas un chiffre) */
static i64 digits(const char *s, int n)
{
	i64 v = 0;
	while (n--) {
		u32 c = (u8)*s++ - '0';
		if (c > 9)
			return -1;
		v = v * 10 + c;
	}
	return v;
}

/* source Latin-1 (ou déjà UTF-8) [s, e[ -> d, en UTF-8, terminé par 0, cap octets au plus */
static void to_utf8(char *d, const char *s, const char *e, u64 cap)
{
	char *lim = d + cap - 3;
	while (s < e && d < lim) {
		u8 c = (u8)*s++;
		if (c == 13)
			continue;
		if (c < 0x80) {
			*d++ = (char)c;
			continue;
		}
		if ((c & 0xE0) == 0xC0 && s < e && ((u8)*s & 0xC0) == 0x80) { /* déjà de l'UTF-8 */
			*d++ = (char)c;
			*d++ = *s++;
			continue;
		}
		*d++ = (char)(0xC0 | (c >> 6));
		*d++ = (char)(0x80 | (c & 0x3F));
	}
	*d = 0;
}

/* ---- fichiers */

/* chemin -> octets dans fbuf, terminé par 0 (0 : pas de fichier) */
static u64 read_file(const char *path)
{
	long n = 0, fd = sys_open(path, O_READ);
	if (fd >= 0) {
		n = sys_read(fd, fbuf, FBUF - 1);
		if (n < 0)
			n = 0;
		sys_close(fd);
	}
	fbuf[n] = 0;
	return (u64)n;
}

/* relit agenda.txt -> 1 si la liste change (ou le jour) */
static int reload(void)
{
	u64 n = read_file(pick(s_agenda_u, s_agenda));
	u64 today = today_key();
	u32 h = 2166136261u; /* empreinte (FNV-1a) */
	for (u64 i = 0; i < n; i++) {
		h ^= (u8)fbuf[i];
		h *= 16777619u;
	}
	if (g_sig == h && g_today == today)
		return 0;
	g_sig = h;
	g_today = today;
	g_nev = 0;
	const char *p = fbuf, *end = fbuf + n;
	while (p < end) {
		const char *l = p;
		while (p < end && *p != 10)
			p++;
		const char *le = p++;
		if (le - l < 10 || l[8] != '|')
			continue;
		i64 key = digits(l, 8);
		if (key < 0 || (u64)key < today || g_nev >= MAXEV) /* (les jours passés : non) */
			continue;
		g_ev[g_nev].key = (u64)key;
		to_utf8(g_ev[g_nev].note, l + 9, le, NOTE);
		g_nev++;
	}
	for (i64 i = 1; i < g_nev; i++) /* par date (insertion, stable) */
		for (i64 j = i; j > 0 && g_ev[j - 1].key > g_ev[j].key; j--) {
			struct ev t = g_ev[j];
			g_ev[j] = g_ev[j - 1];
			g_ev[j - 1] = t;
		}
	return 1;
}

/* les rappels dont la minute vient de passer : des notifications */
static void reminders(void)
{
	i64 now = (i64)((u64)sys_clock(2) / 60);
	i64 last = g_last;
	g_last = now;
	if (last < 0 || now == last) /* (au lancement : le passé reste passé) */
		return;
	u64 n = read_file(pick(s_remind_u, s_remind));
	const char *p = fbuf, *end = fbuf + n;
	while (p < end) {
		const char *l = p;
		while (p < end && *p != 10)
			p++;
		const char *le = p++;
		if (le - l < 14 || l[12] != '|')
			continue;
		i64 y = digits(l, 4);
		if (y < 0)
			continue;
		i64 mo = digits(l + 4, 2), d = digits(l + 6, 2), hh = digits(l + 8, 2), mm = digits(l + 10, 2);
		if (mo < 0 || d < 0 || hh < 0 || mm < 0)
			continue;
		i64 m = days_from_civil(y, (u64)mo, (u64)d) * 1440 + hh * 60 + mm;
		if (m <= last || m > now)
			continue;
		to_utf8(textbuf, l + 13, le, 300);
		notify_send(lang_tr(s_cal), textbuf, 0);
	}
}

/* ---- dessin */

/* clé AAAAMMJJ -> d : « Mar 29 sept. » (l'année si ce n'est pas celle-ci) */
static void fmt_date(u64 key, char *d)
{
	u64 y = key / 10000, m = key / 100 % 100, j = key % 100;
	i64 days = days_from_civil((i64)y, m, j);
	u64 wd = (u64)(days + 3) % 7; /* 0 : lundi */
	*d = 0;
	char *e = str_cat(d, lang_tr(s_days[wd]));
	*e++ = ' ';
	e = fmt_dec(j, e);
	*e++ = ' ';
	*e = 0;
	e = str_cat(e, lang_tr(s_months[(m - 1) % 12]));
	if (g_today / 10000 != y) {
		*e++ = ' ';
		e = fmt_dec(y, e);
	}
	*e = 0;
}

static void text13(const char *s, long x, long y, u32 col, const u8 *font, long maxw)
{
	ui_text_fit((long)s, x, y, col, 13, (long)font, maxw);
}

static void draw(void)
{
	ui_fill(0, 0, CW, CH, 0xFF000000); /* (tout transparent) */
	u32 card = (token(TJ_SURFACE) & 0xFFFFFF) | ALPHA;
	ui_rbox(0, 0, CW, CH, 10, card, card);
	ui_rbox(12, 9, 16, 17, 3, 0x00FFFFFF, 0x00E8E8E8); /* une petite page de calendrier */
	ui_rbox(12, 9, 16, 6, 3, 0x00D23A30, 0x00D23A30);
	long x = ui_text((long)lang_tr(s_title), 36, 23, token(TJ_TEXT1), 14, (long)ui.font_b, 0);
	if (g_nev) { /* (n) */
		char *e = textbuf;
		*e++ = '(';
		e = fmt_dec((u64)g_nev, e);
		*e++ = ')';
		*e = 0;
		ui_text((long)textbuf, x + 6, 23, token(TJ_TEXT2), 13, (long)ui.font, 0);
	}
	ui_fill(10, 33, CW - 20, 1, token(TJ_LINE_STRONG));
	if (!g_nev)
		text13(lang_tr("Aucun rendez-vous \xC3\xA0 venir."), 14, HDR + 24, token(TJ_TEXT2), ui.font, CW - 28);
	for (i64 i = 0; i < g_nev && i < NROWS; i++) {
		long y = i * ROW + HDR + 4;
		struct ev *ev = &g_ev[i];
		if (g_hot == i) {
			u32 c = (u32)ui_mix(token(TJ_SURFACE), token(TJ_TEXT1), 28);
			ui_rbox(6, y, CW - 12, ROW, 6, c, c);
		}
		const u8 *font = ui.font;
		if (ev->key == g_today) { /* aujourd'hui : une pastille */
			u32 a = token(TJ_ACCENT);
			ui_rbox(12, y + 3, NOTE_X - 20, ROW - 6, 6, a, a);
			ui_text((long)lang_tr("Aujourd'hui"), 20, y + 17, ui_ink(a), 12, (long)ui.font_b, 0);
			font = ui.font_b;
		} else {
			fmt_date(ev->key, textbuf);
			ui_text_fit((long)textbuf, 14, y + 17, token(TJ_TEXT2), 12, (long)ui.font, NOTE_X - 18);
		}
		text13(ev->note, NOTE_X, y + 17, token(TJ_TEXT1), font, CW - NOTE_X - 14);
	}
	if (g_nev > NROWS)
		text13("\xE2\x80\xA6", CW - 24, CH - 2, token(TJ_TEXT2), ui.font, 20);
	ui_update_all();
}

/* ---- souris */

static void on_mouse(void)
{
	u32 kind = *(u32 *)(ui.ev + EV_KIND);
	i32 y = *(i32 *)(ui.ev + EV_Y);
	i64 row = -1;
	if (y >= HDR + 4) {
		row = (y - (HDR + 4)) / ROW;
		if (row >= g_nev || row >= NROWS)
			row = -1;
	}
	if (kind == 1) { /* appui : le Calendrier, à ce jour */
		if (row < 0)
			return;
		char *e = fmt_dec(g_ev[row].key, textbuf);
		*e = 0;
		ksys(SYS_SPAWN, (long)p_calendar, (long)textbuf, 0, 0, 0, 0);
		return;
	}
	if (kind == 2 || row == g_hot) /* (relâché) */
		return;
	g_hot = row;
	draw();
}

/* relit agenda.txt ; redessine s'il change */
static void refresh(void)
{
	if (reload())
		draw();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	g_svc = korin_sys2(SYS_SVC_REGISTER, "agenda", 0);
	if (g_svc < 0) /* (un autre agenda tourne) */
		return 0;
	ui_init((long)&ui);
	reload();
	reminders();
	if (ui_window_ex(APPLET_X, APPLET_Y, CW, CH, (long)lang_tr(s_title),
			 WF_FRAMELESS | WF_BACK | WF_NOFOCUS | WF_NOLIST | WF_ALPHA) < 0)
		return 0;
	draw();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, g_svc, msg, 0); /* le Calendrier a enregistré ? */
		if (req > 0) {
			*(u64 *)msg = 0;
			korin_sys2(SYS_SVC_REPLY, req, msg);
			refresh();
		}
		long e = ui_event(1000);
		if (e == WEV_THEME) /* autre thème : redessine */
			draw();
		else if (e == WEV_MOUSE)
			on_mouse();
		else if (e == WEV_CLOSE)
			return 0;
		else if (!e) { /* une nouvelle minute ? */
			u64 min = (u64)sys_clock(2) / 60;
			if (min != g_minute) {
				g_minute = min;
				refresh();
				reminders();
			}
		}
	}
}
