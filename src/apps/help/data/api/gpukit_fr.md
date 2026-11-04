# GpuKit : le GPU

**GpuKit** (`SD0:/lib/gpukit.so`) donne aux programmes le GPU V3D 4.2 du Raspberry Pi 4 : remplissages et copies de rectangles, textures, et rendu de triangles (couleurs, textures, profondeur, mélanges). Le service `v3d` possède le GPU et lance les travaux l'un après l'autre ; GpuKit prépare tout le reste dans le process de l'application.

**Il n'y a pas toujours de GPU** : sous QEMU, avec l'option `no_gpu`, ou après un blocage (le service l'éteint jusqu'au redémarrage). GpuKit est fait pour ça :

| Fonctions | Sans GPU |
|---|---|
| `gk_fill`, `gk_copy`, `gk_draw` | le processeur fait le travail ; le résultat dit qui l'a fait (1 GPU, 0 processeur) |
| `gk_texture`, `gk_texture_rect`, `gk_render` | `GK_E_NOGPU` (-1) : à l'appelant de faire autrement (souvent : `gk_draw`) |
| `gk_demo_triangle` | 0 : rien n'est dessiné |

Plus haut niveau, deux autres chemins mènent au même GPU : **Viewport3D** pour les applications .NET, et **OpenGL ES 3.0 par Mesa** (voir [plus bas](#opengl-es-et-viewport3d)).

## Ouvrir le kit

```c
#include <gpukit_abi.h>

if (!gpukit_kit_open ()) return 1;     // SD0:/lib/gpukit.so absent
int gpu = gk_info (0) == 1;            // 1 : le GPU sert ; 0 : tout ira au processeur
```

Rien à ajouter à l'édition de liens : `pibuild` prend toutes les bibliothèques de `/gcc/lib`.

## Savoir s'il y a un GPU

| Fonction | Arguments | Résultat |
|---|---|---|
| `gk_info` | tampon de `GKI_SIZE` (64) octets, ou 0 | 1 : GPU ; 0 : processeur |

Le tampon se lit comme un tableau de `long` (indice = décalage / 8) :

| Décalage | Contenu |
|---|---|
| `GKI_GPU` | 1 : le GPU sert ; 0 : le processeur |
| `GKI_STATE` | état du service : 1 prêt, 0 absent (QEMU, `no_gpu`), -1 éteint après un blocage |
| `GKI_VERSION` | 42 : V3D 4.2 |
| `GKI_REV` | révision du GPU |
| `GKI_JOBS` | travaux faits par le GPU depuis le démarrage (tout le système) |
| `GKI_BUSY_US` | temps GPU cumulé (µs) |
| `GKI_LAST_US` | durée du dernier travail de ce programme (µs) |

`gk_info` dit ce qu'il en est **maintenant**. Le GPU peut s'arrêter en cours de route : un rendu rend alors `GK_E_LOST` (-3), et tout passe au processeur jusqu'au redémarrage. Un programme bien fait vérifie le résultat de chaque appel plutôt que de se fier à un seul `gk_info` au départ.

## Mémoire et rectangles

Le GPU n'a pas de MMU : il n'écrit que dans des **tampons partagés du premier Go**. `gk_alloc` en donne un. Une cible ailleurs (les pixels d'une fenêtre, un `malloc`) marche aussi : GpuKit passe alors par un tampon d'étape à lui, au prix d'une recopie.

| Fonction | Arguments | Résultat |
|---|---|---|
| `gk_alloc` | octets (64 Mo au plus) | adresse d'un tampon que le GPU atteint, mis à zéro ; 0 : pas de mémoire |
| `gk_free` | adresse rendue par `gk_alloc` | 0 |
| `gk_fill` | pixels, pas (octets), x, y, largeur, hauteur, couleur `0x00RRGGBB` | 1 GPU, 0 processeur |
| `gk_copy` | source, pas, destination, pas, largeur, hauteur | 1 GPU, 0 processeur |
| `gk_demo_triangle` | pixels, pas, largeur, hauteur | 1 : un triangle dégradé rouge, vert, bleu sur fond bleu nuit ; 0 : pas de GPU |

Un travail fait au plus 2048 × 2048 pixels ; au-delà, le processeur prend la main. Les pixels sont `0x00RRGGBB` : l'octet haut vaut 0 après le GPU.

## Textures et rendu

`gk_render` dessine des **triangles** dans une cible, par lots : chaque lot a sa texture, son état (profondeur, faces, mélange, filtrage) et sa matrice 4 × 4. Ce sont les structures et les codes de `gpu_render` d'Onyx.

| Fonction | Arguments | Résultat |
|---|---|---|
| `gk_texture` | texture (< 0 : nouvelle), pixels `0xAARRGGBB` (0 avec une texture existante : la libère), largeur, hauteur (1..2048), pas (**pixels**) | numéro de texture (>= 0), 0 si libérée, `GK_E_*` |
| `gk_texture_rect` | texture, x, y, largeur, hauteur, pixels, pas (pixels) | 0 : ce rectangle remplacé, `GK_E_*` |
| `gk_texture_info` | texture, sortie de 48 octets | 0 : adresse physique, octets, rangement, largeur, hauteur, adresse ; pour rendre dans la texture avec Mesa |
| `gk_render` | cible, sommets, nombre, lots, nombre | 0 : les pixels sont dans la cible au retour ; `GK_E_*` |
| `gk_draw` | cible, sommets, nombre, lots, nombre, table de textures (ou 0) | 1 GPU, 0 processeur, `GK_E_*` |
| `gk_debug` | `GK_DBG_*`, argument, argument | 0, `GK_E_*` ; pour les essais |

Erreurs : `GK_E_NOGPU` (-1) pas de GPU, `GK_E_INVAL` (-2) mauvais arguments ou sommet hors de la bande de garde, `GK_E_LOST` (-3) le GPU s'est arrêté (textures perdues), `GK_E_NOMEM` (-4) plus de mémoire ou de texture libre. Limites : 512 textures par programme, 1024 lots et 196 608 sommets par rendu.

**`gk_render` ou `gk_draw` ?** `gk_render` n'est que le GPU : les sommets, une fois multipliés par la matrice, doivent rester dans la bande de garde (w > 0, |x| et |y| au plus 4 w, z au moins -w), sinon l'image est refusée. `gk_draw` fait le reste sur le processeur : il transforme, rejette ce qui sort de l'image, **découpe** contre le plan proche, puis donne l'image à `gk_render` ; s'il n'y a pas de GPU, ou si une texture n'y est pas, il dessine avec son **rastériseur** (mélanges 0 à 4 seulement). Pour un programme qui veut simplement de la 3D, `gk_draw` suffit.

### Les structures

Elles ne sont pas dans l'en-tête : on les déclare soi-même, et on vérifie leur taille avec les constantes du kit.

```c
struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };	// GKV_SIZE = 32
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };	// GKB_SIZE = 80
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };	// GKF_SIZE = 32
struct gk_tex { unsigned *px; int w, h, id, pad; };				// GKT_SIZE = 24
```

| Champ | Sens |
|---|---|
| sommet `x y z w` | position, multipliée par la matrice du lot (espace de découpe, comme OpenGL) |
| sommet `s t` | coordonnées de texture, 0..1 |
| sommet `rgba` | couleur, multipliée par le texel (octets r, g, b, a) |
| sommet `rgba2` | couleur ajoutée (0 : aucune) |
| lot `first`, `count` | les sommets du lot : `count` / 3 triangles |
| lot `tex` | `gk_render` : numéro de `gk_texture`, -1 : aucune ; `gk_draw` : indice dans sa table de `gk_tex`, -1 : aucune |
| lot `flags` | `GK_B_*` ci-dessous |
| lot `m` | matrice 4 × 4, ligne par ligne : position = m × (x y z w) |
| cible `px`, `w`, `h` | pixels `0x00RRGGBB`, taille |
| cible `stride` | pas en **pixels** |
| cible `clear` | couleur du fond, `0xRRGGBB` |
| cible `flags` | `GK_F_KEEP` : dessiner par-dessus (sinon fond effacé) ; `GK_F_ALPHA` : cible `0xAARRGGBB` prémultipliée (GPU seulement) |
| table `px`, `w`, `h` | les pixels de la texture (pour le processeur) |
| table `id` | son numéro `gk_texture` (pour le GPU), < 0 : pas sur le GPU, l'image va au processeur |

Drapeaux d'un lot (`flags`) :

| Drapeau | Effet |
|---|---|
| `GK_Z_LESS` (0), `GK_Z_EQUAL`, `GK_Z_LEQUAL`, `GK_Z_GREATER`, `GK_Z_NOTEQUAL`, `GK_Z_GEQUAL`, `GK_Z_ALWAYS` (7) | test de profondeur (bits 0 à 2, masque `GK_B_ZFUNC`) |
| `GK_B_NOZWRITE` | ne pas écrire la profondeur |
| `GK_B_CULL_BACK`, `GK_B_CULL_FRONT` | ne pas dessiner les faces arrière, avant |
| mode << `GK_B_BLEND_SHIFT` | mélange : `GK_BLEND_NONE` 0, `ALPHA` 1, `ADD` 2, `MUL` 3, `PREMUL` 4, et pour le GPU seul `MULCOL` 5 à `DSTOUT` 12 |
| `GK_B_LINEAR` | filtrage bilinéaire (sinon le plus proche) |
| mode << `GK_B_WRAP_S_SHIFT`, << `GK_B_WRAP_T_SHIFT` | `GK_WRAP_REPEAT` 0, `GK_WRAP_CLAMP` 1, `GK_WRAP_MIRROR` 2 |
| `GK_B_NOMATRIX` | matrice identité : sommets déjà en espace de découpe (`gk_render`) |
| `GK_B_ALPHATEST` + seuil << `GK_B_AT_SHIFT` | un pixel dont l'alpha est sous le seuil (0..255) n'est pas dessiné |

### Essais sans GPU

`gk_debug (GK_DBG_DRY, 1, 0)` fait construire les listes de commandes de `gk_render` sans rien envoyer au GPU : sous QEMU, cela vérifie qu'une image serait acceptée (structures, bande de garde, textures). `GK_DBG_LISTS` (sortie de `GKD_SIZE` octets) rend les listes du dernier travail, `GK_DBG_TEX` (texture, sortie) les texels d'une texture. Ce sont des outils de mise au point, pas de rendu.

## Exemple : ce que donne le GPU

En mode texte : l'état du GPU, puis un remplissage, une copie, le triangle de démonstration et une texture, chacun disant qui l'a fait. Sous QEMU, tout passe par le processeur et les textures rendent `GK_E_NOGPU`.

```c
#include <stdio.h>
#include <gpukit_abi.h>

int main (void)
{
	if (!gpukit_kit_open ()) { printf ("GpuKit absent\n"); return 1; }

	long info[GKI_SIZE / 8];
	long gpu = gk_info ((long) info);
	long state = info[GKI_STATE / 8];
	printf ("rendu : %s ; service v3d : %s\n", gpu ? "GPU" : "processeur",
		state > 0 ? "prêt" : state < 0 ? "éteint après un blocage" : "absent");
	if (gpu)
		printf ("V3D %ld.%ld, révision %ld, %ld travaux, dernier : %ld µs\n", info[GKI_VERSION / 8] / 10,
			info[GKI_VERSION / 8] % 10, info[GKI_REV / 8], info[GKI_JOBS / 8], info[GKI_LAST_US / 8]);

	enum { W = 320, H = 200 };
	unsigned *a = (unsigned *) gk_alloc (W * H * 4 * 2);	// deux images dans le premier Go
	if (!a) { printf ("pas de mémoire\n"); return 1; }
	unsigned *b = a + W * H;
	long r1 = gk_fill ((long) a, W * 4, 0, 0, W, H, 0x203040);		// pas en octets
	long r2 = gk_fill ((long) a, W * 4, 40, 30, 100, 60, 0xE0A020);
	long r3 = gk_copy ((long) a, W * 4, (long) b, W * 4, W, H);
	printf ("remplissages : %s, %s ; copie : %s\n", r1 ? "GPU" : "processeur", r2 ? "GPU" : "processeur", r3 ? "GPU" : "processeur");
	printf ("copie : pixel (50, 40) = %06x, pixel (0, 0) = %06x\n", b[40 * W + 50] & 0xFFFFFF, b[0] & 0xFFFFFF);

	if (gk_demo_triangle ((long) b, W * 4, W, H))
		printf ("triangle : centre = %06x\n", b[(H / 2) * W + W / 2] & 0xFFFFFF);
	else
		printf ("triangle : pas de GPU, rien de dessiné\n");

	long t = gk_texture (-1, (long) a, 64, 64, W);		// pas en pixels
	if (t == GK_E_NOGPU) printf ("texture : pas de GPU (à faire par le processeur, ou par gk_draw)\n");
	else if (t < 0) printf ("texture : erreur %ld\n", t);
	else { printf ("texture %ld créée\n", t); gk_texture (t, 0, 0, 0, 0); }	// pixels 0 : libérée

	gk_free ((long) a);
	return 0;
}
```

## Exemple : un cube qui tourne

Une fenêtre redimensionnable : `gk_draw` dessine directement dans les pixels de la fenêtre, par le GPU s'il est là, sinon par le processeur. Le fond prend la couleur du thème ; la bande du haut dit qui dessine.

```c
#include <math.h>
#include <string.h>
#include <time.h>
#include <uikit.h>
#include <gpukit_abi.h>

struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
_Static_assert (sizeof (struct gk_vertex) == GKV_SIZE && sizeof (struct gk_batch) == GKB_SIZE
		&& sizeof (struct gk_frame) == GKF_SIZE, "gpukit.inc");

enum { TOP = 40 };
static struct ui_vars ui;
static struct gk_vertex cube[36];
static int by_gpu = -1;

static unsigned tok (int off) { return *(const unsigned *) ((const char *) ui_jetons () + off) & 0xFFFFFF; }

// six faces de deux triangles, une couleur par face
static void build_cube (void)
{
	static const float c[8][3] = { {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} };
	static const int face[6][4] = { {4,5,6,7}, {1,0,3,2}, {5,1,2,6}, {0,4,7,3}, {7,6,2,3}, {0,1,5,4} };
	static const unsigned col[6] = { 0xE05050, 0x50C050, 0x5080E0, 0xE0C040, 0xC060D0, 0x40C0C0 };
	static const int corner[6] = { 0, 1, 2, 0, 2, 3 };
	int n = 0;
	for (int i = 0; i < 6; i++)
		for (int k = 0; k < 6; k++)
		{
			const float *p = c[face[i][corner[k]]];
			struct gk_vertex *q = &cube[n++];
			memset (q, 0, sizeof *q);
			q->x = p[0]; q->y = p[1]; q->z = p[2]; q->w = 1;
			q->rgba[0] = col[i] >> 16; q->rgba[1] = col[i] >> 8 & 255; q->rgba[2] = col[i] & 255; q->rgba[3] = 255;
		}
}

// m = a × b (matrices 4 × 4, ligne par ligne)
static void mul (float *m, const float *a, const float *b)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
		{
			float s = 0;
			for (int k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
			m[r * 4 + c] = s;
		}
}

// projection × recul × rotations
static void matrix (float *m, float t, float aspect)
{
	float cy = cosf (t), sy = sinf (t), cx = cosf (t * 0.7f), sx = sinf (t * 0.7f);
	float ry[16] = { cy, 0, sy, 0,  0, 1, 0, 0,  -sy, 0, cy, 0,  0, 0, 0, 1 };
	float rx[16] = { 1, 0, 0, 0,  0, cx, -sx, 0,  0, sx, cx, 0,  0, 0, 0, 1 };
	float back[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, -5,  0, 0, 0, 1 };
	float n = 1, f = 20, k = 1 / tanf (0.5f);
	float proj[16] = { k / aspect, 0, 0, 0,  0, k, 0, 0,  0, 0, (f + n) / (n - f), 2 * f * n / (n - f),  0, 0, -1, 0 };
	float a[16], b[16];
	mul (a, ry, rx);
	mul (b, back, a);
	mul (m, proj, b);
}

static void header (void)
{
	ui_fill (0, 0, ui.w, TOP, tok (TJ_FACE));
	const char *who = by_gpu < 0 ? "..." : by_gpu ? "GPU" : (const char *) ui_tr ((long) "Processeur");
	ui_text ((long) who, 12, 26, tok (TJ_TEXTE2), 13, (long) ui.font, 0);
	ui_update (0, 0, ui.w, TOP);
}

static long on_resize (long w, long h)
{
	ui_fill (0, 0, ui.w, ui.h, tok (TJ_FACE));
	header ();
	ui_update_all ();
	return 0;
}

static void draw (float t)
{
	long x = 8, y = TOP, w = ui.w - 16, h = ui.h - TOP - 8;
	if (w < 16 || h < 16) return;
	struct gk_batch b = { 0, 36, -1, GK_Z_LESS, { 0 } };
	matrix (b.m, t, (float) w / h);
	struct gk_frame f = { ui.surf + y * ui.w + x, (int) w, (int) h, (int) ui.w, tok (TJ_SURFACE), 0, 0 };
	long r = gk_draw ((long) &f, (long) cube, 36, (long) &b, 1, 0);
	if (r < 0) return;
	if (r != by_gpu) { by_gpu = (int) r; header (); }
	ui_update (x, y, w, h);
}

static double now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main (void)
{
	if (!uikit_open (&ui) || !gpukit_kit_open ()) return 1;
	build_cube ();
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	if (ui_window_ex ((scr[0] - 480) / 2, scr[3] + 40, 480, 400, (long) ui_tr ((long) "Cube"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (200, 160);
	ui_on_resize ((long) on_resize, 0);
	on_resize (ui.w, ui.h);

	double t0 = now ();
	for (;;)
	{
		long ev = ui_event (15);
		if (ev == WEV_CLOSE) break;
		draw ((float) (now () - t0));
	}
	return 0;
}
```

Les textes passés à `ui_tr` sont les clés françaises du dictionnaire du système (le `lang/en.txt` d'une app en donne la version anglaise), comme dans le [squelette](skeleton_fr.md).

## Exemple : une texture, avec ou sans GPU

`gk_render` ne marche qu'avec le GPU : le programme le tente, puis, sur `GK_E_NOGPU` ou `GK_E_LOST`, refait la même image par `gk_draw` en lui donnant les pixels de la texture. L'image est écrite dans `SD0:/home/quad.ppm`.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gpukit_abi.h>

struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
struct gk_tex { unsigned *px; int w, h, id, pad; };
_Static_assert (sizeof (struct gk_tex) == GKT_SIZE && sizeof (struct gk_frame) == GKF_SIZE, "gpukit.inc");

enum { W = 256, H = 256, T = 64 };

// un carré de deux triangles, texture entière, couleur blanche (le texel tel quel)
static void quad (struct gk_vertex *v)
{
	static const float p[6][4] = { {-0.8f,-0.8f,0,0}, {0.8f,-0.8f,1,0}, {0.8f,0.8f,1,1}, {-0.8f,-0.8f,0,0}, {0.8f,0.8f,1,1}, {-0.8f,0.8f,0,1} };
	memset (v, 0, 6 * sizeof *v);
	for (int i = 0; i < 6; i++)
	{
		v[i].x = p[i][0]; v[i].y = p[i][1]; v[i].w = 1;
		v[i].s = p[i][2]; v[i].t = p[i][3];
		memset (v[i].rgba, 255, 4);
	}
}

int main (void)
{
	if (!gpukit_kit_open ()) return 1;
	unsigned *px = (unsigned *) gk_alloc (W * H * 4);	// cible dans le premier Go : pas de recopie
	unsigned *tex = malloc (T * T * 4);
	if (!px || !tex) return 1;
	for (int y = 0; y < T; y++)
		for (int x = 0; x < T; x++) tex[y * T + x] = (x ^ y) & 8 ? 0xFFF0F0F0 : 0xFF3060C0;

	struct gk_vertex v[6];
	quad (v);
	struct gk_batch b = { 0, 6, -1, GK_Z_ALWAYS | GK_B_LINEAR, { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 } };
	struct gk_frame f = { px, W, H, W, 0x202020, 0, 0 };

	const char *how;
	long id = gk_texture (-1, (long) tex, T, T, T);
	long r = id;						// GK_E_NOGPU sans GPU
	if (id >= 0)
	{
		b.tex = (int) id;
		r = gk_render ((long) &f, (long) v, 6, (long) &b, 1);
	}
	if (r == 0) how = "GPU, par gk_render";
	else
	{
		// pas de GPU (GK_E_NOGPU), ou il s'est arrêté (GK_E_LOST) : gk_draw, qui sait faire sans
		struct gk_tex table = { tex, T, T, -1, 0 };	// id < 0 : la texture n'est pas sur le GPU
		b.tex = 0;					// indice dans la table
		r = gk_draw ((long) &f, (long) v, 6, (long) &b, 1, (long) &table);
		how = r == 1 ? "GPU, par gk_draw" : r == 0 ? "processeur, par gk_draw" : "erreur";
	}
	printf ("rendu : %s (%ld)\n", how, r);

	FILE *o = fopen ("SD0:/home/quad.ppm", "wb");
	if (o)
	{
		fprintf (o, "P6\n%d %d\n255\n", W, H);
		for (int i = 0; i < W * H; i++) { unsigned char rgb[3] = { px[i] >> 16, px[i] >> 8, px[i] }; fwrite (rgb, 1, 3, o); }
		fclose (o);
	}
	if (id >= 0) gk_texture (id, 0, 0, 0, 0);
	free (tex);
	gk_free ((long) px);
	return 0;
}
```

## OpenGL ES et Viewport3D

GpuKit a des shaders fixes (texel × couleur + couleur 2). Deux autres couches vont plus loin, sur le même service `v3d` :

- **OpenGL ES 3.0 par Mesa** (le pilote `v3d` de Gallium, porté dans Korin OS) : shaders en GLSL ES, plusieurs textures par dessin. Ce n'est pas un kit : Mesa et son petit frontal `armgl` se lient en statique dans le programme (une quinzaine de Mo), avec les scripts de `tools/mesa` sur le PC. Jet (WebGL), Skia, les émulateurs N64, GameCube et 3DS s'en servent. Sans GPU, `armgl_init` rend 0 : le programme doit alors prévoir son propre rendu, par exemple `gk_draw`. `gk_texture_info` donne l'adresse physique et le rangement d'une texture de GpuKit pour que Mesa rende dedans.
- **Viewport3D** pour les applications .NET (`System.3D.dll`, espace de noms `System.Windows.Media.Media3D`, les noms de WPF) : un contrôle à poser dans une `Form`, une caméra (`PerspectiveCamera`, `OrthographicCamera`), des modèles (`GeometryModel3D`, `MeshBuilder.Box`, `Sphere`, `Plane`), des matériaux et des lumières (`AmbientLight`, `DirectionalLight`, `PointLight`), une lumière calculée par sommet. Le rendu passe par `gk_render`, et par le processeur sans GPU. Une seule vue 3D affichée par fenêtre, rien par-dessus, 65 536 triangles par image.

## Pièges

- **Les pas** : en **octets** pour `gk_fill` et `gk_copy` (comme MediaKit) ; en **pixels** pour `gk_texture`, `gk_texture_rect` et le `stride` d'une cible.
- **QEMU n'a pas de GPU** : tout ce qui marche par le processeur y marche, mais `gk_texture` et `gk_render` rendent `GK_E_NOGPU`. Écrivez le chemin sans GPU dès le début ; essayez la liste de commandes par `GK_DBG_DRY`.
- **`GK_E_LOST`** : le GPU s'est arrêté et ne reviendra pas avant le redémarrage ; les textures sont perdues. Passez au processeur pour de bon (comme le fait `gk_draw`), ne réessayez pas à chaque image.
- **La bande de garde** de `gk_render` : un sommet extrême fait refuser l'image (`GK_E_INVAL`). Des coordonnées folles peuvent même bloquer le GPU. Laissez `gk_draw` découper, sauf si vos sommets restent sagement dans l'image.
- **Pas de recopie** : une cible dans un tampon de `gk_alloc` est écrite directement ; les pixels d'une fenêtre ou d'un `malloc` passent par un tampon d'étape (une copie avant avec `GK_F_KEEP`, une après).
- **Le Z ne survit pas** d'un `gk_render` à l'autre : une image se dessine en un appel.
- **`gk_draw` sans GPU** ne fait que les mélanges 0 à 4 et ignore `GK_F_ALPHA`.
- **Pixels** `0x00RRGGBB` dans la cible, `0xAARRGGBB` dans les textures.

## Voir aussi

- [UIKit](uikit_fr.md) : les fenêtres où l'on dessine.
- [MediaKit](mediakit_fr.md) : les images d'une vidéo.
- [EmuKit](emukit_fr.md) : les émulateurs, qui rendent par GpuKit ou Mesa.
- [Les kits](kit_fr.md) : comment un kit se charge et s'appelle depuis le C.
