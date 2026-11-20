/*
 * host.c -- le relais d'egui-korin vers UIKit (docs/EGUI.md) : une fenetre redimensionnable dont le
 * programme Rust dessine tous les pixels, ses evenements, le presse-papiers, le curseur, les dialogues
 * de fichiers. Les fonctions de UIKit passent par les relais generes (uikit_abi.h, libuikit.a) ; Rust
 * n'appelle que ce qui est ici, avec des types C simples.
 */
#include <stdint.h>
#include <string.h>
#include <uikit.h>

static struct ui_vars ui;

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
	if (ui_window_ex (-1, -1, w, h, (long) title, WF_RESIZABLE) <= 0)
		return 0;
	if (min_w > 0 && min_h > 0)
		ui_min_size (min_w, min_h);
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

/* ouvre un fichier ou une adresse avec l'app associee */
void kh_open_with (const char *path) { ui_open_with ((long) path); }
