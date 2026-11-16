/* gesture.c - voir gesture.h */
#include "gesture.h"

#define TAP_MS 220       /* un toucher plus court… */
#define TAP_DTAP_MS 300  /* double toucher : délai entre le lever et le second contact (TP_DTAP_MS de gt911) */
#define TAP_DTAP_DIST 1000 /* double toucher : écart (centièmes de mm) */
#define TAP_MOVE 250     /* …et qui bouge moins (centièmes de mm) est un clic */
#define ZONE_RIGHT 50    /* clic du pavé dans le coin bas droit (en % : x au-delà, y au-delà de ZONE_BOTTOM) */
#define ZONE_BOTTOM 80

#define JUMP_MAX_MM100 2500 /* 25 mm en une trame (≤ 50 ms) : pas un doigt, un rapport faux */

static int32_t iabs(int32_t v)
{
	return v < 0 ? -v : v;
}

void gest_init(struct gest *g, const struct hid_model *m, const struct gest_cfg *cfg)
{
	uint8_t *p = (uint8_t *)g;
	for (unsigned i = 0; i < sizeof *g; i++)
		p[i] = 0;
	g->cfg = *cfg;
	if (g->cfg.speed <= 0)
		g->cfg.speed = 100;
	if (g->cfg.scroll_mm100 <= 0)
		g->cfg.scroll_mm100 = 300;
	g->xmin = m->xmin;
	g->ymin = m->ymin;
	g->xrange = m->xmax - m->xmin > 0 ? m->xmax - m->xmin : 1;
	g->yrange = m->ymax - m->ymin > 0 ? m->ymax - m->ymin : 1;
	/* taille inconnue : 100 mm de large, et la même résolution en Y */
	g->xmm100 = m->xmm100 ? m->xmm100 : 10000;
	g->ymm100 = m->ymm100 ? m->ymm100 : (int32_t)((int64_t)g->yrange * g->xmm100 / g->xrange);
	g->acc_scan = g->prev_scan = -1;
	g->clickpad = m->t_btn[1] < 0 && m->t_btn[2] < 0;
	g->tap_ms = -100000;
}

int gest_buttons(const struct gest *g)
{
	int b = g->pad_buttons;
	for (int i = 0; i < HID_MAX_MICE; i++)
		b |= g->mouse_buttons[i];
	return b & 7;
}

static void send(struct gest *g, int32_t dx, int32_t dy, gest_emit emit, void *ctx)
{
	emit(ctx, GEV_MOUSE_REL, dx, dy, gest_buttons(g) | GEV_TOUCHPAD);
}

static const struct hid_contact *find(const struct hid_contact *c, int n, int id)
{
	for (int i = 0; i < n; i++)
		if (c[i].id == id)
			return &c[i];
	return 0;
}

/* une trame complète */
static void frame(struct gest *g, const struct hid_contact *c, int n, int button, int scan, long now, gest_emit emit,
		  void *ctx)
{
	/* durée depuis la trame précédente : le temps de balayage (en 100 µs) s'il y en a */
	long dt = now - g->prev_ms;
	if (scan >= 0 && g->prev_scan >= 0) {
		long ds = (scan - g->prev_scan) & 0xFFFF;
		dt = ds / 10;
	}
	if (dt < 4)
		dt = 4;
	if (dt > 50)
		dt = 50;

	/* déplacements des doigts déjà là (centièmes de mm) */
	int32_t mx[HID_MAX_FINGERS], my[HID_MAX_FINGERS];
	int matched = 0, best = -1;
	int32_t best_d = -1, sum_dy = 0;
	for (int i = 0; i < n; i++) {
		const struct hid_contact *p = find(g->prev, g->nprev, c[i].id);
		mx[i] = my[i] = 0;
		if (!p)
			continue;
		mx[i] = (int32_t)((int64_t)(c[i].x - p->x) * g->xmm100 / g->xrange);
		my[i] = (int32_t)((int64_t)(c[i].y - p->y) * g->ymm100 / g->yrange);
		if (iabs(mx[i]) + iabs(my[i]) > JUMP_MAX_MM100) {
			/* (15/11 : un rapport de transition ou relu au lancement, un doigt « téléporté » : sur le ZBook,
			   le pointeur partait en 0,0 et cliquait le menu Korin ; un vrai doigt ne fait pas 25 mm en
			   une trame) : ce doigt repart de là, sans mouvement */
			mx[i] = my[i] = 0;
			g->jumps++;
			continue;
		}
		matched++;
		sum_dy += my[i];
		int32_t d = iabs(mx[i]) + iabs(my[i]);
		if (d > best_d) {
			best_d = d;
			best = i;
		}
	}

	/* un toucher commence ; un doigt reposé aussitôt après un toucher, tout près : glisser */
	if (!g->nprev && n) {
		g->t0 = now;
		g->maxf = n;
		g->moved = 0;
		g->phys_used = 0;
		g->scroll = 0;
		g->x0 = c[0].x;
		g->y0 = c[0].y;
		int32_t ddx = (int32_t)((int64_t)(c[0].x - g->tap_x) * g->xmm100 / g->xrange);
		int32_t ddy = (int32_t)((int64_t)(c[0].y - g->tap_y) * g->ymm100 / g->yrange);
		if (g->cfg.tap && n == 1 && now - g->tap_ms < TAP_DTAP_MS && iabs(ddx) + iabs(ddy) < TAP_DTAP_DIST) {
			g->drag = 1;
			g->pad_buttons |= 1;
			send(g, 0, 0, emit, ctx);
		}
		g->tap_ms = -100000;
	}
	if (n > g->maxf)
		g->maxf = n;
	if (best >= 0)
		g->moved += best_d;

	/* boutons du pavé : directs ; un « clickpad » (un seul bouton) : droit à deux doigts ou dans le coin
	   bas droit */
	if (!g->clickpad) {
		int b = button & 7;
		if (b)
			g->phys_used = 1;
		int keep = g->drag ? 1 : 0;
		if ((g->pad_buttons & ~keep) != b) {
			g->pad_buttons = b | keep;
			send(g, 0, 0, emit, ctx);
		}
		g->phys = b != 0;
	} else if ((button & 1) && !g->phys) {
		int which = 0;
		if (n >= 2)
			which = 1;
		else if (n == 1 && (int64_t)(c[0].x - g->xmin) * 100 > (int64_t)g->xrange * ZONE_RIGHT &&
			 (int64_t)(c[0].y - g->ymin) * 100 > (int64_t)g->yrange * ZONE_BOTTOM)
			which = 1;
		g->phys = 1;
		g->phys_which = which;
		g->phys_used = 1;
		g->pad_buttons |= 1 << which;
		send(g, 0, 0, emit, ctx);
	} else if (!(button & 1) && g->phys) {
		g->phys = 0;
		if (!(g->drag && g->phys_which == 0))
			g->pad_buttons &= ~(1 << g->phys_which);
		send(g, 0, 0, emit, ctx);
	}

	if (n == 2 && !g->phys && matched == 2) {
		/* défilement à deux doigts : la moyenne des deux en Y */
		g->scroll += sum_dy / 2;
		int notches = g->scroll / g->cfg.scroll_mm100;
		if (notches) {
			g->scroll -= notches * g->cfg.scroll_mm100;
			emit(ctx, GEV_WHEEL, g->cfg.natural ? notches : -notches, 0, 0);
		}
	} else if (best >= 0 && (n == 1 || g->phys || g->drag) && best_d > 0) {
		/* le pointeur : le doigt qui bouge le plus ; accélération d'après la vitesse (mm/s) */
		int32_t v = (int32_t)((int64_t)best_d * 10 / dt);
		int32_t gain; /* points par mm, ×100 */
		if (v <= 20)
			gain = 500;
		else if (v >= 150)
			gain = 1500;
		else
			gain = 500 + (v - 20) * 1000 / 130;
		gain = gain * g->cfg.speed / 100;
		/* centièmes de mm × gain (points/mm ×100) -> points ×10000 ; restes en 1/256 de point */
		g->fx += (int32_t)((int64_t)mx[best] * gain * 256 / 10000);
		g->fy += (int32_t)((int64_t)my[best] * gain * 256 / 10000);
		int32_t px = g->fx / 256, py = g->fy / 256;
		g->fx -= px * 256;
		g->fy -= py * 256;
		if (px || py)
			send(g, px, py, emit, ctx);
	}

	/* fin du toucher : fin du glisser, ou clic par toucher (un doigt : gauche, deux : droit, trois : milieu) */
	if (g->nprev && !n) {
		if (g->drag) {
			g->drag = 0;
			if (!g->phys)
				g->pad_buttons &= ~1;
			send(g, 0, 0, emit, ctx);
		} else if (g->cfg.tap && !g->phys_used && now - g->t0 < TAP_MS && g->moved < TAP_MOVE && g->maxf <= 3) {
			int b = g->maxf == 1 ? 0 : g->maxf == 2 ? 1 : 2;
			g->pad_buttons |= 1 << b;
			send(g, 0, 0, emit, ctx);
			g->pad_buttons &= ~(1 << b);
			send(g, 0, 0, emit, ctx);
			if (g->maxf == 1) {
				g->tap_ms = now;
				g->tap_x = g->x0;
				g->tap_y = g->y0;
			}
		}
		g->fx = g->fy = 0;
	}

	for (int i = 0; i < n; i++)
		g->prev[i] = c[i];
	g->nprev = n;
	g->prev_ms = now;
	g->prev_scan = scan;
}

void gest_report(struct gest *g, const struct hid_frame *fr, int nslots, long now, gest_emit emit, void *ctx)
{
	/* mode hybride : count > 0 ouvre une trame de count contacts, les rapports suivants (count 0) la
	 * complètent ; un rapport porte nslots places */
	if (fr->count > 0 || g->remaining <= 0) {
		g->nacc = 0;
		g->remaining = fr->count > 0 ? fr->count : 0;
		g->acc_button = fr->button;
		g->acc_scan = fr->scan;
	} else
		g->acc_button |= fr->button;
	for (int i = 0; i < fr->n && g->nacc < HID_MAX_FINGERS; i++)
		if (fr->c[i].conf) /* (une paume : pas de confiance, ignorée) */
			g->acc[g->nacc++] = fr->c[i];
	g->remaining -= nslots;
	if (g->remaining > 0 && fr->count >= 0)
		return; /* la suite au prochain rapport */
	g->remaining = 0;
	frame(g, g->acc, g->nacc, g->acc_button, g->acc_scan, now, emit, ctx);
}

void gest_mouse(struct gest *g, const struct hid_frame *fr, gest_emit emit, void *ctx)
{
	int src = fr->src >= 0 && fr->src < HID_MAX_MICE ? fr->src : 0;
	int b = fr->buttons & 7;
	if (fr->dx > 127 || fr->dx < -127 || fr->dy > 127 || fr->dy < -127) {
		g->jumps++; /* (un rapport faux, comme un doigt téléporté : ignoré) */
		return;
	}
	if (fr->dx || fr->dy || b != g->mouse_buttons[src]) {
		g->mouse_buttons[src] = b;
		int32_t dx = fr->dx * g->cfg.speed / 100, dy = fr->dy * g->cfg.speed / 100;
		emit(ctx, GEV_MOUSE_REL, dx, dy, gest_buttons(g));
	}
	if (fr->wheel)
		emit(ctx, GEV_WHEEL, fr->wheel, 0, 0);
}
