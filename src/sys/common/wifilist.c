/* wifilist.c : la liste des réseaux Wi-Fi (balayage dans un thread, tri par signal, une ligne par réseau, la
 * clé masquée et « Se connecter » pour un réseau sécurisé inconnu, connexion), sortie de wifimenu.c (traduit de
 * src/inc/wifilist.inc) le 23/11/2026 pour la partager avec les réglages rapides (quickmenu), qui la déplient dans
 * leur volet (wifilist.h). */
#include "sys_consts.h"
#include "wifidraw.h"
#include "wifilist.h"
#include "uikit_import.h"
#include "netkit_import.h"
#include "buskit_import.h"

#define WL_ROWH 26 /* une ligne */
#define WL_NMAX 8  /* réseaux montrés */
#define WL_KEY_H 34 /* ligne dépliée */
#define WL_FIELD_W 150
#define WL_BW 106
#define WL_HT_MAX 48
#define WL_ENT 48 /* une entrée du balayage : SSID (32), longueur (+32), sécurisé (+34), RSSI (+36) */
#define WIFI_ST_CONNECTED 4 /* src/inc/netd.inc */

static const char wl_s_topic[] = "korin.net.wifi";
static const char wl_s_absent[] = "pas de Wi-Fi";
static const char wl_s_nonco[] = "non connect\xC3\xA9";
static const char wl_s_co[] = "connect\xC3\xA9";
static const char wl_s_recherche[] = "Recherche...";
static const char wl_s_connexion[] = "Connexion...";
static const char wl_s_connecte[] = "Connect\xC3\xA9";
static const char wl_s_refuse[] = "Cl\xC3\xA9 refus\xC3\xA9" "e";
static const char wl_s_echec[] = "Connexion impossible";
static const char wl_s_courte[] = "Cl\xC3\xA9 trop courte";
static const char wl_s_attente[] = "Recherche des r\xC3\xA9seaux...";
static const char wl_s_aucun[] = "Aucun r\xC3\xA9seau visible";
static const char wl_s_seco[] = "Se connecter";


long wl_w = 300; /* largeur de la liste */
long wl_x;        /* son bord gauche dans la fenêtre */
const struct krt_ui *wl_ui; /* l'interface du programme (polices, événement) */
static volatile long wl_job, wl_jdone, wl_jres;
long wl_selrow = -1;
static long wl_keylen, wl_nvis, wl_scanned, wl_compte;
static long wl_sub; /* abonnement à korin.net.wifi (0 : pas de bus) */
static long wl_maj; /* l'état a été publié : à relire */
const char *wl_msgp;
static long wl_nhits;
u8 wl_wstat[64]; /* WIFI_OP_STATUS : état, RSSI, MAC, SSID (+16) */
static i32 wl_hits[WL_HT_MAX][8];
static u8 wl_scan[32 * WL_ENT];
static u8 wl_scan2[32 * WL_ENT];
static u8 wl_known[16 * WL_ENT];
static char wl_keybuf[72];
static char wl_jssid[40];
static char wl_jkey[72];
static char wl_tmp[40];
static char wl_buf[128];
static u8 wl_stack[16384] __attribute__((aligned(16)));

static u32 tj(const u8 *t, long off) { return *(const u32 *)(t + off); }
static const char *tr(const char *s) { return lang_tr(s); }


void wl_status(void) { wifi_status((long)wl_wstat); }

/* travail (1 balayage, 2 connexion à wl_jssid / wl_jkey) : au thread, s'il est libre */
void wl_launch(long job)
{
	if (wl_job)
		return;
	wl_jdone = 0;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	wl_job = job;
}

/* le thread des travaux */
#if defined(__x86_64__)
__attribute__((force_align_arg_pointer))
#endif
static NORETURN void wl_jobs(void)
{
	for (;;) {
		sys_sleep(50);
		long job = wl_job;
		if (!job)
			continue;
		long r;
		if (job == 2)
			r = wifi_join((long)wl_jssid, (long)wl_jkey);
		else
			r = wifi_scan((long)wl_scan2, 32);
		wl_jres = r;
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		wl_jdone = job;
		wl_job = 0;
	}
}

/* scan = 1 : balayer tout de suite. Abonnement, état, thread des travaux -> 0 / < 0 */
long wl_start(long scan)
{
	long s = bus_subscribe((long)wl_s_topic, 0); /* l'état : le bus */
	wl_sub = s > 0 ? s : 0;
	wl_status();
	if (scan)
		wl_launch(1);
	if (korin_sys3(SYS_THREAD_CREATE, wl_jobs, wl_stack + sizeof wl_stack, 0) < 0)
		return -1;
	return 0;
}

/* sub = abonnement d'un WEV_BUS -> 1 si korin.net.wifi (à relire au prochain wl_tick) */
long wl_bus(u64 sub)
{
	if (!wl_sub || sub != (u64)wl_sub)
		return 0;
	wl_maj = 1;
	return 1;
}

/* n = entrées dans wl_scan2 : copiées dans wl_scan, du plus fort au plus faible ;
 * wl_nvis = min(n, WL_NMAX) */
static void wl_sort(u64 n)
{
	mem_move(wl_scan, wl_scan2, n * WL_ENT);
	for (u64 i = 1; i < n; i++) { /* tri par insertion (RSSI décroissant) */
		for (u64 j = i; j; j--) {
			u8 *a = wl_scan + j * WL_ENT, *b = a - WL_ENT;
			if (*(int16_t *)(a + 36) <= *(int16_t *)(b + 36))
				break;
			for (long k = 0; k < WL_ENT; k += 8) { /* échange */
				u64 t = *(u64 *)(a + k);
				*(u64 *)(a + k) = *(u64 *)(b + k);
				*(u64 *)(b + k) = t;
			}
		}
	}
	wl_nvis = (long)(n <= WL_NMAX ? n : WL_NMAX);
	wl_scanned = 1;
}

/* (toutes les 200 ms, et à chaque korin.net.wifi) -> 0 rien, 1 redessiner, 2 connecté (le menu
 * se ferme) */
long wl_tick(void)
{
	long r = 0;
	if (wl_sub) { /* abonné : l'état quand il est publié */
		if (wl_maj) {
			wl_maj = 0;
			wl_status();
			r = 1;
		}
	} else if (++wl_compte >= 5) { /* sans bus : l'état chaque seconde */
		wl_compte = 0;
		wl_status();
		r = 1;
	}
	long done = wl_jdone;
	if (!done)
		return r;
	wl_jdone = 0;
	r = 1;
	long res = wl_jres;
	if (done == 1) { /* balayage : triés par signal */
		u64 n = res < 0 ? 0 : (u64)res <= 32 ? (u64)res : 32;
		wl_sort(n);
		return r;
	}
	wl_status();
	const char *m = tr(wl_s_connecte);
	if (res) {
		m = tr(wl_s_refuse);
		if (res != -13)
			m = tr(wl_s_echec);
	}
	wl_msgp = m;
	return res ? r : 2;
}

/* entrée -> wl_tmp : son SSID terminé */
static void wl_ssid_of(const u8 *e)
{
	u64 n = e[32] <= 32 ? e[32] : 32;
	u64 i;
	for (i = 0; i < n; i++)
		wl_tmp[i] = (char)e[i];
	wl_tmp[i] = 0;
}

/* entrée -> 1 si son SSID est le réseau courant */
static long wl_current(const u8 *e)
{
	if (*(u32 *)wl_wstat != WIFI_ST_CONNECTED)
		return 0;
	wl_ssid_of(e);
	return str_eq(wl_tmp, (const char *)wl_wstat + 16);
}

/* entrée -> 1 si connu (wifi_known) */
static long wl_isknown(const u8 *e)
{
	wl_ssid_of(e);
	long n = wifi_known((long)wl_known, 16);
	if (n <= 0)
		return 0;
	for (const u8 *k = wl_known; n; n--, k += WL_ENT)
		if (str_eq((const char *)k, wl_tmp))
			return 1;
	return 0;
}

/* les zones de clic sont refaites à chaque dessin */
void wl_nhits_reset(void) { wl_nhits = 0; }

/* rectangle, action, argument */
void wl_hit_add(long x, long y, long w, long h, long action, long arg)
{
	if ((u64)wl_nhits >= WL_HT_MAX)
		return;
	i32 *t = wl_hits[wl_nhits++];
	t[0] = (i32)x;
	t[1] = (i32)y;
	t[2] = (i32)w;
	t[3] = (i32)h;
	t[4] = (i32)action;
	t[5] = (i32)arg;
}

/* point -> action (0 : aucune), *arg = argument (la dernière zone posée gagne : elles se
 * recouvrent) */
static long wl_hit_find(long x, long y, long *arg)
{
	const i32 *found = 0;
	for (long i = 0; i < wl_nhits; i++) {
		const i32 *t = wl_hits[i];
		/* (les champs relus en 32 bits non signés, puis comparés signés : comme l'asm) */
		long x0 = (u32)t[0], y0 = (u32)t[1], w = (u32)t[2], h = (u32)t[3];
		if (x < x0 || x >= x0 + w || y < y0 || y >= y0 + h)
			continue;
		found = t;
	}
	if (!found) {
		*arg = 0;
		return 0;
	}
	*arg = (u32)found[5];
	return (u32)found[4];
}

/* -> hauteur de la liste (une ligne au moins : un message) */
long wl_height(void)
{
	long n = wl_nvis < 1 ? 1 : wl_nvis;
	long h = n * WL_ROWH;
	if (wl_selrow >= 0)
		h += WL_KEY_H;
	return h;
}

/* -> l'état en quelques mots (pas de Wi-Fi, connecté, Recherche..., ou le message de la dernière
 * action) */
const char *wl_state_text(void)
{
	u32 st = *(u32 *)wl_wstat;
	const char *s = tr(wl_s_absent);
	if (st) {
		s = tr(wl_s_nonco);
		if (st == WIFI_ST_CONNECTED)
			s = tr(wl_s_co);
	}
	long job = wl_job; /* (ou l'action en cours) */
	const char *a = tr(wl_s_recherche);
	if (job == 1)
		s = a;
	a = tr(wl_s_connexion);
	if (job == 2)
		s = a;
	if (!job && wl_msgp) /* (pas de travail et un message) */
		s = wl_msgp;
	return s;
}

/* y du haut : les lignes (largeur wl_w) -> y sous la liste */
long wl_draw(long y)
{
	const u8 *k = (const u8 *)ui_tokens();
	long w = wl_w, X = wl_x;
	const struct krt_ui *u = wl_ui;
	if (!wl_nvis) { /* pas encore de liste */
		const char *s = tr(wl_s_attente);
		if (wl_scanned)
			s = tr(wl_s_aucun);
		ui_text((long)s, X + 16, y + 17, tj(k, TJ_TEXT2), 12, (long)u->font, w - 32);
		return y + WL_ROWH;
	}
	for (long i = 0; i < wl_nvis; i++) {
		const u8 *e = wl_scan + i * WL_ENT;
		wl_hit_add(X + 8, y, w - 16, WL_ROWH, 1, i);
		if (wl_selrow == i)
			ui_fill(X + 8, y, w - 16, WL_ROWH, tj(k, TJ_ACCENT_LIGHT));
		wd_bars(X + 16, y + 6, wd_level(*(const int16_t *)(e + 36)), tj(k, TJ_TEXT1), tj(k, TJ_LINE_STRONG));
		if (e[34])
			wd_lock(X + 38, y + 8, tj(k, TJ_TEXT2));
		wl_ssid_of(e);
		ui_text((long)wl_tmp, X + 54, y + 18, tj(k, TJ_TEXT1), 13, (long)u->font, w - 100);
		if (wl_current(e))
			wd_check(X + w - 34, y + 9, tj(k, TJ_ACCENT));
		y += WL_ROWH;
		if (wl_selrow != i)
			continue;
		/* ligne dépliée : clé, Se connecter */
		u32 c = tj(k, TJ_LINE_STRONG); /* champ (masqué) */
		ui_rbox(X + 16, y + 4, WL_FIELD_W, 26, 5, c, c);
		c = tj(k, TJ_SUNKEN);
		ui_rbox(X + 17, y + 5, WL_FIELD_W - 2, 24, 4, c, c);
		char *p = wl_buf;
		for (u64 n = (u64)wl_keylen <= 18 ? (u64)wl_keylen : 18; n; n--) { /* « • » par octet */
			*p++ = (char)0xE2;
			*p++ = (char)0x80;
			*p++ = (char)0xA2;
		}
		*p++ = '|';
		*p = 0;
		ui_text((long)wl_buf, X + 24, y + 22, tj(k, TJ_TEXT1), 13, (long)u->font, WL_FIELD_W - 12);
		wl_hit_add(X + w - (WL_BW + 12), y + 3, WL_BW, 28, 2, i); /* Se connecter */
		ui_button(X + w - (WL_BW + 12), y + 3, WL_BW, 28, (long)tr(wl_s_seco), 1);
		y += WL_KEY_H;
	}
	return y;
}

/* la ligne dépliée row : Se connecter */
static void wl_connect_sel(long row)
{
	if ((u64)wl_keylen < 8) {
		wl_msgp = tr(wl_s_courte);
		return;
	}
	wl_ssid_of(wl_scan + row * WL_ENT);
	wl_jssid[0] = 0;
	str_cat(wl_jssid, wl_tmp);
	wl_jkey[0] = 0;
	str_cat(wl_jkey, wl_keybuf);
	wl_selrow = -1;
	wl_launch(2);
}

/* clic -> 0 rien, 1 redessiner ; une zone du programme (action >= 3) : l'action, *arg son
 * argument */
long wl_click(long x, long y, long *arg)
{
	long a;
	long act = wl_hit_find(x, y, &a);
	if (!act)
		return 0;
	if (act == 1) { /* une ligne : connu ou ouvert, tout de suite */
		const u8 *e = wl_scan + a * WL_ENT;
		if (e[34] && !wl_isknown(e)) { /* sinon : la clé */
			wl_selrow = a;
			wl_keylen = 0;
			return 1;
		}
		wl_ssid_of(e);
		wl_jssid[0] = 0;
		str_cat(wl_jssid, wl_tmp);
		wl_jkey[0] = 0;
		wl_launch(2);
		return 1;
	}
	if (act == 2) {
		wl_connect_sel(a);
		return 1;
	}
	*arg = a; /* (au programme) */
	return act;
}

/* événement clavier (wl_ui->ev) -> 1 si pris (champ de la clé) */
long wl_key(void)
{
	long row = wl_selrow;
	if (row < 0)
		return 0;
	u32 code = *(u32 *)(wl_ui->ev + EV_CODE);
	if (code == KEY_ENTER) { /* comme Se connecter */
		wl_connect_sel(row);
		return 1;
	}
	if (code == KEY_BACKSPACE) {
		u64 n = (u64)wl_keylen;
		if (!n)
			return 1;
		while (--n && ((u8)wl_keybuf[n] & 0xC0) == 0x80)
			;
		wl_keylen = (long)n;
		wl_keybuf[n] = 0;
		return 1;
	}
	u32 ch = *(u32 *)(wl_ui->ev + EV_CHAR);
	if (ch < 32)
		return 0;
	u64 n = (u64)wl_keylen;
	if (n >= 63)
		return 1;
	n += (u64)utf8_put(ch, wl_keybuf + n);
	if (n > 63)
		return 1;
	wl_keylen = (long)n;
	wl_keybuf[n] = 0;
	return 1;
}

