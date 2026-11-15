---
name: korin-kit-glkit
description: GlKit, OpenGL ES 3.0 sur le GPU du Pi 4 — Mesa (pilote v3d de Gallium) et son frontal armgl en kit partagé, appelés par libglkit.a (fonctions gl*, armgl_*, armegl_*), shaders GLSL ES, rendu direct dans une fenêtre UIKit. À charger pour tout programme C ou C++ qui veut du GLSL ou de l'OpenGL ES, ou pour porter du code GL.
---

# GlKit : OpenGL ES 3.0 par Mesa

`/lib/glkit.so` (15 Mo de code partagé, 549 Kio de variables par process) : Mesa 26.2, armgl et zlib. Le
programme **ne l'ouvre pas lui-même** : il lie `libglkit.a`, un relais par fonction exportée (1323 : `gl*`,
`armgl_*`, `armegl_*`) ; le premier appel charge le kit (`glkit_load`) et remplit les relais. Kit absent ou
incompatible : message et fin du programme (code 127). Sans GLSL, GpuKit suffit (skill `korin-kit-gpukit`).

## Où lire

| Quoi | Sur la carte | Dans le dépôt |
|---|---|---|
| Fonctionnement de GlKit, mesures | — | `docs/MESA.md` (§ « GlKit : Mesa partagée », § 3 et 5) |
| Table du kit (4 fonctions internes) | `/src/kits/glkit/glkit.abi` | `src/kits/glkit/glkit.abi`, `tools/mesa/glkit/glkit.S` |
| armgl (contexte, surfaces, fenêtres) | — | `tools/mesa/armgl/armgl.h` |
| En-têtes GLES 3 / EGL / KHR | — | `prebuilt/mesa/include/` (`GLES3/gl3.h`…) |
| Ce que le programme lie | — | `prebuilt/mesa/lib/libglkit.a` |
| Profondeur, SDL, blocages | — | `docs/SDL.md` (« Le blocage du GPU »), `docs/V3D.md` |

Il n'y a pas de guide `glkit_fr.md` dans l'aide, et **ni `armgl.h`, ni les en-têtes GLES, ni `libglkit.a` ne sont
dans `/devtools/gcc` sur la carte** : un programme GlKit se construit sur le PC, dans WSL, par la chaîne croisée
`/opt/toolchains/aarch64-onyx-elf-14.2` (`tools/mesa/env.sh`).

Usages réels : `tools/mesa/armgl/gltri.c` (fenêtre UIKit, triangle tournant), `src/apps/gcemu/main.cpp`
(`armgl_window` + `emu_gl_hook`), `src/apps/n64emu/gl/n64gl_armpi.c` (`armgl_buffers` pour EmuKit),
`src/apps/n3dsemu/n3ds/n3ds_gl.cpp`. Scripts : `tools/emu/build_gcemu.sh`, `tools/emu/build-n64.sh`.

## Ouvrir le kit

Rien à ouvrir : `#include "armgl.h"` (et `<GLES3/gl3.h>` si l'on appelle les `gl*` par leur nom), puis lier
`libglkit.a`. Réservé aux **programmes liés à libposix** (le kit prend 170 symboles de leur libc, libstdc++, libgcc) :
C, ou C++ sans exception à travers le kit. Pas d'appel depuis l'assembleur : n64emu (asm) passe par un `main` en C
qui lui donne une table de fonctions. Construction (d'après `tools/emu/build_gcemu.sh`), dans WSL :

```sh
TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-   # specs : build/posix/lib/posix.specs
${TC}gcc -specs=posix.specs -mcpu=cortex-a72 -O2 -Itools/mesa/armgl -Iprebuilt/mesa/include \
	-Ibuild/gcc/sdroot/gcc/include/kits -c main.c -o main.o
${TC}g++ -specs=posix.specs -Wl,--gc-sections main.o -Lbuild/gcc/sdroot/gcc/lib -luikit \
	prebuilt/mesa/lib/libglkit.a -o main
```

## L'essentiel (armgl.h)

- `armgl_init(flags)` → `ARMGL_NONE` 0 (pas de GPU : rester sur le processeur), `ARMGL_GPU` 1, `ARMGL_DRY` 2 ;
  flags `ARMGL_F_DRY` (à blanc en mémoire ordinaire), `ARMGL_F_DRY_BUF` (à blanc sur les vrais tampons sous 1 Go)
- `armgl_context(major, minor, share)` → contexte ES (3, 0) ; `armgl_make_current(ctx, surf)` → non nul si réussi
- `armgl_surface(l, h)` → surface hors écran RGBA8 avec profondeur et stencil (tampon par défaut de GL)
- `armgl_window(ui.win, x, y, l, h)` → surface dans une fenêtre UIKit : le GPU y rend, Elegant compose sans recopie ;
  `armgl_window_zone(surf, win, x, y, l, h)` (après un redimensionnement) ; `armgl_window_free(surf)` (pas courante)
- `armgl_swap(surf)` → 0 : attend le GPU et montre l'image ; non nul : image refusée. Tout redessiner ensuite.
- `armgl_buffers(px[], n, octets, l, h, pas)`, `armgl_buffer_bind(surf, i)`, `armgl_buffer_done(surf)` : rendre
  dans ses propres images en mémoire basse (EmuKit)
- `armgl_proc("glXxx")` → adresse d'une fonction GL ; `armgl_finish()` ; `armgl_read(surf, px, pas)` (relit en
  `0xAARRGGBB`, ligne du haut d'abord)
- `armgl_lost()` → 1 : un job refusé ou inachevé, repasser au processeur ; `armgl_jobs()` ; `armgl_times(surf, &f, &c, &a)`
- `armgl_import(va, phys, octets)` + `armgl_image(nom, l, h, pas)` : rendre dans une texture faite ailleurs
  (`gk_texture_info` de GpuKit) ; `armegl_*` : la couche EGL (Skia, SDL, Jet)

## Exemple

Une fenêtre dont le fond change de couleur, rendu par le GPU (adapté de `tools/mesa/armgl/gltri.c`) :

```c
#include <math.h>
#include <stdio.h>
#include <uikit.h>
#include "armgl.h"

#define GL_COLOR_BUFFER_BIT 0x4000
static struct ui_vars ui;

int main (void)
{
	if (armgl_init (0) == ARMGL_NONE) { printf ("pas de GPU\n"); return 0; }
	if (!uikit_open (&ui) || ui_window (320, 240, (long) "glkit") <= 0) return 1;
	void *ctx = armgl_context (3, 0, NULL);
	void *surf = armgl_window (ui.win, 0, 0, 320, 240);
	if (!ctx || !surf || !armgl_make_current (ctx, surf)) return 1;
	void (*glClearColor) (float, float, float, float) = armgl_proc ("glClearColor");
	void (*glClear) (unsigned) = armgl_proc ("glClear");
	for (int n = 0; ui_event (0) != WEV_CLOSE; n++)
	{
		glClearColor (0.5f + 0.5f * sinf (n / 30.0f), 0.2f, 0.4f, 1.0f);
		glClear (GL_COLOR_BUFFER_BIT);
		if (armgl_swap (surf) || armgl_lost ()) { printf ("GPU perdu\n"); break; }
	}
	return 0;
}
```

## Pièges

- **Pas de GPU sous QEMU** : `armgl_init(0)` rend `ARMGL_NONE`. Prévoir son propre rendu (souvent `gk_draw` de
  GpuKit, ou le logiciel) ; pour essayer Mesa et le compilateur GLSL sous QEMU : `ARMGL_F_DRY` (image noire).
- **Contexte par thread** : un thread neuf a sa table de distribution à 0 jusqu'à `armgl_make_current` ; appeler GL
  sans contexte courant **plante** au lieu de ne rien faire. Exception : les threads d'`emu_thread` partagent le TLS
  du thread principal (n64emu, gcemu).
- **Premier lancement lent** : lire les 16 Mo du kit sur la carte (~10 s mesurées sous QEMU), puis ~9 ms.
- Pas d'exception C++ à travers le kit (`.eh_frame` retiré). Lier avec `g++` (le kit importe de libstdc++).
- Changer la fenêtre de place ou de taille : `armgl_window_zone`, ou libérer et recréer la surface après avoir rendu
  une autre surface courante (`gcemu`, `gl_hook`).
- Sans MMU GPU, les tampons sont contigus sous 1 Go : la mémoire GPU est rare, ne pas compter sur les 8 Go du Pi.
- Le service `v3d` se relève seul d'un job expiré, mais éteint le GPU après plus de 3 blocages de suite (`HANG_MAX`) :
  redémarrer. `gpuinfo` donne l'état ; `journal` puis `grep v3d /journal.txt` les blocages.
- Après un changement de Mesa ou d'armgl (`tools/mesa/build-armgl.sh`), committer `prebuilt/mesa` et
  `prebuilt/glkit.so` ; les apps n'ont pas à être reconstruites pour une correction dans le kit.
- Couleurs de l'interface autour du rendu GL : jetons du thème (`ui_tokens()`), jamais en dur.
