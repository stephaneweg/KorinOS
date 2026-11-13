/* clockd - le service des alarmes de l'Horloge (clockd d'Onyx), lancé par la session du bureau
 * (SD0:/etc/session). Il lit SD0:/etc/alarms.txt (le format d'Onyx : src/sys/common/alarms.h),
 * et sonne chaque alarme à sa minute : une notification (notifyd), une carte au-dessus des
 * fenêtres (Répéter, Arrêter), un son en boucle sur les voix FM d'AudioKit. Sans réponse en
 * 2 minutes : le son s'arrête, « Alarme manquée » est notifiée et notée dans le fichier.
 *
 * Sans sondage : il dort (bus_wait) jusqu'à la minute de la prochaine alarme, une heure au plus
 * (l'heure réglée par NTP ou le fuseau) ; le sujet korin.clock.alarms (publié par l'Horloge après
 * chaque changement) le réveille pour relire le fichier. Sans busd, il le relit toutes les 30 s.
 *
 * Comme le Ringer d'Onyx : chaque minute d'alarme sonne une fois, avec 2 minutes de retard au
 * plus ; le passé reste passé au lancement ; un recul de l'heure de moins de 2 h (heure d'été)
 * ne fait rien sonner deux fois. Au premier tour de confiance, les alarmes d'une fois manquées
 * pendant que la machine était éteinte (12 h au plus) sont notifiées, sans son.
 *
 *	clockd [--grace N]	N : secondes après le démarrage avant toute sonnerie (l'heure
 *				n'est sûre qu'après NTP ; 60 par défaut, 0 pour les essais)
 *
 * Traduit ligne à ligne de clockd.asm. */
#include "sys_consts.h"
#include "uikit_import.h"
#include "buskit_import.h"
#include "alarms.h"
#include "gamesnd.h"

#define RING_W 360
#define RING_H 236
#define RING_MS 120000 /* sans réponse : 2 minutes */
#define GRACE_DEF 60   /* secondes */
#define WAIT_MAX 3600000 /* une heure au plus sans regarder l'heure */
#define WAIT_NOBUS 30000 /* sans busd : le fichier relu */
#define MISSED_BACK 720  /* minutes : 12 heures */
#define QMAX 8
#define R_SNOOZE 1 /* fins d'une sonnerie */
#define R_STOP 2
#define R_MISSED 3
#define BTN_Y 182
#define BTN_H 36

static struct krt_ui ui;
static long g_sub;    /* l'abonnement à korin.clock.alarms */
static u64 g_grace;   /* ms après le démarrage */
static i64 g_last;    /* la dernière minute regardée */
static long g_mdone;
static u64 g_qn;
static u64 g_ring_id;
static i64 g_ring_min;
static u64 g_sound;
static const struct gs_ent *g_snd_tab; /* table du son */
static u64 g_snd_round;                /* durée d'un tour */
static long g_btn[2];                  /* Répéter, Arrêter */
static struct {
	u64 id;
	i64 min;
} g_q[QMAX];
static long g_screen[4];
static u8 g_ev[32];
static char g_hm[16];
static char g_name[AL_LABEL_MAX + 8];
static char g_text[160];
static char g_line[320];
static char g_log[320];

static const char s_grace[] = "--grace";
static const char s_clock[] = "Horloge";
static const char s_alarm[] = "Alarme";

static u32 token(long off)
{
	return *(const u32 *)((const u8 *)ui_tokens() + off);
}

/* ligne d'arguments : --grace N (secondes), cherché n'importe où dans la ligne comme l'asm */
static void parse_args(const char *s)
{
	u64 n = GRACE_DEF;
	if (s)
		for (; *s; s++) {
			u64 i = 0; /* « --grace » ici ? */
			while (s_grace[i] && s_grace[i] == s[i])
				i++;
			if (s_grace[i])
				continue;
			s += i;
			while (*s == ' ')
				s++;
			n = parse_dec(s);
			break;
		}
	g_grace = n * 1000;
}

/* le fichier relu ; au journal : combien */
static void reload(void)
{
	u64 n = al_load();
	char *e = str_cat(g_line, "clockd : alarmes lues : ");
	e = fmt_dec(n, e);
	str_cat(e, "\n");
	ak_puts(g_line);
}

/* ---- quand sonner */

/* -> ms à dormir : jusqu'à la prochaine minute d'alarme */
static u64 wait_ms(void)
{
	u64 t = (u64)sys_time_ms(), w;
	if (t < g_grace) /* avant la confiance : jusqu'à elle */
		w = g_grace - t + 100;
	else {
		u64 secs;
		i64 min = al_now(&secs);
		i64 nx = al_next_all(min, 0);
		if (nx < 0)
			w = WAIT_MAX;
		else {
			i64 ms = (nx * 60 - (i64)secs) * 1000 + 500; /* (juste après son début) */
			w = ms < 100 ? 100 : (u64)ms;
		}
	}
	if (w >= WAIT_MAX)
		w = WAIT_MAX;
	if (!g_sub && w >= WAIT_NOBUS)
		w = WAIT_NOBUS;
	return w;
}

/* id, minute : à la fin de la file (pas deux fois la même alarme) */
static void q_push(u64 id, i64 min)
{
	for (u64 i = 0; i < g_qn; i++)
		if (g_q[i].id == id)
			return;
	if (g_qn >= QMAX)
		return;
	g_q[g_qn].id = id;
	g_q[g_qn].min = min;
	g_qn++;
}

/* les minutes d'alarme passées depuis le dernier regard : dans la file */
static void collect(void)
{
	i64 now = al_now(0);
	i64 from = g_last;
	if (now == from)
		return;
	if (now < from) { /* en arrière : plus de 2 h, on repart */
		if (from - now > 120)
			g_last = now;
		return;
	}
	g_last = now;
	from++; /* depuis : après le dernier regard, 2 minutes au plus en arrière */
	if (from < now - 2)
		from = now - 2;
	for (u64 i = 0; i < al_n; i++) {
		struct alarm *a = &al_tab[i];
		if (!a->on || !a->valid)
			continue;
		i64 m = al_next(a, from);
		if (m < 0 || m > now) {
			m = a->snooze;
			if (m < from || m > now)
				continue;
		}
		q_push(a->id, m);
	}
}

/* fin -> saut de ligne */
static char *put_nl(char *e)
{
	*e++ = 10;
	*e = 0;
	return e;
}

/* alarme : g_text <- « 07:00 Nom » (« Alarme » sans nom) */
static void ring_words(struct alarm *a)
{
	char *e = al_fmt_hm(g_text, a);
	*e++ = ' ';
	str_cat(e, a->label[0] ? a->label : lang_tr(s_alarm));
}

/* alarme : « Alarme manquée : 07:00 Nom » notifiée, et au journal */
static void say_missed(struct alarm *a)
{
	ring_words(a);
	char *e = str_cat(g_line, lang_tr("Alarme manqu\xC3\xA9" "e :"));
	*e++ = ' ';
	str_cat(e, g_text);
	notify_send(lang_tr(s_clock), g_line, 0);
	e = str_cat(g_log, "clockd : manquee ");
	put_nl(str_cat(e, g_text));
	ak_puts(g_log);
}

/* machine éteinte : les alarmes d'une fois passées jusqu'à g_last. Dans les 12 dernières heures :
 * notifiées « manquées » (sans son) ; plus vieilles : désactivées sans rien dire */
static void missed_off(void)
{
	i64 until = g_last;
	int changed = 0;
	for (u64 i = 0; i < al_n; i++) {
		struct alarm *a = &al_tab[i];
		if (!a->on || !a->valid || (a->days & AL_DAYS_ALL) || a->date < 0)
			continue;
		i64 m = a->date * 1440 + (i64)a->hh * 60 + a->mm; /* sa minute */
		i64 s = a->snooze;
		if (s > until) /* (un report encore à venir) */
			continue;
		if (s > m) /* (un report passé aussi) */
			m = s;
		if (m > until)
			continue;
		a->snooze = -1;
		a->on = 0;
		changed = 1;
		if (m < until - MISSED_BACK) /* (trop vieille : éteinte, sans mot) */
			continue;
		if (a->missed >= m) /* (déjà dite) */
			continue;
		a->missed = m;
		say_missed(a);
	}
	if (changed) {
		al_save();
		al_publish();
	}
}

/* ---- sonner */

/* le fond de la carte, l'icône, l'heure, le nom, les boutons */
static void ring_draw(void)
{
	ui_fill(0, 0, RING_W, RING_H, token(TJ_LINE_STRONG));
	ui_rbox(1, 1, RING_W - 2, RING_H - 2, 10, token(TJ_SURFACE), token(TJ_SURFACE));
	ui_icon((long)"clock", 20, 18, 40); /* l'icône de l'Horloge */
	ui_text_fit((long)lang_tr(s_alarm), 72, 34, token(TJ_TEXT2), 13, (long)ui.font, RING_W - 92);
	ui_text_fit((long)g_name, 72, 56, token(TJ_TEXT1), 17, (long)ui.font_b, RING_W - 92);
	long w = ui_text_width((long)g_hm, 52, (long)ui.font_b); /* l'heure, au milieu */
	ui_text((long)g_hm, (long)((u64)(RING_W - w) >> 1), 130, token(TJ_ACCENT), 52, (long)ui.font_b, 0);
	const char *k = lang_tr("Entr\xC3\xA9" "e : r\xC3\xA9p\xC3\xA9ter \xC2\xB7 \xC3\x89" "chap : arr\xC3\xAAter");
	w = ui_text_width((long)k, 12, (long)ui.font); /* Entrée, Échap */
	ui_text((long)k, (long)((u64)(RING_W - w) >> 1), 164, token(TJ_TEXT2), 12, (long)ui.font, 0);
	ui_w_draw(0);
	ui_update_all();
}

/* la carte : au-dessus de tout, au tiers haut de l'écran, deux boutons */
static void ring_open(void)
{
	ui_screen((long)g_screen);
	long x = (g_screen[0] - RING_W) >> 1;
	long y = (g_screen[1] - RING_H) / 3;
	if (y < g_screen[3]) /* (sous la barre de menus) */
		y = g_screen[3];
	if (ui_window_ex(x, y, RING_W, RING_H, (long)lang_tr(s_alarm), WF_FRAMELESS | WF_TOP) < 0)
		return;
	char *e = str_cat(g_line, lang_tr("R\xC3\xA9p\xC3\xA9ter dans")); /* « Répéter dans 10 min » */
	*e++ = ' ';
	e = fmt_dec(al_snz, e);
	str_cat(e, " min");
	g_btn[0] = ui_pushbutton(20, BTN_Y, 200, BTN_H, (long)g_line, UB_DEFAULT);
	/* (comme l'asm : « Arrêter » passé tel quel, sans lang_tr) */
	g_btn[1] = ui_pushbutton(232, BTN_Y, RING_W - 232 - 20, BTN_H, (long)"Arr\xC3\xAAter", 0);
	ring_draw();
}

/* id, minute : la carte, le son, la notification, jusqu'à la réponse (ou 2 minutes) ; puis le
 * fichier */
static void ring(u64 id, i64 min)
{
	g_ring_id = id;
	g_ring_min = min;
	struct alarm *a = al_find((u32)id);
	if (!a)
		return;
	g_sound = a->sound;
	ring_words(a); /* « 07:00 Nom » */
	memcpy(g_hm, g_text, 16); /* (l'heure et le nom, gardés pour la carte) */
	g_hm[5] = 0;
	str_cat(g_name, g_text + 6);
	notify_send(lang_tr(s_clock), g_text, 0); /* la notification d'abord */
	char *e = str_cat(g_log, "clockd : sonne ");
	put_nl(str_cat(e, g_text));
	ak_puts(g_log);
	ring_open();
	gs_open(); /* le son */
	g_snd_tab = al_sound(g_sound, &g_snd_round);
	gs_seq(g_snd_tab);
	u64 start = (u64)sys_time_ms(); /* début */
	u64 round = start;              /* début du tour */
	int how;
	for (;;) {
		long ev = ui_event(20);
		if (ev == WEV_CLOSE) {
			how = R_STOP;
			break;
		}
		if (ev == WEV_THEME)
			ring_draw();
		else if (ev == WEV_KEY) {
			if (*(u32 *)(ui.ev + EV_KIND)) {
				u32 code = *(u32 *)(ui.ev + EV_CODE);
				if (code == KEY_ENTER) {
					how = R_SNOOZE;
					break;
				}
				if (code == KEY_ESC) {
					how = R_STOP;
					break;
				}
			}
		} else if (ev == WEV_BUS) { /* l'Horloge a changé le fichier */
			if (*(u32 *)(ui.ev + BEV_SUB) == (u64)g_sub)
				al_load();
		} else if (ev) {
			struct krt_r4 r = krt_call_r4(ui_w_event, 0, 0, 0, 0);
			if (r.r1 == UN_ACTIVATE) {
				if (r.r0 == g_btn[0]) {
					how = R_SNOOZE;
					break;
				}
				if (r.r0 == g_btn[1]) {
					how = R_STOP;
					break;
				}
			}
		}
		gs_tick();
		u64 now = (u64)sys_time_ms();
		if (now - round >= g_snd_round) { /* un tour de plus */
			round += g_snd_round;
			gs_seq(g_snd_tab);
		}
		if (now - start >= RING_MS) {
			how = R_MISSED;
			break;
		}
		collect(); /* (d'autres alarmes : après) */
	}
	gs_close();
	ui_window_close(0);
	al_load(); /* le fichier tel qu'il est maintenant */
	a = al_find((u32)g_ring_id);
	if (!a)
		return;
	a->snooze = -1;
	a->missed = -1;
	if (how == R_SNOOZE) { /* répétée dans snooze minutes */
		a->snooze = al_now(0) + (i64)al_snz;
		e = str_cat(g_log, "clockd : repetee a ");
		put_nl(al_fmt_min_hm(e, (u64)a->snooze));
		ak_puts(g_log);
	} else {
		if (!(a->days & AL_DAYS_ALL)) /* arrêtée ou manquée : une alarme d'une fois s'éteint */
			a->on = 0;
		if (how == R_MISSED) {
			a->missed = g_ring_min;
			say_missed(a);
		} else
			ak_puts("clockd : arretee\n");
	}
	al_save();
	al_publish();
}

/* un tour : ce qui doit sonner sonne */
static void step(void)
{
	if ((u64)sys_time_ms() < g_grace) { /* pas encore sûr de l'heure : rien */
		g_last = al_now(0);
		return;
	}
	if (!g_mdone) { /* une fois : les manquées, machine éteinte */
		g_mdone = 1;
		missed_off();
	}
	collect();
	while (g_qn) { /* la première de la file */
		u64 id = g_q[0].id;
		i64 min = g_q[0].min;
		g_qn--;
		mem_move(&g_q[0], &g_q[1], g_qn * sizeof g_q[0]);
		ring(id, min);
	}
}

int main(int argc, char **argv)
{
	if (korin_sys2(SYS_SVC_REGISTER, "clockd", 0) < 0) /* un seul clockd */
		return 0;
	ui_init((long)&ui);
	/* la ligne du noyau comme l'asm (x0) ; sans elle (arguments donnés par le père en blob) :
	   les arguments après le nom, remis bout à bout avec des espaces */
	const char *line = krt_line;
	if (!line || !*line) {
		char *e = g_line;
		*e = 0;
		for (int i = 1; i < argc && e < g_line + sizeof g_line - 40; i++) {
			if (i > 1)
				*e++ = ' ';
			u64 n = str_len(argv[i]);
			if (n > 32)
				n = 32;
			memcpy(e, argv[i], n);
			e += n;
			*e = 0;
		}
		line = g_line;
	}
	parse_args(line);
	long s = bus_subscribe((long)al_topic, 0); /* l'Horloge a écrit le fichier */
	g_sub = s > 0 ? s : 0;
	bus_subscribe((long)"korin.clock.offset", 0); /* l'heure réglée : la prochaine alarme
							 recalculée tout de suite */
	reload();
	g_last = al_now(0); /* le passé reste passé */
	for (;;) {
		step();
		long r = bus_wait((long)g_ev, (long)wait_ms());
		if (r == WEV_BUS) {
			if (*(u32 *)(g_ev + BEV_SUB) == (u64)g_sub)
				reload();
		} else if (!g_sub) /* sans busd : relu à chaque réveil */
			al_load();
	}
}
