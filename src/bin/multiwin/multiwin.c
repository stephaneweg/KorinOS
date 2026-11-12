/* multiwin - plusieurs fenêtres non modales dans un programme (UIKit,
 * kits/uikit/uikit_window.inc), comme multiwin.asm : une seule boucle
 * d'événements, chaque fenêtre a ses widgets, son focus, sa taille, ses menus
 * et sa fermeture. Le programme finit (code 0) quand la dernière fenêtre est
 * fermée.
 *
 * Dans chaque fenêtre : un bandeau dessiné à la main (sa couleur, son numéro),
 * un compteur, un champ de texte, une horloge que la boucle met à jour dans
 * toutes les fenêtres quelle que soit la courante, et les boutons :
 * Nouvelle fenêtre, Compter, Compter dans la fenêtre 1 (un widget d'une autre
 * fenêtre), Message (dialogue modal), Ouvrir (sélecteur de fichiers).
 * Comme l'asm : 8 fenêtres au plus (« Nouvelle fenêtre » ne fait rien
 * au-delà), numéros jamais réutilisés, l'horloge compte les attentes d'une
 * seconde sans événement (pas le temps écoulé), texte du bandeau en blanc. */
#include "korinrt.h"
#include "uikit_import.h"

#define MAX_WINS 8
#define WIN_W 420
#define WIN_H 280
#define BAND_H 44
#define BTN_N 5
/* actions (donnée des boutons, entrées de menu) */
#define ACT_NEW 1
#define ACT_COUNT 2
#define ACT_COUNT1 3
#define ACT_MESSAGE 4
#define ACT_OPEN 5
#define ACT_CLOSE 6

/* un emplacement de fenêtre */
struct slot {
	long win;   /* poignée (0 : libre) */
	u64 num;    /* numéro */
	u64 count;  /* compteur */
	long lcount; /* étiquette du compteur */
	long ltime; /* étiquette de l'horloge */
	long text;  /* champ de texte */
	long btn[BTN_N];
};

static struct krt_ui ui;
static struct slot slots[MAX_WINS];
static u64 serial;
static u64 seconds;
static char buf[300];
static char path[256];

static const char s_title[] = "Fenêtre";
static long on_resize(long w, long h);
static const char menu_main[] = "MFenêtres\n"
				"I1\tNouvelle fenêtre\tCtrl+N\n"
				"I2\tCompter\tCtrl+K\n"
				"I6\tFermer\tCtrl+W\n";
static const char menu_other[] = "MAutre\n"
				 "I2\tCompter ici\tCtrl+K\n"
				 "I6\tFermer\tCtrl+W\n";

/* fenêtre -> son emplacement (0 : aucun) */
static struct slot *slot_of(long win)
{
	for (int i = 0; i < MAX_WINS; i++)
		if (slots[i].win == win)
			return &slots[i];
	return 0;
}

/* fenêtre : fermée, widgets détruits, emplacement libre */
static void close_win(long win)
{
	struct slot *s = slot_of(win);
	if (s)
		s->win = 0;
	ui_window_close(win);
}

/* le texte du compteur (widget redessiné dans sa fenêtre) */
static void show_count(struct slot *s)
{
	char *e = str_cat(buf, "Compteur : ");
	e = fmt_dec(s->count, e);
	*e = 0;
	ui_w_set_text(s->lcount, (long)buf);
}

/* compteur + 1, dans sa fenêtre */
static void count_in(struct slot *s)
{
	if (!s->win)
		return;
	s->count++;
	show_count(s);
}

/* une seconde de plus : l'horloge de toutes les fenêtres (widgets de fenêtres
 * qui ne sont pas la courante) */
static void tick(void)
{
	seconds++;
	for (int i = 0; i < MAX_WINS; i++) {
		struct slot *s = &slots[i];
		if (!s->win)
			continue;
		char *e = str_cat(buf, "Ouverte depuis ");
		e = fmt_dec(seconds, e);
		*e++ = ' ';
		*e++ = 's';
		*e = 0;
		ui_w_set_text(s->ltime, (long)buf);
	}
}

/* (fenêtre courante) le bandeau : sa couleur, son numéro, sa taille */
static void draw_band(struct slot *s)
{
	u8 *tj = (u8 *)ui_jetons();
	long c = ui_tone(*(u32 *)(tj + TJ_ACCENT), (long)(((s->num & 3) << 4) + 96)); /* une teinte par fenêtre */
	ui_fill(0, 0, ui.w, BAND_H, c);
	char *e = str_cat(buf, s_title);
	*e++ = ' ';
	e = fmt_dec(s->num, e);
	*e = 0;
	ui_text((long)buf, 14, 29, 0x00FFFFFF, 18, (long)ui.font_b, 0);
}

/* (new_win) le bouton de rang *rank : texte, action */
static void add_button(struct slot *s, int *rank, const char *text, long act)
{
	long w = ui_pushbutton(16, BAND_H + 12 + 64 + 36 * *rank, 240, 0, (long)text, 0);
	s->btn[*rank] = w;
	ui_w_on(w, 0, act);
	(*rank)++;
}

/* ouvre une fenêtre de plus, dans un emplacement libre */
static void new_win(void)
{
	struct slot *s = slot_of(0);
	if (!s)
		return;
	s->num = ++serial;
	s->count = 0;
	char *e = str_cat(buf, s_title);
	*e++ = ' ';
	e = fmt_dec(s->num, e);
	*e = 0;
	long win = ui_window_ex(-1, -1, WIN_W, WIN_H + 96, (long)buf, WF_RESIZABLE);
	if (win < 0)
		return;
	s->win = win;
	ui_min_size(320, 300); /* taille, fonction de cette fenêtre */
	ui_on_resize((long)on_resize, 300); /* (x1 : 300, laissé par ui_min_size, ignoré) */
	ui_menu((long)(s->num == 1 ? menu_main : menu_other)); /* ses menus : la première, les autres */
	u8 *tj = (u8 *)ui_jetons(); /* le fond suit le thème */
	ui_fill(0, 0, ui.w, ui.h, *(u32 *)(tj + TJ_FACE));
	draw_band(s);
	s->lcount = ui_label(16, BAND_H + 12, 180, 24, (long)"", ULB_BOLD); /* le compteur, l'horloge, le champ */
	s->ltime = ui_label(212, BAND_H + 12, 180, 24, (long)"", ULB_DIM);
	s->text = ui_textbox(16, BAND_H + 44, WIN_W - 32, 30, 0);
	int rank = 0; /* les boutons */
	add_button(s, &rank, "Nouvelle fenêtre", ACT_NEW);
	add_button(s, &rank, "Compter", ACT_COUNT);
	add_button(s, &rank, "Compter dans la fenêtre 1", ACT_COUNT1);
	add_button(s, &rank, "Message (modal)", ACT_MESSAGE);
	add_button(s, &rank, "Ouvrir un fichier…", ACT_OPEN);
	show_count(s);
	ui_w_focus(s->text);
	ui_w_draw(0);
	ui_update_all();
}

/* (ui_on_resize) largeur, hauteur : la fenêtre de l'événement est la
 * courante ; le champ suit la largeur -> comme l'asm, ce que rend
 * ui_update_all (0 sans emplacement) */
static long on_resize(long w, long h)
{
	(void)h;
	struct slot *s = slot_of(ui.win);
	if (!s)
		return 0;
	u8 *tj = (u8 *)ui_jetons();
	ui_fill(0, 0, ui.w, ui.h, *(u32 *)(tj + TJ_FACE));
	draw_band(s);
	ui_w_move(s->text, 16, BAND_H + 44, w - 32, 30);
	ui_w_draw(0);
	return ui_update_all();
}

/* fenêtre, action */
static void action(long win, long act)
{
	struct slot *s = slot_of(win);
	if (!s)
		return;
	switch (act) {
	case ACT_NEW:
		new_win();
		break;
	case ACT_COUNT:
		count_in(s);
		break;
	case ACT_COUNT1: /* le compteur de la fenêtre 1 (si elle vit) */
		count_in(&slots[0]);
		break;
	case ACT_MESSAGE: { /* le dialogue bloque tout le programme */
		long prev = ui_window_use(s->win);
		ui_message((long)s_title,
			   (long)"Ce dialogue bloque toutes les fenêtres du programme : un clic sur l'une d'elles "
				 "le ramène devant.",
			   UM_OK);
		ui_window_use(prev);
		break;
	}
	case ACT_OPEN:
		ui_file_filter((long)"Textes|*.txt;*.ini|Images|*.png;*.bmp|Tous les fichiers|*");
		if (ui_file_open(0, (long)path, 256))
			ui_w_set_text(s->text, (long)path); /* le chemin choisi dans le champ de cette fenêtre */
		break;
	case ACT_CLOSE:
		close_win(s->win);
		break;
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ui_init((long)&ui);
	new_win();
	for (;;) {
		long type = ui_event(1000);
		if (!type) {
			tick(); /* une seconde : l'horloge de chaque fenêtre */
			continue;
		}
		u32 evwin = *(u32 *)(ui.ev + EV_WIN);
		if (type == WEV_CLOSE) { /* la case de fermeture d'une fenêtre */
			close_win(evwin);
		} else if (type == WEV_MENU) {
			action(evwin, *(u32 *)(ui.ev + EV_CODE));
		} else {
			long id = ui_menu_key(0); /* routage : par EV_WIN */
			if (id >= 0) {
				action(*(u32 *)(ui.ev + EV_WIN), id);
			} else {
				struct krt_r4 r = krt_call_r4(ui_w_event, 0, 0, 0, 0);
				if (r.r1 != UN_ACTIVATE)
					continue;
				long data = ui_w_user(r.r0);
				if (!data)
					continue;
				action(ui_w_window(r.r0), data);
			}
		}
		if (!ui_window_count())
			_exit(0);
	}
}
