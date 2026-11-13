/*
 * piano -- petit synthetiseur : deux octaves cliquables ou jouees au clavier
 * (rangee AZERTY q s d f g h j k l m = do re mi fa sol la si do re mi ;
 *  z e t y u o p = touches noires). Sinus par rotation de vecteur (double),
 * enveloppe exponentielle, flux d'AudioKit (stereo 16 bits 44,1 kHz).
 * Port en C de piano.asm (memes touches, memes calculs), un seul source pour le Pi et le PC ;
 * l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#include <stdint.h>
#include <math.h>
#include <uikit.h>
#include <audiokit_abi.h>

#define WIN_W	700
#define WIN_H	260
#define KEYS_Y	70
#define WHITE_W	46
#define WHITE_H	170
#define BLACK_W	28
#define BLACK_H	105
#define N_WHITE	14
#define FRAMES	256			/* trames generees par passe */

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))

static unsigned tok (int off) { return ((const unsigned *) ui_tokens ())[off / 4]; }

static const char keymap[] = "qzsedftgyhujklopm";
static const unsigned char white_semi[N_WHITE] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23 };
static const unsigned char black_pos[10] = { 0, 1, 3, 4, 5, 7, 8, 10, 11, 12 };	/* blanche a gauche de chaque noire */
static const unsigned char black_semi[10] = { 1, 3, 6, 8, 10, 13, 15, 18, 20, 22 };
static const double semitone_ratio[24] = {
	1.0, 1.0594631, 1.1224620, 1.1892071, 1.2599210, 1.3348399,
	1.4142136, 1.4983071, 1.5874011, 1.6817928, 1.7817974, 1.8877486,
	2.0, 2.1189262, 2.2449241, 2.3784142, 2.5198421, 2.6696797,
	2.8284271, 2.9966142, 3.1748021, 3.3635857, 3.5635949, 3.7754973 };
static const double c4_freq = 261.6256, two_pi_over_rate = 0.000142475857, inv24 = 0.0416666667,
	inv6 = 0.1666666667, decay = 0.99993, amp = 12000.0;

static long stream = -1;
static long cur_note = -1;
static double osc[4] = { 1.0, 0.0, 1.0, 0.0 };	/* (c, s), (cos t, sin t) */
static double env;
static int16_t pcm[FRAMES * 2];

static void draw_keys (void)
{
	for (int i = 0; i < N_WHITE; i++)
	{
		unsigned c = 0x00FFFFFF;
		if (white_semi[i] == cur_note)
			c = tok (TJ_ACCENT_LIGHT);
		ui_fill (WHITE_W * i + 20, KEYS_Y, WHITE_W - 2, WHITE_H, c);
		ui_frame (WHITE_W * i + 20, KEYS_Y, WHITE_W - 2, WHITE_H, tok (TJ_LINE_STRONG));
	}
	for (int i = 0; i < 10; i++)
	{
		unsigned c = 0x00202833;
		if (black_semi[i] == cur_note)
			c = tok (TJ_ACCENT);
		ui_fill (black_pos[i] * WHITE_W + 20 + WHITE_W - BLACK_W / 2, KEYS_Y, BLACK_W, BLACK_H, c);
	}
	ui_update (0, KEYS_Y, WIN_W, WHITE_H);
}

static void draw (void)
{
	ui_text ((long) TR ("Piano Korin"), 20, 34, tok (TJ_TEXT1), 20, (long) ui.font_b, 0);
	ui_text ((long) TR ("Cliquez les touches ou jouez avec  q s d f g h j k l m  (noires : z e t y u o p)"), 20, 56,
		 tok (TJ_TEXT2), 13, (long) ui.font, 0);
	draw_keys ();
	ui_update_all ();
}

/* x, y (fenetre) -> demi-ton (0..23), -1 si aucun */
static long key_at (long x, long y)
{
	y -= KEYS_Y;
	if (y < 0 || y >= WHITE_H)
		return -1;
	x -= 20;
	if (x < 0)
		return -1;
	if (y < BLACK_H)				/* touches noires d'abord */
		for (int i = 0; i < 10; i++)
		{
			long l = black_pos[i] * WHITE_W + WHITE_W - BLACK_W / 2;
			if (x >= l && x < l + BLACK_W)
				return black_semi[i];
		}
	long w = x / WHITE_W;
	return w < N_WHITE ? white_semi[w] : -1;
}

/* demi-ton depuis do4 : frequence = 261,63 * 2^(n/12), enveloppe a 1 */
__attribute__ ((optimize ("fp-contract=off")))
static void note_on (long n)
{
	double t = semitone_ratio[n] * c4_freq * two_pi_over_rate;	/* theta par echantillon */
	double t2 = t * t;					/* cos/sin de theta (series de Taylor) */
	osc[2] = 1.0 - t2 * 0.5 + t2 * t2 * inv24;
	osc[3] = t - t2 * inv6 * t;
	osc[0] = 1.0;						/* vecteur (1, 0) */
	osc[1] = 0.0;
	env = 1.0;
	cur_note = n;
	draw_keys ();
}

/* produit des echantillons tant que le flux a de la place */
__attribute__ ((optimize ("fp-contract=off")))
static void feed (void)
{
	long st[4];
	for (;;)
	{
		if (stream < 0 || ak_status (stream, (long) st) != 0)
			return;
		if (st[1] < FRAMES * 4)			/* place libre (octets) */
			return;
		if ((unsigned long) st[0] >= 16384)	/* deja ~0,1 s d'avance : suffisant */
			return;
		double c = osc[0], s = osc[1], ct = osc[2], sn = osc[3], e = env;
		for (int i = 0; i < FRAMES; i++)
		{
			double nc = c * ct - s * sn;	/* rotation */
			s = s * ct + c * sn;
			c = nc;
			e = e * decay;			/* enveloppe */
			int16_t v = (int16_t) (int32_t) (s * e * amp);
			pcm[2 * i] = v;			/* gauche */
			pcm[2 * i + 1] = v;		/* droite */
		}
		double r = sqrt (c * c + s * s);	/* renormaliser (c, s) */
		osc[0] = c / r;
		osc[1] = s / r;
		env = e;
		ak_write (stream, (long) pcm, FRAMES * 4);
	}
}

int main (void)
{
	if (!uikit_open (&ui) || !audiokit_kit_open ())
		return 1;
	if (ui_window (WIN_W, WIN_H, (long) "Piano") < 0)
		return 0;
	stream = ak_open (SND_RATE, 2, 16, 0);		/* flux s16 stereo 44,1 kHz (sortie du process) */
	draw ();
	for (;;)
	{
		feed ();				/* garder le flux alimente */
		long t = ui_event (8);
		if (t == WEV_CLOSE)
			break;
		if (t == WEV_MOUSE)
		{
			if (ui_ev_field (&ui, EV_KIND) != 1)
				continue;
			long n = key_at (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y));
			if (n >= 0)
				note_on (n);
		}
		else if (t == WEV_KEY)
		{
			if (ui_ev_field (&ui, EV_KIND) == 0)
				continue;
			unsigned ch = (unsigned) ui_ev_field (&ui, EV_CHAR);
			for (int i = 0; keymap[i]; i++)	/* caractere -> demi-ton */
				if ((unsigned char) keymap[i] == ch)
				{
					note_on (i);
					break;
				}
		}
	}
	return 0;
}
