/*
 * applets/power.cpp -- le volet Énergie du panneau de configuration (batterie du PC, docs/etude/BATTERIE-PC.md).
 *
 * Tout vient de ce que le service acpid a découvert (ACPICA : batteries, adaptateurs, boutons ; CPUID : profils
 * HWP ou EIST), sans rien supposer de la machine : le volet ne montre que ce qui existe. Requête ACPID_OP_GET
 * (lignes « clé=valeur ») : l'état sous la carte, relu toutes les 5 s ; les lignes de la carte selon
 *   * profiles=1 : le profil d'énergie (Automatique, Performance, Équilibré, Économie ; ACPID_OP_PROFILE, acpid
 *     l'écrit lui-même dans /etc/power.ini) ;
 *   * batteries > 0 : « Économie sous » (saver, avec profiles) et « Extinction à » (shutdown) ;
 *   * power_button=1 : ce que fait le bouton d'alimentation (power_button).
 * Ces trois derniers vont dans /etc/power.ini par acpid (ACPID_OP_SET, sans élévation depuis le 25/11/2026). Sans acpid (le Pi, ou pas
 * d'ACPI), le volet le dit. needs : /sys/svc/acpid (le PC seul l'installe).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../control.h"

enum
{
	ACPID_OP_GET = 1,			/* src/svc/acpid/acpid.c */
	ACPID_OP_PROFILE = 2,
	ACPID_OP_RELOAD = 3,
	ACPID_OP_SET = 4,
	MSG_DATA_OFF = 48,			/* (MSG_DATA) */
	PW_MSG = 512,
	PW_LINES = 4,
	PW_REFRESH = 5000,			/* ms */
};

#define PW_FILE		"/etc/power.ini"

/* les choix des listes (valeurs du fichier) */
static const int pw_saver_v[] = { 0, 10, 15, 20, 30 };
static const char *const pw_saver_s[] = { "Jamais", "10 %", "15 %", "20 %", "30 %" };
static const int pw_off_v[] = { 0, 3, 5, 10 };
static const char *const pw_off_s[] = { "Jamais", "3 %", "5 %", "10 %" };
static const char *const pw_btn_v[] = { "dialog", "shutdown", "nothing" };
static const char *const pw_btn_s[] = { "Demander", "\xC3\x89teindre", "Ne rien faire" };
static const char *const pw_prof_s[] = { "Automatique", "Performance", "\xC3\x89quilibr\xC3\xA9", "\xC3\x89" "conomie" };

enum { L_NONE = -1, L_PROFILE, L_SAVER, L_OFF, L_BUTTON };

static long pw_msg[PW_MSG / 8] __attribute__ ((aligned (16)));
static int pw_ok;				/* acpid a répondu */
static int pw_line_kind[PW_LINES];		/* L_* de chaque ligne de la carte */
static int pw_cur[PW_LINES];			/* le choix à l'ouverture */
static long pw_next;				/* prochaine relecture */
static char pw_state[3][96];			/* l'état : trois lignes */
static long pw_pct = -1;
static int pw_bats, pw_charging;

/* la valeur de « clé= » dans la réponse (copie), "" si absente */
static const char *pw_val (const char *key)
{
	static char out[4][48];
	static int k;
	char *o = out[k++ & 3];
	o[0] = 0;
	const char *p = (const char *) pw_msg + MSG_DATA_OFF;
	size_t n = strlen (key);
	while (*p)
	{
		if (!strncmp (p, key, n) && p[n] == '=')
		{
			p += n + 1;
			int i = 0;
			while (*p && *p != '\n' && i < 47)
				o[i++] = *p++;
			o[i] = 0;
			return o;
		}
		while (*p && *p != '\n')
			p++;
		if (*p)
			p++;
	}
	return o;
}

static long pw_num (const char *key, long def)
{
	const char *v = pw_val (key);
	return *v ? atol (v) : def;
}

/* une requête à acpid -> sa réponse (pw_msg[0]), < 0 : pas de service */
static long pw_call (long op, long a, long b = 0)
{
	long ch = svc_open ("acpid", 300);
	if (ch < 0)
		return ch;
	memset (pw_msg, 0, sizeof pw_msg);
	pw_msg[0] = op;
	pw_msg[1] = a;
	pw_msg[2] = b;
	long r = svc_call (ch, pw_msg);
	if (r < 0)
		return r;
	return pw_msg[0];
}

static void pw_time (char *b, size_t n, long min)
{
	if (min < 60)
		snprintf (b, n, "%ld min", min);
	else
		snprintf (b, n, "%ld h %02ld", min / 60, min % 60);
}

/* l'état (GET déjà fait) -> les trois lignes ; -> 1 si elles ont changé */
static int pw_state_text (void)
{
	char old[sizeof pw_state];
	memcpy (old, pw_state, sizeof old);
	memset (pw_state, 0, sizeof pw_state);
	pw_bats = (int) pw_num ("batteries", 0);
	const char *st = pw_val ("state");
	char s = *st ? *st : 'n';
	pw_pct = pw_num ("pct", -1);
	pw_charging = s == 'c';
	long min = pw_num ("min", -1);
	if (s == 'n' || pw_pct < 0)
	{
		pw_pct = -1;
		snprintf (pw_state[0], sizeof pw_state[0], "%s",
			  TR (pw_bats ? "Batterie absente ou illisible" : "Pas de batterie (secteur)"));
	}
	else
	{
		char t[32] = "";
		char *o = pw_state[0];
		size_t n = sizeof pw_state[0];
		int k = snprintf (o, n, "%ld %%", pw_pct);
		if (min >= 0 && (s == 'd' || s == 'c'))
		{
			pw_time (t, sizeof t, min);
			if (s == 'c')
				snprintf (o + k, n - k, " \xE2\x80\x94 %s %s", TR ("pleine dans"), t);
			else
				snprintf (o + k, n - k, " \xE2\x80\x94 %s %s", t, TR ("restantes"));
		}
		snprintf (pw_state[1], sizeof pw_state[1], "%s",
			  TR (s == 'c' ? "Secteur branch\xC3\xA9, en charge" : s == 'f' ? "Secteur branch\xC3\xA9, batterie pleine"
			      : s == 'd' ? "Sur batterie" : "Secteur branch\xC3\xA9"));
		long full = pw_num ("full_mwh", -1), design = pw_num ("design_mwh", -1), cyc = pw_num ("cycles", -1);
		char *q = pw_state[2];
		size_t qn = sizeof pw_state[2];
		int j = 0;
		if (full > 0 && design >= full)
			j += snprintf (q + j, qn - j, "%s %ld %%", TR ("Usure :"), (design - full) * 100 / design);
		if (cyc > 0)
			j += snprintf (q + j, qn - j, "%s%ld %s", j ? " \xC2\xB7 " : "", cyc, TR ("cycles"));
		const char *oem = pw_val ("oem"), *model = pw_val ("model");
		if (*oem || *model)
			snprintf (q + j, qn - j, "%s%s%s%s", j ? " \xC2\xB7 " : "", oem, *oem && *model ? " " : "", model);
	}
	return memcmp (old, pw_state, sizeof old) != 0;
}

static void pw_ini (char *key, char *val, void *)
{
	for (int i = 0; i < PW_LINES; i++)
	{
		if (pw_line_kind[i] == L_SAVER && !strcmp (key, "saver"))
		{
			long v = atol (val);
			for (int c = 0; c < (int) (sizeof pw_saver_v / sizeof pw_saver_v[0]); c++)
				if (pw_saver_v[c] == v)
					pw_cur[i] = c;
		}
		else if (pw_line_kind[i] == L_OFF && !strcmp (key, "shutdown"))
		{
			long v = atol (val);
			for (int c = 0; c < (int) (sizeof pw_off_v / sizeof pw_off_v[0]); c++)
				if (pw_off_v[c] == v)
					pw_cur[i] = c;
		}
		else if (pw_line_kind[i] == L_BUTTON && !strcmp (key, "power_button"))
		{
			for (int c = 0; c < 3; c++)
				if (!strcmp (pw_btn_v[c], val))
					pw_cur[i] = c;
		}
	}
}

/* (ap.draw) la carte, puis l'état dessous : jauge et trois lignes */
static void pw_draw (void)
{
	fm_draw ();
	long x = SIDE_W + AP_MARGIN, y = fm_top () + fm_n * FM_H + (fm_n ? 48 : 0);
	long cw = ui.w - (SIDE_W + 2 * AP_MARGIN);
	if (!pw_ok)
	{
		ui_text ((long) TR ("Le service acpid ne r\xC3\xA9pond pas : pas de gestion d'\xC3\xA9nergie sur cet ordinateur."),
			 x + 2, y + 20, tok (TJ_TEXT2), 13, F (), cw);
		return;
	}
	ui_text ((long) TR ("\xC3\x89tat"), x + 2, y + 16, tok (TJ_TEXT1), 14, FB (), 0);
	y += 28;
	long tx = x + 2;
	if (pw_pct >= 0)			/* la jauge : 120 x 16 */
	{
		enum { GW = 120, GH = 16 };
		ui_rbox (x + 2, y, GW, GH, 4, tok (TJ_LINE), tok (TJ_FACE));
		uint32_t c = pw_charging ? tok (TJ_ACCENT) : pw_pct <= 10 ? tok (TJ_DANGER)
			: pw_pct <= 20 ? tok (TJ_WARNING) : tok (TJ_ACCENT);
		long fw = (GW - 4) * pw_pct / 100;
		if (fw > 0)
			ui_rbox (x + 4, y + 2, fw, GH - 4, 3, c, c);
		tx = x + 2 + GW + 12;
	}
	ui_text ((long) pw_state[0], tx, y + 13, tok (TJ_TEXT1), 14, FB (), cw - (tx - x));
	y += 26;
	for (int i = 1; i < 3; i++)
		if (pw_state[i][0])
		{
			ui_text ((long) pw_state[i], x + 2, y + 13, tok (TJ_TEXT2), 13, F (), cw);
			y += 22;
		}
}

/* (ap.tick) l'état toutes les 5 s -> 1 : à redessiner */
static int pw_tick (void)
{
	if (!pw_ok || sys_ms () < pw_next)
		return 0;
	pw_next = sys_ms () + PW_REFRESH;
	if (pw_call (ACPID_OP_GET, 0) < 0)
		return 0;
	return pw_state_text ();
}

static void pw_open (void)
{
	for (int i = 0; i < PW_LINES; i++)
		pw_line_kind[i] = L_NONE, pw_cur[i] = 0;
	pw_ok = pw_call (ACPID_OP_GET, 0) >= 0;
	int profiles = 0, bats = 0, button = 0, prof = -1, autop = 1;
	if (pw_ok)
	{
		profiles = (int) pw_num ("profiles", 0);
		bats = (int) pw_num ("batteries", 0);
		button = (int) pw_num ("power_button", 0);
		prof = (int) pw_num ("profile", -1);
		autop = (int) pw_num ("auto", 1);
		pw_state_text ();
	}
	pw_next = sys_ms () + PW_REFRESH;
	int n = 0;
	if (profiles)
		pw_line_kind[n++] = L_PROFILE;
	if (bats && profiles)
		pw_line_kind[n++] = L_SAVER;
	if (bats)
		pw_line_kind[n++] = L_OFF;
	if (button)
		pw_line_kind[n++] = L_BUTTON;
	pw_cur[0] = 0;
	for (int i = 0; i < n; i++)		/* les défauts d'acpid (power.ini absent) */
		pw_cur[i] = pw_line_kind[i] == L_SAVER ? 3 : pw_line_kind[i] == L_OFF ? 2 : 0;
	ini_read (PW_FILE, pw_ini);
	fm_begin ("Batterie, profil d'\xC3\xA9nergie des c\xC5\x93urs et bouton d'alimentation.",
		  n ? "Le reste (seuils des alertes, profils sur secteur ou batterie) : /etc/power.ini." : 0);
	for (int i = 0; i < n; i++)
		switch (pw_line_kind[i])
		{
		case L_PROFILE:
			fm_line ("Profil d'\xC3\xA9nergie");
			for (int c = 0; c < 4; c++)
				fm_item (i, pw_prof_s[c], c == 0 ? autop : !autop && prof == c - 1);
			pw_cur[i] = autop ? 0 : prof + 1;
			break;
		case L_SAVER:
			fm_line ("\xC3\x89" "conomie automatique sous");
			for (int c = 0; c < 5; c++)
				fm_item (i, pw_saver_s[c], c == pw_cur[i]);
			break;
		case L_OFF:
			fm_line ("Extinction automatique \xC3\xA0");
			for (int c = 0; c < 4; c++)
				fm_item (i, pw_off_s[c], c == pw_cur[i]);
			break;
		case L_BUTTON:
			fm_line ("Bouton d'alimentation");
			for (int c = 0; c < 3; c++)
				fm_item (i, pw_btn_s[c], c == pw_cur[i]);
			break;
		}
	ap.draw = pw_draw;
	ap.tick = pw_tick;
}

static void pw_apply (void)
{
	if (!pw_ok)
		return;
	int file = 0, err = 0;
	for (long i = 0; i < fm_n; i++)
	{
		long v = fm_value (i);
		if (v == pw_cur[i])
			continue;
		if (pw_line_kind[i] == L_PROFILE)
		{
			if (pw_call (ACPID_OP_PROFILE, v - 1) < 0)
				err = 1;
			else
				pw_cur[i] = (int) v;
		}
		else
			file = 1;
	}
	if (file)	/* (acpid écrit /etc/power.ini pour le compte de la session, sans élévation : 25/11/2026) */
	{
		for (long i = 0; i < fm_n; i++)
		{
			long c = fm_value (i);
			long r = 0;
			if (pw_line_kind[i] == L_SAVER && c >= 0 && c < 5)
				r = pw_call (ACPID_OP_SET, 1, pw_saver_v[c]);
			else if (pw_line_kind[i] == L_OFF && c >= 0 && c < 4)
				r = pw_call (ACPID_OP_SET, 2, pw_off_v[c]);
			else if (pw_line_kind[i] == L_BUTTON && c >= 0 && c < 3)
				r = pw_call (ACPID_OP_SET, 3, c);	/* (pw_btn_v : dialog, shutdown, nothing, comme acpid) */
			else
				continue;
			if (r < 0)
				err = 1;
			else
				pw_cur[i] = (int) c;
		}
	}
	ap.msg = err ? "/etc/power.ini non modifi\xC3\xA9" : "Enregistr\xC3\xA9.";
}

const Applet power_applet = { "power", "\xC3\x89nergie", "\xC3\x89nergie", "control", pw_open, pw_apply,
	"/sys/svc/acpid" };
