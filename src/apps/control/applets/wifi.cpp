/*
 * applets/wifi.cpp -- le volet Wi-Fi du panneau de configuration (docs/RESEAU.md 4.5), dessiné par lui-même (ap.draw,
 * ap.click) et rafraîchi chaque seconde (ap.tick : les travaux du thread) ; l'état est relu quand netd ou le pilote
 * le publient sur le bus (korin.net.*, ap_net_update), sans busd chaque seconde :
 *   * État : réseau, adresse, passerelle, signal ; bouton Déconnecter ;
 *   * Réseaux visibles (balayage dans un thread : l'interface ne gèle pas ; bouton du bas : Actualiser) : signal,
 *     cadenas, coche sur le réseau courant ; un clic déplie la ligne : champ de clé (masqué) si le réseau est inconnu
 *     et sécurisé, Se connecter (mémorisé) ;
 *   * Réseaux connus : priorité (- / +), Oublier.
 * Tout passe par NetKit (service « wifi ») ; sans Wi-Fi : « Pas de Wi-Fi ».
 *
 * Port en C++ de applets/wifi.inc et de src/inc/wifidraw.inc (Korin ; copie locale de src/sys/common/wifidraw.c,
 * écrit pour korinrt). Onyx avait deux apps pour cela (user/Apps/wpaconf : réseaux connus et clés,
 * user/Apps/wifimenu : balayage et connexion, sur kapi_wifi_*) ; Korin les a réunies dans ce volet, que le port
 * reprend tel que l'asm le montre, avec le thread des travaux d'Onyx/Setup (pthread au lieu de SYS_THREAD_CREATE).
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <netkit_abi.h>
#include "../control.h"

enum
{
	WIFI_ST_ABSENT = 0,			/* états (src/inc/netd.inc, WIFI_OP_STATUS) */
	WIFI_ST_START = 1,
	WIFI_ST_READY = 2,
	WIFI_ST_JOINING = 3,
	WIFI_ST_CONNECTED = 4,
	WF_VIS = 6,				/* réseaux visibles montrés */
	WF_KN = 5,				/* réseaux connus montrés */
	WF_RH = 24,				/* hauteur d'une ligne */
	WF_HT_MAX = 40,				/* zones cliquables */
	WF_FIELD_W = 200,
	WF_SCAN_MAX = 32,
	WF_KNOWN_MAX = 16,
	WF_ENT = 48,				/* entrée de balayage : SSID (32), +32 longueur, +34 sécurisé, +36 signal
						 * (16 bits) ; entrée connue : SSID terminé, +40 priorité */
	WF_KEY_MAX = 63,			/* WPA : 8 à 63 caractères */
};

static const char wf_s_absent[] = "Pas de Wi-Fi sur cet appareil";
static const char wf_s_note[] = "La puce Wi-Fi ne r\xC3\xA9pond pas (ou QEMU) : voir le journal.";
static const char wf_s_start[] = "Mise en route du Wi-Fi...";
static const char wf_s_notconn[] = "Non connect\xC3\xA9";
static const char wf_s_joining[] = "Connexion en cours...";
static const char wf_s_conn_to[] = "Connect\xC3\xA9 \xC3\xA0 \xC2\xAB ";
static const char wf_s_quote[] = " \xC2\xBB";
static const char wf_s_disc[] = "D\xC3\xA9" "connecter";
static const char wf_s_addr[] = "Adresse ";
static const char wf_s_gw[] = " \xC2\xB7 passerelle ";
static const char wf_s_dot[] = " \xC2\xB7 ";
static const char wf_s_signal[] = "signal ";
static const char wf_s_visible[] = "R\xC3\xA9seaux visibles";
static const char wf_s_searching[] = "Recherche...";
static const char wf_s_no_vis[] = "Aucun r\xC3\xA9seau visible (Actualiser).";
static const char wf_s_known[] = "R\xC3\xA9seaux connus";
static const char wf_s_no_known[] = "Aucun r\xC3\xA9seau m\xC3\xA9moris\xC3\xA9.";
static const char wf_s_prio[] = "priorit\xC3\xA9 ";
static const char wf_s_forget[] = "Oublier";
static const char wf_s_join[] = "Se connecter";
static const char wf_s_key[] = "cl\xC3\xA9";
static const char wf_s_connecting[] = "Connexion...";
static const char wf_s_connected[] = "Connect\xC3\xA9.";
static const char wf_s_refused[] = "Cl\xC3\xA9 refus\xC3\xA9" "e.";
static const char wf_s_notfound[] = "R\xC3\xA9seau introuvable.";
static const char wf_s_timeout[] = "D\xC3\xA9lai d'attente d\xC3\xA9pass\xC3\xA9.";
static const char wf_s_failed[] = "Connexion impossible.";
static const char wf_s_short[] = "Cl\xC3\xA9 trop courte (8 caract\xC3\xA8res au moins).";
static const char wf_s_left[] = "D\xC3\xA9" "connect\xC3\xA9.";

/* les actions des zones cliquables */
enum { HIT_LEAVE = 1, HIT_ROW = 3, HIT_JOIN = 4, HIT_MINUS = 5, HIT_PLUS = 6, HIT_FORGET = 7 };
struct Hit { int x, y, w, h, action, arg; };

/* les travaux du thread : wf_job demandé (1 balayage, 2 connexion), wf_done fini (son genre), wf_res son résultat */
static volatile long wf_job, wf_done, wf_res;
static bool wf_thread_ok;
static long wf_sel = -1;			/* ligne visible dépliée */
static long wf_keylen, wf_scan_n, wf_known_n, wf_nhits;
static unsigned char wf_st[64];			/* wifi_status : +0 état, +4 signal (dBm), +16 SSID */
static unsigned char wf_ni[32];			/* net_info : +0 adresse, +8 passerelle */
static Hit wf_hits[WF_HT_MAX];
static unsigned char wf_scan[WF_SCAN_MAX * WF_ENT];
static unsigned char wf_scan2[WF_SCAN_MAX * WF_ENT];	/* (le thread y balaie : l'interface lit wf_scan) */
static unsigned char wf_known[WF_KNOWN_MAX * WF_ENT];
static char wf_key[72], wf_jssid[40], wf_jkey[72], wf_tmp[40], wf_buf[160];

static inline uint32_t st_state (void) { return *(const uint32_t *) wf_st; }

/* ---- src/inc/wifidraw.inc ---- */

/* signal (dBm) -> barres allumées, 1 à 4 */
static long wd_level (long rssi)
{
	long r = (int32_t) rssi;
	if (r >= -55)
		return 4;
	if (r >= -65)
		return 3;
	if (r >= -75)
		return 2;
	return 1;
}

/* quatre barres de 3 px (hauteurs 4, 7, 10, 13), les lit premières en on */
static void wd_bars (long x, long top, long lit, uint32_t on, uint32_t off)
{
	for (unsigned long i = 0; i < 4; i++)
	{
		long h = 4 + 3 * (long) i;
		ui_fill (x + 4 * (long) i, top + 13 - h, 3, h, i < (unsigned long) lit ? on : off);
	}
}

/* un cadenas de 8 x 10 */
static void wd_lock (long x, long top, uint32_t color)
{
	ui_fill (x + 2, top, 4, 1, color);	/* anse : un cadre de 4 x 5 */
	ui_fill (x + 1, top + 1, 1, 4, color);
	ui_fill (x + 6, top + 1, 1, 4, color);
	ui_fill (x, top + 5, 8, 5, color);	/* corps : 8 x 5 */
}

/* une coche de 11 x 8 */
static void wd_check (long x, long top, uint32_t color)
{
	for (long i = 0; i < 3; i++)		/* branche courte : vers le bas */
		ui_fill (x + i, top + 3 + i, 2, 2, color);
	for (long i = 0; i < 6; i++)		/* branche longue : vers le haut */
		ui_fill (x + 3 + i, top + 5 - i, 2, 2, color);
}

/* ---- le thread des travaux : balayage (3 s), connexion (jusqu'à 20 s) ---- */

static void *wf_worker (void *)
{
	for (;;)
	{
		sys_sleep (50);
		long job = wf_job;
		if (!job)
			continue;
		long r;
		if (job == 2)
			r = wifi_join_ex ((long) wf_jssid, (long) wf_jkey, 1, 0);	/* mémorisé */
		else
			r = wifi_scan ((long) wf_scan2, WF_SCAN_MAX);
		wf_res = r;
		__atomic_thread_fence (__ATOMIC_SEQ_CST);
		wf_done = job;
		wf_job = 0;
	}
	return 0;
}

/* job = 1 balayage, 2 connexion : au thread, s'il est libre */
static void wf_start (long job)
{
	if (wf_job)
		return;
	wf_done = 0;
	__atomic_thread_fence (__ATOMIC_SEQ_CST);
	wf_job = job;
}

static void wf_read_state (void)
{
	wifi_status ((long) wf_st);
	net_info ((long) wf_ni);
}

static void wf_read_known (void)
{
	long n = wifi_known ((long) wf_known, WF_KNOWN_MAX);
	wf_known_n = n < 0 ? 0 : n;
}

/* (ap.tick) chaque seconde, et à chaque korin.net.* -> 1 (redessiner) */
static int wf_tick (void)
{
	if (!ap_net_sub)			/* pas de bus : l'état chaque seconde */
		wf_read_state ();
	else if (ap_net_update)			/* abonné : l'état quand il est publié */
	{
		ap_net_update = 0;
		wf_read_state ();
	}
	long done = wf_done;			/* un travail fini ? */
	if (!done)
		return 1;
	wf_done = 0;
	long r = wf_res;
	if (done == 1)				/* balayage : la liste */
	{
		if (r >= 0)
		{
			wf_scan_n = r > WF_SCAN_MAX ? (long) WF_SCAN_MAX : r;
			memmove (wf_scan, wf_scan2, wf_scan_n * WF_ENT);
		}
		return 1;
	}
	ap.msg = !r ? wf_s_connected : r == -13 ? wf_s_refused : r == -2 ? wf_s_notfound	/* connexion : le message */
	       : r == -110 ? wf_s_timeout : wf_s_failed;
	if (!r)
		wf_sel = -1;			/* connecté : la ligne se replie */
	wf_read_known ();
	return 1;
}

static void wf_hit_add (long x, long y, long w, long h, int action, long arg)
{
	if (wf_nhits >= WF_HT_MAX)
		return;
	wf_hits[wf_nhits++] = { (int) x, (int) y, (int) w, (int) h, action, (int) arg };
}

/* entrée de balayage -> 1 si son SSID est le réseau courant */
static int wf_current (const unsigned char *e)
{
	if (st_state () != WIFI_ST_CONNECTED)
		return 0;
	const unsigned char *s = wf_st + 16;
	long n = e[32];				/* longueur du SSID */
	for (long i = 0; i < n; i++)
		if (e[i] != s[i])
			return 0;
	return s[n] == 0;
}

/* SSID (terminé) -> 1 s'il est connu */
static int wf_is_known (const char *ssid)
{
	for (long i = 0; i < wf_known_n; i++)
		if (!strcmp ((const char *) wf_known + i * WF_ENT, ssid))
			return 1;
	return 0;
}

/* entrée de balayage -> wf_tmp : son SSID terminé par 0 */
static void wf_ssid_of (const unsigned char *e)
{
	long n = e[32] > 32 ? 32 : e[32];
	memcpy (wf_tmp, e, n);
	wf_tmp[n] = 0;
}

/* entrée de balayage -> 1 s'il faut une clé (sécurisé, inconnu) */
static int wf_needs_key (const unsigned char *e)
{
	if (!e[34])
		return 0;
	wf_ssid_of (e);
	return !wf_is_known (wf_tmp);
}

/* ---- le dessin ---- */

/* titre de section : texte gras et trait */
static void wf_section (const char *title, long x, long y, long w)
{
	ui_text ((long) TR (title), x, y + 14, tok (TJ_TEXT1), 13, FB (), 0);
	ui_fill (x, y + 20, w, 1, tok (TJ_LINE));
}

/* petit bouton de 20 x 18 (« - », « + ») sur la ligne y */
static void wf_small (long x, long y, const char *text, long arg, int action)
{
	wf_hit_add (x, y + 3, 20, 18, action, arg);
	ui_rbox (x, y + 3, 20, 18, 4, tok (TJ_RAISED), tok (TJ_RAISED_LOW));
	ui_text ((long) text, x + 6, y + 17, tok (TJ_TEXT1), 13, FB (), 0);
}

static void wf_draw (void)
{
	wf_nhits = 0;
	long x = SIDE_W + AP_MARGIN, y = AP_HEAD + 4, w = ui.w - (SIDE_W + 2 * AP_MARGIN);
	uint32_t st = st_state ();
	/* --- état --- */
	const char *s;
	if (st == WIFI_ST_ABSENT)
		s = wf_s_absent;
	else if (st == WIFI_ST_START)
		s = wf_s_start;
	else if (st == WIFI_ST_READY)
		s = wf_s_notconn;
	else if (st == WIFI_ST_JOINING)
		s = wf_s_joining;
	else				/* « Connecté à « X » » */
	{
		wf_buf[0] = 0;
		strcat (tr_cat (wf_buf, wf_s_conn_to), (const char *) wf_st + 16);
		tr_cat (wf_buf, wf_s_quote);
		s = wf_buf;
	}
	ui_text ((long) TR (s), x, y + 16, tok (TJ_TEXT1), 14, FB (), w - BTN_W - 8);
	if (st == WIFI_ST_CONNECTED)
	{
		ap_button (x + w - BTN_W, y, wf_s_disc, 0);	/* Déconnecter */
		wf_hit_add (x + w - BTN_W, y, BTN_W, BTN_H, HIT_LEAVE, 0);
		char *p = wf_buf;		/* adresse, passerelle, signal */
		*p = 0;
		uint32_t ip = *(const uint32_t *) wf_ni;
		if (ip)
		{
			p = tr_cat (p, wf_s_addr);
			p = (char *) fmt_ip (ip, (long) p);
			*p = 0;
			p = tr_cat (p, wf_s_gw);
			p = (char *) fmt_ip (*(const uint32_t *) (wf_ni + 8), (long) p);
			*p = 0;
			strcat (p, wf_s_dot);
		}
		p = tr_cat (p, wf_s_signal);
		sprintf (p, "%ld dBm", (long) *(const int32_t *) (wf_st + 4));
		ui_text ((long) wf_buf, x, y + 38, tok (TJ_TEXT2), 12, F (), w);
	}
	y += 52;
	if (st < WIFI_ST_READY)			/* pas de Wi-Fi : une note */
	{
		ui_text ((long) TR (wf_s_note), x, y + 4, tok (TJ_TEXT2), 12, F (), w);
		return;
	}
	/* --- réseaux visibles --- */
	wf_section (wf_s_visible, x, y, w);
	if (wf_job == 1)			/* balayage en cours */
		ui_text ((long) TR (wf_s_searching), x + w - 90, y + 14, tok (TJ_TEXT3), 12, F (), 0);
	y += 24;
	long i = 0;
	for (; i < wf_scan_n && i < WF_VIS; i++)
	{
		const unsigned char *e = wf_scan + i * WF_ENT;
		wf_hit_add (x, y, w, WF_RH, HIT_ROW, i);
		if (wf_sel == i)		/* choisie : fond accentué */
			ui_fill (x, y, w, WF_RH, tok (TJ_ACCENT_LIGHT));
		wd_bars (x + 4, y + 5, wd_level (*(const int16_t *) (e + 36)), tok (TJ_TEXT1), tok (TJ_LINE_STRONG));
		if (e[34])			/* cadenas */
			wd_lock (x + 24, y + 7, tok (TJ_TEXT2));
		wf_ssid_of (e);
		ui_text ((long) wf_tmp, x + 40, y + 17, tok (TJ_TEXT1), 13, F (), w - 70);
		if (wf_current (e))		/* coche : le réseau courant */
			wd_check (x + w - 22, y + 8, tok (TJ_ACCENT));
		y += WF_RH;
		if (wf_sel != i)
			continue;
		if (wf_needs_key (e))		/* ligne dépliée : champ de la clé (masqué) */
		{
			ui_rbox (x + 40, y + 3, WF_FIELD_W, 26, 5, tok (TJ_LINE_STRONG), tok (TJ_LINE_STRONG));
			ui_rbox (x + 41, y + 4, WF_FIELD_W - 2, 24, 4, tok (TJ_SUNKEN), tok (TJ_SUNKEN));
			char *p = wf_buf;	/* une puce par octet (comme l'asm), puis le curseur */
			for (long k = wf_keylen > 24 ? 24 : wf_keylen; k; k--)
			{
				memcpy (p, "\xE2\x80\xA2", 3);	/* U+2022 */
				p += 3;
			}
			*p++ = '|';
			*p = 0;
			ui_text ((long) wf_buf, x + 48, y + 21, tok (TJ_TEXT1), 13, F (), WF_FIELD_W - 12);
			ui_text ((long) TR (wf_s_key), x + 40 + WF_FIELD_W + 8, y + 21, tok (TJ_TEXT3), 12, F (), 0);
		}
		ap_button (x + w - BTN_W, y + 2, wf_s_join, 1);	/* Se connecter */
		wf_hit_add (x + w - BTN_W, y + 2, BTN_W, BTN_H, HIT_JOIN, i);
		y += 34;
	}
	if (!i)
	{
		ui_text ((long) TR (wf_s_no_vis), x, y + 16, tok (TJ_TEXT2), 12, F (), w);
		y += WF_RH;
	}
	/* --- réseaux connus --- */
	y += 8;
	wf_section (wf_s_known, x, y, w);
	y += 24;
	for (i = 0; i < wf_known_n && i < WF_KN; i++)
	{
		const unsigned char *e = wf_known + i * WF_ENT;
		ui_text ((long) e, x + 4, y + 17, tok (TJ_TEXT1), 13, F (), w - 230);	/* SSID */
		wf_buf[0] = 0;			/* « priorité N » */
		sprintf (tr_cat (wf_buf, wf_s_prio), "%u", *(const uint32_t *) (e + 40));
		ui_text ((long) wf_buf, x + w - 222, y + 17, tok (TJ_TEXT2), 12, F (), 70);
		long bx = x + w - 146;		/* - et + */
		wf_small (bx, y, "-", i, HIT_MINUS);
		wf_small (bx + 26, y, "+", i, HIT_PLUS);
		wf_hit_add (x + w - BTN_W, y + 1, BTN_W, 22, HIT_FORGET, i);	/* Oublier */
		ui_text ((long) TR (wf_s_forget), x + w - (BTN_W - 14), y + 17, tok (TJ_DANGER), 13, F (), 0);
		y += WF_RH;
	}
	if (!i)
		ui_text ((long) TR (wf_s_no_known), x, y + 16, tok (TJ_TEXT2), 12, F (), w);
}

/* ---- clics et clavier ---- */

/* ligne visible : connexion (dans le thread) avec la clé saisie */
static void wf_connect (long row)
{
	const unsigned char *e = wf_scan + row * WF_ENT;
	if (wf_job)				/* un travail en cours : plus tard */
		return;
	wf_ssid_of (e);
	strcpy (wf_jssid, wf_tmp);
	wf_jkey[0] = 0;				/* la clé saisie (vide : connu ou ouvert) */
	if (wf_needs_key (e))
	{
		if (wf_keylen < 8)		/* WPA : 8 à 63 caractères */
		{
			ap.msg = wf_s_short;
			return;
		}
		strcpy (wf_jkey, wf_key);
	}
	ap.msg = wf_s_connecting;
	wf_start (2);
}

/* clic -> 1 si à redessiner (le cadre efface alors le message : « Déconnecté. », « Connexion... » ne se voient pas,
 * comme dans l'asm) */
static int wf_click (long x, long y)
{
	for (long i = 0; i < wf_nhits; i++)
	{
		const Hit &h = wf_hits[i];
		if (x < h.x || x >= h.x + h.w || y < h.y || y >= h.y + h.h)
			continue;
		unsigned char *k = wf_known + h.arg * WF_ENT;
		switch (h.action)
		{
		case HIT_LEAVE:
			wifi_leave ();
			ap.msg = wf_s_left;
			return 1;
		case HIT_ROW:			/* une autre ligne : clé effacée */
			if (wf_sel != h.arg)
			{
				wf_sel = h.arg;
				wf_keylen = 0;
				wf_key[0] = 0;
			}
			return 1;
		case HIT_JOIN:
			wf_connect (h.arg);
			return 1;
		case HIT_MINUS:
		case HIT_PLUS:
		{
			long p = (long) *(const int32_t *) (k + 40) + (h.action == HIT_PLUS ? 1 : -1);
			wifi_priority ((long) k, p < 0 ? 0 : p);
			wf_read_known ();
			return 1;
		}
		case HIT_FORGET:
			wifi_forget ((long) k);
			wf_read_known ();
			return 1;
		}
		return 0;
	}
	return 0;
}

/* c -> octets UTF-8 dans out (1 à 4) */
static long utf8_put (unsigned long c, char *out)
{
	if (c < 0x80)
	{
		out[0] = (char) c;
		return 1;
	}
	if (c < 0x800)
	{
		out[0] = (char) (0xC0 | (c >> 6));
		out[1] = (char) (0x80 | (c & 63));
		return 2;
	}
	if (c < 0x10000)
	{
		out[0] = (char) (0xE0 | (c >> 12));
		out[1] = (char) (0x80 | ((c >> 6) & 63));
		out[2] = (char) (0x80 | (c & 63));
		return 3;
	}
	out[0] = (char) (0xF0 | (c >> 18));
	out[1] = (char) (0x80 | ((c >> 12) & 63));
	out[2] = (char) (0x80 | ((c >> 6) & 63));
	out[3] = (char) (0x80 | (c & 63));
	return 4;
}

/* (ap.key) -> 1 si prise (le champ de la clé ; Entrée : Se connecter) */
static int wf_key_ev (void)
{
	if (wf_sel < 0)				/* une ligne dépliée ? */
		return 0;
	long code = ui_ev_field (&ui, EV_CODE);
	if (code == KEY_ENTER)
	{
		wf_connect (wf_sel);
		return 1;
	}
	if (!wf_needs_key (wf_scan + wf_sel * WF_ENT))
		return 0;
	if (code == KEY_BACKSPACE)		/* (dernier caractère UTF-8) */
	{
		long l = wf_keylen;
		if (!l)
			return 1;
		while (--l && ((unsigned char) wf_key[l] & 0xC0) == 0x80)
			;
		wf_keylen = l;
		wf_key[l] = 0;
		return 1;
	}
	unsigned long c = (unsigned long) (unsigned) ui_ev_field (&ui, EV_CHAR);	/* un caractère */
	if (c < 32)
		return 0;
	char tmp[4];				/* (octets ; trop long : refusé) */
	long n = utf8_put (c, tmp);
	if (wf_keylen + n > WF_KEY_MAX)
		return 1;
	memcpy (wf_key + wf_keylen, tmp, n);
	wf_keylen += n;
	wf_key[wf_keylen] = 0;
	return 1;
}

/* ---- le volet ---- */

static void wf_open (void)
{
	ap.draw = wf_draw;
	ap.click = wf_click;
	ap.tick = wf_tick;
	ap.key = wf_key_ev;
	ap.button = "Actualiser";
	wf_sel = -1;
	wf_keylen = 0;
	wf_key[0] = 0;
	wf_read_state ();
	wf_read_known ();
	if (!wf_thread_ok)			/* le thread des travaux (une fois) */
	{
		pthread_t t;
		if (pthread_create (&t, 0, wf_worker, 0) == 0)
		{
			pthread_detach (t);
			wf_thread_ok = true;
		}
	}
	if (st_state () >= WIFI_ST_READY)	/* Wi-Fi présent : un balayage */
		wf_start (1);
}

/* Actualiser (bouton du bas) */
static void wf_apply (void) { wf_start (1); }

const Applet wifi_applet = { "wifi", "Wi-Fi", "Wi-Fi", "wifi", wf_open, wf_apply };
