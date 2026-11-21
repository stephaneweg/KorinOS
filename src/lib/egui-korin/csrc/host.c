/*
 * host.c -- le relais d'egui-korin vers UIKit (docs/EGUI.md) : une fenetre redimensionnable dont le
 * programme Rust dessine tous les pixels, ses evenements, le presse-papiers, le curseur, les dialogues
 * de fichiers. Les fonctions de UIKit passent par les relais generes (uikit_abi.h, libuikit.a) ; Rust
 * n'appelle que ce qui est ici, avec des types C simples.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <uikit.h>

static struct ui_vars ui;

/* la fenetre est-elle ouverte ? Avant, UIKit n'est pas charge : les menus donnes trop tot (muda les pose avant la
 * fenetre, VectorCraft, SoundCraft) sont gardes ici et envoyes par kh_open */
static int kh_ready;
static char *menu_pending;
static long *states_pending, states_pending_n;

/* un evenement : les champs EV_* de UIKit, dans cet ordre (src/lib/egui-korin/src/sys.rs) */
struct kh_event
{
	int32_t type, kind, x, y, code, ch, mods, win;
};

/* kits ouverts, fenetre creee -> 1 ; 0 : echec */
int kh_open (const char *title, int w, int h, int min_w, int min_h)
{
	if (!uikit_open (&ui))
		return 0;
	/* la taille demandee, ramenee a la place de l'ecran (sans le dock ni la barre de menus, et le cadre
	 * autour) : les apps de bureau demandent souvent plus que l'ecran du PC */
	long s[4] = { 0, 0, 0, 0 };
	ui_screen ((long) s);
	if (s[0] > 0 && s[1] > 0)
	{
		long aw = s[0] - 16, ah = s[1] - s[2] - s[3] - 16;
		if (aw < 320) aw = 320;
		if (ah < 240) ah = 240;
		if (w > aw) w = (int) aw;
		if (h > ah) h = (int) ah;
		if (min_w > aw) min_w = (int) aw;
		if (min_h > ah) min_h = (int) ah;
	}
	if (ui_window_ex (-1, -1, w, h, (long) title, WF_RESIZABLE) <= 0)
		return 0;
	if (min_w > 0 && min_h > 0)
		ui_min_size (min_w, min_h);
	kh_ready = 1;
	if (menu_pending)
		ui_menu ((long) menu_pending);	/* garde : UIKit retient le texte a son adresse */
	if (states_pending)
	{
		ui_menu_states ((long) states_pending, states_pending_n);
		free (states_pending);
		states_pending = 0;
	}
	return 1;
}

/* les pixels de la fenetre (0x00RRGGBB, pas = largeur * 4) ; ils changent apres un WEV_RESIZE */
uint32_t *kh_pixels (int *w, int *h)
{
	*w = (int) ui.w;
	*h = (int) ui.h;
	return ui.surf;
}

/* attend un evenement (ms ; 0 : sans attendre) -> son type WEV_* (0 : aucun). Le redimensionnement
 * est fait ici (uikit_w_event : nouveau tampon) : apres un WEV_RESIZE, relire kh_pixels. */
int kh_event (int timeout_ms, struct kh_event *e)
{
	long t = ui_event (timeout_ms);
	memcpy (e, ui.ev, sizeof *e);
	e->type = (int32_t) t;
	if (t > 0 && t != WEV_CLOSE)
	{
		long code = 0, taken = 0;
		uikit_w_event (&code, &taken);
	}
	return (int) t;
}

/* montre une zone dessinee */
void kh_update (int x, int y, int w, int h)
{
	ui_update (x, y, w, h);
}

void kh_title (const char *title) { ui_title ((long) title); }

/* l'ecran : largeur, hauteur, place reservee en bas (dock) et en haut (barre de menus) */
void kh_screen (int *w, int *h, int *bottom, int *top)
{
	long s[4] = { 0, 0, 0, 0 };
	ui_screen ((long) s);
	*w = (int) s[0];
	*h = (int) s[1];
	*bottom = (int) s[2];
	*top = (int) s[3];
}

/* les commandes de fenetre d'egui (ViewportCommand) */
void kh_minimize (void) { ui_minimize (); }
void kh_maximize (void) { ui_maximize (); }
void kh_drag (void) { ui_window_drag (); }
void kh_resize (int w, int h) { ui_resize (w, h); }
void kh_cursor (int shape) { ui_cursor (shape); }
void kh_min_size (int w, int h) { ui_min_size (w, h); }

/* 1 : le theme est sombre (jeton TJ_DARK) */
int kh_dark (void)
{
	const unsigned *t = (const unsigned *) ui_tokens ();
	return t && (t[TJ_DARK / 4] & 1) ? 1 : 0;
}

/* une couleur du theme, 0x00RRGGBB (tj : decalage TJ_* en octets) */
unsigned kh_token (int tj)
{
	const unsigned *t = (const unsigned *) ui_tokens ();
	return t ? t[tj / 4] & 0xFFFFFF : 0;
}

/* presse-papiers (texte UTF-8) */
void kh_clip_set (const char *s, long n) { ui_clip_set ((long) s, n); }
long kh_clip_get (char *buf, long cap) { return ui_clip_get ((long) buf, cap); }

/* dialogues de fichiers : 1 si choisi (chemin dans buf) ; filtre « *.docx;*.odt » ou 0 */
int kh_file_open (const char *dir, const char *filter, char *buf, long cap)
{
	ui_file_filter ((long) filter);
	return ui_file_open ((long) dir, (long) buf, cap) == 1;
}

int kh_file_save (const char *dir, const char *name, const char *filter, char *buf, long cap)
{
	ui_file_filter ((long) filter);
	return ui_file_save ((long) dir, (long) (name ? name : ""), (long) buf, cap) == 1;
}

/* une boite de message (UM_OK) */
void kh_message (const char *title, const char *text) { ui_message ((long) title, (long) text, UM_OK); }

/* une boite de message aux boutons choisis (0 UM_OK, 1 UM_OKCANCEL, 2 UM_YESNO, 3 UM_YESNOCANCEL)
 * -> 1 OK / Oui, 0 Annuler / Non (Echap), 2 Non de UM_YESNOCANCEL */
int kh_message_ex (const char *title, const char *text, int buttons)
{
	return (int) ui_message ((long) title, (long) text, buttons);
}

/* le choix d'un dossier : 1 si choisi (chemin dans buf) */
int kh_folder_choose (const char *dir, char *buf, long cap)
{
	return ui_folder_choose ((long) dir, (long) buf, cap) == 1;
}

/* ouvre un fichier ou une adresse avec l'app associee */
void kh_open_with (const char *path) { ui_open_with ((long) path); }

/* la barre de menus de Korin : le texte des menus de la fenetre (lignes M, I, -, S ... E ; 0 : plus de
 * menu) ; l'adresse change a chaque nouveau texte (UIKit reconnait un texte deja traduit a son adresse) */
long kh_menu_set (const char *text)
{
	if (kh_ready)
		return ui_menu ((long) text);
	free (menu_pending);
	menu_pending = text ? strdup (text) : 0;
	return 0;
}

/* les etats de plusieurs entrees (paires id, UMS_* en long), un seul envoi */
long kh_menu_states (const long *pairs, long n)
{
	if (kh_ready)
		return ui_menu_states ((long) pairs, n);
	long *p = n > 0 ? malloc ((size_t) n * 2 * sizeof (long)) : 0;
	if (!p)
		return 0;
	if (states_pending)	/* les plus recents l'emportent : on les met apres */
	{
		long *q = realloc (states_pending, (size_t) (states_pending_n + n) * 2 * sizeof (long));
		free (p);
		if (!q)
			return 0;
		memcpy (q + states_pending_n * 2, pairs, (size_t) n * 2 * sizeof (long));
		states_pending = q;
		states_pending_n += n;
		return 0;
	}
	memcpy (p, pairs, (size_t) n * 2 * sizeof (long));
	states_pending = p;
	states_pending_n = n;
	return 0;
}

/* la touche du dernier evenement (kh_event) est-elle le raccourci d'une entree des menus ? -> son id (-1 : non) */
long kh_menu_key (void) { return kh_ready ? ui_menu_key (0) : -1; }
