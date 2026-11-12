/* uk_dialog.c -- UIKit : dialogues modaux (uikit_dialog.inc).
 *
 * Un dialogue est une fenêtre de plus du programme (uk_window.c), au rôle
 * WF_MODAL : Elegant bloque les autres fenêtres du programme. Le kit crée
 * cette fenêtre, qui devient la courante, y place des widgets et fait tourner
 * sa propre boucle jusqu'à la réponse ; puis il détruit les widgets du
 * dialogue, ferme sa fenêtre et rend la fenêtre d'avant. Les dialogues
 * s'emboîtent (« Remplacer ? » dans « Enregistrer »).
 *
 *	ui_message(titre, texte, boutons)	OK, OK/Annuler, Oui/Non, Oui/Non/Annuler
 *	ui_ask(titre, question, tampon, taille)	saisie d'une ligne
 *	ui_file_open / ui_file_save / ui_folder_choose
 *	ui_dialog_begin / ui_dialog_run / ui_dialog_end : un dialogue du programme
 *
 * Port ligne à ligne de l'assembleur (src/kits/uikit/PORT.md). Le bloc du
 * dialogue (DG_*) est lu et écrit par décalages, comme l'asm. Rappels : la
 * fonction du dialogue (DG_FN) et celle du programme (DG_UFN) reçoivent
 * (dialogue, widget, code) ; DG_KEYFN (dialogue, code, mods) ; DG_TICK
 * (dialogue). */
#include "uk.h"

/* les fonctions internes (adresses rangées dans le bloc du dialogue) */
static long uk_fd_fn(long dlg, long w, long code);
static long uk_fd_key(long dlg, long code, long mods);
static long uk_fd_resize(long w, long h);
static long uk_fd_load(long dlg);

#define FD_FILTER LDX(UKA(fd_filter), 0) /* uk_fd_filter (ui_file_filter) */

/* ---------------------------------------------------------------------------
 *  Dialogue générique
 * ------------------------------------------------------------------------- */

/* x0 = largeur, x1 = hauteur, x2 = titre -> x0 = dialogue (0 : échec). La
 * fenêtre du dialogue devient la fenêtre courante (fond FACE) : les widgets
 * créés ensuite sont les siens. */
long ui_dialog_begin(long wd, long ht, long title)
{
	long dlg, x, y, role, id;

	title = ui_tr(title); /* titre traduit */
	dlg = uk_alloc(DG_BYTES);
	if (!dlg)
		return 0;
	STX(dlg, DG_W, wd);
	STX(dlg, DG_H, ht);
	STX(dlg, DG_TICK, 0);
	if (uk.cur) /* la fenêtre d'avant (sinon DG_PREV reste tel qu'alloué, comme l'asm) */
		STX(dlg, DG_PREV, LDX(uk.cur, WB_ID));
	uk.hover = 0; /* (la fenêtre d'avant attend : ni survol, ni capture) */
	uk.capture = 0;
	if (uk.pop_place[2]) { /* à une place donnée ? */
		uk.pop_place[2] = 0;
		x = uk.pop_place[0];
		y = uk.pop_place[1];
	} else { /* centré à l'écran */
		x = -1;
		y = -1;
		if (ui_screen(UKA(scr)) >= 0) {
			x = ASR(LDX(UKA(scr), 0) - wd, 1);
			y = ASR(LDX(UKA(scr), 8) - ht, 1);
			if (x < 0)
				x = 0;
			if (y < 40)
				y = 40;
		}
	}
	role = uk.dlg_role;
	uk.dlg_role = 0;
	role |= WF_MODAL;
	id = ui_window_ex(x, y, wd, ht, title, role);
	if (id < 0) {
		uk_free(dlg);
		return 0;
	}
	STX(dlg, DG_WIN, id);
	uk.win_dlg = dlg;
	ui_fill(0, 0, wd, ht, JET(TJ_FACE));
	STX(dlg, DG_FN, (long)uk_dlg_buttons_fn);
	return dlg;
}

/* x0 = dialogue : détruit ses widgets, ferme sa fenêtre, rend la fenêtre d'avant */
long ui_dialog_end(long dlg)
{
	long prev;

	if (!dlg)
		return 0;
	ui_window_use(LDX(dlg, DG_WIN));
	uk_w_destroy_win(LDX(dlg, DG_WIN));
	uk_free(LDX(dlg, DG_CWD));
	uk_free(LDX(dlg, DG_HIST));
	uk_free(LDX(dlg, DG_SP));
	ui_close();
	prev = LDX(dlg, DG_PREV); /* la fenêtre d'avant revient devant */
	if (prev) {
		ui_window_use(prev);
		ui_raise(uk_win());
	} else if (!uk.cur) { /* (pas de fenêtre avant lui) */
		uk.minw = 120;
		uk.minh = 80;
		uk.resize_fn = 0;
	}
	return uk_free(dlg);
}

/* x0 = dialogue, x1 = numéro (0..2), x2 = texte, x3 = réponse, x4 = style
 * (UB_DEFAULT : Entrée), x5 = bord droit -> x0 = bord droit du suivant. Bouton
 * du bas, aligné à droite (principal le plus à droite, 8 px). */
long ui_dialog_button(long dlg, long n, long text, long resp, long style, long right)
{
	long b, x, ctl;

	text = ui_tr(text); /* (texte traduit : TR) */
	ctl = LDX(uk_met(), MT_CTL);
	b = ui_pushbutton(0, LDX(dlg, DG_H) - ctl - 12, 0, ctl, text, style); /* (largeur selon le texte) */
	if (!b)
		return 0;
	x = right - LDW(b, UW_W);
	STW(b, UW_X, x);
	STX(dlg + DG_B0, n * 8, b); /* (numéro non borné, comme l'asm) */
	STX(dlg + DG_RV, n * 8, resp);
	if (style & 1)
		STX(dlg, DG_DEF, b);
	return x - 8;
}

/* uk_dlg_bar : x0 = dialogue : barre du bas (trait LIGNE au-dessus des boutons) */
long uk_dlg_bar(long dlg)
{
	long ctl = LDX(uk_met(), MT_CTL);

	return ui_fill(0, LDX(dlg, DG_H) - ctl - 24, LDX(dlg, DG_W), 1, JET(TJ_LIGNE));
}

/* uk_dlg_buttons_fn : fonction des dialogues à boutons : x0 = dialogue, x1 =
 * widget, x2 = code (un bouton : sa réponse ; Entrée dans le champ : le bouton
 * par défaut) -> x0 = dialogue (inchangé, comme l'asm) */
long uk_dlg_buttons_fn(long dlg, long w, long code)
{
	long i, b;

	if (code != UN_ACTIVATE)
		return dlg;
	if (w == LDX(dlg, DG_TB))
		w = LDX(dlg, DG_DEF);
	for (i = 0; i < 3; i++) {
		b = LDX(dlg + DG_B0, i * 8);
		if (b == w) {
			if (!b) /* (widget 0 : rien, comme l'asm) */
				return dlg;
			STX(dlg, DG_RES, LDX(dlg + DG_RV, i * 8));
			STX(dlg, DG_DONE, 1);
			return dlg;
		}
	}
	return dlg;
}

/* x0 = dialogue, x1 = fonction du programme (x0 = dialogue, x1 = widget,
 * x2 = code ; 0 : boutons seulement) -> x0 = réponse. Dessine les widgets du
 * dialogue et attend : un bouton de ui_dialog_button donne sa réponse ;
 * Échap ou la case de fermeture : celle de ui_dialog_cancel (0) ; Entrée
 * (non prise par un widget) : le bouton par défaut. */
long ui_dialog_run(long dlg, long fn)
{
	long type, tick, ms, ev, kf, code, def, u;
	ukr r;

	STX(dlg, DG_UFN, fn);
	ui_w_draw(0);
	ui_update_all();
	for (;;) {
		if (LDX(dlg, DG_DONE))
			goto done;
		tick = LDX(dlg, DG_TICK);
		ms = 30000;
		if (tick) {
			UK_CB1(tick, dlg);
			if (LDX(dlg, DG_DONE))
				goto done;
			ms = LDX(dlg, DG_TICKMS);
		}
		type = ui_event(ms);
		if (!type)
			continue;
		ev = UV(UV_EV);
		if (LDW(ev, EV_WIN) != LDX(dlg, DG_WIN)) {
			/* une autre fenêtre : son état suit, le reste est perdu */
			if (type == WEV_FOCUS || type == WEV_RESIZE)
				ui_w_event(0);
			continue;
		}
		if (type == WEV_CLOSE)
			goto cancel;
		r = ui_w_event(0);
		if (r.r1) {
			UK_CB3(LDX(dlg, DG_FN), dlg, r.r0, r.r1);
			u = LDX(dlg, DG_UFN);
			if (u)
				UK_CB3(u, dlg, r.r0, r.r1);
			continue;
		}
		if (r.r2) /* (pris par un widget) */
			continue;
		if (type != WEV_KEY)
			continue;
		if (!LDW(ev, EV_KIND))
			continue;
		kf = LDX(dlg, DG_KEYFN);
		if (kf && UK_CB3(kf, dlg, LDW(ev, EV_CODE), LDW(ev, EV_MODS)))
			continue;
		code = LDW(ev, EV_CODE);
		if (code == KEY_ESC)
			goto cancel;
		if (code != KEY_ENTER)
			continue;
		def = LDX(dlg, DG_DEF);
		if (!def)
			continue;
		UK_CB3(LDX(dlg, DG_FN), dlg, def, UN_ACTIVATE);
	}
cancel:
	STX(dlg, DG_RES, LDX(dlg, DG_CANRES));
done:
	return LDX(dlg, DG_RES);
}

/* x0 = dialogue, x1 = réponse : finit le dialogue (depuis sa fonction) -> x0 inchangé */
long ui_dialog_close(long dlg, long res)
{
	STX(dlg, DG_RES, res);
	STX(dlg, DG_DONE, 1);
	return dlg;
}

/* x0 = dialogue, x1 = fonction (x0 = dialogue), x2 = période en ms : appelée
 * par ui_dialog_run avant chaque attente -> x0 inchangé */
long ui_dialog_timer(long dlg, long fn, long ms)
{
	STX(dlg, DG_TICK, fn);
	STX(dlg, DG_TICKMS, ms);
	return dlg;
}

/* x0 = dialogue, x1 = réponse d'Échap et de la case de fermeture -> x0 inchangé */
long ui_dialog_cancel(long dlg, long res)
{
	STX(dlg, DG_CANRES, res);
	return dlg;
}

/* ---------------------------------------------------------------------------
 *  Message
 * ------------------------------------------------------------------------- */

/* x0 = titre, x1 = texte (« \n », lignes coupées), x2 = boutons (0 OK, 1 OK
 * Annuler, 2 Oui Non, 3 Oui Non Annuler) -> x0 = 1 OK / Oui, 0 Annuler / Non
 * (3 : Non = 2) ; Échap : 0 */
long ui_message(long title, long text, long kind)
{
	long mt, h, dlg, right, s, resp, res, color;

	title = TR(title);
	text = TR(text);
	mt = uk_met();
	h = ui_text_wrap(text, 0, 0, UK_DLG_W - 2 * UK_DLG_PAD, 0, uk_font_r(), LDX(mt, MT_PX), 1); /* hauteur du texte */
	dlg = ui_dialog_begin(UK_DLG_W, LDX(mt, MT_CTL) + h + UK_DLG_PAD * 2 + 24, title);
	if (!dlg)
		return 0;
	color = JET(TJ_TEXTE1); /* le texte */
	ui_text_wrap(text, UK_DLG_PAD, UK_DLG_PAD, UK_DLG_W - 2 * UK_DLG_PAD, color, uk_font_r(), LDX(mt, MT_PX), 0);
	uk_dlg_bar(dlg);
	right = UK_DLG_W - 16; /* boutons, de droite à gauche */
	s = S(uk_s_ok);
	if (!ULT(kind, 2))
		s = S(uk_s_yes);
	right = ui_dialog_button(dlg, 0, s, 1, UB_DEFAULT, right);
	if (kind) {
		s = S(uk_s_cancel);
		resp = 0;
		if (!ULT(kind, 2)) {
			s = S(uk_s_no);
			if (kind == 3)
				resp = 2;
		}
		right = ui_dialog_button(dlg, 1, s, resp, 0, right);
		if (kind == 3)
			ui_dialog_button(dlg, 2, S(uk_s_cancel), 0, 0, right);
	}
	ui_w_focus(LDX(dlg, DG_DEF));
	res = ui_dialog_run(dlg, 0);
	ui_dialog_end(dlg);
	return res;
}

/* ---------------------------------------------------------------------------
 *  Saisie
 * ------------------------------------------------------------------------- */

/* x0 = titre, x1 = question, x2 = tampon (texte de départ, puis la réponse),
 * x3 = taille du tampon -> x0 = 1 (OK : réponse dans le tampon), 0 (Annuler) */
long ui_ask(long title, long text, long buf, long size)
{
	long mt, qh, dlg, tb, x, res, n, m, e;

	title = TR(title);
	text = TR(text);
	mt = uk_met();
	qh = ui_text_wrap(text, 0, 0, UK_DLG_W - 2 * UK_DLG_PAD, 0, uk_font_r(), LDX(mt, MT_PX), 1); /* hauteur de la question */
	dlg = ui_dialog_begin(UK_DLG_W, qh + LSL(LDX(mt, MT_CTL), 1) + UK_DLG_PAD * 2 + 24 + 12, title);
	if (!dlg)
		return 0;
	ui_text_wrap(text, UK_DLG_PAD, UK_DLG_PAD, UK_DLG_W - 2 * UK_DLG_PAD, JET(TJ_TEXTE1), uk_font_r(),
		     LDX(mt, MT_PX), 0);
	tb = ui_textbox(UK_DLG_PAD, qh + UK_DLG_PAD + 8, UK_DLG_W - 2 * UK_DLG_PAD, LDX(mt, MT_CTL), 0); /* le champ */
	STX(dlg, DG_TB, tb);
	STX(tb, UW_MAX, size - 1); /* (champ 0 : écrit quand même, comme l'asm) */
	ui_w_set_text(tb, buf);
	ui_textbox_select(tb, 0, -1);
	uk_dlg_bar(dlg);
	x = ui_dialog_button(dlg, 0, S(uk_s_ok), 1, UB_DEFAULT, UK_DLG_W - 16);
	ui_dialog_button(dlg, 1, S(uk_s_cancel), 0, 0, x);
	ui_w_focus(tb);
	res = ui_dialog_run(dlg, 0);
	if (res) { /* la réponse */
		n = uk_strlen(LDX(tb, UW_TEXT));
		m = size - 1;
		if (ULT(n, m))
			m = n;
		e = uk_memcpy(buf, LDX(tb, UW_TEXT), m);
		STB(e, 0, 0);
	}
	ui_dialog_end(dlg);
	return res;
}

/* ---------------------------------------------------------------------------
 *  Ouvrir, enregistrer, choisir un dossier
 *
 *  Une fenêtre modale redimensionnable : en haut Retour, Avant, Parent et le
 *  chemin (on peut le taper) ; à gauche les emplacements (SD0:/home,
 *  SD0:/etc/places.ini) et les volumes (VFS_VOLUMES) ; la liste Nom, Taille,
 *  Date, dossiers d'abord ; le nom ; en bas les types de fichiers
 *  (ui_file_filter) ou le nombre d'éléments, et les boutons.
 * ------------------------------------------------------------------------- */

static long uk_file_dialog(long dir, long kind, long name, long buf, long size);

/* x0 = dossier de départ (0 : SD0:/), x1 = tampon, x2 = sa taille -> x0 = 1 / 0 */
long ui_file_open(long dir, long buf, long size)
{
	return uk_file_dialog(dir, 0, 0, buf, size);
}

/* x0 = dossier de départ, x1 = nom proposé (0 : aucun ; un chemin : son dossier
 * et son nom), x2 = tampon, x3 = taille -> x0 = 1 / 0 */
long ui_file_save(long dir, long name, long buf, long size)
{
	return uk_file_dialog(dir, 1, name, buf, size);
}

/* x0 = types de fichiers montrés par les dialogues suivants (0 : tous) -> x0 inchangé */
long ui_file_filter(long filter)
{
	STX(UKA(fd_filter), 0, filter);
	return filter;
}

/* uk_fd_match : x0 = nom, x1 = motifs (« *suite;*suite », finis par NUL ou
 * « | » ; 0 : tout) -> x0 = 1 s'il passe */
static long uk_fd_match(long name, long pats)
{
	long end, e, len, a, b, c, c2;

	if (!pats)
		return 1;
	end = name; /* fin du nom */
	while (LDB(end, 0))
		end++;
pat:
	c = LDB(pats, 0); /* un motif : « *suite » */
	if (!c || c == '|')
		return 0;
	if (c != '*')
		goto skip;
	pats++;
	e = pats; /* sa fin */
	for (;;) {
		c = LDB(e, 0);
		if (!c || c == ';' || c == '|')
			break;
		e++;
	}
	len = e - pats;
	if (len == 2 && LDB(pats, 0) == '.' && LDB(pats, 1) == '*') /* « *.* » : tout */
		return 1;
	if (UGT(len, end - name))
		goto next;
	a = end - len; /* la fin du nom, comparée */
	b = pats;
	for (;;) {
		if (UGE(b, e))
			return 1;
		c = LDB(a, 0) | 0x20;
		c2 = LDB(b, 0) | 0x20;
		a++;
		b++;
		if (c != c2)
			break;
	}
next:
	pats = e;
skip:
	for (;;) { /* le motif suivant */
		c = LDB(pats, 0);
		if (!c || c == '|')
			return 0;
		pats++;
		if (c == ';')
			goto pat;
	}
}

/* x0 = dossier de départ, x1 = tampon, x2 = taille -> x0 = 1 / 0 (le dossier
 * choisi, terminé par « / ») */
long ui_folder_choose(long dir, long buf, long size)
{
	return uk_file_dialog(dir, 2, 0, buf, size);
}

/* uk_path_up : x0 = dossier (terminé par « / ») : remonte d'un cran (« SD0:/ »
 * reste) -> x0 = 1 s'il a changé */
static long uk_path_up(long p)
{
	long n = 0, i, c;

	while (LDB(p, n))
		n++;
	if (ULT(n, 2))
		return 0;
	i = n - 2; /* avant le « / » final */
	for (;;) {
		c = LDB(p, i);
		if (c == ':')
			return 0; /* (racine d'un volume) */
		if (c == '/')
			break;
		if (!i)
			return 0;
		i--;
	}
	STB(p, i + 1, 0);
	return 1;
}

/* uk_path_set : x0 = tampon du dossier (UK_PATH_MAX), x1 = chemin : le copie,
 * terminé par « / » */
static long uk_path_set(long d, long s)
{
	long i = 0, c;

	for (;;) {
		c = LDB(s, i);
		STB(d, i, c);
		if (!c)
			break;
		i++;
		if (!ULT(i, UK_PATH_MAX - 2))
			break;
	}
	if (!i || LDB(d, i - 1) != '/') {
		STB(d, i, '/');
		i++;
	}
	STB(d, i, 0);
	return d;
}

/* uk_path_join : x0 = destination, x1 = dossier, x2 = nom (jusqu'à la
 * tabulation ou NUL), x3 = taille -> chemin complet */
static void uk_path_join(long d, long dir, long name, long size)
{
	long i = 0, c;

	size--;
	for (;;) {
		if (UGE(i, size))
			goto z;
		c = LDB(dir, 0);
		dir++;
		if (!c)
			break;
		STB(d, i, c);
		i++;
	}
	for (;;) {
		if (UGE(i, size))
			break;
		c = LDB(name, 0);
		name++;
		if (!c || c == 9)
			break;
		STB(d, i, c);
		i++;
	}
z:
	STB(d, i, 0);
}

/* uk_fd_isdir : x0 = chemin -> x0 = 1 si c'est un dossier */
static long uk_fd_isdir(long path)
{
	long t = UKA(fdtmp), i = 0, c;

	for (;;) { /* sans son « / » final (sauf à la racine) */
		c = LDB(path, i);
		STB(t, i, c);
		if (!c)
			break;
		i++;
		if (!ULT(i, UK_PATH_MAX - 1)) {
			STB(t, i, 0);
			break;
		}
	}
	if (!ULT(i, 2) && LDB(t, i - 1) == '/' && LDB(t, i - 2) != ':')
		STB(t, i - 1, 0);
	if (SYS2(SYS_STAT, t, UKA(fdstat)))
		return 0;
	return LSR(uk.fdstat[1], 4) & 1; /* ATTR_DIR */
}

/* uk_fd_start : x0 = dialogue, x1 = dossier de départ (0 : SD0:/), x2 = nom
 * proposé (0 : aucun) : le dossier courant, le plus proche qui existe -> x0 =
 * le nom à proposer */
static long uk_fd_start(long dlg, long dir, long name)
{
	long p, last, n, end, src, tried, cwd, len, i, j, k, c;

	if (name) {
		p = name; /* un chemin : la fin après « / » ou « : » */
		last = name;
		for (;;) {
			c = LDB(p, 0);
			p++;
			if (!c)
				break;
			if (c == '/' || c == ':')
				last = p;
		}
		if (last != name) {
			n = last - name; /* son dossier : le départ */
			if (UGT(n, UK_PATH_MAX - 2))
				n = UK_PATH_MAX - 2;
			end = UKA(fdline) + n;
			uk_memcpy(UKA(fdline), name, n);
			STB(end, 0, 0);
			dir = UKA(fdline);
			name = last;
		}
	}
	src = S(uk_s_sd0);
	if (dir)
		src = dir;
	uk_path_set(LDX(dlg, DG_CWD), src);
	tried = 0; /* encore un dossier : un seul essai de nom */
	for (;;) {
		if (uk_fd_isdir(LDX(dlg, DG_CWD)))
			return name;
		if (tried || name)
			goto up;
		cwd = LDX(dlg, DG_CWD); /* le dernier composant : un fichier ? */
		len = 0;
		while (LDB(cwd, len))
			len++;
		len--; /* (avant le « / » final) */
		i = len;
		for (;;) {
			if (!i)
				goto up;
			j = i - 1;
			c = LDB(cwd, j);
			if (c == '/' || c == ':')
				break;
			i = j;
		}
		k = 0; /* le nom */
		for (;;) {
			if (UGE(i, len))
				break;
			STB(UKA(fdname), k, LDB(cwd, i));
			i++;
			k++;
			if (!ULT(k, 255))
				break;
		}
		STB(UKA(fdname), k, 0);
		name = UKA(fdname);
	up:
		tried = 1;
		if (!uk_path_up(LDX(dlg, DG_CWD)))
			break;
	}
	uk_path_set(LDX(dlg, DG_CWD), S(uk_s_sd0)); /* rien : SD0:/ */
	return name;
}

/* uk_fd_date : x0 = chemin -> x0 = date (secondes UTC ; 0 : aucune, ou inconnue) */
static long uk_fd_date(long path)
{
	u8 msg[MSG_SIZE] __attribute__((aligned(16))); /* (MSG_B, MSG_C non écrits, comme l'asm) */
	long m = (long)(uintptr_t)msg, i = 0, c, h;

	STX(m, 0, V_STAT);
	STX(m, 8, 0);
	STX(m, MSG_D, 0);
	for (;;) {
		c = LDB(path, i);
		STB(m + MSG_DATA, i, c);
		i++;
		if (!c)
			break;
		if (!ULT(i, 254))
			return 0;
	}
	STW(m, MSG_LEN, i);
	h = SYS2(SYS_SVC_OPEN, S(uk_s_vfs), 0);
	if (h < 0)
		return 0;
	if (SYS2(SYS_SVC_CALL, h, m) < 0)
		return 0;
	return LDX(m, MSG_D);
}

/* uk_fd_date_text : x0 = date (secondes UTC), x1 = tampon <- « AAAA-MM-JJ
 * HH:MM » à l'heure locale -> x0 = fin du texte */
static long uk_fd_date_text(long t, long buf)
{
	u64 s, days, era, doe, yoe, y, doy, mp, d, m, x;
	long p;

	s = (u64)t + (u64)SYS1(SYS_CLOCK_GET, 1); /* décalage de l'heure locale */
	days = s / 86400;			/* jours depuis 1970 */
	s = s - days * 86400;			/* secondes du jour */
	x = days + 719468;			/* année, mois, jour (civil_from_days) */
	era = x / 146097;
	doe = x - era * 146097;
	yoe = doe - doe / 1460;
	yoe = yoe + doe / 36524;
	yoe = yoe - doe / 146096;
	yoe = yoe / 365;
	y = era * 400 + yoe;
	doy = doe - (yoe * 365 + (yoe >> 2) - yoe / 100);
	mp = (doy * 5 + 2) / 153;
	d = doy - (mp * 153 + 2) / 5 + 1;
	m = mp + 3;
	if (!(mp < 10))
		m = mp - 9;
	if (m <= 2)
		y++;
	/* (uk_date_text : x1 au NUL) */
	p = uk_date_text((long)((y << 16) | (m << 8) | d), buf).r1;
	STB(p, 0, ' ');
	p++;
	x = s / 3600;
	s = s - x * 3600;
	STB(p, 0, (u32)(x / 10 + '0'));
	STB(p, 1, (u32)(x - x / 10 * 10 + '0'));
	p += 2;
	STB(p, 0, ':');
	p++;
	x = s / 60;
	STB(p, 0, (u32)(x / 10 + '0'));
	STB(p, 1, (u32)(x - x / 10 * 10 + '0'));
	p += 2;
	STB(p, 0, 0);
	return p;
}

/* uk_fd_size_text : x0 = octets, x1 = tampon <- « 812 o », « 14 Ko », « 3,2 Mo »,
 * « 1,5 Go » -> x0 = fin du texte */
static long uk_fd_size_text(long n, long buf)
{
	long unit = S(uk_s_oct), dec = 10, v = n, sh, p, c; /* décimale (10 : aucune) */

	if (!ULT(n, 1024)) {
		unit = S(uk_s_ko);
		v = LSR(n, 10);
		if (!ULT(v, 1024)) {
			sh = 20; /* Mo, Go : une décimale sous 10 */
			unit = S(uk_s_mo);
			if (!ULT(n, 0x40000000)) {
				sh = 30;
				unit = S(uk_s_go);
			}
			v = LSR(n, sh);
			if (ULT(v, 10))
				dec = LSR((LSR(n, sh - 10) & 1023) * 10, 10);
		}
	}
	p = fmt_dec(v, buf);
	if (ULT(dec, 10)) {
		STB(p, 0, ',');
		STB(p, 1, (u32)(dec + '0'));
		p += 2;
	}
	do {
		c = LDB(unit, 0);
		unit++;
		STB(p, 0, c);
		p++;
	} while (c);
	return p - 1;
}

/* uk_fd_side_add : x0 = dialogue, x1 = libellé, x2 = chemin, x3 = icône (1
 * dossier, 2 volume) : une ligne du panneau ; donnée : chemin + icône */
static void uk_fd_side_add(long dlg, long label, long path, long icon)
{
	long n = LDX(dlg, DG_SN), d, i, c;

	if (!ULT(n, UK_FD_SMAX))
		return;
	STX(dlg, DG_SN, n + 1);
	d = LDX(dlg, DG_SP) + n * UK_FD_SPATH;
	i = 0;
	for (;;) {
		c = LDB(path, i);
		STB(d, i, c);
		if (!c)
			break;
		i++;
		if (!ULT(i, UK_FD_SPATH - 1)) {
			STB(d, i, 0);
			break;
		}
	}
	ui_list_add(LDX(dlg, DG_SIDE), label, d | icon);
}

/* uk_fd_places : x0 = dialogue : les accès rapides de SD0:/etc/places.ini
 * (« libellé = dossier » ; un dossier absent n'est pas montré) */
static void uk_fd_places(long dlg)
{
	u8 b[2048] __attribute__((aligned(16)));
	long sp = (long)(uintptr_t)b, fd, n, l, e, q, d, t, c, c24;

	fd = SYS2(SYS_OPEN, S(uk_s_placesf), O_READ);
	if (fd < 0)
		return;
	n = SYS3(SYS_READ, fd, sp, 2047);
	SYS1(SYS_CLOSE, fd);
	if (n <= 0)
		return;
	STB(sp, n, 0);
	l = sp; /* ligne */
	for (;;) {
		if (!LDB(l, 0))
			return;
		e = l; /* sa fin */
		for (;;) {
			c = LDB(e, 0);
			if (!c || c == 10)
				break;
			e++;
		}
		c24 = LDB(e, 0);
		STB(e, 0, 0);
		while ((c = LDB(l, 0)) == ' ') /* libellé */
			l++;
		if (!c || c == 13 || c == '#')
			goto next;
		q = l; /* « = » */
		for (;;) {
			c = LDB(q, 0);
			if (!c)
				goto next;
			if (c == '=')
				break;
			q++;
		}
		STB(q, 0, 0);
		d = q + 1; /* dossier : espaces autour */
		while (LDB(d, 0) == ' ')
			d++;
		t = q; /* fin du libellé : sans espaces */
		while (UGT(t, l) && LDB(t - 1, 0) == ' ') {
			STB(t - 1, 0, 0);
			t--;
		}
		t = e; /* fin du dossier */
		while (UGT(t, d)) {
			c = LDB(t - 1, 0);
			if (c != ' ' && c != 13)
				break;
			STB(t - 1, 0, 0);
			t--;
		}
		if (uk_fd_isdir(d))
			uk_fd_side_add(dlg, l, d, 1);
	next:
		if (!c24)
			return;
		l = e + 1;
	}
}

/* uk_fd_side : x0 = dialogue : le panneau latéral, emplacements puis volumes */
static void uk_fd_side(long dlg)
{
	u8 msg[MSG_SIZE] __attribute__((aligned(16)));
	long m = (long)(uintptr_t)msg, line = UKA(fdline), tmp = UKA(fdtmp);
	long cnt, v, h, i, k, c;

	STB(line, 0, '#'); /* emplacements (une section : « #titre ») */
	str_cat(line + 1, ui_tr(S(uk_s_places)));
	ui_list_add(LDX(dlg, DG_SIDE), line, 0);
	if (uk_fd_isdir(S(uk_s_home)))
		uk_fd_side_add(dlg, ui_tr(S(uk_s_homel)), S(uk_s_home), 1);
	uk_fd_places(dlg);
	STB(line, 0, '#'); /* volumes */
	str_cat(line + 1, ui_tr(S(uk_s_volumes)));
	ui_list_add(LDX(dlg, DG_SIDE), line, 0);
	uk_fd_side_add(dlg, S(uk_s_sd0c), S(uk_s_sd0), 2); /* SD0: toujours, d'abord */
	h = SYS2(SYS_SVC_OPEN, S(uk_s_vfs), 0);
	if (h < 0)
		return;
	STX(m, 0, VFS_VOLUMES);
	STX(m, MSG_B, 0);
	if (SYS2(SYS_SVC_CALL, h, m) < 0)
		return;
	cnt = LDX(m, MSG_B); /* nombre */
	if (UGT(cnt, 464 / UK_FD_VE))
		cnt = 464 / UK_FD_VE;
	v = m + MSG_DATA;
	for (; cnt; v += UK_FD_VE, cnt--) {
		if (LDB(v, 0) == 's' && LDB(v, 1) == 'd' && LDB(v, 2) == '0' && !LDB(v, 3))
			continue; /* (sd0 : déjà là) */
		i = 0; /* « nom » -> « NOM: », « NOM:/ » */
		for (;;) {
			c = LDB(v, i);
			if (!c)
				break;
			if ((u32)(c - 'a') <= 25)
				c -= 32;
			STB(line, i, c);
			i++;
			if (!ULT(i, 15))
				break;
		}
		STB(line, i, ':');
		i++;
		STB(line, i, 0);
		k = 0; /* (uk_fdline + « / ») */
		do {
			STB(tmp, k, LDB(line, k));
			k++;
		} while (ULT(k, i));
		STB(tmp, k, '/');
		k++;
		STB(tmp, k, 0);
		uk_fd_side_add(dlg, line, tmp, 2);
	}
}

/* uk_fd_side_sync : x0 = dialogue : la ligne du panneau dont le chemin contient
 * le dossier courant (la plus longue) est choisie */
static long uk_fd_side_sync(long dlg)
{
	long side = LDX(dlg, DG_SIDE), best = -1, bl = 0, n, p, cwd, i, c;

	n = LDX(side, UW_NITEMS);
	while (n) {
		n--;
		p = LDX(LDX(side, UW_ITEMS) + LSL(n, 4), 8) & ~3L;
		if (!p)
			continue;
		cwd = LDX(dlg, DG_CWD);
		i = 0;
		for (;;) {
			c = LDB(p, i);
			if (!c)
				break;
			if (c != LDB(cwd, i))
				goto cont;
			i++;
		}
		if (!i) /* (fin d'un composant du dossier ?) */
			continue;
		if (LDB(p, i - 1) != '/') {
			c = LDB(cwd, i);
			if (c && c != '/')
				continue;
		}
		if (ULE(i, bl))
			continue;
		bl = i;
		best = n;
	cont:;
	}
	return ui_w_set_value(side, best);
}

/* uk_fd_pick : x0 = dialogue, x1 = numéro du type : ses motifs dans DG_PATS */
static void uk_fd_pick(long dlg, long k)
{
	long f = FD_FILTER, p = f, c;

	for (;;) { /* « | » dans le texte ? */
		c = LDB(p, 0);
		p++;
		if (!c)
			goto one;
		if (c == '|')
			break;
	}
	for (;;) {
		for (;;) { /* le nom */
			c = LDB(f, 0);
			f++;
			if (!c)
				goto none;
			if (c == '|')
				break;
		}
		if (!k) /* (f : ses motifs) */
			goto set;
		for (;;) { /* le type suivant */
			c = LDB(f, 0);
			f++;
			if (!c)
				goto none;
			if (c == '|')
				break;
		}
		k--;
	}
one:
	if (!k)
		goto set;
none:
	f = 0;
set:
	STX(dlg, DG_PATS, f);
}

/* uk_fd_kinds : x0 = dialogue : la liste des types de fichiers (ou le nombre
 * d'éléments), et les motifs du premier type */
static void uk_fd_kinds(long dlg)
{
	long f = FD_FILTER, dd, p, b, i, c;

	if (LDX(dlg, DG_KIND) == 2 || !f || !LDB(f, 0)) { /* (un dossier : pas de types) */
		STX(dlg, DG_COUNT, ui_label(0, 0, 200, 0, S(uk_empty), ULB_DIM));
		return;
	}
	dd = ui_dropdown(0, 0, 200, 0, 0);
	STX(dlg, DG_FILT, dd);
	p = f; /* « | » dans le texte ? */
	for (;;) {
		c = LDB(p, 0);
		p++;
		if (!c)
			goto one;
		if (c == '|')
			break;
	}
	b = UKA(fdline);
	for (;;) { /* --- « Nom|motifs|Nom|motifs » */
		if (!LDB(f, 0))
			goto first;
		i = 0; /* « Nom (motifs) » */
		for (;;) {
			c = LDB(f, 0);
			if (!c)
				goto first; /* (un nom sans motifs : pas un type) */
			f++;
			if (c == '|')
				break;
			if (UGE(i, 120))
				continue;
			STB(b, i, c);
			i++;
		}
		STB(b, i, ' ');
		i++;
		STB(b, i, '(');
		i++;
		for (;;) {
			c = LDB(f, 0);
			if (!c)
				break;
			f++;
			if (c == '|')
				break;
			if (UGE(i, 240))
				continue;
			STB(b, i, c);
			i++;
		}
		STB(b, i, ')');
		i++;
		STB(b, i, 0);
		ui_list_add(dd, b, 0);
	}
one: /* --- un filtre : lui, ou tout */
	ui_list_add(dd, f, 0);
	ui_list_add(dd, ui_tr(S(uk_s_allfiles)), 0);
first:
	ui_w_set_value(dd, 0);
	uk_fd_pick(dlg, 0);
}

/* uk_fd_layout : x0 = dialogue : place ses widgets (taille DG_W x DG_H) */
static void uk_fd_layout(long dlg)
{
	long ctl, w, h, by, x, i, ly, lb, list, cw, b, bw;

	ctl = LDX(uk_met(), MT_CTL);
	w = LDX(dlg, DG_W);
	h = LDX(dlg, DG_H);
	by = h - ctl - 12; /* y des boutons */
	x = 12; /* haut : boutons carrés, chemin */
	for (i = 0; ULT(i, 3); i++) { /* (Retour, Avant, Parent) */
		b = i == 2 ? LDX(dlg, DG_UP) : LDX(dlg + DG_BACK, i * 8);
		ui_w_move(b, x, 12, ctl, ctl);
		x = x + ctl + 4;
	}
	x += 4;
	ui_w_move(LDX(dlg, DG_TB), x, 12, w - (12 + FRAME_CAP_W) - x, ctl); /* (à gauche des boutons de la fenêtre) */
	ly = ctl + 20; /* y du panneau et de la liste */
	lb = by - 20; /* bas de la liste */
	if (LDX(dlg, DG_NAME))
		lb = lb - ctl - 8;
	x = 12; /* panneau (fenêtre étroite : aucun) */
	if (!ULT(w, 520)) {
		ui_w_move(LDX(dlg, DG_SIDE), 12, ly, UK_FD_SIDE, lb - ly);
		ui_w_show(LDX(dlg, DG_SIDE), 1);
		x = 12 + UK_FD_SIDE + 8;
	} else {
		ui_w_show(LDX(dlg, DG_SIDE), 0);
	}
	list = LDX(dlg, DG_LIST);
	ui_w_move(list, x, ly, w - 12 - x, lb - ly);
	cw = LDW(list, UW_W) - (UK_FD_CSIZE + UK_FD_CDATE + 14); /* colonne du nom : le reste */
	if (cw < 80)
		cw = 80;
	STW(LDX(list, UW_COLS), 0, cw);
	ctl = LDX(uk_met(), MT_CTL);
	if (LDX(dlg, DG_NAME)) { /* « Nom : » et son champ */
		ui_w_move(LDX(dlg, DG_NAMEL), x, lb + 8, 56, ctl);
		ui_w_move(LDX(dlg, DG_NAME), x + 56, lb + 8, w - 12 - (x + 56), ctl);
	}
	b = LDX(dlg, DG_B0); /* boutons, de droite à gauche */
	bw = LDW(b, UW_W);
	x = w - 12 - bw;
	ui_w_move(b, x, by, bw, LDW(b, UW_H));
	b = LDX(dlg, DG_B0 + 8);
	bw = LDW(b, UW_W);
	x = x - 8 - bw;
	ui_w_move(b, x, by, bw, LDW(b, UW_H));
	bw = x - 28; /* types, ou nombre d'éléments */
	if (bw > 280)
		bw = 280;
	b = LDX(dlg, DG_FILT);
	if (!b)
		b = LDX(dlg, DG_COUNT);
	ui_w_move(b, 12, by, bw, ctl);
}

/* uk_fd_resize : (ui_on_resize du dialogue) x0 = largeur, x1 = hauteur */
static long uk_fd_resize(long w, long h)
{
	long dlg = uk.win_dlg;

	STX(dlg, DG_W, w);
	STX(dlg, DG_H, h);
	uk_fd_layout(dlg);
	uk_dlg_bar(dlg);
	ui_w_draw(0);
	return ui_update_all();
}

/* uk_tr_copy : x0 = destination, x1 = texte : copie sa traduction -> x0 = fin */
static long uk_tr_copy(long d, long text)
{
	long s = ui_tr(text), c;

	do {
		c = LDB(s, 0);
		s++;
		STB(d, 0, c);
		d++;
	} while (c);
	return d - 1;
}

/* uk_fd_line : (uk_fd_load) dialogue, 1 dossier : la ligne de uk_dirent dans
 * uk_fdline : « nom<tab>taille<tab>date<tab>octets » (la dernière colonne,
 * cachée, trie par taille) */
static void uk_fd_line(long dlg, long dir)
{
	long s = UKA(dirent) + DIRENT_NAME, d = UKA(fdline), n = 240, size, t, c;

	for (;;) {
		c = LDB(s, 0);
		s++;
		if (!c)
			break;
		STB(d, 0, c);
		d++;
		if (!--n)
			break;
	}
	STB(d, 0, 9);
	d++;
	size = LDX(UKA(dirent), DIRENT_SIZE);
	if (!dir)
		d = uk_fd_size_text(size, d);
	else
		d = uk_tr_copy(d, S(uk_s_dir));
	STB(d, 0, 9);
	d++;
	if (ULT(LDX(LDX(dlg, DG_LIST), UW_NITEMS), UK_FD_STATS)) { /* la date (un appel à vfs par élément) */
		uk_path_join(UKA(fdpath), LDX(dlg, DG_CWD), UKA(dirent) + DIRENT_NAME, UK_PATH_MAX);
		t = uk_fd_date(UKA(fdpath));
		if (t)
			d = uk_fd_date_text(t, d);
	}
	STB(d, 0, 9);
	d++;
	d = fmt_dec(dir ? 0 : size, d); /* octets (dossiers : 0) */
	STB(d, 0, 0);
}

/* uk_fd_sort : x0 = dialogue : trie la liste, dossiers d'abord, selon
 * DG_SORTC, DG_SORTD */
static void uk_fd_sort(long dlg)
{
	long list = LDX(dlg, DG_LIST), col = LDX(dlg, DG_SORTC), n;

	if (col == 1) /* Taille : les octets (colonne cachée) */
		col = 3;
	n = LDX(list, UW_NITEMS);
	STX(list, UW_NITEMS, LDX(dlg, DG_NDIR)); /* les dossiers */
	uk_ls_sort_range(list, col, LDX(dlg, DG_SORTD), 0);
	STX(list, UW_NITEMS, n); /* puis les fichiers */
	uk_ls_sort_range(list, col, LDX(dlg, DG_SORTD), LDX(dlg, DG_NDIR));
	STX(list, UW_SORT, LDX(dlg, DG_SORTC)); /* (la flèche : la colonne montrée) */
	uk_w_refresh(list);
}

/* uk_fd_load : x0 = dialogue : lit son dossier dans la liste (dossiers d'abord ;
 * Dossier : les dossiers seulement), met à jour les boutons, le panneau, le
 * nombre */
static long uk_fd_load(long dlg)
{
	long list, pass, fd, e, c, cnt, p, r;

	ui_w_set_text(LDX(dlg, DG_TB), LDX(dlg, DG_CWD)); /* le chemin dans son champ */
	list = LDX(dlg, DG_LIST);
	ui_list_clear(list);
	pass = 1; /* passe : 1 dossiers, 0 fichiers */
	for (;;) {
		fd = SYS2(SYS_OPEN, LDX(dlg, DG_CWD), O_READ);
		if (fd >= 0) {
			for (;;) {
				if (SYS2(SYS_READDIR, fd, UKA(dirent)) != 1)
					break;
				e = UKA(dirent);
				c = LDB(e, DIRENT_NAME); /* « . », « .. » : sautés */
				if (c == '.') {
					c = LDB(e, DIRENT_NAME + 1);
					if (!c)
						continue;
					if (c == '.' && !LDB(e, DIRENT_NAME + 2))
						continue;
				}
				if ((LSR(LDX(e, DIRENT_ATTR), 4) & 1) != pass) /* ATTR_DIR */
					continue;
				if (!pass && !uk_fd_match(e + DIRENT_NAME, LDX(dlg, DG_PATS))) /* un fichier : le filtre */
					continue;
				uk_fd_line(dlg, pass);
				ui_list_add(list, UKA(fdline), pass); /* donnée : 1 dossier (icône) */
			}
			SYS1(SYS_CLOSE, fd);
		}
		if (!pass)
			break;
		STX(dlg, DG_NDIR, LDX(list, UW_NITEMS));
		if (LDX(dlg, DG_KIND) == 2)
			break;
		pass = 0;
	}
	uk_fd_sort(dlg);
	ui_w_set_value(list, -1);
	ui_w_enable(LDX(dlg, DG_BACK), LDX(dlg, DG_HPOS) != 0); /* Retour, Avant, Parent */
	ui_w_enable(LDX(dlg, DG_FWD), ULT(LDX(dlg, DG_HPOS) + 1, LDX(dlg, DG_HN)));
	uk_path_set(UKA(fdtmp), LDX(dlg, DG_CWD));
	ui_w_enable(LDX(dlg, DG_UP), uk_path_up(UKA(fdtmp)));
	r = uk_fd_side_sync(dlg);
	cnt = LDX(dlg, DG_COUNT); /* nombre d'éléments */
	if (!cnt)
		return r; /* (x0 de uk_fd_side_sync, comme l'asm) */
	p = fmt_dec(LDX(list, UW_NITEMS), UKA(numbuf));
	STB(p, 0, ' ');
	uk_tr_copy(p + 1, S(uk_s_items));
	return ui_w_set_text(cnt, UKA(numbuf));
}

/* uk_fd_hist_put : x0 = dialogue : le dossier courant dans l'historique (à sa place) */
static void uk_fd_hist_put(long dlg)
{
	uk_path_set(LDX(dlg, DG_HIST) + LDX(dlg, DG_HPOS) * UK_PATH_MAX, LDX(dlg, DG_CWD));
}

/* uk_fd_go : x0 = dialogue, x1 = chemin : y aller (Retour y reviendra) */
static void uk_fd_go(long dlg, long path)
{
	long pos;

	uk_path_set(LDX(dlg, DG_CWD), path);
	pos = LDX(dlg, DG_HPOS) + 1;
	if (!ULT(pos, UK_FD_HIST)) { /* plein : le plus vieux sort */
		uk_memcpy(LDX(dlg, DG_HIST), LDX(dlg, DG_HIST) + UK_PATH_MAX, (UK_FD_HIST - 1) * UK_PATH_MAX);
		pos = UK_FD_HIST - 1;
	}
	STX(dlg, DG_HPOS, pos);
	STX(dlg, DG_HN, pos + 1);
	uk_fd_hist_put(dlg);
	uk_fd_load(dlg);
}

/* uk_fd_step : x0 = dialogue, x1 = -1 Retour / 1 Avant */
static void uk_fd_step(long dlg, long dir)
{
	long pos = LDX(dlg, DG_HPOS) + dir;

	if (pos < 0)
		return;
	if (!ULT(pos, LDX(dlg, DG_HN)))
		return;
	STX(dlg, DG_HPOS, pos);
	uk_path_set(LDX(dlg, DG_CWD), LDX(dlg, DG_HIST) + pos * UK_PATH_MAX);
	uk_fd_load(dlg);
}

/* uk_fd_parent : x0 = dialogue : le dossier parent */
static void uk_fd_parent(long dlg)
{
	uk_path_set(UKA(fdtmp2), LDX(dlg, DG_CWD));
	if (!uk_path_up(UKA(fdtmp2)))
		return;
	uk_fd_go(dlg, UKA(fdtmp2));
}

/* uk_fd_key : x0 = dialogue, x1 = code, x2 = mods : touche laissée par les
 * widgets -> 1 prise */
static long uk_fd_key(long dlg, long code, long mods)
{
	if ((u32)code == KEY_BACKSPACE) {
		uk_fd_parent(dlg);
		return 1;
	}
	if (!((u32)mods & 0x44)) /* Alt */
		return 0;
	if ((u32)code == KEY_LEFT) {
		uk_fd_step(dlg, -1);
		return 1;
	}
	if ((u32)code != KEY_RIGHT)
		return 0;
	uk_fd_step(dlg, 1);
	return 1;
}

/* uk_fd_enter : x0 = dialogue, x1 = nom (jusqu'à la tabulation) : entre dans ce dossier */
static void uk_fd_enter(long dlg, long name)
{
	uk_path_join(UKA(fdline), LDX(dlg, DG_CWD), name, UK_PATH_MAX);
	uk_fd_go(dlg, UKA(fdline));
}

/* uk_fd_accept : x0 = dialogue, x1 = chemin choisi : dans le tampon du
 * programme, fin (1) */
static void uk_fd_accept(long dlg, long path)
{
	long d = LDX(dlg, DG_BUF), n = LDX(dlg, DG_SIZE) - 1, i = 0, c;

	for (;;) {
		if (UGE(i, n)) {
			STB(d, i, 0);
			break;
		}
		c = LDB(path, i);
		STB(d, i, c);
		if (!c)
			break;
		i++;
	}
	ui_dialog_close(dlg, 1);
}

/* uk_fd_name_path : x0 = dialogue, x1 = nom tapé : son chemin dans uk_fdpath (un
 * chemin complet reste tel quel) ; Enregistrer : sans extension, celle du type */
static void uk_fd_name_path(long dlg, long name)
{
	long i = 0, c, p, q, d;

	for (;;) { /* « : » : un chemin complet */
		c = LDB(name, i);
		if (!c) {
			uk_path_join(UKA(fdpath), LDX(dlg, DG_CWD), name, UK_PATH_MAX);
			break;
		}
		i++;
		if (c == ':') {
			uk_path_join(UKA(fdpath), S(uk_empty), name, UK_PATH_MAX);
			break;
		}
	}
	if (LDX(dlg, DG_KIND) != 1)
		return;
	p = LDX(dlg, DG_PATS); /* « *.ext » : le type a une extension ? */
	if (!p)
		return;
	if (LDB(p, 0) != '*' || LDB(p, 1) != '.')
		return;
	c = LDB(p, 2);
	if (c == '*' || !c)
		return;
	q = name; /* le nom en a-t-il une ? */
	for (;;) {
		c = LDB(q, 0);
		q++;
		if (!c)
			break;
		if (c == '.')
			return;
	}
	d = UKA(fdpath);
	i = 0;
	while (LDB(d, i))
		i++;
	p++; /* après « * » */
	for (;;) {
		c = LDB(p, 0);
		p++;
		if (!c || c == ';' || c == '|')
			break;
		if (!ULT(i, UK_PATH_MAX - 2))
			break;
		STB(d, i, c);
		i++;
	}
	STB(d, i, 0);
}

/* uk_fd_fn : fonction du dialogue de fichiers : x0 = dialogue, x1 = widget,
 * x2 = code (rendu : sans valeur, comme l'asm) */
static long uk_fd_fn(long dlg, long w, long code)
{
	long x, item, kind, t;
	ukr r;

	if (code != UN_ACTIVATE)
		goto side;
	if (w == LDX(dlg, DG_UP)) { /* Parent, Retour, Avant */
		uk_fd_parent(dlg);
		goto out;
	}
	if (w == LDX(dlg, DG_BACK)) {
		uk_fd_step(dlg, -1);
		goto out;
	}
	if (w == LDX(dlg, DG_FWD)) {
		uk_fd_step(dlg, 1);
		goto out;
	}
	goto path;
side:
	if (code != UN_CHANGE)
		goto path;
	if (w == LDX(dlg, DG_SIDE)) { /* une ligne du panneau */
		x = ui_w_get_value(w);
		if (x < 0)
			goto out;
		r = ui_list_get(w, x);
		t = r.r1 & ~3L;
		if (!t)
			goto out;
		uk_fd_go(dlg, t);
		goto out;
	}
	if (w == LDX(dlg, DG_FILT)) { /* un autre type */
		uk_fd_pick(dlg, ui_w_get_value(w));
		uk_fd_load(dlg);
		goto out;
	}
path:
	if (w == LDX(dlg, DG_TB)) { /* chemin tapé, Entrée */
		if (code != UN_ACTIVATE)
			goto out;
		if (!uk_fd_isdir(LDX(w, UW_TEXT))) {
			ui_w_set_text(w, LDX(dlg, DG_CWD)); /* (pas un dossier : le chemin revient) */
			goto out;
		}
		uk_fd_go(dlg, LDX(w, UW_TEXT));
		goto out;
	}
	if (w != LDX(dlg, DG_LIST))
		goto name;
	if (code == UN_SORT) { /* tri par un en-tête */
		STX(dlg, DG_SORTC, LDX(w, UW_SORT));
		STX(dlg, DG_SORTD, LDX(w, UW_ANCHOR));
		uk_fd_sort(dlg);
		goto out;
	}
	x = ui_w_get_value(w); /* élément choisi : texte, dossier ? */
	if (x < 0)
		goto out;
	r = ui_list_get(w, x);
	item = r.r0;
	if (code == UN_ACTIVATE)
		goto lact;
	if (r.r1) /* choix d'un fichier : son nom dans le champ */
		goto out;
	if (!LDX(dlg, DG_NAME))
		goto out;
	t = uk_field(item, 0);
	ui_w_set_text(LDX(dlg, DG_NAME), t);
	goto out;
lact:
	if (r.r1) { /* double-clic : dossier -> y entrer */
		uk_fd_enter(dlg, item);
		goto out;
	}
	if (LDX(dlg, DG_KIND)) /* fichier -> choisi (Ouvrir), ou comme le bouton par défaut */
		goto okb;
	uk_path_join(UKA(fdpath), LDX(dlg, DG_CWD), item, UK_PATH_MAX);
	uk_fd_accept(dlg, UKA(fdpath));
	goto out;
name:
	if (w == LDX(dlg, DG_NAME)) { /* Entrée dans le nom */
		if (code == UN_ACTIVATE)
			goto typed;
		goto out;
	}
	if (code != UN_ACTIVATE)
		goto out;
	if (w == LDX(dlg, DG_B0 + 8)) { /* Annuler */
		ui_dialog_close(dlg, 0);
		goto out;
	}
	if (w != LDX(dlg, DG_B0))
		goto out;
okb: /* --- le bouton principal */
	x = ui_w_get_value(LDX(dlg, DG_LIST));
	if (x < 0)
		goto nosel;
	r = ui_list_get(LDX(dlg, DG_LIST), x);
	item = r.r0;
	kind = LDX(dlg, DG_KIND);
	if (kind == 2)
		goto fold;
	if (!r.r1) /* un fichier : le nom */
		goto nosel;
	t = LDX(LDX(dlg, DG_NAME), UW_TEXT); /* un dossier : y entrer, sauf si Enregistrer a un nom tapé */
	if (kind && LDB(t, 0))
		goto typed;
	uk_fd_enter(dlg, item);
	goto out;
fold: /* Dossier : le dossier choisi */
	uk_path_join(UKA(fdpath), LDX(dlg, DG_CWD), item, UK_PATH_MAX - 2);
	uk_path_set(UKA(fdline), UKA(fdpath));
	uk_fd_accept(dlg, UKA(fdline));
	goto out;
nosel: /* rien de choisi : Dossier = le courant */
	if (LDX(dlg, DG_KIND) != 2)
		goto typed;
	uk_fd_accept(dlg, LDX(dlg, DG_CWD));
	goto out;
typed: /* Ouvrir, Enregistrer : le nom tapé */
	t = LDX(dlg, DG_NAME);
	if (!t)
		goto out;
	t = LDX(t, UW_TEXT);
	if (!LDB(t, 0))
		goto out;
	uk_fd_name_path(dlg, t);
	if (uk_fd_isdir(UKA(fdpath))) { /* un dossier : y aller */
		uk_fd_go(dlg, UKA(fdpath));
		ui_w_set_text(LDX(dlg, DG_NAME), S(uk_empty));
		goto out;
	}
	x = SYS2(SYS_STAT, UKA(fdpath), UKA(fdstat)); /* il existe ? */
	if (LDX(dlg, DG_KIND)) {
		if (x) /* Enregistrer : remplacer ? */
			goto add;
		if (ui_message(S(uk_s_save), ui_tr(S(uk_s_replace)), 2) != 1)
			goto out;
	} else if (x) { /* Ouvrir : il le faut */
		goto out;
	}
add:
	uk_fd_accept(dlg, UKA(fdpath));
out:
	return 0;
}

/* uk_fd_nav : (uk_file_dialog) x4 = symbole -> un bouton carré */
static long uk_fd_nav(long mt, long sym)
{
	long ctl = LDX(mt, MT_CTL);

	return ui_pushbutton(0, 0, ctl, ctl, sym, 0);
}

/* uk_file_dialog : x0 = dossier, x1 = genre (0 ouvrir, 1 enregistrer, 2
 * dossier), x2 = nom proposé, x3 = tampon, x4 = taille -> x0 = 1 / 0 */
static long uk_file_dialog(long dir, long kind, long name, long buf, long size)
{
	long mt, w, h, t, dlg, list, res, b, f;

	mt = uk_met();
	w = UK_FD_W; /* taille : dans l'écran */
	h = UK_FD_H;
	if (ui_screen(UKA(scr)) >= 0) {
		t = LDX(UKA(scr), 0) - 2 * FRAME_BORDER;
		if (w > t)
			w = t;
		t = LDX(UKA(scr), 8) - LDX(UKA(scr), 16) - LDX(UKA(scr), 24) - (FRAME_TITLE_H + FRAME_BORDER);
		if (h > t)
			h = t;
	}
	uk.dlg_role = WF_RESIZABLE;
	t = S(uk_s_open);
	if (kind == 1)
		t = S(uk_s_save);
	if (kind == 2)
		t = S(uk_s_folder);
	dlg = ui_dialog_begin(w, h, t);
	if (!dlg)
		return 0;
	STX(dlg, DG_BUF, buf);
	STX(dlg, DG_SIZE, size);
	STX(dlg, DG_KIND, kind);
	STX(dlg, DG_FN, (long)uk_fd_fn);
	STX(dlg, DG_KEYFN, (long)uk_fd_key);
	res = 0;
	t = uk_alloc(UK_PATH_MAX); /* dossier courant, historique, panneau */
	STX(dlg, DG_CWD, t);
	if (!t)
		goto end;
	t = uk_alloc(UK_FD_HIST * UK_PATH_MAX);
	STX(dlg, DG_HIST, t);
	if (!t)
		goto end;
	t = uk_alloc(UK_FD_SMAX * UK_FD_SPATH);
	STX(dlg, DG_SP, t);
	if (!t)
		goto end;
	name = uk_fd_start(dlg, dir, name); /* départ : le dossier, le nom proposé */
	uk_path_set(LDX(dlg, DG_HIST), LDX(dlg, DG_CWD));
	STX(dlg, DG_HN, 1);
	ui_min_size(520, 320); /* taille */
	ui_on_resize((long)uk_fd_resize);
	STX(dlg, DG_BACK, uk_fd_nav(mt, S(uk_s_left))); /* --- haut : Retour, Avant, Parent, chemin */
	STX(dlg, DG_FWD, uk_fd_nav(mt, S(uk_s_right)));
	STX(dlg, DG_UP, uk_fd_nav(mt, S(uk_s_upar)));
	STX(dlg, DG_TB, ui_textbox(0, 0, 100, LDX(mt, MT_CTL), 0));
	STX(dlg, DG_SIDE, ui_list(0, 0, 100, 100, UL_SIDEBAR | UL_ICONS)); /* --- panneau, liste : Nom, Taille, Date */
	list = ui_list(0, 0, 100, 100, UL_HEADER | UL_USERSORT | UL_ICONS);
	STX(dlg, DG_LIST, list);
	ui_list_column(list, ui_tr(S(uk_s_name)), 200, 0);
	ui_list_column(list, ui_tr(S(uk_s_size)), UK_FD_CSIZE, ULC_RIGHT);
	ui_list_column(list, ui_tr(S(uk_s_date)), UK_FD_CDATE, 0);
	if (kind != 2) { /* --- « Nom : » et son champ */
		t = ui_tr(S(uk_s_namec));
		STX(dlg, DG_NAMEL, ui_label(0, 0, 60, LDX(mt, MT_CTL), t, 0));
		t = ui_textbox(0, 0, 100, LDX(mt, MT_CTL), 0);
		STX(dlg, DG_NAME, t);
		ui_w_set_text(t, name);
	}
	t = S(uk_s_bopen);
	if (kind == 1)
		t = S(uk_s_bsave);
	if (kind == 2)
		t = S(uk_s_bchoose);
	b = ui_dialog_button(dlg, 0, t, 1, UB_DEFAULT, w - 12);
	ui_dialog_button(dlg, 1, S(uk_s_cancel), 0, 0, b);
	uk_fd_kinds(dlg); /* types de fichiers, ou nombre d'éléments */
	uk_fd_layout(dlg);
	ui_fill(0, 0, w, h, JET(TJ_FACE)); /* (un widget a pu se dessiner en 0, 0) */
	uk_dlg_bar(dlg);
	uk_fd_side(dlg);
	uk_fd_load(dlg);
	f = LDX(dlg, DG_NAME); /* focus : le nom, sinon la liste */
	if (!kind || !f)
		f = LDX(dlg, DG_LIST);
	ui_w_focus(f);
	if (kind && LDX(dlg, DG_NAME))
		ui_textbox_select(LDX(dlg, DG_NAME), 0, -1);
	res = ui_dialog_run(dlg, 0);
end:
	ui_dialog_end(dlg);
	return res;
}

/* uk_dialog_at : x0 = x, x1 = y (contenu, à l'écran), x2 = largeur, x3 =
 * hauteur, x4 = titre -> un dialogue à cette place */
static long uk_dialog_at(long x, long y, long wd, long ht, long title)
{
	uk.pop_place[0] = x;
	uk.pop_place[1] = y;
	uk.pop_place[2] = 1;
	return ui_dialog_begin(wd, ht, title);
}

/* x0, x1 = point de l'écran : le prochain ui_dialog_begin sera sans cadre, à ce
 * point, devant tout -> x0 = le rôle (comme l'asm) */
long ui_dialog_at(long x, long y)
{
	uk.pop_place[0] = x;
	uk.pop_place[1] = y;
	uk.pop_place[2] = 1;
	uk.dlg_role = WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_GRAB;
	return WF_FRAMELESS | WF_TOP | WF_NOLIST | WF_GRAB;
}

/* uk_dialog_under : x0 = widget, x1 = largeur, x2 = hauteur, x3 = titre ->
 * dialogue sous le widget, cadre compris (au-dessus s'il n'y a pas la place) */
long uk_dialog_under(long w, long wd, long ht, long title)
{
	ukr p = uk_win_pos();
	long x, top, y, lim;

	x = p.r0 + LDSW(w, UW_X);
	top = p.r1 + LDSW(w, UW_Y); /* haut du widget à l'écran */
	y = top + LDW(w, UW_H) + 2 + FRAME_TITLE_H;
	ui_screen(UKA(scr));
	lim = LDX(UKA(scr), 8) - LDX(UKA(scr), 16); /* réserve en bas */
	if (y + ht + FRAME_BORDER > lim)
		y = top - ht - (2 + FRAME_BORDER); /* au-dessus */
	return uk_dialog_at(x, y, wd, ht, title);
}
