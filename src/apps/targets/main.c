/*
 * targets -- Cibles, petit jeu d'adresse original : des cibles apparaissent au hasard et retrecissent ;
 * un clic dessus rapporte d'autant plus de points qu'elle est petite. Trois cibles disparues sans avoir
 * ete touchees : fin de partie. Meilleur score dans /targets.sco.
 * Port en C de targets.asm (memes regles, memes couleurs du terrain, memes textes), un seul source pour
 * le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <armpi/syscall.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <uikit.h>

#define WIN_W	560
#define WIN_H	460
#define PLAY_H	400
#define MAX_T	8
#define FRAME	30			/* ms par image */
#define LIVES	3
#define BTN_X	(WIN_W - 130)
#define BTN_Y	(PLAY_H + 14)
#define BTN_W	116
#define BTN_H	32
#define SCORE_FILE "/targets.sco"

#define SYS_TIME_MS_ 0x19

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static unsigned tok (int off) { return ((const unsigned *) ui_tokens ())[off / 4]; }

/* une cible : rayon en 1/256 de pixel */
struct target { long on, x, y, r, r0; unsigned col; };
static struct target targets[MAX_T];
static const unsigned palette[6] = { 0x00E05A55, 0x003D8BE0, 0x0045B858, 0x00E8A33A, 0x00A04BC8, 0x0029B6C6 };

/* la partie : score, cibles manquees, en cours, images depuis la derniere cible, intervalle (images) */
static struct { unsigned long score, missed, running, frames, interval; } game;
static unsigned long best, next_frame, rng;

static unsigned long time_ms (void) { return (unsigned long) armpi_syscall (SYS_TIME_MS_, 0, 0, 0, 0, 0, 0); }

/* xorshift64 */
static unsigned long rnd (void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}
static long rand_below (long n) { return (long) ((rnd () >> 16) % (unsigned long) n); }

static void clear_targets (void) { memset (targets, 0, sizeof targets); }

static void new_game (void)
{
	clear_targets ();
	game.score = game.missed = 0;
	game.running = 1;
	game.frames = 0;
	game.interval = 900 / FRAME;
}

/* ---- meilleur score ---- */
static void load_best (void)
{
	char buf[32];
	int fd = open (SCORE_FILE, O_RDONLY);
	if (fd < 0)
		return;
	long n = read (fd, buf, 31);
	close (fd);
	unsigned long v = 0;
	for (long i = 0; i < n && (unsigned) (buf[i] - '0') <= 9; i++)
		v = v * 10 + (unsigned) (buf[i] - '0');
	best = v;
}

static void save_best (void)
{
	if (game.score <= best)
		return;
	best = game.score;
	char buf[32];
	int n = snprintf (buf, sizeof buf, "%lu\n", best);
	int fd = open (SCORE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	write (fd, buf, (size_t) n);
	close (fd);
}

/* place une nouvelle cible dans un emplacement libre */
static void spawn (void)
{
	struct target *t = 0;
	for (int i = 0; i < MAX_T; i++)
		if (!targets[i].on)
		{
			t = &targets[i];
			break;
		}
	if (!t)
		return;
	long r = rand_below (20) + 26;		/* rayon 26..45 */
	t->r = r << 8;
	t->r0 = r;
	t->x = rand_below (WIN_W - 2 * 48) + 48;	/* centre a l'interieur de la zone */
	t->y = rand_below (PLAY_H - 2 * 48) + 48;
	t->col = palette[rand_below (6)];
	t->on = 1;
	if (game.interval > 12)			/* accelere les apparitions */
		game.interval--;
}

/* une image de jeu : apparition, retrecissement, cibles manquees */
static void step (void)
{
	if (!game.running)
		return;
	if (++game.frames >= game.interval)
	{
		game.frames = 0;
		spawn ();
	}
	long speed = (long) (game.score >> 4) + 40;	/* (x256 par image) : plus rapide avec le score */
	if (speed > 160)
		speed = 160;
	for (int i = 0; i < MAX_T; i++)
	{
		struct target *t = &targets[i];
		if (!t->on)
			continue;
		t->r -= speed;
		if (t->r >= 4 * 256)
			continue;
		t->on = 0;			/* disparue sans etre touchee */
		if (++game.missed >= LIVES)
		{
			game.running = 0;	/* fin de partie : terrain vide */
			save_best ();
			clear_targets ();
		}
	}
}

static void on_click (void)
{
	if (!game.running)
	{					/* partie terminee : Rejouer */
		if (ui_hit (BTN_X, BTN_Y, BTN_W, BTN_H))
			new_game ();
		return;
	}
	long mx = ui_ev_field (&ui, EV_X), my = ui_ev_field (&ui, EV_Y);
	for (int i = MAX_T - 1; i >= 0; i--)	/* la plus recente d'abord (dessinee au-dessus) */
	{
		struct target *t = &targets[i];
		if (!t->on)
			continue;
		long dx = mx - t->x, dy = my - t->y, r = t->r >> 8;
		if (dx * dx + dy * dy > r * r)
			continue;
		t->on = 0;			/* touchee : points = 60 - rayon */
		game.score += (unsigned long) (60 - r);
		return;
	}
}

/* ---- dessin ---- */

/* disque plein, ligne par ligne */
static void tdisc (const struct target *t, long r, unsigned col)
{
	for (long dy = -r; dy <= r; dy++)
	{
		long h = (long) sqrt ((double) (r * r - dy * dy));	/* demi-largeur */
		long y = t->y + dy;
		if (y < PLAY_H)			/* ne pas deborder sur la barre */
			ui_fill (t->x - h, y, 2 * h + 1, 1, col);
	}
}

static void draw_game_over (void)
{
	char s[64];
	const char *over = TR ("Partie termin\xC3\xA9" "e");
	long w = ui_text_width ((long) over, 30, (long) ui.font_b);
	ui_text ((long) over, (WIN_W - w) >> 1, PLAY_H / 2 - 10, 0x00FFFFFF, 30, (long) ui.font_b, 0);
	snprintf (s, sizeof s, "%s%lu", TR ("Score final : "), game.score);
	w = ui_text_width ((long) s, 16, (long) ui.font);
	ui_text ((long) s, (WIN_W - w) >> 1, PLAY_H / 2 + 24, 0x00C8D2DE, 16, (long) ui.font, 0);
}

static void draw_bar (void)
{
	char s[64];
	ui_fill (0, PLAY_H, WIN_W, WIN_H - PLAY_H, tok (TJ_FACE));
	snprintf (s, sizeof s, "%s%lu", TR ("Score "), game.score);
	ui_text ((long) s, 16, PLAY_H + 38, tok (TJ_TEXT1), 20, (long) ui.font_b, 0);
	snprintf (s, sizeof s, "%s%lu", TR ("Record "), best);
	ui_text ((long) s, 170, PLAY_H + 36, tok (TJ_TEXT2), 15, (long) ui.font, 0);
	for (unsigned long i = 0; i < LIVES; i++)	/* vies restantes : pastilles */
		ui_fill (300 + 22 * (long) i, PLAY_H + 22, 16, 16, i + game.missed < LIVES ? 0x00D9534F : 0x00CBD3DD);
	if (!game.running)
		ui_button (BTN_X, BTN_Y, BTN_W, BTN_H, (long) TR ("Rejouer"), 1);
	ui_update (0, PLAY_H, WIN_W, WIN_H - PLAY_H);
}

static void draw (void)
{
	ui_fill (0, 0, WIN_W, PLAY_H, 0x00182230);
	for (int i = 0; i < MAX_T; i++)
	{
		const struct target *t = &targets[i];
		if (!t->on)
			continue;
		long r = t->r >> 8;		/* anneaux : couleur, blanc, couleur, blanc */
		tdisc (t, r, t->col);
		tdisc (t, r * 4 / 5, 0x00FFFFFF);
		tdisc (t, r * 3 / 5, t->col);
		tdisc (t, r / 5, 0x00FFFFFF);
	}
	if (!game.running)
		draw_game_over ();
	ui_update (0, 0, WIN_W, PLAY_H);
	draw_bar ();
}

int main (void)
{
	if (!uikit_open (&ui))
		return 1;
	if (ui_window_ex (-1, -1, WIN_W, WIN_H, (long) TR ("Cibles"), WF_GLYPH_LIGHT | WF_BODY_DARK) < 0)
		return 0;
	rng = time_ms () ^ 0x9E3779B97F4A7C15ul;	/* graine du generateur */
	load_best ();
	new_game ();
	for (;;)
	{
		long t = ui_event (FRAME);
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1)
			on_click ();
		unsigned long now = time_ms ();	/* une image toutes les FRAME ms */
		if (now < next_frame)
			continue;
		next_frame = now + FRAME;
		step ();
		draw ();
	}
	return 0;
}
