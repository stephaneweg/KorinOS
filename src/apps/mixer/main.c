/*
 * mixer -- l'application Mixer (docs/SON.md 4.4) : le service sound par AudioKit.
 *  * en haut, une ligne par sortie : etoile (la principale ; un clic en fait la preferee), nom,
 *    curseur du volume, case Muet, crete ;
 *  * en bas, une ligne par process qui joue : nom, liste deroulante de sa sortie (Principale, puis
 *    les sorties de sound : ak_route), curseur de son volume, case Muet, crete.
 * Les listes deroulantes sont des widgets d'UIKit (ui_dropdown), une par ligne montree (DROP_MAX),
 * deplacees ou cachees a chaque dessin et refaites quand la liste des sorties change.
 * Volumes, routage et preferee sont retenus par sound (/etc/sound.txt).
 * L'etat (tampon partage de sound) : son adresse redemandee a chaque korin.audio.state (docs/BUS.md ;
 * sans busd, toutes les 200 ms) ; les vumetres sont redessines toutes les 200 ms.
 *
 * Port en C de mixer.asm, un seul source pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa
 * validation sur le Pi. Les dessins de sounddraw.inc sont recopies ici (sounddraw.c de src/sys/common
 * est ecrit pour korinrt, pas pour libposix).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uikit.h>
#include <audiokit_abi.h>
#include <buskit_abi.h>

/* src/inc/sound.inc : le tampon d'etat de sound */
#define SS_OUTS		64
#define SS_OUT_MAX	8
#define SO_ID		0
#define SO_VOLUME	12
#define SO_MUTE		16
#define SO_FLAGS	20
#define SO_PEAK		24
#define SO_NAME		32
#define SS_CLIS		(64 + SS_OUT_MAX * 64)
#define SS_CLI_MAX	32
#define SC_PID		0
#define SC_VOLUME	4
#define SC_MUTE		8
#define SC_ROUTE	12
#define SC_PEAK		20
#define SC_NAME		32

#define WIN_W		690
#define WIN_H		420
#define ROW		38
#define OUT_Y		36			/* premiere sortie */
#define NAME_X		40
#define DROP_X		156			/* liste deroulante de la sortie d'un client */
#define DROP_W		180
#define DROP_H		26
#define DROP_DY		3
#define DROP_MAX	10
#define SL_X		348
#define SL_W		160
#define VAL_X		516
#define MUTE_X		558
#define METER_X		618
#define METER_W		54

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static uint32_t tok (int off) { return ((const uint32_t *) ui_tokens ())[off / 4]; }
static uint32_t rd32 (const uint8_t *e, int off) { return *(const uint32_t *) (e + off); }

struct row { const uint8_t *ent; long kind; long y; long pad; };	/* genre : 1 sortie, 2 client */

static const uint8_t *stptr;			/* tampon d'etat de sound */
static long audio_sub;				/* abonnement a korin.audio.state (0 : pas de bus) */
static struct row *drag;			/* ligne du curseur tenu (0 : aucun) */
static long nrows;
static struct row rows[48];
static long drop_w[DROP_MAX];			/* listes deroulantes (0 : pas faite) */
static long drop_on[DROP_MAX];			/* 1 : montree */
static long drop_pid[DROP_MAX];			/* client de chacune */
static long drop_ids[SS_OUT_MAX + 1];		/* numero de sortie de chaque element */
static long drop_n;				/* elements */
static char drop_text[288];			/* « Principale|sortie|... » */
static char drop_prev[288];			/* celle des listes faites */

/* ---- les dessins du son (src/inc/sounddraw.inc) ---- */

static void sd_slider (long x, long y, long w, unsigned long value, long grey)
{
	unsigned long v = value <= 100 ? value : 100;
	uint32_t c = tok (TJ_SUNKEN);			/* piste */
	ui_rbox (x, y + 6, w, 4, 2, c, c);
	long pos = (long) ((unsigned long) (w - 12) * v / 100);	/* position du bouton */
	c = grey ? tok (TJ_TEXT3) : tok (TJ_ACCENT);	/* part remplie */
	ui_rbox (x, y + 6, pos + 6, 4, 2, c, c);
	c = tok (TJ_LINE_STRONG);			/* bouton : contour, puis clair */
	ui_rbox (x + pos, y, 16, 16, 8, c, c);
	ui_rbox (x + pos + 1, y + 1, 14, 14, 7, tok (TJ_RAISED), tok (TJ_RAISED_LOW));
}

static long sd_slider_value (long cx, long x, long w)
{
	long v = cx - x - 8, span = w - 12;
	if (v < 0)
		v = 0;
	if (v > span)
		v = span;
	return (long) (((unsigned long) v * 100 + ((unsigned long) span >> 1)) / (unsigned long) span);
}

static void sd_check (long x, long y, long checked)
{
	uint32_t c = checked ? tok (TJ_ACCENT) : tok (TJ_LINE_STRONG);
	ui_rbox (x, y, 14, 14, 3, c, c);
	if (!checked)
	{
		c = tok (TJ_FACE);
		ui_rbox (x + 1, y + 1, 12, 12, 2, c, c);
		return;
	}
	ui_text ((long) "\xE2\x9C\x93", x + 2, y + 12, 0xFFFFFF, 12, (long) ui.font_b, 0);	/* (en dur, comme l'asm) */
}

static void sd_meter (long x, long y, long w, long h, unsigned long peak)
{
	ui_fill (x, y, w, h, tok (TJ_SUNKEN));
	unsigned long lit = ((unsigned long) w * peak) >> 15;	/* part allumee */
	if (!lit)
		return;
	ui_fill (x, y, (long) lit, h, peak >= 29000 ? tok (TJ_WARNING) : tok (TJ_SUCCESS));	/* pres de la saturation : alerte */
}

/* ---- les lignes ---- */

/* nom, curseur, valeur, case Muet, vumetre (communs aux sorties et aux clients) */
static void line_common (const char *name, long y, uint32_t vol, uint32_t mute, uint32_t peak, long name_w)
{
	char num[24];
	ui_text ((long) name, NAME_X, y + 20, tok (TJ_TEXT1), 13, (long) ui.font, name_w);
	sd_meter (METER_X, y + 15, METER_W, 8, peak);
	sd_slider (SL_X, y + 4, SL_W, vol, mute);
	snprintf (num, sizeof num, "%u %%", (unsigned) vol);
	ui_text ((long) num, VAL_X, y + 18, tok (TJ_TEXT2), 12, (long) ui.font, 0);
	sd_check (MUTE_X, y + 6, mute);
	ui_text ((long) TR ("Muet"), MUTE_X + 18, y + 18, tok (TJ_TEXT2), 11, (long) ui.font, 0);
}

static void row_note (const uint8_t *ent, long kind, long y)
{
	if (nrows >= 48)
		return;
	rows[nrows].ent = ent;
	rows[nrows].kind = kind;
	rows[nrows].y = y;
	nrows++;
}

static struct row *row_at (long y)
{
	for (long i = 0; i < nrows; i++)
		if (y >= rows[i].y && y < rows[i].y + ROW)
			return &rows[i];
	return 0;
}

static void set_vol (struct row *r, long vol, long mute)
{
	if (r->kind == 1)
		ak_out_volume (rd32 (r->ent, SO_ID), vol, mute);
	else
		ak_client_volume (rd32 (r->ent, SC_PID), vol, mute);
}

/* ---- les listes deroulantes ---- */

/* les elements (drop_text) et le numero de sortie de chacun (drop_ids, 0 : la principale) ; si le
 * texte change (sortie branchee, retiree), les listes faites sont detruites */
static void drop_items (void)
{
	char *p = drop_text;
	const char *m = TR ("Principale");
	while (*m)
		*p++ = *m++;
	*p = 0;
	long n = 0;
	drop_ids[n++] = 0;
	const uint8_t *o = stptr + SS_OUTS;
	for (int i = 0; i < SS_OUT_MAX; i++, o += 64)
	{
		uint32_t id = rd32 (o, SO_ID);
		if (!id)
			continue;
		drop_ids[n++] = id;
		*p++ = '|';
		const char *s = (const char *) o + SO_NAME;
		for (int k = 0; k < 31 && s[k]; k++)
			*p++ = s[k] == '|' ? '/' : s[k];	/* (le separateur des elements) */
		*p = 0;
	}
	drop_n = n;
	if (strcmp (drop_text, drop_prev) == 0)
		return;
	strcpy (drop_prev, drop_text);
	for (int i = 0; i < DROP_MAX; i++)
	{
		long w = drop_w[i];
		drop_w[i] = 0;
		if (w)
			ui_w_destroy (w);
	}
}

/* ligne de client montree i, son entree dans l'etat, y : sa liste (faite, ou deplacee), sur sa sortie */
static void drop_row (long i, const uint8_t *ent, long y)
{
	if (i >= DROP_MAX)
		return;
	drop_pid[i] = rd32 (ent, SC_PID);
	if (!drop_w[i])
	{
		drop_w[i] = ui_dropdown (DROP_X, y + DROP_DY, DROP_W, DROP_H, (long) drop_text);
		if (!drop_w[i])
			return;
		drop_on[i] = 1;
	}
	else
	{
		ui_w_move (drop_w[i], DROP_X, y + DROP_DY, DROP_W, DROP_H);
		if (!drop_on[i])
		{
			ui_w_show (drop_w[i], 1);
			drop_on[i] = 1;
		}
	}
	uint32_t route = rd32 (ent, SC_ROUTE);		/* l'element de sa sortie (absente : la principale) */
	long k = 0;
	while (k < drop_n && (unsigned long) drop_ids[k] != route)
		k++;
	if (k >= drop_n)
		k = 0;
	if (ui_w_get_value (drop_w[i]) != k)
		ui_w_set_value (drop_w[i], k);
}

/* cache les listes des lignes a partir de i */
static void drop_hide (long i)
{
	for (; i < DROP_MAX; i++)
		if (drop_on[i])
		{
			drop_on[i] = 0;
			ui_w_show (drop_w[i], 0);
		}
}

/* widget dont l'element a change : si c'est une liste deroulante, son client va sur la sortie choisie */
static void drop_chosen (long w)
{
	for (int i = 0; i < DROP_MAX; i++)
		if (drop_w[i] == w)
		{
			long v = ui_w_get_value (w);
			if (v < 0 || v >= drop_n)
				return;
			ak_route (drop_pid[i], drop_ids[v]);
			return;
		}
}

/* ---- le dessin ---- */

static void draw (void)
{
	long shown = 0;					/* clients montres */
	ui_fill (0, 0, WIN_W, WIN_H, tok (TJ_FACE));
	nrows = 0;
	if (!stptr)
		ui_text ((long) TR ("Le service sound ne r\xC3\xA9pond pas."), 16, 40, tok (TJ_TEXT2), 14, (long) ui.font, 0);
	else
	{
		ui_text ((long) TR ("Sorties"), 14, 24, tok (TJ_TEXT3), 12, (long) ui.font_b, 0);
		long y = OUT_Y;
		const uint8_t *o = stptr + SS_OUTS;
		for (int i = 0; i < SS_OUT_MAX; i++, o += 64)
		{
			if (!rd32 (o, SO_ID))
				continue;
			row_note (o, 1, y);
			/* etoile : la principale (U+2605, sinon U+2606) */
			ui_text ((long) ((rd32 (o, SO_FLAGS) & 1) ? "\xE2\x98\x85" : "\xE2\x98\x86"), 14, y + 20, tok (TJ_ACCENT), 16,
				 (long) ui.font, 0);
			line_common ((const char *) o + SO_NAME, y, rd32 (o, SO_VOLUME), rd32 (o, SO_MUTE), rd32 (o, SO_PEAK),
				     SL_X - NAME_X - 12);
			y += ROW;
		}
		y += 8;					/* les applications */
		ui_fill (14, y, WIN_W - 28, 1, tok (TJ_LINE));
		ui_text ((long) TR ("Applications"), 14, y + 22, tok (TJ_TEXT3), 12, (long) ui.font_b, 0);
		y += 32;
		drop_items ();
		const uint8_t *c = stptr + SS_CLIS;
		for (int i = 0; i < SS_CLI_MAX; i++, c += 64)
		{
			if (!rd32 (c, SC_PID) || y + ROW > WIN_H)
				continue;
			row_note (c, 2, y);
			line_common ((const char *) c + SC_NAME, y, rd32 (c, SC_VOLUME), rd32 (c, SC_MUTE), rd32 (c, SC_PEAK),
				     DROP_X - NAME_X - 6);
			drop_row (shown, c, y);		/* sa sortie */
			shown++;
			y += ROW;
		}
		if (!shown)
			ui_text ((long) TR ("Aucune application ne joue."), 40, y + 18, tok (TJ_TEXT3), 12, (long) ui.font, 0);
	}
	drop_hide (shown);				/* listes des lignes absentes */
	ui_w_draw (0);
	ui_update_all ();
}

/* x : le curseur qu'on fait glisser */
static void slide (long x)
{
	struct row *r = drag;
	long v = sd_slider_value (x, SL_X, SL_W);
	set_vol (r, v, rd32 (r->ent, r->kind == 1 ? SO_MUTE : SC_MUTE));
	draw ();
}

static void click (long x, long y)
{
	struct row *r = row_at (y);
	if (!r)
		return;
	if (x >= SL_X - 8 && x < SL_X + SL_W + 4)	/* curseur */
	{
		drag = r;
		slide (x);
	}
	else if (x >= SL_X + SL_W + 4)
	{
		if (x < MUTE_X - 4)			/* Muet */
			return;
		if (r->kind == 1)
			set_vol (r, rd32 (r->ent, SO_VOLUME), rd32 (r->ent, SO_MUTE) ^ 1);
		else
			set_vol (r, rd32 (r->ent, SC_VOLUME), rd32 (r->ent, SC_MUTE) ^ 1);
	}
	else if (r->kind != 2 && x < NAME_X)		/* etoile d'une sortie : preferee (client : sa liste) */
		ak_main (rd32 (r->ent, SO_ID));
}

int main (void)
{
	if (!uikit_open (&ui) || !audiokit_kit_open ())
		return 1;
	if (ui_window (WIN_W, WIN_H, (long) "Mixer") < 0)
		return 0;
	long s = buskit_kit_open () ? bus_subscribe ((long) "korin.audio.state", 0) : 0;	/* sound a change : le bus */
	audio_sub = s > 0 ? s : 0;
	stptr = (const uint8_t *) ak_state ();
	draw ();
	for (;;)
	{
		long t = ui_event (200);
		if (t == 0)
		{
			if (!audio_sub)			/* sans bus : l'etat redemande */
				stptr = (const uint8_t *) ak_state ();
			draw ();			/* vumetres et changements */
			continue;
		}
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_BUS)
		{
			stptr = (const uint8_t *) ak_state ();	/* (sound relance : nouveau tampon) */
			draw ();
			continue;
		}
		long code, taken;
		long w = uikit_w_event (&code, &taken);		/* les listes deroulantes d'abord */
		if (taken)
		{
			if (code == UN_CHANGE)
			{
				drop_chosen (w);
				draw ();
				continue;
			}
			if (!drag)			/* (un curseur tenu continue) */
				continue;
		}
		if (t != WEV_MOUSE)
			continue;
		long kind = ui_ev_field (&ui, EV_KIND), x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
		if (kind == 1)
		{
			click (x, y);
			draw ();
		}
		else if (kind == 2)
			drag = 0;
		else if (drag)
			slide (x);
	}
	return 0;
}
