---
name: korin-game
description: Écrire un jeu d'arcade en C (ou C++) pour Korin OS comme ceux du système (Tetris, Invaders, Arkanoid) - le cadre commun des jeux (en-têtes gamefb.h, gamedraw.h, gamepause.h, gamesound.h, gamepad.h fournis avec cette compétence) : plein écran à taille logique fixe, 60 pas par seconde, couche fixe et sprites, menu de pause, manette, sons, record ; puis la façon de concevoir le jeu (états, objets en tableaux fixes, virgule fixe, collisions, niveaux, hasard). À utiliser dès qu'on demande un jeu en C, C++ ou assembleur (casse-briques, tir, puzzle, plateforme, labyrinthe...) qui n'est pas un projet Godot.
user-invocable: true
---

# Écrire un jeu en C pour Korin

Les jeux du système (Tetris, Invaders, Arkanoid, Snake...) partagent un **cadre de jeu** : cinq en-têtes C, joints à
cette compétence dans `inc/`, et trois jeux complets à lire dans `examples/` (`arkanoid/main.c`, `tetris/main.cpp`,
`invaders/main.cpp`). Ce que le cadre donne tout fait : la fenêtre plein écran, la mise à l'échelle, la cadence, le
menu de pause, la manette, le clavier tenu, les sons, le record, la traduction.

À combiner avec : `korin-app` (dossier, `app.txt`, `lang/`, construire et installer avec `pibuild`) et, pour le dessin
UIKit lui-même (texte, canevas), `korin-graphics`. Un jeu **dans une fenêtre** avec des widgets autour (un plateau de
jeu de société, un jeu de cartes) : plutôt la boucle de `korin-graphics` sur UIKit seul. Un jeu 3D ou avec un éditeur
de scènes : `korin-godot` (le PC).

## 1. Le projet

```
/home/projects/monjeu/
  main.c          (ou main.cpp) : tout le jeu, un seul fichier source
  gamefb.h gamedraw.h gamepause.h gamesound.h gamepad.h    copiés depuis inc/ de cette compétence
  app.txt         category = Jeux, description, keywords, help (les touches) : voir korin-app
  lang/en.txt     les textes anglais (les chaînes du jeu sont en français, traduites par gf_tr / ui_tr)
```

Copier les en-têtes : `cp <dossier de cette compétence>/inc/*.h /home/projects/monjeu/`. Dans `main.c` :

```c
#define GF_W 640			/* taille LOGIQUE du jeu (largeur multiple de 8) : tout se calcule là-dedans, */
#define GF_H 480			/* le cadre met l'image à l'échelle de l'écran (proportions gardées) */
#include "gamepause.h"			/* inclut gamedraw.h, gamefb.h, gamesound.h, gamepad.h, uikit.h */
```

(Les exemples écrivent `#include "../../inc/gamepause.h"` : le chemin de l'arbre de Korin ; dans un projet, les
en-têtes sont à côté de `main.c`.) Tout est `static` dans les en-têtes : **un seul fichier `.c`** les inclut.
Construire : `pibuild app /home/projects/monjeu monjeu` (il lie tous les kits, AudioKit compris).

## 2. Le squelette

```c
enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER };
enum { ID_NEW = 1, ID_PAUSE, ID_SOUND, ID_QUIT };
static const char s_menus[] = "MPartie\nI1\tNouvelle partie\tCtrl+N\nI2\tPause\tP\nI3c\tSon\n-\nI4\tQuitter\n";
#define P_DIR  "HOME:/monjeu"
#define P_BEST "HOME:/monjeu/best.txt"

static long g_state, g_prev;
static unsigned long g_score, g_best;

static void new_game (void) { /* remettre le monde à zéro */ g_score = 0; g_state = ST_PLAY; }
static long game_pause (void) { if (g_state != ST_PLAY) return 0; g_prev = g_state; g_state = ST_PAUSE; return 1; }
static void game_resume (void) { if (g_state == ST_PAUSE) g_state = g_prev; }
static void on_pad (void) { if (gp_new & PAD_A) { /* tir, saut... */ } }	/* fronts : gp_new ; tenus : gp_now */

static void do_menu (long id)
{
	if (id == ID_QUIT) gf_quit = 1;
	else if (id == ID_NEW) new_game ();
	else if (id == ID_PAUSE) gp_open ();
	else if (id == ID_SOUND) ui_menu_state (ID_SOUND, gs_toggle () ? UMS_CHECKED : 0);
}

static void on_event (long t)
{
	if (gp_event (t)) return;			/* le menu de pause d'abord (Échap, Pause, Start, bouton ||) */
	if (t == WEV_PAD) on_pad ();
	else if (t == WEV_MENU) do_menu (GF_EV (EV_CODE));
	else if (t == WEV_KEY && GF_EV (EV_KIND))	/* un appui (0 : relâche) */
	{
		long id = ui_menu_key (0);
		if (id >= 0) { do_menu (id); return; }
		if (g_state == ST_TITLE || g_state == ST_OVER) new_game ();
	}
	else if (t == WEV_MOUSE) { long x, y; gf_mouse (&x, &y); /* x, y dans l'image LOGIQUE ; doigt = souris */ }
}

static void step (void)				/* 60 fois par seconde, exactement : pas de dt à gérer */
{
	if (g_state != ST_PLAY) return;
	if (gf_held[KEY_LEFT]) { /* ... */ }	/* touches tenues (codes HID), vidées à la perte du focus */
	/* déplacer, collisions, score ; fin : */
	/* if (perdu) { if (g_score > g_best) gf_save_best (P_DIR, P_BEST, g_best = g_score); g_state = ST_OVER; } */
}

static void draw (void)				/* toute l'image dans gf_frame (GF_W x GF_H), à chaque fois */
{
	gf_copy (gf_frame, gf_layer, GF_PITCH * GF_H);	/* le décor (couche fixe), puis ce qui bouge : */
	gf_rect (10, 10, 20, 20, 0x00FFCC00);
	/* gf_sprite (x, y, sprite) ; gf_glow (lumière additive) ; ga_parts_draw () ... */
}

int main (void)
{
	if (gf_open ("Mon jeu") < 0) return 0;	/* fenêtre plein écran sans cadre, au-dessus de tout */
	ui_menu ((long) s_menus);
	gs_open ();					/* le son (silencieux s'il n'y en a pas : le jeu tourne quand même) */
	g_best = gf_load_best (P_BEST);
	if (!ga_init (256 * 1024)) return 0;		/* la réserve des sprites générés (octets) */
	gp_init (game_pause, game_resume, new_game, GF_W - 32, 0, on_pad);	/* bouton || en haut à droite */
	ui_canvas_begin (gf_cv);			/* la couche fixe : décor et textes avec UIKit */
	ui_fill (0, 0, GF_W, GF_H, 0x00101828);
	gf_text_c ("Mon jeu", GF_W / 2, 200, 0x00FFFFFF, 32, ui.font_b);
	ui_canvas_end ();
	gf_run (on_event, step, draw);			/* rend la main à la fin (Quitter, fermeture) */
	gs_close ();
	return 0;
}
```

## 3. Ce que donnent les en-têtes

| En-tête | L'essentiel |
|---|---|
| `gamefb.h` (`gf_*`) | `gf_open`, `gf_run (event, step, draw)` (60 pas/s, rattrape 5 pas au plus, `gf_still = 1` : plus d'images, `gf_redraw = 1` : une seule) ; `gf_frame` (l'image), `gf_layer` + `gf_cv` (couche fixe, un canevas UIKit) ; `gf_dst` (où dessinent `gf_rect`, `gf_sprite` : `gf_frame` ou `gf_layer`) ; `gf_rect`, `gf_sprite`, `gf_glow`, `gf_bglow` ; `gf_text`, `gf_text_c` (dans la couche, entre `ui_canvas_begin`/`_end`) ; `gf_held[]` ; `gf_mouse` ; `gf_rand`, `gf_below (n)` (hasard 0..n-1) ; `gf_load_best`, `gf_save_best` ; `gf_tr` ; `gf_quit` |
| `gamedraw.h` (`ga_*`) | sprites générés au démarrage, à couche alpha : `ga_block` (brique en relief), `ga_ball` (sphère), `ga_disc`, `ga_panel` (panneau de verre), `ga_text` (texte pré-rendu, dessiné sans UIKit à chaque image), `ga_digits` + `ga_num` (score) ; `ga_ablit` (avec alpha), `ga_shadow` ; `ga_mix`, `ga_shade` ; particules `ga_burst`, `ga_parts_step`, `ga_parts_draw`, `ga_parts_clear` ; bruit `ga_fbm` |
| `gamepause.h` (`gp_*`) | le menu Reprendre / Recommencer / Quitter, le bouton ‖, la perte du focus qui met en pause ; `gp_event` en tête de `on_event` ; `gp_now`, `gp_new` (boutons `PAD_*` tenus, nouveaux) ; les lettres a, b, x, y du clavier font A, B, X, Y |
| `gamesound.h` (`gs_*`) | `gs_note (hz, ms, onde, volume)` (ondes `AKW_SINE`, `AKW_SQUARE`, `AKW_TRIANGLE`, `AKW_SAW`, `AKW_NOISE`...), `gs_later (délai, ...)`, `gs_seq (table)` pour un petit air ; `gs_toggle` (entrée Son du menu) |
| `gamepad.h` | `PAD_A`, `PAD_B`, `PAD_X`, `PAD_Y` (par leur place, manette type Xbox), `PAD_START`, `PAD_UP`... |

Lire la tête de chaque en-tête (commentaire) et les exemples : Arkanoid montre la couche fixe, les sprites générés,
les particules, les capsules ; Tetris une grille, la réserve, le fantôme, les lignes animées ; Invaders les vagues
d'ennemis et les tirs.

## 4. Concevoir le jeu

* **Des états** (`ST_TITLE`, `ST_READY`, `ST_PLAY`, `ST_PAUSE`, `ST_LOST`, `ST_CLEAR`, `ST_OVER`) et un
  `set_state` ; `step` et `draw` font un `switch` dessus. Les transitions temporisées (vie perdue, niveau fini) :
  un compteur de pas (`if (++g_timer >= 110) next_level ();`), pas d'attente bloquante.
* **Des tableaux fixes** d'objets avec un drapeau `on` (balles, tirs, ennemis, particules) : rien d'alloué pendant
  la partie. Ajouter = chercher une case libre ; trop d'objets = on n'en ajoute pas.
* **Virgule fixe 16.16** pour positions et vitesses (`x` en `long`, `x >> 16` pour dessiner) : des mouvements fins
  sans flottants, et les mêmes résultats partout. Le pas étant fixe (1/60 s), une vitesse est « par pas ».
* **Collisions** : rectangles (AABB) pour presque tout ; une balle contre une brique : tester le point avancé sur
  chaque axe séparément (inverser `vx` ou `vy` selon le côté touché) ; une grille (Tetris, briques) : convertir la
  position en case (`(x - gauche) / largeur_case`). Une balle rapide : la faire avancer en plusieurs sous-pas.
* **Niveaux en tables** dans le source (`static const char *levels[] = { "XXXX..", ... }`) ou générés ; tout le
  contenu peut être généré au démarrage (sprites `ga_*`, décors au bruit) : aucun fichier à livrer.
* **Le hasard** : `gf_rand` / `gf_below` (graine prise à l'heure par `gf_open`).
* **La difficulté** monte avec le niveau (vitesse, nombre d'ennemis), le score donne des vies, un record est gardé
  dans `HOME:/<jeu>/best.txt` (`gf_save_best` crée le dossier).
* **Le retour** : un son court à chaque événement (rebond, tir, explosion, ligne), quelques particules, un léger
  tremblement de l'image pour un gros coup : c'est ce qui rend un jeu vivant.
* **Les commandes** : clavier (flèches, Espace, Entrée ; jamais une lettre seule pour une action de jeu, sauf
  P = pause), souris et **doigt** (le mode poche, 800 × 600 tactile, sans clavier : prévoir que tout se joue au
  doigt), manette (`on_pad`). L'aide de `app.txt` dit toutes les touches.
* **L'écran titre** (le nom, le record, « Espace ou appui pour jouer ») et l'écran de fin (score, record) dans la
  couche fixe ; un message temporaire par-dessus l'image.
* Les textes affichés : en français dans le source, traduits par `gf_tr` (ou `ui_tr`) avec `lang/en.txt`
  (`texte français = English text`).

## 5. Et l'assembleur ?

Les jeux existent aussi en assembleur FASMARM (`tetris.asm`, `invaders.asm`, `arkanoid.asm` dans l'arbre de Korin),
sur les mêmes calculs (`gamefb.inc`, `gameart.inc`, `gamepause.inc`, `gamesnd.inc`, que ces en-têtes C traduisent) :
c'est l'**ancienne** version, **pour le Pi seulement** (AArch64), construite sur un PC Windows avec le dépôt, pas
avec `pibuild`. Un jeu neuf s'écrit en **C** (un seul source pour le Pi et le PC). Si l'utilisateur veut de
l'assembleur, voir la compétence `korin-lang-asm` (Claude) et ne mettre en assembleur qu'une routine chaude appelée
depuis le C.

## 6. Vérifier avant de rendre

* `pibuild` réussi, puis lancer détaché : `(/apps/<nom>.app/main >/dev/null 2>&1 &)`, `screenshot /tmp/s.bmp`.
* Le jeu est en **plein écran au-dessus de tout** : Échap ouvre le menu de pause, Quitter le ferme ; sans clavier,
  le bouton ‖ en haut à droite.
* Image noire : rien dessiné dans `gf_frame` (oublier `gf_copy` de la couche), ou dessin dans la couche fait hors de
  `ui_canvas_begin`/`_end` pour du texte UIKit. Pas de son : normal sous QEMU sans sortie son (le jeu tourne).
