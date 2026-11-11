---
name: korin-kit-gpukit
description: GpuKit, le GPU V3D 4.2 du Pi 4 côté application — remplissages et copies de rectangles, textures, triangles 3D (profondeur, faces, mélanges), avec repli automatique sur le processeur (fonctions gk_*). À charger pour tout programme qui dessine en 3D ou accélère des rectangles sans passer par Mesa.
---

# GpuKit : le GPU (et son repli processeur)

`SD0:/lib/gpukit.so`. Le service `v3d` possède le GPU et lance les travaux ; GpuKit prépare le reste dans le process.
Shaders fixes (texel × couleur + couleur 2). Pour du GLSL ES, voir la skill `korin-kit-glkit` (Mesa) ; en .NET,
Viewport3D (`System.3D.dll`, `docs/VIEWPORT3D.md`) rend par `gk_render`. Préfixe `gk_` (`gp_` est à GpioKit).

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Guide de l'API (3 exemples complets, pièges) | `SD0:/apps/help.app/api/gpukit_fr.md` | `src/apps/help/data/api/gpukit_fr.md` |
| Table du kit | — | `src/kits/gpukit/gpukit.abi`, constantes et structures `gpukit.inc` |
| En-tête C | `/gcc/include/kits/gpukit_abi.h` | généré par `tools/kits/abi2c.py` |
| Fonctionnement, mesures, `gk_draw` | — | `docs/V3D.md`, `docs/VIEWPORT3D.md` |

Usages réels : `src/apps/cosmozoom/main.cpp` (C++, `gk_draw` + textures), `src/bin/gputest/gputest.asm` et
`src/apps/n64emu/n64emu.asm` (asm). Commandes : `gputest` (essai complet, `gputest lists` à blanc), `gpuinfo` (état).

## Ouvrir le kit

C / C++ :

```c
#include <gpukit_abi.h>
if (!gpukit_kit_open ()) return 1;     /* SD0:/lib/gpukit.so absent */
int gpu = gk_info (0) == 1;            /* 1 : GPU ; 0 : tout ira au processeur */
```

`pibuild` lie déjà tout `/gcc/lib/lib*.a` ; à la main : `gcc -I/gcc/include/kits main.c -lgpukit`.

Assembleur : `include '../../kits/gpukit/gpukit.inc'`, `kit_use '../../kits/gpukit/gpukit.abi'` dans le segment de
code, `bl kits_open` au départ, `kit_data` dans les données. Convention d'appel ordinaire (x0-x7 arguments,
x19-x28 gardés). Les noms sont les mêmes dans tous les langages ; les skills `korin-lang-*` expliquent le mécanisme.

## L'essentiel

Tous les arguments et résultats sont des `long`. Pixels `0x00RRGGBB` dans une cible, `0xAARRGGBB` dans une texture.

État :
- `gk_info(buf64 | 0)` → 1 GPU, 0 processeur ; tampon de `long` lu à `[GKI_x / 8]` : `GKI_STATE` (1 prêt, 0 absent,
  -1 éteint après un blocage), `GKI_VERSION` (42), `GKI_REV`, `GKI_JOBS`, `GKI_BUSY_US`, `GKI_LAST_US`

Mémoire et rectangles (marchent toujours, résultat 1 GPU / 0 processeur) :
- `gk_alloc(octets)` → tampon du premier Go (le seul que le GPU atteint), à zéro, 64 Mo au plus ; 0 : pas de mémoire
- `gk_free(adr)` → 0
- `gk_fill(px, pas_octets, x, y, l, h, 0xRRGGBB)` ; `gk_copy(src, pas, dst, pas, l, h)`
- `gk_demo_triangle(px, pas, l, h)` → 1 dessiné, 0 pas de GPU

Textures et triangles :
- `gk_texture(tex<0 : nouvelle, px, l, h, pas_pixels)` → numéro >= 0 ; `gk_texture(t, 0, 0, 0, 0)` la libère ;
  sans GPU `GK_E_NOGPU`
- `gk_texture_rect(t, x, y, l, h, px, pas_pixels)` → 0 : rectangle remplacé
- `gk_texture_info(t, sortie48)` → physique, octets, rangement, l, h, adresse (pour que Mesa rende dedans)
- `gk_draw(cible, sommets, n, lots, nlots, table_gk_tex | 0)` → 1 GPU, 0 processeur, `GK_E_*` : transforme,
  rejette, découpe, puis GPU ou rastériseur processeur. **Le bon choix par défaut.**
- `gk_render(cible, sommets, n, lots, nlots)` → 0 ; GPU seul, sommets dans la bande de garde
  (w > 0, |x|,|y| ≤ 4w, z ≥ -w), sinon `GK_E_INVAL`
- `gk_debug(GK_DBG_DRY, 1, 0)` : listes construites sans rien envoyer (essai sous QEMU) ; `GK_DBG_LISTS`, `GK_DBG_TEX`

Structures (pas dans l'en-tête : à déclarer et vérifier par `_Static_assert` contre `GKV_SIZE` 32, `GKB_SIZE` 80,
`GKF_SIZE` 32, `GKT_SIZE` 24) :
`gk_vertex {float x,y,z,w,s,t; uchar rgba[4], rgba2[4]}`, `gk_batch {unsigned first,count; int tex; unsigned flags;
float m[16]}` (matrice ligne par ligne), `gk_frame {unsigned *px; int w,h,stride; unsigned clear,flags,pad}`
(stride en **pixels**), `gk_tex {unsigned *px; int w,h,id,pad}` (id < 0 : pas sur le GPU).

Drapeaux de lot : `GK_Z_LESS`…`GK_Z_ALWAYS` (masque `GK_B_ZFUNC`), `GK_B_NOZWRITE`, `GK_B_CULL_BACK`/`FRONT`,
`GK_BLEND_*` << `GK_B_BLEND_SHIFT` (NONE, ALPHA, ADD, MUL, PREMUL ; MULCOL à DSTOUT : GPU seul), `GK_B_LINEAR`,
`GK_WRAP_*` << `GK_B_WRAP_S_SHIFT`/`_T_SHIFT`, `GK_B_NOMATRIX`, `GK_B_ALPHATEST` + seuil << `GK_B_AT_SHIFT`.
Cible : `GK_F_KEEP` (dessiner par-dessus), `GK_F_ALPHA` (cible prémultipliée, GPU seul).
Erreurs : `GK_E_NOGPU` -1, `GK_E_INVAL` -2, `GK_E_LOST` -3, `GK_E_NOMEM` -4. Limites : 2048 × 2048 par travail,
512 textures par programme, 1024 lots et 196 608 sommets par rendu.

## Exemple

Un triangle par `gk_draw`, GPU ou processeur (adapté des exemples de `gpukit_fr.md`) :

```c
#include <stdio.h>
#include <string.h>
#include <gpukit_abi.h>

struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
_Static_assert (sizeof (struct gk_vertex) == GKV_SIZE && sizeof (struct gk_batch) == GKB_SIZE
		&& sizeof (struct gk_frame) == GKF_SIZE, "gpukit.inc");

int main (void)
{
	enum { W = 320, H = 240 };
	if (!gpukit_kit_open ()) return 1;
	unsigned *px = (unsigned *) gk_alloc (W * H * 4);	/* premier Go : pas de recopie */
	if (!px) return 1;
	static const float p[3][2] = { {0, 0.8f}, {-0.7f, -0.6f}, {0.7f, -0.6f} };
	struct gk_vertex v[3];
	memset (v, 0, sizeof v);
	for (int i = 0; i < 3; i++)
	{
		v[i].x = p[i][0]; v[i].y = p[i][1]; v[i].w = 1;
		v[i].rgba[i] = 255; v[i].rgba[3] = 255;		/* rouge, vert, bleu */
	}
	struct gk_batch b = { 0, 3, -1, GK_Z_ALWAYS, { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 } };
	struct gk_frame f = { px, W, H, W, 0x202020, 0, 0 };	/* stride en pixels */
	long r = gk_draw ((long) &f, (long) v, 3, (long) &b, 1, 0);
	printf ("rendu : %s, centre = %06x\n", r == 1 ? "GPU" : r == 0 ? "processeur" : "erreur",
		px[(H / 2) * W + W / 2] & 0xFFFFFF);
	gk_free ((long) px);
	return 0;
}
```

Dans une fenêtre UIKit : cible `ui.surf + y * ui.w + x`, stride `ui.w`, puis `ui_update` (exemple du cube dans le guide).

## Pièges

- **Pas de GPU sous QEMU** (ni avec `no_gpu` dans `options.txt`, ni après un blocage) : `gk_fill`, `gk_copy`,
  `gk_draw` passent au processeur ; `gk_texture`, `gk_render` rendent `GK_E_NOGPU`. Écrire le chemin sans GPU dès le
  début ; vérifier les listes par `GK_DBG_DRY`.
- **`GK_E_LOST`** : le GPU s'est arrêté jusqu'au redémarrage, textures perdues. Passer au processeur pour de bon, ne
  pas réessayer à chaque image. Vérifier le résultat de chaque appel, pas un seul `gk_info` au départ.
- **Les pas** : en octets pour `gk_fill`/`gk_copy` ; en pixels pour `gk_texture`, `gk_texture_rect`, `stride`.
- **Bande de garde** de `gk_render` : un sommet extrême fait refuser l'image, voire bloque le GPU. Laisser `gk_draw`
  découper.
- Une cible hors `gk_alloc` (fenêtre, `malloc`) passe par un tampon d'étape (recopie). Le Z ne survit pas d'un rendu
  à l'autre : une image = un appel.
- `gk_draw` sans GPU : mélanges 0 à 4 seulement, `GK_F_ALPHA` ignoré.
- Couleurs de fond d'une app : jetons du thème (`ui_jetons()`, `TJ_FACE`…), jamais en dur.
