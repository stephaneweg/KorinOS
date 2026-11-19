/* shutdown - le dialogue de fin de session (/sys/shutdown, lancé par le bouton
 * d'alimentation du panel et du dock ; comme le shutdown d'Onyx) : Redémarrer, Éteindre,
 * Annuler (ou Échap, ou la case de fermeture). Les deux premiers demandent à init2
 * (INIT_OP_REBOOT), qui laisse finir les écritures (PC : prévient les programmes, démonte les
 * volumes, arrête le disque). Déjà ouvert : sa fenêtre revient devant.
 *
 * Traduit ligne à ligne de shutdown.asm (un seul source pour le Pi et le PC) ; seul écart : le
 * texte d'échec dit « le PC » sur le PC. */
#include "sys_consts.h"
#include "uikit_import.h"

#define W 500
#define H 150
#define BTN_W 104
#define BTN_H 30
#define BTN_PAS 112

static struct krt_ui ui;
static long scr[4];
static long w_note, w_restart, w_off, w_cancel, w_logout;
static u8 wlist[WL_PER_MSG * WL_SIZE];
static u64 msg[MSG_SIZE / 8];

static const char s_name[] = "shutdown";
static const char s_title[] = "\xC3\x89teindre";
static const char s_question[] = "Voulez-vous \xC3\xA9teindre Korin OS ?";
static const char s_note[] = "Les documents ouverts ne sont pas enregistr\xC3\xA9s automatiquement.";
static const char s_restart[] = "Red\xC3\xA9marrer";
static const char s_off[] = "\xC3\x89teindre";
static const char s_cancel[] = "Annuler";
static const char s_logout[] = "Fermer la session";
static const char s_logging_out[] = "Fermeture de la session...";
static const char s_logout_failed[] = "La session n'a pas pu \xC3\xAAtre ferm\xC3\xA9" "e (init2).";
static const char s_restarting[] = "Red\xC3\xA9marrage...";
static const char s_shutting[] = "Extinction...";
#ifdef __x86_64__
static const char s_failed[] = "init2 ne r\xC3\xA9pond pas : le PC tourne toujours.";
#else
static const char s_failed[] = "init2 ne r\xC3\xA9pond pas : le Pi tourne toujours.";
#endif

static u32 token(long off)
{
	return *(const u32 *)((const u8 *)ui_tokens() + off);
}

/* la fenêtre : le fond, le trait au-dessus des boutons, les widgets */
static void draw(void)
{
	ui_fill(0, 0, W, H, token(TJ_FACE));
	ui_fill(12, H - 56, W - 24, 1, token(TJ_LINE));
	ui_w_draw(0);
	ui_update_all();
}

/* flags de INIT_OP_REBOOT, texte : le dit, le demande à init2 ; ne revient que si init2 n'a
 * rien fait (le texte dit alors l'échec) */
static void finish(long flags, const char *text)
{
	ui_w_set_text(w_note, (long)lang_tr(text));
	draw();
	sys_sleep(400); /* (le temps de le voir) */
	long ch = korin_sys2(SYS_SVC_OPEN, "init2", 1000);
	if (ch >= 0) {
		msg[0] = INIT_OP_REBOOT;
		msg[1] = (u64)flags;
		*(u32 *)((u8 *)msg + MSG_LEN) = 0;
		if (korin_sys2(SYS_SVC_CALL, ch, msg) >= 0)
			sys_sleep(5000); /* (la machine s'arrête pendant ce temps) */
	}
	ui_w_set_text(w_note, (long)lang_tr(s_failed));
	draw();
}

/* Fermer la session (INIT_OP_LOGOUT d'init2) : ses programmes s'arrêtent, celui-ci compris, puis l'écran de
 * connexion revient ; ne revient que si init2 a refusé */
static void logout(void)
{
	ui_w_set_text(w_note, (long)lang_tr(s_logging_out));
	draw();
	long ch = korin_sys2(SYS_SVC_OPEN, "init2", 1000);
	if (ch >= 0) {
		msg[0] = INIT_OP_LOGOUT;
		msg[1] = 0;
		*(u32 *)((u8 *)msg + MSG_LEN) = 0;
		if (korin_sys2(SYS_SVC_CALL, ch, msg) >= 0 && msg[0] == 0)
			sys_sleep(5000); /* (init2 arrête la session, ce programme compris) */
	}
	ui_w_set_text(w_note, (long)lang_tr(s_logout_failed));
	draw();
}

/* -> 1 si un autre shutdown tourne (sa fenêtre vient devant) */
static int already_open(void)
{
	long me = korin_sys0(SYS_GETPID), other = 0;
	for (long from = 0;;) {
		long p = proc_find(s_name, from); /* (procfind.inc, korinrt) */
		if (!p)
			return 0;
		from = p;
		if (p != me) {
			other = p;
			break;
		}
	}
	for (long first = 0;; first += WL_PER_MSG) { /* ses fenêtres : la première */
		long n = ui_winlist(first, (long)wlist);
		if (n <= first)
			return 1;
		n -= first;
		if (n > WL_PER_MSG)
			n = WL_PER_MSG;
		for (long k = 0; k < n; k++) {
			u8 *e = wlist + k * WL_SIZE;
			if ((long)M32(e, WL_PID) == other) {
				ui_raise((long)M64(e, WL_ID));
				return 1;
			}
		}
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ui_init((long)&ui);
	if (already_open())
		return 0;
	long x = -1, y = -1; /* au milieu de l'écran, un peu plus haut */
	ui_screen((long)scr);
	if (scr[0]) {
		x = (scr[0] - W) >> 1;
		y = ((scr[1] - H) >> 1) - 40;
	}
	/* un dialogue (en mode poche : centré, sur un voile), hors des listes */
	if (ui_window_ex(x, y, W, H, (long)lang_tr(s_title), WF_MODAL | WF_NOLIST) < 0)
		return 0;
	ui_label(18, 12, W - 36, 20, (long)lang_tr(s_question), ULB_BOLD);
	w_note = ui_label(18, 38, W - 36, 20, (long)lang_tr(s_note), ULB_DIM);
	w_restart = ui_pushbutton(W - 3 * BTN_PAS, H - 44, BTN_W, BTN_H, (long)lang_tr(s_restart), 0);
	w_off = ui_pushbutton(W - 2 * BTN_PAS, H - 44, BTN_W, BTN_H, (long)lang_tr(s_off), UB_DEFAULT);
	w_cancel = ui_pushbutton(W - 1 * BTN_PAS, H - 44, BTN_W, BTN_H, (long)lang_tr(s_cancel), 0);
	long st[2]; /* (un écran de connexion, une session ouverte par lui : le PC avec des comptes) */
	w_logout = 0;
	if (korin_sys0(SYS_GETUID) > 0 && sys_stat("/sys/login", st) >= 0 && sys_stat("/tmp/session.ini", st) >= 0)
		w_logout = ui_pushbutton(12, H - 44, W - 3 * BTN_PAS - 24, BTN_H, (long)lang_tr(s_logout), 0);
	draw();
	for (;;) {
		long ev = ui_event(1000);
		if (!ev) {
			ui_w_idle();
			continue;
		}
		if (ev == WEV_CLOSE)
			return 0;
		struct krt_r4 r = krt_call_r4(ui_w_event, 0, 0, 0, 0);
		if (r.r1 == UN_ACTIVATE) {
			if (r.r0 == w_cancel)
				return 0;
			if (w_logout && r.r0 == w_logout)
				logout();
			else if (r.r0 == w_restart)
				finish(0, s_restarting);
			else if (r.r0 == w_off)
				finish(INIT_REBOOT_HALT, s_shutting);
			continue;
		}
		if (r.r2)
			continue;
		if (ev == WEV_KEY && *(u32 *)(ui.ev + EV_KIND) && *(u32 *)(ui.ev + EV_CODE) == KEY_ESC) /* Échap */
			return 0;
	}
}
