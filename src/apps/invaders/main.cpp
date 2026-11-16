/*
 * invaders -- Space Invaders, le jeu d'Onyx (C:\Projects\Onyx\user\Apps\invaders\main.cpp). Fleches tenues, souris
 * ou doigt : le canon ; Espace, clic ou appui : le tir (un seul a la fois, comme la borne). La flotte accelere en
 * s'amenuisant, les boucliers s'effritent des deux cotes, une soucoupe passe de temps en temps (points mysteres) ; un
 * alien au sol finit la partie. P : pause. Manette : croix, A tire. Plein ecran ; Start, Echap, P ou le bouton || :
 * le menu de pause (gamepause.h). Taille logique 448 x 480.
 *
 * Port en C++ depuis Onyx : la classe Invaders et ses regles (newGame, newWave, buildShields, hitShield, fire, march,
 * alienFire, shipHit, tick) en viennent, memes nombres et memes durees. L'habillage vient d'invaders.asm (la version
 * de Korin, qui fait foi pour ce qu'on voit) : sprites « neon » calcules depuis les dessins d'Onyx (degrade, lueur),
 * ciel de nebuleuse, eclats, bandeau de verre, ecran titre, record HOME:/invaders/best.txt, manette, menu de
 * pause ; memes pixels que l'asm par gamefb.h, gamedraw.h, gamepause.h, gamesound.h. Le hasard est celui de l'asm
 * (gf_below, xorshift64), pas le xorshift32 d'Onyx (rng_n), tire dans le meme ordre que l'asm. Un seul source pour
 * le Pi et le PC ; l'asm reste dans le dossier jusqu'a sa validation sur le Pi.
 */
#define GF_W		448
#define GF_H		480
#define GF_BAND_TOP	0x00040616		/* les bandes de part et d'autre, comme le ciel */
#define GF_BAND_BOT	0x00140A26
#include "../../inc/gamepause.h"

#define HUD		26
#define S		2			/* echelle des sprites */
#define ACOLS		11
#define AROWS		5
#define ASTEPX		32			/* ecart de la flotte */
#define ASTEPY		26
#define GROUND		(GF_H - 30)
#define SHIPY		(GROUND - 22)
#define NSHIELD		4
#define SHW		22			/* taille d'un bouclier, en cases */
#define SHH		16
#define SHIELDY		(SHIPY - 30 - SHH * S)
#define SHIELDDX	((GF_W - 88 - SHW * S) / (NSHIELD - 1))
#define SHIP_W		26
#define UFO_Y		(HUD + 10)
#define UFO_W		32
#define GLOW		4			/* lueur autour des sprites */

/* les etats d'Onyx (0 jeu, 1 pause, 2 canon touche, 3 fin, 4 vague finie), plus le titre de l'asm */
enum { ST_PLAY, ST_PAUSE, ST_HIT, ST_OVER, ST_CLEAR, ST_TITLE };

enum { TX_LOGO, TX_SUB, TX_PROMPT, TX_RECORD, TX_OVER, TX_AGAIN, TX_NEWBEST, TX_CLEAR, TX_SCORE, TX_BEST, TX_WAVE, NTX };

enum { ID_NEW = 1, ID_PAUSE, ID_SOUND, ID_QUIT };

#define KEY_ENTER_	0x28
#define KEY_SPACE_	0x2C
#define KEY_RIGHT_	0x4F
#define KEY_LEFT_	0x50

#define COL_SHIP	0x0040FF40
#define COL_SQUID	0x00F060F0
#define COL_CRAB	0x0060E0F0
#define COL_OCTO	0x0060F060
#define COL_UFO		0x00FF4040

#define P_DIR		"HOME:/invaders"
#define P_BEST		"HOME:/invaders/best.txt"

/* ---- les dessins d'Onyx : '#' = une case ; deux images par alien (la marche), a la suite ---- */
static const char spr_ship[] =
	"......#......" ".....###....." ".....###....." ".###########."
	"#############" "#############" "#############" "#############";
static const char spr_squid[] =
	"...##..." "..####.." ".######." "##.##.##" "########" "..#..#.." ".#.##.#." "#.#..#.#"
	"...##..." "..####.." ".######." "##.##.##" "########" ".#.##.#." "#......#" ".#....#.";
static const char spr_crab[] =
	"..#.....#.." "...#...#..." "..#######.." ".##.###.##." "###########" "#.#######.#" "#.#.....#.#" "...##.##..."
	"..#.....#.." "#..#...#..#" "#.#######.#" "###.###.###" "###########" ".#########." "..#.....#.." ".#.......#.";
static const char spr_octo[] =
	"....####...." ".##########." "############" "###..##..###" "############" "...##..##..." "..##.##.##.." "##........##"
	"....####...." ".##########." "############" "###..##..###" "############" "..###..###.." ".##..##..##." "..##....##..";
static const char spr_ufo[] =
	".....######....." "...##########..." "..############.." ".##.##.##.##.##." "################" "..###..##..###.."
	"...#........#...";
static const char spr_boom[] =
	"#...#..#...#" ".#...##...#." "..#......#.." "##........##" "..#......#.." ".#..#..#..#." "#...#..#...#" "............";
static const char spr_shot[] = "####";
static const char spr_bomb[] = "#..##..#" ".##..##.";
/* la moitie gauche d'un bouclier ; la droite est en miroir */
static const char shield_rows[] =
	"....#######" "...########" "..#########" ".##########" "###########" "###########"
	"###########" "###########" "###########" "###########" "###########" "#######...."
	"######....." "#####......" "#####......" "#####......";

/* rangee -> sorte (calmar, crabe, poulpe), largeur du dessin, couleur ; sorte -> points */
static const unsigned char alien_kind[AROWS] = { 0, 1, 1, 2, 2 };
static const unsigned char alien_w[AROWS] = { 8, 11, 11, 12, 12 };
static const uint32_t alien_col[AROWS] = { COL_SQUID, COL_CRAB, COL_CRAB, COL_OCTO, COL_OCTO };
static const unsigned short kind_pts[3] = { 30, 20, 10 };
static const struct { const char *art; unsigned w; uint32_t col; } neon_tab[6] = {
	{ spr_squid, 8, COL_SQUID }, { spr_squid + 64, 8, COL_SQUID }, { spr_crab, 11, COL_CRAB },
	{ spr_crab + 88, 11, COL_CRAB }, { spr_octo, 12, COL_OCTO }, { spr_octo + 96, 12, COL_OCTO } };
/* les boucliers en degrade, une couleur par ligne */
static const uint32_t shield_col[SHH] = { 0x0090FFD0, 0x0084F8C4, 0x0078F0B8, 0x006CE8AC, 0x0060E0A0, 0x0058D498, 0x0050C890,
	0x0048BC88, 0x0040B080, 0x0038A478, 0x00349870, 0x00308C68, 0x002C8060, 0x00287458, 0x00246850, 0x00205C48 };
static const unsigned short bass[4] = { 98, 87, 78, 73 };		/* la fameuse basse qui descend */
static const unsigned short ufo_pts[4] = { 50, 100, 150, 300 };
static const struct gs_ent snd_fire[] = { { 0, 1600, 40, AKW_SAW, 60 }, { 40, 1100, 40, AKW_SAW, 50 }, { 0, 0, 0, 0, 0 } };
static const struct gs_ent snd_ufo[] = { { 0, 880, 80, AKW_SQUARE, 90 }, { 90, 660, 80, AKW_SQUARE, 90 },
	{ 180, 440, 120, AKW_SQUARE, 90 }, { 0, 0, 0, 0, 0 } };

static const struct ga_txe tx_table[NTX] = {
	{ "INVADERS", 58, 1, 0x00C8FFF0, 0x0040B0FF },
	{ "La flotte descend : tenez la ligne", 15, 0, 0x00D0D8F0, 0x0098A4C8 },
	{ "Espace, A ou toucher pour jouer", 18, 1, 0x00FFFFFF, 0x00D0E0FF },
	{ "Record", 15, 1, 0x00B8C4E0, 0x008C98B8 },
	{ "Partie termin\xC3\xA9" "e", 26, 1, 0x00FFD878, 0x00F09040 },
	{ "Espace, A ou toucher : nouvelle partie", 13, 0, 0x00D0D8F0, 0x00A0A8C8 },
	{ "Nouveau record !", 15, 1, 0x00FFE080, 0x00F0B040 },
	{ "Vague termin\xC3\xA9" "e !", 24, 1, 0x00C8FFD0, 0x0060D890 },
	{ "Score", 12, 0, 0x0098A8C8, 0x007888A8 },
	{ "Record", 12, 0, 0x0098A8C8, 0x007888A8 },
	{ "Vague", 12, 0, 0x0098A8C8, 0x007888A8 },
};

static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI2\tPause\tP\nI3c\tSon\n-\nI4\tQuitter\n";

struct Shot { long live, x, y; };
struct Bomb { long live, x, y, vy; };

/* ---- les sprites « neon » de l'asm ---- */
/* dessin '#', largeur, hauteur (cases), couleur, echelle -> sprite (GLOW points de marge) : les cases en degrade,
 * clair en haut, entourees d'une lueur de leur couleur */
static gf_sprite_t *neon (const char *art, unsigned long w, unsigned long h, uint32_t col, unsigned long sc)
{
	unsigned long sw = w * sc + GLOW * 2, sh = h * sc + GLOW * 2;
	gf_sprite_t *s = ga_new (sw, sh);
	uint32_t *p = s + 2;
	for (unsigned long y = 0; y < sh; y++)
		for (unsigned long x = 0; x < sw; x++, p++)
		{
			long cx = (long) x - GLOW, cy = (long) y - GLOW;	/* la case sous le point */
			if (cx >= 0 && cy >= 0 && (unsigned long) cx / sc < w && (unsigned long) cy / sc < h
			    && art[(unsigned long) cy / sc * w + (unsigned long) cx / sc] == '#')
			{					/* le corps : 380 en haut, 200 en bas */
				unsigned long f = (((unsigned long) cy << 8) / (h * sc)) * 180 >> 8;
				ga_put (p, ga_shade (col, (uint32_t) (380 - f)), 256);
				continue;
			}
			long px = (long) (x << 1) + 1, py = (long) (y << 1) + 1;	/* en demi-points */
			unsigned long best = 0x7FFFFFFF;	/* distance (au carre) a la case allumee la plus proche */
			for (unsigned long r = 0; r < h; r++)
				for (unsigned long c = 0; c < w; c++)
				{
					if (art[r * w + c] != '#')
						continue;
					long a = (long) ((c * sc + GLOW) << 1), b = a + (long) (sc << 1);
					long dx = a - px, t = px - b;
					if (!(dx > t)) dx = t;
					if (dx < 0) dx = 0;
					a = (long) ((r * sc + GLOW) << 1);
					b = a + (long) (sc << 1);
					long dy = a - py;
					t = py - b;
					if (!(dy > t)) dy = t;
					if (dy < 0) dy = 0;
					unsigned long d = (unsigned long) (dx * dx + dy * dy);
					if (d < best)
						best = d;
				}
			if (best >= 4 * GLOW * GLOW)
				continue;
			unsigned long a = (4 * GLOW * GLOW - best) * 110 / (4 * GLOW * GLOW);
			ga_put (p, col, a * a / 110);
		}
	return s;
}

/* ---------------------------------------------------------------------------------------------------------------
 *  Le jeu (la classe d'Onyx)
 * --------------------------------------------------------------------------------------------------------------- */
class Invaders
{
public:
	unsigned char alive[AROWS][ACOLS];
	long fx, fy, fdir, frame;		/* origine de la flotte, sens, image de la marche */
	unsigned long stepT;
	long marchNote;
	long shipX, lives, wave;
	unsigned long score, hiscore;
	long newBest;
	Shot shot;
	Bomb bomb[3];
	unsigned char shield[NSHIELD][SHH][SHW];
	long ufoLive, ufoX, ufoDir, ufoScore, ufoShowX;
	unsigned long ufoNext, ufoShowT;
	long state, prev;
	unsigned long stateT, pauseSt, pauseT;
	unsigned long boomT;			/* la derniere explosion */
	long boomX, boomY;
	long mx, lastMouseX, btn;		/* souris (logique, -1 : pas encore), bouton ou doigt tenu */
	unsigned long now, tickN;
	long dirty;				/* couche fixe a refaire */
	gf_sprite_t *sAlien[6], *sShip, *sLife, *sUfo, *sBoom, *sShot, *sBomb[2], *sOrb[2];
	gf_sprite_t *dig[10], *dsm[10], *tx[NTX];

	void setState (long s)
	{
		state = s;
		stateT = now;
		gf_still = s == ST_PAUSE;
		gf_redraw = 1;
	}

	void newGame ()
	{
		ga_parts_clear ();
		newBest = 0;
		score = 0;
		wave = 0;
		lives = 3;
		newWave ();
		buildShields ();
	}

	void newWave ()
	{
		for (int r = 0; r < AROWS; r++)
			for (int c = 0; c < ACOLS; c++)
				alive[r][c] = 1;
		fx = 40;
		fy = HUD + 60 + (unsigned long) wave % 6 * 12;
		fdir = 1;
		frame = 0;
		marchNote = 0;
		now = gf_time_ms ();
		stepT = now;
		shipX = GF_W / 2 - 13;
		shot.live = 0;
		for (int i = 0; i < 3; i++)
			bomb[i].live = 0;
		ufoLive = 0;
		ufoShowT = 0;
		boomT = 0;
		ufoNext = gf_below (10000) + 15000 + now;
		setState (ST_PLAY);
	}

	void buildShields ()
	{
		for (int s = 0; s < NSHIELD; s++)
			for (int y = 0; y < SHH; y++)
				for (int x = 0; x < SHW; x++)
				{
					int xx = x < SHW / 2 ? x : SHW - 1 - x;
					shield[s][y][x] = shield_rows[y * (SHW / 2) + xx] == '#';
				}
	}

	static long shieldX (long s) { return 44 + s * SHIELDDX; }

	long remaining () const
	{
		long n = 0;
		for (int r = 0; r < AROWS; r++)
			for (int c = 0; c < ACOLS; c++)
				n += alive[r][c];
		return n;
	}

	static long alienPoints (long r) { return r == 0 ? 30 : r < 3 ? 20 : 10; }
	static long alienW (long r) { return alien_w[r] * S; }
	long alienX (long r, long c) const { return fx + c * ASTEPX + (long) ((unsigned long) (24 - alienW (r)) >> 1); }
	long alienY (long r) const { return fy + r * ASTEPY; }

	/* un tir contre les boucliers (une petite explosion) ; down : 1 vers le bas, -1 vers le haut -> 1 s'il a touche */
	long hitShield (long x, long y, long down)
	{
		for (long s = 0; s < NSHIELD; s++)
		{
			long sx = (x - shieldX (s)) / S, sy = (y - SHIELDY) / S;
			if ((unsigned long) sx >= SHW || (unsigned long) sy >= SHH || !shield[s][sy][sx])
				continue;
			for (long dy = -2; dy <= 2; dy++)
				for (long dx = -2; dx <= 2; dx++)
				{
					long yy = sy + dy + down, xx = sx + dx;
					if (dx * dx + dy * dy > 4 || (unsigned long) yy >= SHH || (unsigned long) xx >= SHW)
						continue;
					if (gf_below (4))
						shield[s][yy][xx] = 0;
				}
			return 1;
		}
		return 0;
	}

	void fire ()
	{
		if (state != ST_PLAY || shot.live)
			return;
		shot.live = 1;
		shot.x = shipX + 12;
		shot.y = SHIPY - 8;
		gs_seq (snd_fire);
	}

	void keepBest ()
	{
		if (score <= hiscore)
			return;
		hiscore = score;
		newBest = 1;
		gf_save_best (P_DIR, P_BEST, hiscore);
	}

	void march ()
	{
		long every = remaining () * 14 + 30 - wave * 20;
		if (every < 30)
			every = 30;
		if (now - stepT < (unsigned long) every)
			return;
		stepT = now;
		long minx = GF_W, maxx = 0, maxy = 0;	/* les bords de la flotte vivante */
		for (long r = 0; r < AROWS; r++)
			for (long c = 0; c < ACOLS; c++)
				if (alive[r][c])
				{
					long x = alienX (r, c), y = alienY (r);
					if (x < minx) minx = x;
					if (x + alienW (r) > maxx) maxx = x + alienW (r);
					if (y + 16 > maxy) maxy = y + 16;
				}
		if ((fdir > 0 && maxx + 8 > GF_W - 8) || (fdir <= 0 && minx - 8 < 8))
		{
			fdir = -fdir;
			fy += 12;
		}
		else
			fx += fdir * 8;
		frame ^= 1;
		gs_note (bass[marchNote], 70, AKW_SQUARE, 110);
		marchNote = (marchNote + 1) & 3;
		/* la flotte mange les boucliers qu'elle traverse, et gagne au sol */
		for (long r = 0; r < AROWS; r++)
			for (long c = 0; c < ACOLS; c++)
			{
				if (!alive[r][c])
					continue;
				long ax = alienX (r, c), ay = alienY (r);
				if (ay + 16 <= SHIELDY)
					continue;
				for (long s = 0; s < NSHIELD; s++)
					for (long y = 0; y < SHH; y++)
						for (long x = 0; x < SHW; x++)
						{
							long px = shieldX (s) + x * 2, py = SHIELDY + y * 2;
							if (px >= ax && px < ax + 24 && py >= ay && py < ay + 16)
								shield[s][y][x] = 0;
						}
			}
		if (maxy >= SHIPY)
		{
			lives = 0;
			shipHit ();
		}
	}

	void alienFire ()
	{
		if (gf_below (100) >= (unsigned long) (wave + 2))	/* chance par image, sur 100 */
			return;
		long c = (long) gf_below (ACOLS);
		for (long r = AROWS - 1; r >= 0; r--)
		{
			if (!alive[r][c])
				continue;
			for (int i = 0; i < 3; i++)
				if (!bomb[i].live)
				{
					bomb[i].live = 1;
					bomb[i].x = alienX (r, c) + 10;
					bomb[i].y = alienY (r) + 16;
					bomb[i].vy = (long) gf_below (2) + 3 + ((unsigned long) wave > 3 ? 1 : 0);
					return;
				}
			return;
		}
	}

	void shipHit ()
	{
		ga_burst (shipX + SHIP_W / 2, SHIPY + 8, 0x0080FFC0, 60, 0x40000, 40, 3, 0x1800);
		setState (ST_HIT);
		gs_note (120, 400, AKW_NOISE, 160);
		if (--lives <= 0)
		{
			lives = 0;
			setState (ST_OVER);
			keepBest ();
			gs_seq (gs_tab_lose);
		}
		shot.live = 0;
		for (int i = 0; i < 3; i++)
			bomb[i].live = 0;
	}

	/* le tir du canon */
	void shotStep ()
	{
		if (!shot.live)
			return;
		long x = shot.x, y = shot.y -= 9;
		if (y < HUD + 4 || hitShield (x, y, -1))
		{
			shot.live = 0;
			return;
		}
		for (long r = 0; r < AROWS; r++)
			for (long c = 0; c < ACOLS; c++)
			{
				if (!alive[r][c])
					continue;
				long ax = alienX (r, c), ay = alienY (r);
				if (x < ax || x >= ax + alienW (r) || y < ay || y >= ay + 16)
					continue;
				boomX = ax;			/* touche */
				boomY = ay;
				boomT = now;
				alive[r][c] = 0;
				score += alienPoints (r);
				ga_burst (ax + 12, ay + 8, alien_col[r], 22, 0x28000, 18, 2, 0x1000);	/* les eclats */
				gs_note (220, 90, AKW_NOISE, 120);
				shot.live = 0;
				return;
			}
		for (int i = 0; i < 3; i++)		/* les tirs peuvent se heurter */
			if (bomb[i].live && x >= bomb[i].x - 3 && x <= bomb[i].x + 3 && y <= bomb[i].y + 10 && y >= bomb[i].y - 4)
			{
				bomb[i].live = 0;
				shot.live = 0;
				return;
			}
		if (ufoLive && x >= ufoX && x < ufoX + UFO_W && y >= HUD + 10 && y < HUD + 24)
		{					/* la soucoupe */
			ufoScore = ufo_pts[gf_below (4)];
			score += (unsigned long) ufoScore;
			ufoLive = 0;
			ufoShowX = ufoX;
			ufoShowT = now;
			ga_burst (ufoX + UFO_W / 2, UFO_Y + 7, COL_UFO, 40, 0x38000, 26, 2, 0x800);
			gs_seq (snd_ufo);
			shot.live = 0;
		}
	}

	void bombsStep ()
	{
		for (int i = 0; i < 3; i++)
		{
			Bomb &b = bomb[i];
			if (!b.live)
				continue;
			b.y += b.vy;
			if (b.y > GROUND || hitShield (b.x, b.y + 8, 1))
				b.live = 0;
			else if (b.x >= shipX && b.x < shipX + SHIP_W && b.y + 8 >= SHIPY && b.y < SHIPY + 16)
			{
				b.live = 0;
				shipHit ();
				return;
			}
		}
	}

	void ufoStep ()
	{
		if (!ufoLive)
		{
			if (now <= ufoNext || remaining () <= 6)
				return;
			if (gf_below (2))
			{
				ufoDir = 1;
				ufoX = -UFO_W;
			}
			else
			{
				ufoDir = -1;
				ufoX = GF_W;
			}
			ufoLive = 1;
			ufoNext = now + 20000 + gf_below (15000);
		}
		ufoX += ufoDir * 2;
		if ((now / 120) & 1)
			gs_note (ufoDir < 0 ? 700 : 740, 60, AKW_TRIANGLE, 50);
		if (ufoX < -40 || ufoX > GF_W + 8)
			ufoLive = 0;
	}

	/* une image (60 par seconde) */
	void tick ()
	{
		tickN++;
		ga_parts_step ();
		now = gf_time_ms ();
		if (ufoShowT && now - ufoShowT >= 1000)	/* le score de la soucoupe s'efface */
			ufoShowT = 0;
		if (state == ST_PAUSE || state == ST_TITLE || state == ST_OVER)
			return;
		if (state == ST_HIT)
		{
			if (now - stateT > 1200)
				setState (ST_PLAY);
			return;
		}
		if (state == ST_CLEAR)
		{
			if (now - stateT > 1500)
			{
				wave++;
				newWave ();
			}
			return;
		}
		/* le canon (touches ou croix tenues, sinon la souris) */
		long d = 0;
		if (gf_held[KEY_LEFT_] | ((gp_now & PAD_LEFT) != 0))
			d -= 4;
		if (gf_held[KEY_RIGHT_] | ((gp_now & PAD_RIGHT) != 0))
			d += 4;
		long x = shipX, m = mx, last = lastMouseX;
		lastMouseX = m;
		if (d)
			x += d;
		else if (m != last && m >= 0)
			x = m - 13;
		if (x < 8) x = 8;
		if (x > GF_W - 8 - SHIP_W) x = GF_W - 8 - SHIP_W;
		shipX = x;
		if (gf_held[KEY_SPACE_] | btn | ((gp_now & PAD_A) != 0))	/* (A tenu tire aussi) */
			fire ();
		march ();
		if (state != ST_PLAY)
			return;
		alienFire ();
		shotStep ();
		bombsStep ();
		if (state != ST_PLAY)
			return;
		ufoStep ();
		if (remaining ())
			return;
		setState (ST_CLEAR);
		keepBest ();
		gs_seq (gs_tab_win);
	}

	/* ---- dessin ---- */
	void genSprites ()
	{
		for (int i = 0; i < 6; i++)
			sAlien[i] = neon (neon_tab[i].art, neon_tab[i].w, 8, neon_tab[i].col, S);
		sShip = neon (spr_ship, 13, 8, COL_SHIP, S);
		sLife = neon (spr_ship, 13, 8, COL_SHIP, 1);
		sUfo = neon (spr_ufo, 16, 7, COL_UFO, S);
		sBoom = neon (spr_boom, 12, 8, 0x00FFE0A0, S);
		sShot = neon (spr_shot, 1, 4, 0x00B0E8FF, 2);
		sBomb[0] = neon (spr_bomb, 2, 4, 0x00FFB040, 2);
		sBomb[1] = neon (spr_bomb + 8, 2, 4, 0x00FFB040, 2);
		sOrb[0] = ga_disc (200, 0x00140828);
		sOrb[1] = ga_disc (160, 0x00081428);
		ui_canvas_begin (gf_cv);
		ga_digits (dig, 16, (long) ui.font_b, 0x00FFFFFF, 0x00B8E0FF);
		ga_digits (dsm, 14, (long) ui.font_b, 0x00FFD0C0, 0x00FF6050);
		ga_texts (tx_table, tx, NTX);
		ui_canvas_end ();
	}

	/* la couche fixe : nebuleuse, etoiles, sol lumineux */
	void paintLayer ()
	{
		dirty = 0;
		gf_dst = gf_layer;
		ga_sky (0x00040616, 0x00140A26, 0x00342050, 260);
		gf_rect (0, GROUND - 1, GF_W, 4, 0x00103828);
		gf_rect (0, GROUND, GF_W, 2, 0x0060E0A8);
		gf_rect (0, GROUND + 3, GF_W, GF_H - GROUND - 3, 0x00060A12);
		gf_dst = gf_frame;
	}

	/* etiquette et chiffres du bandeau */
	void hudItem (int t, long x, unsigned long v)
	{
		const gf_sprite_t *s = tx[t];
		ga_ablit (x, 4, s);
		ga_num (v, x + (long) s[0] + 2, 3, dig, 4);
	}

	/* un panneau de verre au milieu */
	static void glass (long y, unsigned long h)
	{
		ga_panel (56, y, GF_W - 112, h, 16, 0x00241C44, 0x00100C22, 232, 0x006858A8);
	}

	const gf_sprite_t *alienSprite (long r) const { return sAlien[(alien_kind[r] << 1) + frame]; }

	void drawTitle ()
	{
		ga_show (tx[TX_LOGO], GF_W / 2, 86);
		ga_show (tx[TX_SUB], GF_W / 2, 170);
		for (unsigned long k = 0; k < 3; k++)	/* les trois aliens et leurs points */
		{
			long top = (long) (ga_sin8 ((tickN + (k << 6)) << 2) >> 5) + 226;	/* le haut, qui flotte */
			long cx = (long) (110 * k) + GF_W / 2 - 110;			/* le centre */
			const gf_sprite_t *s = sAlien[((tickN >> 5) & 1) + (k << 1)];
			ga_ablit (cx - (long) (s[0] >> 1), top, s);
			ga_num (kind_pts[k], cx, 268, dsm, 5);
		}
		if (ga_sin8 (tickN << 1) >= 40)
			ga_show (tx[TX_PROMPT], GF_W / 2, 322);
		ga_show (tx[TX_RECORD], GF_W / 2 - 34, 370);
		ga_num (hiscore, GF_W / 2 + 12, 368, dig, 4);
	}

	void paint ()
	{
		if (dirty)
			paintLayer ();
		gf_copy (gf_frame, gf_layer, GF_PITCH * GF_H);
		ga_orbs (sOrb[0], sOrb[1], tickN);
		if (state == ST_TITLE)
		{
			drawTitle ();
			ga_parts_draw ();
			return;
		}
		ga_panel (0, 0, GF_W, HUD, 0, 0x00202848, 0x00101428, 200, 0);	/* le bandeau */
		gf_rect (0, HUD - 1, GF_W, 1, 0x004068C0);
		hudItem (TX_SCORE, 10, score);
		hudItem (TX_BEST, 148, hiscore);
		hudItem (TX_WAVE, 290, (unsigned long) wave + 1);
		for (long r = 0; r < AROWS; r++)	/* la flotte */
			for (long c = 0; c < ACOLS; c++)
				if (alive[r][c])
					ga_ablit (alienX (r, c) - GLOW, alienY (r) - GLOW, alienSprite (r));
		if (boomT && now - boomT < 200)		/* l'explosion */
			ga_ablit (boomX - GLOW, boomY - GLOW, sBoom);
		if (ufoLive)				/* la soucoupe */
			ga_ablit (ufoX - GLOW, UFO_Y - GLOW, sUfo);
		for (long s = 0; s < NSHIELD; s++)	/* les boucliers, en degrade */
		{
			uint32_t *d = gf_dst + SHIELDY * GF_W + shieldX (s);
			for (int y = 0; y < SHH; y++, d += 2 * GF_W)
			{
				uint32_t c = shield_col[y];
				for (int x = 0; x < SHW; x++)
					if (shield[s][y][x])
						d[2 * x] = d[2 * x + 1] = d[GF_W + 2 * x] = d[GF_W + 2 * x + 1] = c;
			}
		}
		if (state != ST_OVER)			/* le canon */
		{
			const gf_sprite_t *sp = sShip;
			if (state == ST_HIT && ((now / 100) & 1))
				sp = sBoom;
			ga_ablit (shipX - GLOW, SHIPY - GLOW, sp);
		}
		if (shot.live)
			ga_ablit (shot.x - 1 - GLOW, shot.y - GLOW, sShot);
		for (int i = 0; i < 3; i++)
			if (bomb[i].live)
				ga_ablit (bomb[i].x - 1 - GLOW, bomb[i].y - GLOW, sBomb[((unsigned long) bomb[i].y >> 2) & 1]);
		ga_parts_draw ();
		if (ufoShowT)				/* les points de la soucoupe */
			ga_num ((unsigned long) ufoScore, ufoShowX + UFO_W / 2, UFO_Y - 4, dsm, 5);
		ga_num ((unsigned long) lives, 10, GROUND + 5, dig, 4);	/* les vies */
		for (long i = 0; i < lives - 1 && i < 5; i++)
			ga_ablit (28 * i + 32, GROUND + 6, sLife);
		if (state == ST_OVER)
		{
			glass (150, 172);
			ga_show (tx[TX_OVER], GF_W / 2, 164);
			ga_num (score, GF_W / 2, 208, dig, 5);
			if (newBest)
				ga_show (tx[TX_NEWBEST], GF_W / 2, 240);
			ga_show (tx[TX_AGAIN], GF_W / 2, 280);
		}
		else if (state == ST_CLEAR)
		{
			glass (196, 64);
			ga_show (tx[TX_CLEAR], GF_W / 2, 210);
		}
	}

	/* ---- evenements ---- */
	/* Espace, Entree, clic : selon l'etat */
	void action ()
	{
		if (state == ST_TITLE)
			newGame ();
		else if (state == ST_OVER)
		{
			if (now - stateT >= 600)	/* (pas un tir d'avant la fin) */
				newGame ();
		}
		else if (state == ST_PAUSE)
			setState (ST_PLAY);
		else
			fire ();
	}
};

static Invaders g;

/* ---------------------------------------------------------------------------------------------------------------
 *  Les rappels de gamefb et gamepause
 * --------------------------------------------------------------------------------------------------------------- */
static void new_game (void) { g.newGame (); }
static void step (void) { g.tick (); }
static void draw (void) { g.paint (); }

/* (gamepause) le jeu s'arrete, une explosion ou une fin de vague comprise -> 1 si une partie etait en cours */
static long game_pause (void)
{
	g.btn = 0;
	long s = g.state;
	if (s == ST_PAUSE || s == ST_OVER || s == ST_TITLE)
		return 0;
	g.prev = s;
	g.pauseSt = g.stateT;
	g.pauseT = gf_time_ms ();
	g.setState (ST_PAUSE);
	return 1;
}

/* (gamepause) le jeu repart : les heures retenues avancent de la duree de la pause */
static void game_resume (void)
{
	if (g.state != ST_PAUSE)
		return;
	g.now = gf_time_ms ();
	unsigned long d = g.now - g.pauseT;
	g.setState (g.prev);
	g.stateT = g.pauseSt + d;
	g.stepT += d;
	g.ufoNext += d;
	if (g.ufoShowT)
		g.ufoShowT += d;
	if (g.boomT)
		g.boomT += d;
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

/* la manette : A hors de la partie (en jeu, A tenu tire, voir tick) */
static void on_pad (void)
{
	if ((gp_new & PAD_A) && g.state != ST_PLAY)
		g.action ();
}

static void on_event (long t)
{
	if (gp_event (t))
		return;
	if (t == WEV_PAD)
		on_pad ();
	else if (t == WEV_MENU)
		do_menu ((uint32_t) GF_EV (EV_CODE));
	else if (t == WEV_KEY)
	{
		if (!GF_EV (EV_KIND))
			return;
		long id = ui_menu_key (0);
		if (id >= 0)
		{
			do_menu (id);
			return;
		}
		if (((uint32_t) GF_EV (EV_CHAR) | 0x20) == 'p')
		{
			gp_open ();
			return;
		}
		uint32_t code = (uint32_t) GF_EV (EV_CODE);
		if (code == KEY_SPACE_ || code == KEY_ENTER_)
			g.action ();
	}
	else if (t == WEV_FOCUS)
		g.btn = 0;
	else if (t == WEV_MOUSE)
	{
		long x, y;
		gf_mouse (&x, &y);
		long kind = GF_EV (EV_KIND);
		if (!kind)
			g.mx = x;
		else if (kind == 1)
		{
			if (!(GF_EV (EV_CODE) & 1))
				return;
			g.mx = x;
			if (gf_drag_top (HUD))
				return;
			g.btn = 1;
			g.action ();
		}
		else
			g.btn = 0;
	}
}

int main (void)
{
	if (gf_open ("Invaders") < 0)
		return 0;
	ui_menu ((long) s_menus);
	if (!ga_init (1024 * 1024))
		return 0;
	gs_open ();
	g.hiscore = gf_load_best (P_BEST);
	g.genSprites ();
	gp_init (game_pause, game_resume, new_game, GF_W - 30, 0, on_pad);
	g.mx = g.lastMouseX = -1;
	g.dirty = 1;
	g.now = gf_time_ms ();
	g.setState (ST_TITLE);
	gf_run (on_event, step, draw);
	gs_close ();
	return 0;
}
