/* authcard : la carte de confirmation de l'élévation (docs/etude/MULTI-UTILISATEUR.md §2.6,
 * docs/DESIGN-CONNEXION.md §7 : la même carte que l'écran de connexion, sans pied). Lancée par authd en
 * uid 0, jamais par l'app qui demande : « Logiciels veut installer Tetris », le compte, son mot de passe (ou
 * un simple bouton Autoriser pour un compte sans mot de passe). La réponse va à authd (AUTH_CARD) ; fermer
 * la carte vaut un refus.
 *
 * Argument : « jeton|raison|compte|1 » (1 : mot de passe demandé). Avec un mot de passe, la case « Retenir
 * pendant 5 minutes » (décochée à chaque ouverture) : cochée, authd n'en redemande pas pendant 5 minutes à ce
 * compte pour la session graphique (maquette docs/maquettes/elevation-retenir.html). */
#include "sys_consts.h"
#include "uikit_import.h"

#define AUTH_CARD 6
#define E_ACCES -13
#define M64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define M32(m, off) (*(u32 *)((u8 *)(m) + (off)))

static struct krt_ui ui;
static long chan;
static u64 token;
static long w_pw, w_msg, w_keep;
static u8 msg[MSG_SIZE];

#define TR(s) ((const char *)ui_tr((long)(s)))

static long answer(int cancel, const char *pw, int keep)
{
	memset(msg, 0, MSG_SIZE);
	M64(msg, 0) = AUTH_CARD;
	M64(msg, 8) = token;
	M64(msg, 16) = cancel ? 1 : 0;
	M64(msg, 24) = keep ? 1 : 0;
	u64 n = pw ? (u64)str_len(pw) : 0;
	if (n > 400)
		n = 400;
	if (pw)
		memcpy(msg + MSG_DATA, pw, n);
	M32(msg, 40) = (u32)(n + 1);
	long r = korin_sys2(SYS_SVC_CALL, chan, msg);
	memset(msg, 0, MSG_SIZE);
	return r < 0 ? r : (long)M64(msg, 0);
}

/* Autoriser : le mot de passe à authd ; mauvais, la carte reste */
static long card_fn(long d, long w, long code)
{
	(void)w;
	if (code == UN_ACTIVATE && w == w_pw)
		ui_dialog_close(d, 1);
	return 0;
}

/* « a|b|c|d » -> les morceaux, coupés en place */
static int split(char *s, char **part, int max)
{
	int n = 0;
	part[n++] = s;
	for (; *s && n < max; s++)
		if (*s == '|') {
			*s = 0;
			part[n++] = s + 1;
		}
	return n;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	static char line[512];
	str_copy_n(line, krt_line ? krt_line : "", sizeof line);
	char *part[4];
	if (split(line, part, 4) < 4)
		return 1;
	token = parse_dec(part[0]);
	const char *raison = part[1], *compte = part[2];
	int need = part[3][0] == '1';
	chan = korin_sys2(SYS_SVC_OPEN, "auth", 3000);
	if (chan < 0)
		return 1;
	ui_init((long)&ui);
	enum { DW = 420, M = 20 };
	long DH = need ? 280 : 190;
	int keep = 0; /* (gardée d'un essai à l'autre après un mauvais mot de passe) */
	for (;;) {
		long d = ui_dialog_begin(DW, DH, (long)TR("Autorisation"));
		if (!d)
			return 1;
		long y = 20;
		ui_label(M, y, DW - 2 * M, 44, (long)raison, ULB_WRAP);
		y += 50;
		static char who[96];
		str_cat(str_cat(who, TR("Compte : ")), compte);
		ui_label(M, y, DW - 2 * M, 20, (long)who, ULB_DIM);
		y += 30;
		w_pw = w_keep = 0;
		if (need) {
			w_pw = ui_textbox(M, y, DW - 2 * M, 30, UTF_PASSWORD);
			ui_textbox_hint(w_pw, (long)TR("Mot de passe"));
			y += 40;
			w_keep = ui_checkbox(M, y, 0, 0, (long)TR("Retenir pendant 5 minutes"), keep);
			y += 30;
		}
		w_msg = ui_label(M, y, DW - 2 * M, 20, (long)"", ULB_DIM);
		long e = ui_dialog_button(d, 0, (long)TR("Autoriser"), 1, UB_DEFAULT, DW - 12);
		ui_dialog_button(d, 1, (long)TR("Annuler"), 0, 0, e);
		ui_dialog_cancel(d, 0);
		if (w_pw)
			ui_w_focus(w_pw);
		long r = ui_dialog_run(d, (long)card_fn, 0);
		static char pw[128];
		pw[0] = 0;
		if (w_pw)
			str_copy_n(pw, (const char *)ui_w_get_text(w_pw), sizeof pw);
		keep = w_keep && ui_w_get_value(w_keep) ? 1 : 0;
		ui_dialog_end(d);
		if (r != 1) {
			answer(1, 0, 0);
			return 0;
		}
		long a = answer(0, pw, keep);
		memset(pw, 0, sizeof pw);
		if (a != E_ACCES)
			return 0;
		ui_message((long)TR("Autorisation"), (long)TR("Mot de passe incorrect."), UM_OK);
	}
}
