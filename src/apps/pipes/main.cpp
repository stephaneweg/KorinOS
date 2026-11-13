/*
 * pipes -- le Pipes d'Onyx (C:\Projects\Onyx\user\Apps\pipes\main.cpp, d'apres « Pipe Dream ») : poser les tuyaux de
 * la file avant que l'eau n'arrive ; chaque piece traversee vaut 50 (100 en debit rapide), une croix traversee deux
 * fois 500 de plus, une piece remplacee coute 50 ; la manche est gagnee si l'eau passe par assez de pieces avant de
 * deborder. Des murs a partir de la manche 3 ; l'eau va plus vite a chaque manche.
 *
 * Souris ou doigt ; fleches et Espace ; F : l'eau part ; P : pause ; manette : croix, A pose, X l'eau. Plein ecran ;
 * Start, Echap, P ou le bouton Pause : le menu de pause (gamepause.h). Taille logique 640 x 480.
 *
 * Port en C++ depuis Onyx : la classe Pipes et ses regles (newGame, newRound, place, advance, spill, tick, la table
 * CONN des pieces, les durees) en viennent. L'habillage vient de pipes.asm (la version de Korin, qui fait foi pour ce
 * qu'on voit) : tuyaux de cuivre calcules point par point, eau qui avance dans le tuyau, eclaboussures, ecran titre,
 * boutons Eau et Pause, record SD0:/home/pipes/best.txt, manette, menu de pause ; memes pixels que l'asm par
 * gamefb.h, gamedraw.h, gamepause.h, gamesound.h. Comme l'asm, le temps avance de 17, 17, 16 ms par image (Onyx :
 * le temps mesure) ; le hasard est celui de l'asm (gf_below, xorshift64), pas le xorshift32 d'Onyx (rng_n), tire
 * dans le meme ordre que l'asm. Un seul source pour le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa
 * validation sur le Pi.
 */
#define GF_W		640
#define GF_H		480
#include "../../inc/gamepause.h"

#define GW		10
#define GH		7
#define NCELLS		(GW * GH)
#define CELL		54
#define NQ		5
#define QX		14			/* la file */
#define PX0		88			/* le plateau */
#define PY0		54
#define TUBE		(16 * 16)		/* rayon du tuyau, en 1/16 de point */
#define BORE		(7 * 16)		/* rayon de l'interieur */
#define NMAP		13			/* 0 rien ; 1..7 les pieces ; 9..12 le depart (N, E, S, O) */

enum { N = 1, E = 2, S = 4, Wd = 8 };		/* cotes (masque) */
/* pieces : 0 aucune, 1 ─, 2 │, 3 └ (N+E), 4 ┌ (E+S), 5 ┐ (S+O), 6 ┘ (O+N), 7 ┼, 8 depart, 9 mur */
#define P_START		8
#define P_WALL		9
static const unsigned char CONN[10] = { 0, E | Wd, N | S, N | E, E | S, S | Wd, Wd | N, N | E | S | Wd, 0, 0 };
/* le cote par ou commence la carte de chaque piece */
static const unsigned char FIRST[10] = { 0, Wd, N, N, E, S, Wd, 0, 0, 0 };
static const unsigned char DIRS[4] = { N, E, S, Wd };
static int opposite (int d) { return d == N ? S : d == S ? N : d == E ? Wd : E; }
static int dx_of (int d) { return d == E ? 1 : d == Wd ? -1 : 0; }
static int dy_of (int d) { return d == S ? 1 : d == N ? -1 : 0; }
static int side_rank (int d) { return d == N ? 0 : d == E ? 1 : d == S ? 2 : 3; }	/* 0 N, 1 E, 2 S, 3 O */

/* les etats d'Onyx (0 construire, 1 l'eau coule, 2 manche gagnee, 3 perdue, 4 pause), plus le titre de l'asm */
enum { ST_BUILD, ST_FLOW, ST_WON, ST_LOST, ST_PAUSE, ST_TITLE };

enum { TX_LOGO, TX_SUB, TX_PROMPT, TX_RECORD, TX_PAUSE, TX_RESUME, TX_WON, TX_NEXT, TX_LOST, TX_AGAIN, TX_HINT, TX_FAST,
	TX_SLASH, TX_NEWBEST, NTX };

enum { Z_FAST = 1, Z_PAUSE };
enum { ID_NEW = 1, ID_PAUSE, ID_SOUND, ID_QUIT };

#define KEY_ENTER_	0x28
#define KEY_SPACE_	0x2C
#define KEY_RIGHT_	0x4F
#define KEY_LEFT_	0x50
#define KEY_DOWN_	0x51
#define KEY_UP_		0x52

#define P_DIR		"SD0:/home/pipes"
#define P_BEST		"SD0:/home/pipes/best.txt"

static const struct ga_zone_t zones[] = {
	{ QX - 4, 338, CELL + 8, 44, Z_FAST }, { QX - 4, 388, CELL + 8, 44, Z_PAUSE }, { 0, 0, 0, 0, 0 } };

static const struct ga_txe tx_table[NTX] = {
	{ "Pipes", 72, 1, 0x00FFE0B0, 0x00D87838 },
	{ "Des tuyaux avant que l'eau n'arrive", 15, 0, 0x00D0E0E8, 0x00A0B8C0 },
	{ "Espace, A ou toucher pour jouer", 18, 1, 0x00FFFFFF, 0x00C8E8F0 },
	{ "Record", 15, 1, 0x00B8C8D0, 0x0090A8B0 },
	{ "Pause", 26, 1, 0x00FFFFFF, 0x00B8E0F0 },
	{ "P ou toucher pour reprendre", 13, 0, 0x00D0E0E8, 0x00A0B8C0 },
	{ "Manche gagn\xC3\xA9" "e !", 24, 1, 0x00C8FFD0, 0x0060D890 },
	{ "Toucher pour la manche suivante", 13, 0, 0x00D0E0E8, 0x00A0B8C0 },
	{ "Ca d\xC3\xA9" "borde !", 24, 1, 0x00FFD878, 0x00F09040 },
	{ "Toucher pour une nouvelle partie", 13, 0, 0x00D0E0E8, 0x00A0B8C0 },
	{ "Posez les tuyaux ! F ou Eau : l'eau part tout de suite", 14, 0, 0x00E0ECF0, 0x00B0C4CC },
	{ "D\xC3\xA9" "bit rapide : points doubl\xC3\xA9" "s", 14, 1, 0x0090E0FF, 0x0060B8E8 },
	{ "/", 20, 1, 0x00C8D8E0, 0x00A0B0B8 },
	{ "Nouveau record !", 14, 1, 0x00FFE080, 0x00F0B040 },
};

static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI2\tPause\tP\nI3c\tSon\n-\nI4\tQuitter\n";

/* la carte de chaque piece : position le long de l'interieur (1..255), 0 ailleurs */
static unsigned char g_maps[NMAP][CELL * CELL];

/* ---------------------------------------------------------------------------------------------------------------
 *  Les tuyaux, point par point (l'asm)
 * --------------------------------------------------------------------------------------------------------------- */
static unsigned long isqrt (unsigned long v)
{
	unsigned long r = 0, b = 1ul << 30;
	while (b > v)
	{
		b >>= 2;
		if (!b)
			return 0;
	}
	while (b)
	{
		if (v >= r + b)
		{
			v -= r + b;
			r = (r + b + b) >> 1;
		}
		else
			r >>= 1;
		b >>= 2;
	}
	return r;
}

/* piece k, point (px, py) en 1/16 -> distance a l'axe (1/16, grand : dehors), *pos = position le long du tuyau
 * 0..255 depuis le premier cote */
static unsigned long pipe_eval (int k, unsigned long px, unsigned long py, unsigned long *pos)
{
	const unsigned long mid = CELL * 8, edge = CELL * 16;
	if (k == 1)
	{
		*pos = px * 255 / edge;
		return py >= mid ? py - mid : mid - py;
	}
	if (k == 2)
	{
		*pos = py * 255 / edge;
		return px >= mid ? px - mid : mid - px;
	}
	if (k == 7)
	{
		unsigned long a = py >= mid ? py - mid : mid - py, b = px >= mid ? px - mid : mid - px;
		*pos = 128;
		return b < a ? b : a;
	}
	if (k < 9)
	{						/* les coudes : un quart de cercle centre sur un coin */
		unsigned long dx = px, dy = py;		/* distance au coin */
		if (k == 3 || k == 4)
			dx = edge - px;			/* coin de droite */
		if (k == 4 || k == 5)
			dy = edge - py;			/* coin du bas */
		unsigned long r = isqrt (dx * dx + dy * dy);
		unsigned long n = (k == 4 || k == 6) ? dx : dy;	/* 3, 5 : dy / (dx + dy) ; 4, 6 : dx / (dx + dy) */
		*pos = n * 255 / (dx + dy + 1);
		return r >= mid ? r - mid : mid - r;
	}
	long d, p;					/* le depart : du milieu vers son cote */
	*pos = 0;
	switch (k - 9)
	{
	case 0:						/* N */
		if (py >= mid) return mid << 4;
		d = (long) px - (long) mid;
		p = (long) mid - (long) py;
		break;
	case 1:						/* E */
		if (px < mid) return mid << 4;
		d = (long) py - (long) mid;
		p = (long) px - (long) mid;
		break;
	case 2:						/* S */
		if (py < mid) return mid << 4;
		d = (long) px - (long) mid;
		p = (long) py - (long) mid;
		break;
	default:					/* O */
		if (px >= mid) return mid << 4;
		d = (long) py - (long) mid;
		p = (long) mid - (long) px;
		break;
	}
	*pos = (unsigned long) p * 255 / mid;
	return (unsigned long) (d < 0 ? -d : d);
}

/* piece k : son sprite (cuivre, ombre et reflet), sa carte */
static gf_sprite_t *gen_pipe (int k)
{
	gf_sprite_t *s = ga_new (CELL, CELL);
	uint32_t *p = s + 2;
	unsigned char *m = g_maps[k];
	for (unsigned long y = 0; y < CELL; y++)
		for (unsigned long x = 0; x < CELL; x++, p++, m++)
		{
			unsigned long pos, dist = pipe_eval (k, (x << 4) + 8, (y << 4) + 8, &pos);
			unsigned long r = TUBE, e = x;		/* la bride, au bord de la case */
			int flange = 0;
			if (e > CELL - 1 - x) e = CELL - 1 - x;
			if (e > y) e = y;
			if (e > CELL - 1 - y) e = CELL - 1 - y;
			if (e < 5 && k < 9)
			{
				r += 32;
				flange = 1;
			}
			if (dist >= r + 8)			/* dehors */
				continue;
			if (dist < BORE)			/* l'interieur : plus sombre au bord */
			{
				ga_put (p, ga_mix (0x00202C34, 0x00141C20, (uint32_t) (64 - dist * 64 / BORE)), 256);
				*m = (unsigned char) (pos | 1);
				continue;
			}
			/* le cuivre : un cylindre eclaire d'en haut a gauche */
			long v = 370 - (long) (dist * dist * 190 / (r * r)) - ((long) (x + y) - CELL) * 5;
			if (flange)
				v += 40;
			if ((unsigned long) v < 60)
				v = 60;
			ga_put (p, ga_shade (0x00C87C44, (uint32_t) v), (r + 8 - dist) << 4);	/* le bord, adouci */
		}
	return s;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Le jeu (la classe d'Onyx)
 * --------------------------------------------------------------------------------------------------------------- */
struct Tile
{
	unsigned char type;
	unsigned char startDir;			/* depart : sa sortie */
	unsigned char inSide, inSideV;		/* cote d'entree de l'eau, horizontal ou seul / vertical (la croix) */
	unsigned short fillH, fillV;		/* croix : remplie horizontalement / verticalement (0..1000) ; les autres : fillH */
};

class Pipes
{
public:
	Tile t[GH][GW];
	long queue[NQ];				/* la file : [NQ - 1] part la premiere */
	long curX, curY;			/* curseur */
	long level, score, show, need, done;	/* score signe : peut passer sous 0 ; show : le score affiche, qui le rattrape */
	long hiscore, newBest;
	long state, prev;
	unsigned long timer, tickN;		/* images depuis le dernier etat, depuis le debut */
	unsigned long countdown, total;		/* ms avant l'eau */
	unsigned long flowMs;			/* ms par piece */
	long fast;
	long wx, wy, wside;			/* la case qui se remplit + le cote d'entree */
	long bombT, bombX, bombY;		/* piece remplacee : images restantes, case */
	long placeT, placeX, placeY;		/* piece posee */
	long btn;				/* bouton appuye (Z_*) */
	long ldirty;
	gf_sprite_t *sp[NMAP], *sWall, *sCur, *sValve, *sPiece, *sOrb[2];
	gf_sprite_t *dbig[10], *dsmall[10], *tx[NTX];
	uint32_t pal[256];			/* l'eau */

	void setState (long s)
	{
		state = s;
		timer = 0;
		gf_still = s == ST_PAUSE;
		gf_redraw = 1;
	}

	long randPiece ()
	{
		if (gf_below (100) < 8)
			return 7;
		return (long) gf_below (6) + 1;
	}

	void newGame ()
	{
		level = 0;
		score = 0;
		show = 0;
		newBest = 0;
		newRound ();
		ldirty = 1;
	}

	void newRound ()
	{
		ga_parts_clear ();
		memset (t, 0, sizeof t);
		/* le depart : loin des bords, sa sortie vers l'interieur */
		long sx = (long) gf_below (GW - 2) + 1, sy = (long) gf_below (GH - 2) + 1, d, nx, ny;
		do
		{
			d = DIRS[gf_below (4)];
			nx = sx + dx_of ((int) d);
			ny = sy + dy_of ((int) d);
		}
		while ((unsigned long) (nx - 1) >= GW - 2 || (unsigned long) (ny - 1) >= GH - 2);
		t[sy][sx].type = P_START;
		t[sy][sx].startDir = (unsigned char) d;
		/* des murs, a partir de la manche 3 */
		long walls = level >= 2 ? (level - 1 > 6 ? 6 : level - 1) : 0;
		while (walls)
		{
			long x = (long) gf_below (GW), y = (long) gf_below (GH);
			if ((x == nx && y == ny) || t[y][x].type)
				continue;
			t[y][x].type = P_WALL;
			walls--;
		}
		for (int i = 0; i < NQ; i++)
			queue[i] = randPiece ();
		curX = nx;
		curY = ny;
		need = 10 + level * 2;			/* 10 + 2 par manche, 30 au plus */
		if (need > 30)
			need = 30;
		done = 0;
		long c = 22000 - level * 1500;		/* 22 s - 1,5 s par manche, 8 s au moins */
		countdown = total = (unsigned long) (c < 8000 ? 8000 : c);
		long f = 3200 - level * 250;		/* 3,2 s - 0,25 s par manche, 1 s au moins */
		flowMs = (unsigned long) (f < 1000 ? 1000 : f);
		fast = 0;
		wx = sx;
		wy = sy;
		wside = 0;
		bombT = 0;
		placeT = 0;
		setState (ST_BUILD);
	}

	void keepBest ()
	{
		if (score <= hiscore)
			return;
		hiscore = score;
		newBest = 1;
		gf_save_best (P_DIR, P_BEST, (unsigned long) hiscore);
	}

	/* poser la piece suivante */
	void place (long x, long y)
	{
		if ((unsigned long) state > ST_FLOW)
			return;
		Tile &k = t[y][x];
		if (k.type >= P_START || k.fillH || k.fillV)
		{
			gs_note (140, 60, AKW_SQUARE, 70);
			return;
		}
		if (k.type)
		{					/* une piece remplacee : -50 */
			score -= 50;
			bombT = 18;
			bombX = x;
			bombY = y;
			ga_burst (x * CELL + PX0 + CELL / 2, y * CELL + PY0 + CELL / 2, 0x00E89048, 26, 220000, 18, 3, 8000);
			gs_note (90, 160, AKW_NOISE, 120);
		}
		else
			gs_note (700, 25, AKW_TRIANGLE, 100);
		k.type = (unsigned char) queue[NQ - 1];
		for (int i = NQ - 1; i > 0; i--)
			queue[i] = queue[i - 1];
		queue[0] = randPiece ();
		placeT = 12;
		placeX = x;
		placeY = y;
	}

	/* l'eau deborde de (x, y) */
	void spill (long x, long y)
	{
		ga_burst (x * CELL + PX0 + CELL / 2, y * CELL + PY0 + CELL / 2, 0x0060C0FF, 60, 260000, 26, 3, 14000);
		if (done >= need)
		{
			score += 1000 + level * 250;
			setState (ST_WON);
			gs_seq (gs_tab_win);
		}
		else
		{
			setState (ST_LOST);
			gs_seq (gs_tab_lose);
		}
		keepBest ();
	}

	/* (wx, wy) est plein : l'eau passe a la case suivante par sa sortie */
	void advance ()
	{
		Tile &k = t[wy][wx];
		int out;
		if (k.type == P_START)
			out = k.startDir;
		else if (k.type == 7)
			out = opposite ((int) wside);
		else
			out = CONN[k.type] & ~(int) wside;
		long nx = wx + dx_of (out), ny = wy + dy_of (out);
		int in = opposite (out);
		if ((unsigned long) nx >= GW || (unsigned long) ny >= GH)
		{
			spill (wx, wy);
			return;
		}
		Tile &n = t[ny][nx];
		bool vert = n.type == 7 && !(in & (E | Wd));	/* la croix prise dans le sens vertical */
		if (n.type == 0 || n.type >= P_START || !(CONN[n.type] & in) || (vert ? n.fillV : n.fillH))
		{
			spill (wx, wy);
			return;
		}
		wx = nx;
		wy = ny;
		wside = in;
		if (vert)
			n.inSideV = (unsigned char) in;
		else
			n.inSide = (unsigned char) in;
	}

	void flowStep (unsigned long dt)
	{
		Tile &k = t[wy][wx];
		unsigned short &f = (k.type == 7 && (wside & (N | S))) ? k.fillV : k.fillH;
		unsigned long ms = fast ? 160 : flowMs;
		if (k.type == P_START)
			ms >>= 1;
		unsigned long v = f + dt * 1000 / ms;
		if (v < 1000)
		{
			f = (unsigned short) v;
			return;
		}
		f = 1000;
		if (k.type != P_START)
		{
			done++;
			score += fast ? 100 : 50;
			if (k.type == 7 && k.fillH == 1000 && k.fillV == 1000)
			{				/* la croix prise dans les deux sens */
				score += 500;
				gs_note (1320, 80, AKW_SQUARE, 100);
			}
			else
				gs_note (400 + (done & 7) * 60, 30, AKW_TRIANGLE, 80);
		}
		advance ();
	}

	/* une image (60 par seconde) */
	void tick ()
	{
		tickN++;
		gs_tick ();
		ga_parts_step ();
		timer++;
		long d = score - show;			/* le score affiche rattrape le vrai */
		if (d)
		{
			long s = d >> 3;
			show += s ? s : d;
		}
		if (bombT)
			bombT--;
		if (placeT)
			placeT--;
		unsigned long dt = tickN % 3 ? 17 : 16;	/* 16,67 ms : 17, 17, 16 */
		if (state == ST_BUILD)
		{
			if (fast || dt >= countdown)
			{
				countdown = 0;
				setState (ST_FLOW);
				gs_note (220, 200, AKW_NOISE, 60);
				return;
			}
			countdown -= dt;
			if (countdown < 5000 && countdown % 1000 < dt)	/* les bips des 5 dernieres secondes */
				gs_note (1000, 30, AKW_SQUARE, 60);
		}
		else if (state == ST_FLOW)
			flowStep (dt);
	}

	/* ---- dessin ---- */
	void genSprites ()
	{
		for (int k = 1; k < NMAP; k++)
			if (k != P_START)
				sp[k] = gen_pipe (k);
		for (unsigned long i = 0; i < 256; i++)	/* l'eau : une onde entre deux bleus */
			pal[i] = ga_mix (0x001C74E0, 0x0068D0FF, (uint32_t) ga_sin8 (i << 2));
		sWall = ga_block (CELL - 8, CELL - 8, 5, 0x00A85C40);	/* un mur de briques */
		uint32_t *w = sWall + 2;
		for (unsigned long y = 0; y < CELL - 8; y++)
			for (unsigned long x = 0; x < CELL - 8; x++)
			{				/* (un joint toutes les 8 lignes, decale d'un rang a l'autre) */
				unsigned long j = (x + ((y & 8) ? 11 : 0)) & 15;
				if (j > 1 && (y & 7))
					continue;
				uint32_t &v = w[y * (CELL - 8) + x];
				if (v >> 24)
					v = 0xFF482820;
			}
		sValve = ga_ball (28, 0x00E04030);	/* le volant du depart */
		sCur = ga_new (CELL, CELL);		/* le curseur : un cadre arrondi */
		uint32_t *p = sCur + 2;
		for (unsigned long y = 0; y < CELL; y++)
			for (unsigned long x = 0; x < CELL; x++, p++)
			{
				unsigned long a = ga_cover (x, y, CELL, CELL, 10), b = 0;
				if (x - 3 < CELL - 6 && y - 3 < CELL - 6)
					b = ga_cover (x - 3, y - 3, CELL - 6, CELL - 6, 7);
				if (a > b)
					ga_put (p, 0x00FFE860, a - b);
			}
		sPiece = ga_disc (CELL + 16, 0x00606058);
		sOrb[0] = ga_disc (170, 0x00102428);
		sOrb[1] = ga_disc (140, 0x00141C30);
		ui_canvas_begin (gf_cv);
		ga_digits (dbig, 20, (long) ui.font_b, 0x00FFFFFF, 0x00B8E0F0);
		ga_digits (dsmall, 15, (long) ui.font_b, 0x00FFFFFF, 0x00C8D8E0);
		ga_texts (tx_table, tx, NTX);
		ui_canvas_end ();
	}

	/* la couche : le ciel ; en jeu, le plateau, la file, les boutons, les titres */
	void buildLayer ()
	{
		ldirty = 0;
		ga_sky (0x00123038, 0x00081820, 0x00206068, 0);
		gf_dst = gf_layer;
		if (state != ST_TITLE)
		{
			ga_panel (PX0 - 6, PY0 - 6, GW * CELL + 12, GH * CELL + 12, 8, 0x00687C84, 0x00303C44, 256, 0x00A0B8C0);
			for (long y = 0; y < GH; y++)	/* un cadre, des dalles */
				for (long x = 0; x < GW; x++)
					ga_panel (PX0 + x * CELL, PY0 + y * CELL, CELL - 1, CELL - 1, 3,
						  ((x + y) & 1) ? 0x00303F48 : 0x00384850, 0x00283438, 256, 0);
			ga_mark (PX0 - 6, PY0 - 6, GW * CELL + 12, GH * CELL + 12);
			ga_panel (QX - 4, PY0 - 6, CELL + 8, NQ * CELL + 12, 8, 0x00687C84, 0x00303C44, 256, 0x00A0B8C0);
			for (long i = 0; i < NQ; i++)	/* la file */
				ga_panel (QX, PY0 + i * CELL, CELL - 1, CELL - 1, 4, i == NQ - 1 ? 0x00486878 : 0x00203038,
					  i == NQ - 1 ? 0x00385060 : 0x00182428, 256, 0);
			ga_mark (QX - 4, PY0 - 6, CELL + 8, NQ * CELL + 12);
			for (const ga_zone_t *z = zones; z->id; z++)	/* les boutons */
				ga_zone_panel (z, 8, 0x00385060, 220, 0x0090A8B8);
			ui_canvas_begin (gf_cv);
			gf_text_c ("Eau", QX + CELL / 2 - 2, 371, 0x00E8F0F4, 13, (long) ui.font_b);
			gf_text_c ("Pause", QX + CELL / 2 - 2, 421, 0x00E8F0F4, 13, (long) ui.font_b);
			gf_text ("Score", QX, 28, 0x00B8C8D0, 13, (long) ui.font_b);
			gf_text ("Manche", 194, 28, 0x00B8C8D0, 13, (long) ui.font_b);
			gf_text ("Tuyaux", 316, 28, 0x00B8C8D0, 13, (long) ui.font_b);
			ui_canvas_end ();
		}
		gf_dst = gf_frame;
	}

	/* l'eau d'une carte en (x, y) : remplissage 0..1000, rev : a rebours de la carte */
	void drawWater (int map, long x, long y, unsigned long fill, int rev)
	{
		const unsigned char *m = g_maps[map];
		unsigned long lim = fill * 255 / 1000;	/* ou elle en est */
		uint32_t *d = gf_dst + y * GF_W + x;
		unsigned long t4 = tickN << 2;
		for (unsigned long yy = 0; yy < CELL; yy++, d += GF_W)
			for (unsigned long xx = 0; xx < CELL; xx++)
			{
				unsigned long v = *m++;
				if (!v)
					continue;
				if (rev)
					v = 256 - v;
				if (v > lim)
					continue;
				uint32_t c = pal[(v + v + (yy << 1) - t4) & 255];
				if (lim - v < 10)		/* le front, plus clair */
					c |= 0xC0C0C0C0;
				d[xx] = c;
			}
	}

	/* l'eau d'une case */
	void tileWater (long x, long y)
	{
		const Tile &k = t[y][x];
		long px = PX0 + x * CELL, py = PY0 + y * CELL;
		if (k.type == P_START)
		{
			if (k.fillH)
				drawWater (9 + side_rank (k.startDir), px, py, k.fillH, 0);
			return;
		}
		if (k.fillH)
		{
			int map = k.type == 7 ? 1 : k.type;
			drawWater (map, px, py, k.fillH, k.inSide != FIRST[map]);
		}
		if (k.type == 7 && k.fillV)
			drawWater (2, px, py, k.fillV, k.inSideV != FIRST[2]);
	}

	void drawBoard ()
	{
		for (long y = 0; y < GH; y++)
			for (long x = 0; x < GW; x++)
			{
				const Tile &k = t[y][x];
				long px = PX0 + x * CELL, py = PY0 + y * CELL;
				if (!k.type)
					continue;
				if (k.type == P_WALL)
					ga_ablit (px + 4, py + 4, sWall);
				else if (k.type == P_START)
				{
					ga_ablit (px, py, sp[9 + side_rank (k.startDir)]);
					tileWater (x, y);
					ga_ablit (px + 13, py + 13, sValve);	/* le volant */
				}
				else
				{
					ga_ablit (px, py, sp[k.type]);
					tileWater (x, y);
				}
			}
		if (placeT)				/* la piece posee luit un instant */
			gf_glow (PX0 + placeX * CELL - 8, PY0 + placeY * CELL - 8, sPiece);
		if (bombT)
			gf_glow (PX0 + bombX * CELL - 8, PY0 + bombY * CELL - 8, sPiece);
	}

	void drawSide ()
	{
		for (long i = 0; i < NQ; i++)		/* la file */
			ga_ablit (QX, PY0 + i * CELL, sp[queue[i]]);
		if (state == ST_BUILD)			/* le temps : l'eau monte dans un tube */
		{
			unsigned long h = countdown * (GH * CELL) / total;
			if (h)
				ga_panel (PX0 - 17, PY0 + GH * CELL - (long) h, 8, h, 3, 0x0070C8FF, 0x002C80E0, 256, 0);
		}
		ga_num ((unsigned long) (show < 0 ? 0 : show), 62, 10, dbig, 4);
		ga_num ((unsigned long) level + 1, 258, 10, dbig, 4);
		ga_num ((unsigned long) done, 420, 10, dbig, 6);
		ga_ablit (422, 10, tx[TX_SLASH]);
		ga_num ((unsigned long) need, 434, 10, dbig, 4);
		if (btn)
			ga_zone_panel (&zones[btn - 1], 8, 0x0080C0F0, 80, 0);
		if ((unsigned long) state <= ST_FLOW)
		{
			const gf_sprite_t *s = fast ? tx[TX_FAST] : state == ST_BUILD ? tx[TX_HINT] : 0;
			if (s)
				ga_show (s, PX0 + GW * CELL / 2, 446);
		}
	}

	/* une boite au milieu du plateau, le score */
	void box (int t1, int t2)
	{
		ga_panel (PX0 + 90, PY0 + 90, GW * CELL - 180, GH * CELL - 180, 14, 0x00304858, 0x00182830, 240, 0x0090B8C8);
		ga_show (tx[t1], PX0 + GW * CELL / 2, PY0 + 106);
		ga_num ((unsigned long) (score < 0 ? 0 : score), PX0 + GW * CELL / 2, PY0 + 142, dbig, 5);
		if (newBest)
			ga_show (tx[TX_NEWBEST], PX0 + GW * CELL / 2, PY0 + 170);
		ga_show (tx[t2], PX0 + GW * CELL / 2, PY0 + 196);
	}

	void drawTitle ()
	{
		unsigned long ph = tickN % 400;		/* une conduite qui se remplit, d'un bord a l'autre */
		for (unsigned long i = 0; i < 12; i++)
		{
			ga_ablit ((long) (CELL * i) - 10, 352, sp[1]);
			if (ph < 36 * i || !i)
				continue;
			unsigned long v = ph - 36 * i;
			if (v > 36)
				v = 36;
			drawWater (1, (long) (CELL * i) - 10, 352, v * 1000 / 36, 0);
		}
		ga_show (tx[TX_LOGO], GF_W / 2, 84);
		ga_show (tx[TX_SUB], GF_W / 2, 182);
		if (ga_sin8 (tickN << 1) >= 40)
			ga_show (tx[TX_PROMPT], GF_W / 2, 234);
		ga_show (tx[TX_RECORD], GF_W / 2 - 40, 282);
		ga_num ((unsigned long) hiscore, GF_W / 2 + 8, 280, dbig, 4);
	}

	void paint ()
	{
		if (ldirty)
			buildLayer ();
		gf_copy (gf_frame, gf_layer, GF_PITCH * GF_H);
		ga_orbs (sOrb[0], sOrb[1], tickN);
		if (state == ST_TITLE)
		{
			drawTitle ();
			return;
		}
		drawSide ();
		drawBoard ();
		ga_ablit (PX0 + curX * CELL, PY0 + curY * CELL, sCur);	/* le curseur */
		ga_parts_draw ();
		if (state == ST_WON)			/* (la pause : gamepause) */
			box (TX_WON, TX_NEXT);
		else if (state == ST_LOST)
			box (TX_LOST, TX_AGAIN);
	}

	/* ---- evenements ---- */
	void doFast ()
	{
		if ((unsigned long) state <= ST_FLOW)
			fast = 1;
	}

	/* -> 1 si l'appui a servi (titre, fin de manche, pause) */
	long startAct ()
	{
		switch (state)
		{
		case ST_TITLE:
			newGame ();
			return 1;
		case ST_PAUSE:
			gp_open ();
			return 1;
		case ST_WON:
			if (timer >= 30)		/* (pas d'un coup de doigt egare) */
			{
				level++;
				newRound ();
			}
			return 1;
		case ST_LOST:
			if (timer >= 30)
				newGame ();
			return 1;
		}
		return 0;
	}

	void cursor (long dx, long dy)
	{
		long x = curX + dx, y = curY + dy;
		if (x < 0) x = 0;
		if (x > GW - 1) x = GW - 1;
		if (y < 0) y = 0;
		if (y > GH - 1) y = GH - 1;
		curX = x;
		curY = y;
	}

	/* point -> 1 si sur le plateau, *x, *y : la case */
	static long cellAt (long px, long py, long *x, long *y)
	{
		unsigned long a = (unsigned long) (px - PX0), b = (unsigned long) (py - PY0);
		if (a >= GW * CELL || b >= GH * CELL)
			return 0;
		*x = (long) (a / CELL);
		*y = (long) (b / CELL);
		return 1;
	}
};

static Pipes g;

/* ---------------------------------------------------------------------------------------------------------------
 *  Les rappels de gamefb et gamepause
 * --------------------------------------------------------------------------------------------------------------- */
static void new_game (void) { g.newGame (); }
static void step (void) { g.tick (); }
static void draw (void) { g.paint (); }

/* (gamepause) la manche s'arrete -> 1 si elle etait en cours (construire ou l'eau qui coule) */
static long game_pause (void)
{
	if ((unsigned long) g.state > ST_FLOW)
		return 0;
	g.prev = g.state;
	g.setState (ST_PAUSE);
	return 1;
}

/* (gamepause) la manche repart */
static void game_resume (void)
{
	if (g.state == ST_PAUSE)
		g.setState (g.prev);
}

static void do_menu (long id)
{
	if (id == ID_QUIT)
		gf_quit = 1;
	else if (id == ID_NEW)
		g.newGame ();
	else if (id == ID_PAUSE)
		gp_open ();
	else if (id == ID_SOUND)
		ui_menu_state (ID_SOUND, gs_toggle () ? UMS_CHECKED : 0);
}

static void on_key (void)
{
	if (!GF_EV (EV_KIND))
		return;
	long id = ui_menu_key (0);
	if (id >= 0)
	{
		do_menu (id);
		return;
	}
	uint32_t ch = (uint32_t) GF_EV (EV_CHAR) | 0x20;
	if (ch == 'p')
	{
		gp_open ();
		return;
	}
	if (ch == 'f')
	{
		g.doFast ();
		return;
	}
	uint32_t code = (uint32_t) GF_EV (EV_CODE);
	if (code == KEY_SPACE_ || code == KEY_ENTER_)
	{
		if (!g.startAct ())
			g.place (g.curX, g.curY);
	}
	else if (code == KEY_LEFT_)
		g.cursor (-1, 0);
	else if (code == KEY_RIGHT_)
		g.cursor (1, 0);
	else if (code == KEY_UP_)
		g.cursor (0, -1);
	else if (code == KEY_DOWN_)
		g.cursor (0, 1);
}

static void on_mouse (void)
{
	long mx, my, x, y;
	gf_mouse (&mx, &my);
	long kind = GF_EV (EV_KIND);
	if (kind == 2)
	{
		g.btn = 0;
		return;
	}
	if (kind != 1)
	{					/* survol : le curseur suit */
		if (Pipes::cellAt (mx, my, &x, &y))
		{
			g.curX = x;
			g.curY = y;
		}
		return;
	}
	if (!(GF_EV (EV_CODE) & 1) || gf_drag_top (PY0 - 14) || g.startAct ())
		return;
	unsigned long z = ga_zone (zones, (unsigned long) mx, (unsigned long) my, 0);
	if (z)
	{
		g.btn = (long) z;
		if (z == Z_FAST)
			g.doFast ();
		else
			gp_open ();
		return;
	}
	if (Pipes::cellAt (mx, my, &x, &y))
	{
		g.curX = x;
		g.curY = y;
		g.place (x, y);
	}
}

/* la manette : A pose (ou passe le titre, la fin de manche), X lache l'eau, la croix deplace le curseur (Start :
 * gamepause) */
static void on_pad (void)
{
	if (!GF_EV (EV_KIND))
		return;
	unsigned long n = gp_new;
	if ((n & PAD_A) && !g.startAct ())
		g.place (g.curX, g.curY);
	else if (n & PAD_A)
		return;
	if (n & PAD_X)
		g.doFast ();
	if (n & PAD_LEFT)
		g.cursor (-1, 0);
	if (n & PAD_RIGHT)
		g.cursor (1, 0);
	if (n & PAD_UP)
		g.cursor (0, -1);
	if (n & PAD_DOWN)
		g.cursor (0, 1);
}

static void on_event (long t)
{
	if (gp_event (t))
		return;
	if (t == WEV_MENU)
		do_menu ((uint32_t) GF_EV (EV_CODE));
	else if (t == WEV_KEY)
		on_key ();
	else if (t == WEV_MOUSE)
		on_mouse ();
	else if (t == WEV_PAD)
		on_pad ();
}

int main (void)
{
	if (gf_open ("Pipes") < 0)
		return 0;
	ui_menu ((long) s_menus);
	if (!ga_init (1024 * 1024))
		return 0;
	gs_open ();
	g.hiscore = (long) gf_load_best (P_BEST);
	g.genSprites ();
	gp_init (game_pause, game_resume, new_game, -1, 0, on_pad);
	g.setState (ST_TITLE);
	g.ldirty = 1;
	gf_run (on_event, step, draw);
	gs_close ();
	return 0;
}
