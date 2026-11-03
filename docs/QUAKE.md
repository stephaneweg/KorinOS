# Quake

Quake d'id Software, en rendu logiciel, sans Mesa ni SDL : `src/apps/quake` (`quake.app`). Le moteur est le source
WinQuake publié par id sous GPL, dans `third_party/quake`, avec sa licence (`gnu.txt`) et la liste de ce qui y a changé
(`README.ARMPI`). `src/apps/quake` est sous la même licence. C'est un port tiers : la couche d'adaptation en C est permise.

## Ce qu'il y a

| Morceau | Où | Rôle |
|---|---|---|
| Moteur | `third_party/quake` | C d'id ; trois petites corrections, plus les `#if ARMPI_ASM` |
| Adaptation | `src/apps/quake/main.c`, `vid.c`, `snd.c`, `in.c` | fichiers, horloge, image 8 bits vers 32 bits, anneau du son, entrées |
| Routines A64 | `src/apps/quake/asm`, en **FASMARM** | 1 100 lignes (liste plus bas) |
| EmuKit | `emu_game`, `emu_pointer` | un jeu plutôt qu'une console : ni ROM ni dialogue, toutes les touches à l'application |
| Elegant, UIKit | `WS_OP_POINTER_LOCK`, `ui_pointer_lock` | souris relative |
| Outils | `tools/quake` | construction, données, carte d'essai, harnais, comparaison |

EmuKit donne la fenêtre, le thread de la machine, les trois vues (fenêtrée, maximisée, plein écran par F9, comme les
émulateurs ; F12 montre les images par seconde), le son vers AudioKit. L'image 8 bits est convertie à chaque image dans le
tampon d'EmuKit. La fenêtre suit le thème par EmuKit ; le contenu est celui du jeu.

Côté clavier : en jeu, une touche vaut sa **place** (sur un AZERTY, la touche Z fait le W de WASD, et la rangée du haut
donne les chiffres) ; dans la console et les menus, elle vaut son **caractère**, celui de HIDManager. F9 et F12 restent à la
vue, F10 et F11 à Elegant. La souris devient relative quand on joue vraiment (ni menu, ni console, ni démo) et si la fenêtre a
le clavier ; le curseur disparaît alors. Échap ouvre le menu du jeu et rend la souris.

La manette arrive par `WEV_PAD` : croix = WASD, A = saut, B = tir, Start = menu, stick droit = regard. La manette 0 d'HIDManager
est le clavier : elle est ignorée (`EMG_NOROM`), le jeu lit le clavier en direct. **Jamais essayée** : QEMU n'a pas de manette.

## Données

Rien de lourd dans le dépôt. Le dossier est `SD0:/quake/id1` : `pak0.pak`, `pak1.pak`, puis `config.cfg` et les sauvegardes, que
le jeu y écrit lui-même. `SD0:/quake/args.txt` peut donner des paramètres de Quake sur une ligne (`+map start`, `-mem`…).
Sans `pak0.pak`, l'application le dit et s'arrête.

```bash
sh tools/quake/fetch_data.sh
```

Ce script prend **LibreQuake** v0.09-beta (version `lite`, 56 Mo ; `full` : 116 Mo) dans `build/quake/sdroot/quake/id1`, et
`build.ps1` le copie sur la carte. Licence vérifiée (`docs/README-IMPORTANT-LICENCE-INFO` de LibreQuake) : BSD à trois clauses
pour les modèles, textures et sons, GPL 2 pour `progs.dat`, le QuakeC et `pop.lmp`. Les textes suivent les données
(`id1/docs`), comme la BSD le demande. Le moteur d'origine lit LibreQuake tel quel : cartes BSP 29, `progs.dat` version 6, CRC 5927,
et `pop.lmp` lui fait croire à la version enregistrée.

Le `pak0.pak` d'origine se lit de la même façon, le code n'y fait aucune différence : le copier dans `SD0:/quake/id1` à la place de
ceux-ci. **Pas essayé** : il n'est pas sur cette machine.

## Construction

```bash
sh tools/quake/build.sh
```

Dans WSL, depuis la racine du dépôt, après un `build.ps1` qui a posé les kits (GCC et POSIX : l'arbre de la copie principale, comme
`tools/emu/build.sh`). Résultat dans `build/quake/sdroot/apps/quake.app`, que `build.ps1` met sur la carte. `ASM=0` construit le C
seul. Compilé en `-O3 -mcpu=cortex-a72 -fno-strict-aliasing -fwrapv -ffp-contract=off -fcommon`, le C d'id en `gnu89`.

Les routines A64 sont en **FASMARM** : `format ELF64` donne un vrai objet, lié par `ld` avec le C. Une seule retouche :
FASMARM écrit ses relocations de 64 bits sous le numéro de x86-64 (1), `tools/quake/fixreloc.py` les renumérote en
`R_AARCH64_ABS64`. L'asm lit les variables du C par des mots d'adresse (`pool`, `la`). `asm_check.c` fait échouer la
construction si un décalage de l'asm ne suit plus les structures du C.

## Passer en 64 bits

Le source d'id tourne en 64 bits à peu de frais : deux alignements à 8 octets (`edict_t`, `surfcache_t`). Le piège est ailleurs : le
moteur range des pointeurs en décalages de 32 bits depuis `pr_strings`. Le tas de Quake est donc **dans** le programme
(`g_mem`, 96 Mo), à moins de 2 Go de ses variables, plutôt qu'alloué par `mmap`. `R_RenderView` exige aussi d'être sur le thread de
`R_Init` : `r_stack_start` est recalé à chaque image.

## Assembleur

Profil de la version C (`gprof` sur le PC, 8 000 images des trois démos, 320 × 200) : `D_DrawSpans8` 29 %, `D_DrawZSpans` 9 %,
`R_RenderFace` 7 %, `R_EmitEdge` 6 %, `R_RecursiveWorldNode` 4 %, `R_LeadingEdge` 4 %, `R_AliasTransformAndProjectFinalVerts` 3 %.

| Routine A64 | Remplace | Note |
|---|---|---|
| `D_DrawSpans8` | d_scan.c | huit points déroulés |
| `D_DrawZSpans` | d_scan.c | NEON, 8 valeurs à la fois |
| `D_DrawTurbulent8Span` | d_scan.c | l'eau |
| `R_DrawSurfaceBlock8_mip0` à `3` | r_surf.c | la lumière dans le cache des surfaces |
| `D_PolysetDrawSpans8` | d_polyse.c | les modèles |
| `R_AliasTransformAndProjectFinalVerts` | r_alias.c | NEON : les trois lignes de la matrice dans trois voies |
| `D_DrawParticle` | d_part.c | |
| `R_InsertNewEdges`, `R_RemoveEdges`, `R_StepActiveU`, `R_GenerateSpans` | r_edge.c | avec `R_LeadingEdge` et `R_TrailingEdge` à l'intérieur |
| `SND_PaintChannelFrom8`, `From16`, `Snd_WriteLinearBlastStereo16` | snd_mix.c | NEON, sans la table des volumes |
| `qa_present` | vid.c | 8 bits vers 32 bits |

Restent en C : `R_RenderFace`, `R_EmitEdge`, `R_ClipEdge`, `R_RecursiveWorldNode` (surtout des flottants à l'ordre imposé : gain attendu
faible, non mesuré), `D_PolysetRecursiveTriangle` et le reste du tracé des triangles, le ciel, les sprites, le QuakeC.

Pour rester identique au C, l'asm garde l'ordre exact des flottants : `fmul` puis `fadd`, jamais `fmadd`, et `-ffp-contract=off` côté C.
Quelques détails du C sont reproduits tels quels, comme le décalage **logique** des pas de lumière dans `R_DrawSurfaceBlock8` : `r_lightptr` est
non signé.

## Ce qui est prouvé

Tout sous QEMU, avec LibreQuake v0.09-beta `lite`. **Rien n'a été essayé sur le Pi.**

* **Le chemin C et le chemin asm donnent les mêmes images.** `tools/quake/check.sh` : Linux aarch64 sous `qemu-aarch64`, temps compté
  (1/60 s par image), une somme FNV par image pour le tampon 8 bits, le son et la palette. Identiques sur 18 000 images, en neuf passages : les
  trois démos en 320 × 200, deux en 640 × 480 et 800 × 600, deux cartes sans entrées (`lq_e0m1`, `lq_e0m5`), et deux où le joueur tire, avance et
  tourne à volume 8, pour les particules et l'écrêtage du son.
* **Ce contrôle contrôle quelque chose.** `tools/quake/mutate.sh` glisse 18 erreurs dans les routines ; 17 changent les sommes. La dernière est
  un trou connu : le tri sur 1/z de `R_GenerateSpans`, pour deux modèles de décor de même clé dans la même feuille, n'est passé par aucune de
  ces scènes. Ce chemin est relu, pas prouvé. La première version du contrôle laissait passer l'écrêtage et les particules, d'où les deux passages
  à volume 8. `qa_present` n'est pas dans le harnais.
* **Le jeu tourne dans l'OS** (`raspi4b`, carte d'essai jetable, `tools/quake/testcard.py`) : démarrage, carte chargée, démo rejouée jusqu'au bout
  (`timedemo demo2`, 4 570 images), vue fenêtrée et plein écran, menu du jeu, console, marche au clavier, tir à la souris (munitions de 25 à 22), regard
  à la souris capturée, passage en 800 × 600, sortie par `quit` avec le code 0, message sans données. Le jeu a d'abord tourné en C, puis avec l'asm ;
  `qa_present` n'y est vérifié que sur capture, pas par somme.
* **Le son est présent** : `tools/emu/wavcheck.py` sur l'enregistrement de QEMU, crête à 20 900 pendant les tirs.

Le harnais ne voit que ce que ses scènes exercent : il fixe la taille au départ, et n'a donc pas vu le changement de taille en cours de partie. Ce
défaut, dans `vid.c` et non dans l'asm, plantait `D_DrawSpans8` en 800 × 600 dans l'OS : les surfaces gardaient leurs pointeurs vers l'ancien cache.
`D_FlushCaches` le corrige ; l'essai repassé dans l'OS est bon. `demo1` n'a aucun son ; `demo2` et `demo3` en ont.

## Ce qui ne l'est pas

* **La vitesse sur le Pi** : à mesurer avec F12 ou `timedemo`. Les 320 × 200 sont le défaut ; le menu Video (ou Options > Video du jeu) passe en
  640 × 480 et 800 × 600. Le passage à 800 × 600 par défaut attend cette mesure.
* La souris relative sur une vraie souris USB : sous QEMU, c'est une tablette absolue, d'où des écarts. Le code relatif d'Elegant n'a pas tourné.
* La manette, le `pak0.pak` d'origine.
* La partie jouée à la main d'un bout à l'autre d'une carte, avec sauvegarde et rechargement.
* Le son du Pi : AudioKit y est le même, mais le tampon suit l'horloge du processeur, et une image de plus de 100 ms laisse un trou.

Pas fait : le réseau par `netd` (une partie seul fonctionne en boucle locale, `net_loop.c` ; les sockets de `net_udp.c` demanderaient deux
machines pour être essayées, ce qui ne coûte pas « presque rien »), la musique (`cd_null.c` ; LibreQuake a des `.ogg`), les paquets.

## Mesures

Sous `qemu-aarch64`, le meilleur de 3 passages, le C et l'asm en `-O3` (`tools/quake/time.sh`) :

| Taille | Démo | C | Asm | Gain |
|---|---|---|---|---|
| 320 × 200 | demo1, 2 000 images | 5,68 ms | 5,24 ms | 8 % |
| 320 × 200 | demo2, 2 000 images | 3,20 ms | 2,86 ms | 11 % |
| 800 × 600 | demo1, 1 500 images | 15,04 ms | 12,51 ms | 17 % |
| 800 × 600 | demo2, 1 500 images | 10,37 ms | 8,18 ms | 21 % |

Dans l'OS, sous QEMU complet : `timedemo demo2` donne 71,8 images/s en 320 × 200, mais ce chiffre est le plafond d'EmuKit, 72 images/s, et pas une
vitesse : c'est F12 qui mesure. F12 y montre de 4 à 8 ms par image en 320 × 200 sur `lq_e0m1`, et 51,6 ms à un instant de `demo2` en 800 × 600. Le temps d'une image
va jusqu'à doubler quand le PC fait autre chose ; ces valeurs dépendent de la charge.

Ces temps ne comparent que le C et l'asm entre eux : **QEMU ne dit rien de la vitesse du Pi.** Aucune milliseconde par image n'est mesurée sur le Pi.

## Essais

```bash
sh tools/quake/mk_harness.sh
```

```bash
sh tools/quake/check.sh
```

```bash
sh tools/quake/mutate.sh
```

```bash
sh tools/quake/time.sh
```

Dans WSL, depuis la racine du dépôt, avec les données dans `build/quake/data/id1` (`OPT=-O3` pour le temps, comme l'application). Pour QEMU :
`python -I tools/quake/testcard.py +map start`, puis `tools/qemu_drive.py --sd build/quake/sd.img`. Le clavier de QEMU est AZERTY pour
l'OS : `type` y écrit `qui` en tapant `aui`.

Une carte met 20 s à se charger dans QEMU, davantage si le PC est occupé. Pendant ce temps, l'image reste noire et le titre dit 0 fps : ce n'est
pas un blocage.
