/*
 * applet.cpp -- le volet « Coffre à secrets » de Réglages, installé avec keyvault (docs/KEYVAULT.md, § 4 bis) :
 * /sys/applets/vault.so, chargé par Réglages (src/apps/control/control.h, « les applets installées »).
 *
 *   * la liste des secrets de tous les programmes du compte (KV_MGR_LIST : programme, nom, taille ; jamais les
 *     valeurs) ; « Secrets du système » : ceux du compte system (services, @system : accès à distance, Wi-Fi,
 *     partages), après la carte d'élévation ;
 *   * Afficher… et Copier : la valeur, après le mot de passe du compte (vérifié par le coffre, valable 5 minutes
 *     pour Réglages) ; la fenêtre de la valeur se ferme seule après 30 s ;
 *   * Supprimer (même règle), Actualiser.
 * Le coffre ne sert ces opérations qu'à Réglages (/apps/control.app, identité vérifiée par keyvault). Sans libc à
 * lui : celle de Réglages ; le client du coffre est l'en-tête src/lib/keyvault/keyvault.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "control.h"
#include "../../../lib/keyvault/keyvault.h"

enum { VB_BTN = 28, VB_CT = 24, VB_TOPS = 16, VB_MAX = 256, SHOW_S = 30 };
enum { E_PERM_ = -1, E_NOENT_ = -2, E_ACCES_ = -13, E_CANCELED_ = -125 };

#define KEYVAULT	"/sys/svc/keyvault"

static long tops[VB_TOPS];
static int ntop;
static long w_list, w_sys, w_state, w_status;
static struct Sec { char id[160], name[KV_NAME_MAX + 1]; long len; } secs[VB_MAX];
static int nsec, sys_mode;
static char buf[65536 + 1];

/* ---- les widgets ---- */

static long top (long w)
{
	if (ntop < VB_TOPS)
		tops[ntop++] = w;
	return w;
}

static long in (long g, long w)
{
	ui_w_set_parent (w, g);
	return w;
}

typedef void (*vb_fn) (long w, long code);

static long button (long g, long x, long y, long w, const char *text, vb_fn fn)
{
	long b = in (g, ui_pushbutton (x, y, w, VB_BTN, (long) TR (text), 0));
	ui_w_on (b, (long) fn, 0);
	return b;
}

static void vb_close (void)
{
	for (int i = 0; i < ntop; i++)
		ui_w_destroy (tops[i]);
	ntop = 0;
	memset (buf, 0, sizeof buf);
}

static void vb_draw (void) { ui_w_draw (0); }

static void status (const char *text)
{
	ui_w_set_text (w_status, (long) text);
	ui_w_draw (w_status);
}

/* le texte de la dernière erreur du coffre, après what */
static void fail (const char *what, long r)
{
	char e[300] = "", m[600];
	if (kv_error (e, sizeof e) < 0)
		e[0] = 0;
	snprintf (m, sizeof m, "%s (%ld)%s%s", TR (what), r, e[0] ? "\n" : "", e);
	ui_message ((long) TR ("Coffre à secrets"), (long) m, UM_OK);
}

/* ---- l'élévation (les secrets du système) ---- */

/* fn sous l'uid 0 (la carte d'authd), puis l'uid d'avant -> son résultat, ou E_CANCELED */
static long elevated (long (*fn) (void *), void *arg)
{
	unsigned me;
	if (ap_elevate (TR ("Réglages veut lire les secrets du système."), &me)) {
		status (TR ("Autorisation refusée."));
		return E_CANCELED_;
	}
	long r = fn (arg);
	ap_unelevate (me);
	return r;
}

/* ---- la liste ---- */

static long list_fn (void *) { return kv_mgr_list (sys_mode, buf, sizeof buf - 1); }

static void state (void)
{
	long st = kv_status ();
	const char *t = st == KVS_OPEN ? "Le coffre de votre compte est ouvert." : st == KVS_LOCKED
		? "Le coffre de votre compte est fermé : il s'ouvrira à la première demande (mot de passe)."
		: st == KVS_NONE ? "Pas encore de coffre : il naîtra au premier secret rangé." : "Le coffre ne répond pas.";
	ui_w_set_text (w_state, (long) TR (t));
}

static void load (void)
{
	nsec = 0;
	status (TR ("Lecture du coffre…"));
	long r = kv_mgr_list (sys_mode, buf, sizeof buf - 1);
	if (r == E_PERM_ && sys_mode)		/* (les 5 minutes de l'élévation passées) */
		r = elevated (list_fn, 0);
	status ("");
	if (r < 0) {
		if (r != E_CANCELED_)
			fail ("Le coffre ne peut pas être lu", r);
		r = 0;
		if (sys_mode) {
			sys_mode = 0;
			ui_w_set_value (w_sys, 0);
		}
	}
	buf[r < (long) sizeof buf ? r : (long) sizeof buf - 1] = 0;
	for (char *l = buf; *l && nsec < VB_MAX;) {
		char *e = l + strcspn (l, "\n");
		char *t1 = (char *) memchr (l, '\t', (size_t) (e - l));
		char *t2 = t1 ? (char *) memchr (t1 + 1, '\t', (size_t) (e - t1 - 1)) : 0;
		if (t1 && t2) {
			Sec *s = &secs[nsec++];
			snprintf (s->id, sizeof s->id, "%.*s", (int) (t1 - l), l);
			snprintf (s->name, sizeof s->name, "%.*s", (int) (t2 - t1 - 1), t1 + 1);
			s->len = atol (t2 + 1);
		}
		l = *e ? e + 1 : e;
	}
	ui_list_clear (w_list);
	for (int i = 0; i < nsec; i++) {
		char row[400], sz[32];
		if (secs[i].len < 1024)
			snprintf (sz, sizeof sz, "%ld o", secs[i].len);
		else
			snprintf (sz, sizeof sz, "%ld Kio", (secs[i].len + 1023) / 1024);
		snprintf (row, sizeof row, "%s\t%s\t%s", secs[i].id, secs[i].name, sz);
		ui_list_add (w_list, (long) row, i);
	}
	ui_w_set_value (w_list, nsec ? 0 : -1);
	state ();
	char m[120];
	snprintf (m, sizeof m, "%d %s", nsec, TR (nsec == 1 ? "secret" : "secrets"));
	status (m);
	ui_w_draw (0);
}

static int sel (void)
{
	long v = ui_w_get_value (w_list);
	return v >= 0 && v < nsec ? (int) v : -1;
}

/* ---- le mot de passe du compte ---- */

static long pw_box;

static long pw_fn (long d, long w, long code)
{
	if (w == pw_box && code == UN_ACTIVATE)
		ui_dialog_close (d, 1);
	return 0;
}

/* -> 1 : pw rempli */
static int ask_password (char *pw, size_t n, int again)
{
	enum { DW = 440, DH = 190, M = 16 };
	long d = ui_dialog_begin (DW, DH, (long) TR ("Coffre à secrets"));
	if (!d)
		return 0;
	ui_label (M, 16, DW - 2 * M, 60, (long) TR (again ? "Mot de passe refusé. Le mot de passe de votre compte :"
		: "Pour voir ou effacer un secret, le mot de passe de votre compte (valable 5 minutes) :"), ULB_WRAP);
	pw_box = ui_textbox (M, 84, DW - 2 * M, VB_BTN, UTF_PASSWORD);
	long b = ui_dialog_button (d, 0, (long) TR ("OK"), 1, UB_DEFAULT, DW - 12);
	ui_dialog_button (d, 1, (long) TR ("Annuler"), 0, 0, b);
	ui_dialog_cancel (d, 0);
	ui_w_focus (pw_box);
	long r = ui_dialog_run (d, (long) pw_fn, 0);
	snprintf (pw, n, "%s", (const char *) ui_w_get_text (pw_box));
	ui_w_set_text (pw_box, (long) "");
	ui_dialog_end (d);
	ui_w_draw (0);
	return r == 1 && pw[0];
}

/* ---- les opérations sur un secret ---- */

static struct { int op, i; const char *pw; } rq;

static long req_fn (void *)
{
	const Sec *s = &secs[rq.i];
	return rq.op == KV_MGR_GET ? kv_mgr_get (sys_mode, s->id, s->name, rq.pw, buf, sizeof buf - 1)
				   : kv_mgr_delete (sys_mode, s->id, s->name, rq.pw);
}

/* op sur le secret i : le mot de passe ou l'élévation demandés s'il le faut -> le résultat (E_CANCELED : abandon) */
static long request (int op, int i)
{
	char pw[256] = "";
	rq.op = op;
	rq.i = i;
	rq.pw = 0;
	long r = req_fn (0);
	if (r == E_PERM_ && sys_mode)
		r = elevated (req_fn, 0);
	for (int tries = 0; r == E_ACCES_ && !sys_mode && tries < 3; tries++) {
		if (!ask_password (pw, sizeof pw, tries > 0))
			return E_CANCELED_;
		status (TR ("Vérification du mot de passe…"));
		rq.pw = pw;
		r = req_fn (0);
		status ("");
	}
	memset (pw, 0, sizeof pw);
	rq.pw = 0;
	return r;
}

/* la valeur pour l'affichage : du texte (UTF-8, sauts de ligne gardés), sinon en hexadécimal */
static const char *printable (long n)
{
	static char out[4096];
	int text = 1;
	for (long i = 0; i < n && text; i++) {
		unsigned char c = (unsigned char) buf[i];
		text = c >= 0x20 || c == '\n' || c == '\t' || c == '\r';
	}
	size_t o = 0;
	if (text) {
		for (long i = 0; i < n && o < sizeof out - 8; i++)
			out[o++] = buf[i] == '\t' ? ' ' : buf[i];
	} else {
		static const char hx[] = "0123456789abcdef";
		for (long i = 0; i < n && o < sizeof out - 8; i++) {
			out[o++] = hx[(unsigned char) buf[i] >> 4];
			out[o++] = hx[buf[i] & 15];
			if (i % 2)
				out[o++] = ' ';
		}
	}
	if (o >= sizeof out - 8)
		o += (size_t) snprintf (out + o, sizeof out - o, "…");
	out[o] = 0;
	return out;
}

static struct { long d, left, last, wait, copy; long n; } sh;

static long show_tick (long d)
{
	long t = sys_ms ();
	if (t - sh.last < 1000)
		return 0;
	sh.last = t;
	if (--sh.left <= 0) {
		ui_dialog_close (d, 0);
		return 0;
	}
	char m[120];
	snprintf (m, sizeof m, "%s %ld s.", TR ("Cette fenêtre se fermera dans"), sh.left);
	ui_w_set_text (sh.wait, (long) m);
	ui_w_draw (sh.wait);
	return 0;
}

static long show_fn (long, long w, long code)
{
	if (w == sh.copy && code == UN_ACTIVATE)
		ui_clip_set ((long) buf, sh.n);
	return 0;
}

static void on_show (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	long n = request (KV_MGR_GET, i);
	if (n == E_CANCELED_)
		return;
	if (n < 0) {
		fail ("Le secret ne peut pas être lu", n);
		return;
	}
	if (n > (long) sizeof buf - 1)
		n = sizeof buf - 1;
	enum { DW = 600, DH = 340, M = 16 };
	char title[300];
	snprintf (title, sizeof title, "%s — %s", secs[i].name, secs[i].id);
	long d = ui_dialog_begin (DW, DH, (long) title);
	if (d) {
		ui_label (M, 14, DW - 2 * M, DH - 110, (long) printable (n), ULB_WRAP);
		sh.wait = ui_label (M, DH - 88, DW - 2 * M - 120, 20, (long) "", ULB_DIM);
		sh.copy = ui_pushbutton (DW - M - 110, DH - 92, 110, VB_BTN, (long) TR ("Copier"), 0);
		ui_dialog_button (d, 0, (long) TR ("Fermer"), 1, UB_DEFAULT, DW - 12);
		ui_dialog_cancel (d, 0);
		sh.n = n;
		sh.left = SHOW_S + 1;
		sh.last = 0;
		ui_dialog_timer (d, (long) show_tick, 250);
		ui_dialog_run (d, (long) show_fn, 0);
		ui_dialog_end (d);
	}
	memset (buf, 0, sizeof buf);
	ui_w_draw (0);
}

static void on_copy (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	long n = request (KV_MGR_GET, i);
	if (n == E_CANCELED_)
		return;
	if (n < 0) {
		fail ("Le secret ne peut pas être lu", n);
		return;
	}
	ui_clip_set ((long) buf, n);
	memset (buf, 0, sizeof buf);
	status (TR ("Valeur copiée dans le presse-papier."));
}

static void on_delete (long, long)
{
	int i = sel ();
	if (i < 0)
		return;
	char q[500];
	snprintf (q, sizeof q, "%s « %s » (%s) ? %s", TR ("Effacer le secret"), secs[i].name, secs[i].id,
		  TR ("Le programme devra le redemander (mot de passe, jeton, connexion)."));
	if (!ui_message ((long) TR ("Coffre à secrets"), (long) q, UM_YESNO))
		return;
	long r = request (KV_MGR_DELETE, i);
	if (r == E_CANCELED_)
		return;
	if (r < 0) {
		fail ("Le secret ne peut pas être effacé", r);
		return;
	}
	load ();
	status (TR ("Secret effacé."));
}

static void on_list (long, long code)
{
	if (code == UN_ACTIVATE)
		on_show (0, 0);
}

static void on_refresh (long, long) { load (); }

static void on_sys (long w, long)
{
	int want = ui_w_get_value (w) != 0;
	if (want == sys_mode)
		return;
	sys_mode = want;
	if (sys_mode && elevated (list_fn, 0) == E_CANCELED_) {	/* (la carte d'élévation tout de suite) */
		sys_mode = 0;
		ui_w_set_value (w, 0);
		ui_w_draw (0);
		return;
	}
	load ();
}

/* ---- le volet ---- */

static void vb_open (void)
{
	ap.draw = vb_draw;
	ap.close = vb_close;
	ap.widgets = 1;
	ntop = 0;
	sys_mode = 0;
	long x = SIDE_W + 10, y = AP_HEAD + 4;
	long pw = ui.w - SIDE_W - 20, ph = ui.h - AP_HEAD - 10;

	w_state = top (ui_label (x, y + 4, pw - 260, 20, (long) "", ULB_BOLD));
	w_sys = top (ui_checkbox (x + pw - 250, y + 4, 250, 20, (long) TR ("Secrets du système"), 0));
	ui_w_on (w_sys, (long) on_sys, 0);
	y += 30;
	top (ui_label (x, y, pw, 40, (long) TR ("Les mots de passe, jetons et clés que les programmes ont rangés dans le "
		"coffre. Les valeurs ne se montrent qu'après le mot de passe de votre compte."), ULB_DIM | ULB_WRAP));
	y += 44;
	long gh = ph - 74 - 30, bw = 140;
	long g = top (ui_groupbox (x, y, pw, gh, (long) TR ("Secrets")));
	long lw = pw - bw - 30;
	w_list = in (g, ui_list (x + 10, y + VB_CT, lw, gh - VB_CT - 10, UL_HEADER | UL_NOSORT));
	ui_list_column (w_list, (long) TR ("Programme"), lw - 220 - 80, 0);
	ui_list_column (w_list, (long) TR ("Nom"), 220, 0);
	ui_list_column (w_list, (long) TR ("Taille"), 80, 0);
	ui_w_on (w_list, (long) on_list, 0);
	long bx = x + pw - bw - 10, by = y + VB_CT;
	button (g, bx, by, bw, "Afficher…", on_show);
	button (g, bx, by + 34, bw, "Copier", on_copy);
	button (g, bx, by + 68, bw, "Supprimer", on_delete);
	button (g, bx, by + 116, bw, "Actualiser", on_refresh);
	y += gh + 6;
	w_status = top (ui_label (x, y + 2, pw, 20, (long) "", ULB_DIM));
	load ();
}

static void vb_apply (void) {}

extern "C" {
extern const int korin_applet_abi;
extern const Applet korin_applet;
const int korin_applet_abi = APPLET_ABI;
const Applet korin_applet = { "vault", "Coffre", "Coffre à secrets", "control", vb_open, vb_apply, KEYVAULT };
}
