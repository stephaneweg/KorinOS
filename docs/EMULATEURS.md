# Émulateurs : NES, SNES, N64, GBC, GBA, NDS, GameCube, 3DS

Source reprenable : les cœurs d'Onyx (`$ONYX/user\Emulators`, à l'utilisateur, MIT, écrits d'après
les documentations publiques). Les sept y sont, avec leur app (`user/Apps/<nom>emu`). Rien n'y manque pour ces
sept consoles ; la 3DS y est aussi, et a sa section plus bas.

## Ce qu'Onyx a, et n'a pas

| Console | Cœur Onyx (lignes) | Contenu | Manque |
|---|---|---|---|
| NES | 933 | 6502, PPU, APU, mappers 0 1 2 3 4 7 66 | autres mappers |
| GB / GBC | 937 | LR35902, LCD, APU, MBC | — |
| SNES | 2 167 | 65816, SPC700, S-DSP, PPU | SA-1, Super FX, DSP-1 |
| GBA | 2 212 | ARM7TDMI, PPU, APU, BIOS de remplacement (`gba_bios.cpp`, ici `gba_bios.inc`) | — |
| N64 | 2 745 | R4300i interprété, RSP de haut niveau (graphismes, audio), rendu par GPU ou logiciel ; recompilateur écrit ici | — |
| NDS | 6 777 | ARM9 + ARM7, **recompilateur** ARM / Thumb vers A64, 2D, 3D logicielle, SPU, BIOS de remplacement | Wi-Fi, fente GBA, micro |
| GameCube | 7 100 (sans le JIT x86-64) | Gekko interprété, **recompilateur** PowerPC vers A64, GX, DSP de haut niveau (Zelda), carte mémoire | son AX (la plupart des jeux sont muets, Mario Kart compris), EFB relu par le processeur, opérations logiques |

Absents d'ARMPI et écrits ici : le pilote `usbgamepad`, les événements manette de HIDManager. Onyx lançait
les machines sur un cœur réservé en EL1 (`kapi_core_run`) ; ici ce sont des threads EL0, à priorité haute.

## Décision, émulateur par émulateur

Vitesse visée : part d'un cœur A72 à 1,5 GHz, **estimée** par le décompte instructions × coût ; aucune n'est
mesurée sur le Pi à ce stade.

| Ordre | Console | Langage | Pourquoi | Charge estimée, cœur A72 | Vitesse réelle |
|---|---|---|---|---|---|
| 1 | **NES** | **asm** | 8 bits, entiers, petites tables, machine la plus simple ; pose le socle | ~10 % | oui |
| 2 | **GBC** | **asm** | même forme : un processeur 8 bits et une ligne de pixels ; 4 à 8 MHz | ~10 % | oui |
| 3 | **SNES** | **asm** (fait) | entiers, trois processeurs simples ; il reproduit le cœur C++ image pour image | ~25 % | oui |
| 4 | **GBA** | **asm** (fait, décision de l'utilisateur) | deux jeux d'instructions, mais les drapeaux ARM7 sont ceux de l'A64 ; il reproduit le cœur C++ image pour image | ~30 % | oui, à confirmer |
| 5 | **NDS** | **C++** | 6 800 lignes dont un recompilateur écrit en C++ ; le code machine est produit à l'exécution, écrire l'hôte en asm n'y gagne rien | 2D : ~80 % ; 3D : > 100 % sur un cœur | 2D probable ; 3D lourde **non** sans sauts d'images |
| 6 | **N64** | **asm** (fait, décision de l'utilisateur) | R4300 à 93,75 MHz en virgule flottante, RSP de haut niveau, rendu par GpuKit ; recompilateur compris, il reproduit le cœur C++ image pour image | interprète : > 150 % en jeu 3D ; recompilateur : à mesurer | avec le recompilateur : plausible, non démontré |
| 7 | **GameCube** | **C++**, boucles du GX en **asm** | voir plus bas : 98 % du temps de la machine est dans le code produit par le recompilateur | deux cœurs presque pleins, GPU en plus | mesuré sans son : 36 à 63 trames/s sur 50 sur Wind Waker ; temps réel **pas atteint partout** |

### Le rendu N64 par Mesa

`n64emu` a maintenant trois rendus, essayés dans cet ordre : Mesa, GpuKit, logiciel. Le chemin par GpuKit évalue le combineur du RDP par sommet, avec une seule texture. Le chemin par Mesa le fait par pixel, dans un shader GLSL ES 3.00 généré selon le réglage du RDP.

| Sujet | GpuKit (`gpu_frame`) | Mesa (`gl/n64gl.c`) |
|---|---|---|
| Combineur | par sommet, puis interpolé | par pixel, deux cycles |
| Textures | une, `TEXEL1` remplacé par `TEXEL0` | deux vraies : la tuile et la suivante ; au 2e cycle, `TEXEL0` et `TEXEL1` s'échangent, car le RDP a avancé |
| Filtre | bilinéaire du TMU | trois points en shader, comme la console ; bilinéaire au menu CPU |
| Blender | modes figés (`GF_BLEND_ALPHA`, `GF_ALPHATEST`) | P, A, M, B des deux cycles : le premier en shader, le dernier en `glBlendFunc` |
| Brouillard | aucun | celui du microcode : `z / w * fm + fo` dans l'alpha du sommet, `G_FOG` |
| Autres | | ciseaux, `CVG_X_ALPHA`, `ALPHA_CVG_SEL`, seuil et tramage alpha, bruit, `PRIM_LOD_FRAC`, `LOD_FRACTION` approché |

**La répartition.** Le cœur reste en asm : `n64_gl.inc` remplit les mêmes tableaux `GF_v` et `GF_b` avec des données brutes (`struct n64gl_vert`, `n64gl_batch`), sans passer par `gfx_combine`. Tout ce qui parle à GL, générateur de shaders compris, est en C dans `src/apps/n64emu/gl/`. Ce choix a une raison précise : le même `n64gl.c` se rejoue sur le PC avec llvmpipe, ce qui a permis de vérifier de vraies images sans GPU. Il n'y a pas de code GL par pixel sur le processeur, seulement un tour par lot.

**La liaison.** `tools/emu/build-n64.sh` assemble `n64emu.asm` en programme FASMARM entier, à `0x4004000000`, soit 64 Mo au-dessus du C. `n64link.py` pose ses segments tels quels dans l'édition de liens de libposix et Mesa, sans relocation : `ld` n'a pas à comprendre l'asm. `n64gl_armpi.c` entre par `gl_start`, juste avant `start`, avec la table `struct n64gl_api`. L'assembleur seul se construit toujours, par `build.ps1` et dans l'OS : `start` laisse `g_gl` à 0 et l'émulateur reste sur GpuKit. `build.ps1` met sur la carte la version reliée à Mesa si `build/emu/sdroot` la contient.

| Mesure | Valeur |
|---|---|
| Binaire | 16 216 320 octets, contre 55 Ko pour l'assembleur seul |
| Chargement sur le Pi | 3,0 s la première fois, 54 ms une fois dans le cache de fatfs |
| Pile de la machine | passée de 1 à 8 Mo dans EmuKit, pour le compilateur de Mesa |

Le thread de la machine reçoit le `tpidr_el0` du thread principal (`emu_thread`) : il partage son bloc TLS, donc le contexte courant de Mesa lui sert sans rien refaire. Rien d'autre n'appelle Mesa.

**La compilation ne doit pas saccader.** Deux étages, plus une compilation au premier lot :

1. Chaque réglage est une clef de 16 octets, et la table de programmes les garde pour la partie.
2. Les clefs vues s'ajoutent à `SD0:/home/n64gl/<rom>.glk` et sont recompilées au chargement de la ROM, avec un triangle vide pour forcer le code QPU, que Gallium ne produit qu'au premier dessin. Ce fichier ne va jamais à côté de la ROM, qui peut être sur un volume de l'utilisateur.
3. Un réglage inconnu se compile à son premier lot, sur le thread de la machine. Sur le Pi, `gltest` donne de l'ordre de 10 ms par programme ; ce temps n'a pas été mesuré pour ces shaders.

Un shader général, avec les réglages en uniformes, a d'abord servi de repli pendant ces compilations. **Il est retiré.** Lors du premier essai sur le vrai GPU, 560 instructions et une boucle, il dessinait presque tous les lots des premières images, et ces jobs n'ont jamais fini. Rien ne prouve qu'il soit la cause, mais les autres chemins n'ont pas pu être départagés sur le Pi, et son gain ne justifiait pas ce risque.

Le seul fil d'exécution reste celui de la machine : pas de compilation en parallèle, car un contexte par thread n'a jamais été essayé ici et le code QPU est de toute façon par contexte.

**Les FBO ne sont pas faits.** Ils ne servent qu'aux jeux qui dessinent des triangles dans une image relue ensuite. Aujourd'hui, un triangle vers une image hors écran est perdu, un rectangle passe par le processeur (`rect_cpu`), et `n64_snapshot` écrit l'image finale en RDRAM. Sur les images essayées, rien ne manque, mais elles se limitent à l'introduction de deux jeux : le besoin reste à juger en jeu.

#### Ce qui a été prouvé

| Essai | Résultat |
|---|---|
| `check_n64.sh` : le cœur reste identique au C++, `m_glOn` à 0 | Ocarina of Time 3 000 images à l'interprète et au recompilateur, 1 500 en rendu logiciel ; Majora's Mask 3 000 au recompilateur : aucun écart |
| `n64gl_check.sh` : réglages du RDP au hasard contre un calcul indépendant, sur llvmpipe | 11 208 pixels, filtre compris, 0 écart |
| Filtre à trois points, les 9 combinaisons de repli | 9 216 pixels, 0 écart |
| Code QPU de Mesa, produit dans ARMPI, exécuté par `qpusim` (`n64gl_qpu.*`) : deux cycles, deux textures, trois points, brouillard | 1 024 valeurs, 0 écart |
| Images de jeu rejouées par llvmpipe | Ocarina of Time : écran de langue et rêve (brouillard, pluie, cheval de Ganondorf), lus à l'œil, justes ; Majora's Mask : introduction. 35 et 17 programmes, tous acceptés. Pas comparés au pixel près : la console n'est pas là pour référence |
| `n64gltest` sous QEMU, le pilote `v3d` à blanc | ces mêmes 13 images, 35 programmes, `glGetError` non relevé, aucun refus |
| `n64emu --gldry --bench 1000` sous QEMU | 1 000 images par le chemin complet, `.glk` écrit sur `SD0:` |
| Seconde partie, `.glk` relu | 23 programmes compilés d'avance, 2,96 s sous QEMU |
| `n64emu` sans GPU sous QEMU | `armgl_init` rend 0, repli sur le logiciel, écran-titre affiché |
| GPU perdu en jeu, sous QEMU à blanc (`--gllose 40`) | rendu logiciel dès la liste suivante, 700 images sans plantage |

Le banc `qpusim` sait échouer : il a signalé un décalage quand Mesa ne demandait au TMU que le mot de l'alpha d'une texture. `qpusim` ignore ce masque ; le lot d'essai lit donc T1 en entier. Ce cas précis n'est donc pas couvert par le simulateur, seulement par llvmpipe et par la compilation.

#### Ce qui n'a pas été prouvé

**Aucune image n'a encore été rendue juste par le vrai GPU, et la comparaison avec GpuKit n'est pas faite.** Deux essais sur le Pi :

| Date | Résultat |
|---|---|
| 2 novembre | `armgl_init` refuse le GPU, déjà éteint par un autre essai ; `--bench` tourne en repli, puis le Pi cesse de répondre au réseau, cause non établie |
| 3 novembre | Mesa démarre : `n64gl : pret en 54 ms (GPU)`. Les 4 premiers jobs ne finissent pas (`CT0CS 0`, `CT1CS 288` : le binning passe, le rendu reste bloqué), et le service laisse le GPU éteint jusqu'au redémarrage |

Les mêmes valeurs de `CT1CS` sont au journal pour un autre programme, avant cet essai. Les ciseaux sont écartés : aucun n'est vide sur les 300 premières images. Le shader général, retiré depuis, reste le suspect principal, sans preuve. Ces deux séries de chiffres ne valent pas mesure.

Si le GPU tombe en jeu, `n64gl` le voit (`armgl_lost`) et l'émulateur finit en rendu logiciel, au lieu de garder une image figée.

Pour mesurer, après un redémarrage du Pi :

```sh
SD0:/n64gl/main --bench 4801 SD1:/Roms/ZeldaOcarinaOfTime/ZeldaOcarinaOfTime.z64
SD0:/n64gl/main --nogl --bench 4801 SD1:/Roms/ZeldaOcarinaOfTime/ZeldaOcarinaOfTime.z64
```

`--bench` n'écrit jamais de `.sav`. Une ligne `n64gl : pret ... (GPU)` doit apparaître au début du premier essai, et aucune ligne `MESA: error`. Le coût par pixel reste l'inconnue principale : un filtre à trois points lit trois fois chaque texture. S'il ne tient pas, le menu CPU repasse au bilinéaire du TMU.

Sur le Pi, l'interface de `n64emu` s'est arrêtée au démarrage avec `kit absent ou trop ancien : SD0:/lib/uikit`. Le fichier de la carte est bien en version 203, celle qu'attend `emukit.so`. Un UIKit plus ancien encore chargé en mémoire expliquerait ce refus, ce qui n'est pas vérifié. `--bench` ne passe pas par ce chemin.

Limites connues :

* `LOD_FRACTION` utilise deux textures seulement : pas de choix de tuile par pixel au-delà de la deuxième.
* Le blender lit `MEM` au premier cycle comme le pixel lui-même, et l'alpha de la mémoire comme 1.
* Un rectangle retourné (`0xE5`) garde l'orientation du chemin par GpuKit, fausse.
* La profondeur d'un rectangle reste 0 : `G_ZS_PRIM` n'est pas lu.
* Le filtre à trois points ignore `mid_texel`.
* Le bord droit d'une image peut montrer une colonne non couverte : un rectangle en un cycle s'arrête à `x1`.

Refaire :

```bash
sh tools/emu/n64gl_check.sh
```

```bash
sh tools/emu/n64gl_check.sh rom.z64 4801 1200 300 oot
```

```bash
sh tools/emu/build-n64.sh
```

```bash
python -I tools/emu/n64gl_qpu.py build/mesa/run.out build/emu/n64gl_qpu.h
```

```bash
sh tools/emu/n64gl_qpu.sh
```

### Le recompilateur R4300 (`src/apps/n64emu/n64_jit.inc`)

Blocs A64 chaînés, 15 registres MIPS fixés dans des registres de l'hôte, COP1 traduit. Aucun appel C dans un bloc : tout cas
lent sort avant l'instruction, et l'interprète la fait. Les cycles restent ceux de l'interprète, à l'instruction près : un
bloc ne part que s'il tient avant le prochain événement ; les deux modes donnent donc la même machine
(`tools/emu/check_n64.sh`). Écrit d'abord en C++ (`tools/emu/n64ref/n64/n64_jit.cpp`) : sous `qemu-aarch64`, 3 000 images, 120 s contre 42 s
sur Ocarina of Time, 105 s contre 46 s sur Majora's Mask, et 0,2 % des instructions encore à l'interprète (COP0 surtout). Le reste du temps est dans les
graphismes et le son de haut niveau. Ce gain ne dit rien du Pi, à mesurer avec F12.

Limites : le code hors de kseg0 et kseg1 (TLB) et le code de la cartouche restent à l'interprète ; ces deux jeux n'en ont
pas, d'autres si (GoldenEye, par exemple). Menu CPU : retour à l'interprète.

### Franchement, pour le N64 et le NDS

* **N64** : l'interprète d'Onyx coûte une quarantaine de cycles par instruction. Un jeu 3D exécute 40 à 60 millions
  d'instructions R4300 par seconde : il faudrait 2 GHz et plus. Le recompilateur lève ce verrou ; reste à voir sur le Pi si
  les graphismes et le son de haut niveau tiennent. Les microcodes inconnus restent sans image.
* **NDS** : le recompilateur existe et sert ici tel quel. Reste qu'il faut deux processeurs et la 3D logicielle sur
  quatre cœurs déjà partagés avec Elegant et sound. Le thread de la 3D est prévu. La vitesse réelle en 2D est
  plausible, pas démontrée ; les jeux 3D chargés n'y arriveront pas tous.

**QEMU ne dit rien de la vitesse du Pi** : il sert à montrer que l'image, le son et les entrées fonctionnent.

Temps par image sous `qemu-aarch64`, le meilleur de 3 passages de 600 images, C++ `-O3 -mcpu=cortex-a72` contre asm (`time_snes.sh`,
`time_gba.sh`) : A Link to the Past 6,2 ms contre 5,4 ms, ROM SNES au hasard 8,8 contre 8,1, Minish Cap 8,3 contre 7,4, Pokémon
Émeraude 20,0 contre 20,1. Mesures bruitées (de 20 à 33 ms sur Émeraude) : l'asm va au plus de 10 à 15 % plus vite, et pas toujours.
Sous QEMU, cela ne compare que les deux entre eux ; la vitesse du Pi reste à mesurer.

N64, 600 images (`time_n64.sh`, trois passages) : au recompilateur, Ocarina of Time de 10,4 à 11,0 ms en C++ contre 10,5 à 11,3 en asm,
Majora's Mask de 11,4 à 11,8 contre 11,4 à 12,1 : pas de gain, le code produit étant le même ; à l'interprète, Ocarina of Time 19,1 à
19,9 ms contre 17,0 à 17,8, soit une dizaine de pour cent. QEMU retraduit le code recompilé : ce chiffre compare encore moins le Pi.

Le stress du N64 a montré deux choses. Le DMA du SI écrivait en RDRAM sans invalider les blocs recompilés : défaut du C++, reproduit
à l'identique par l'asm, corrigé des deux côtés (`tools/emu/n64ref`). Et une addition flottante sur deux NaN dépend de l'ordre des
opérandes, que GCC échange en `-O2` : la référence se compile donc en `-O1` (`check_n64.sh`) ; aucun des deux n'imite ici le NaN du R4300.

### Les saccades du rêve d'Ocarina of Time

La scène du rêve (Zelda poursuivie par Ganondorf) a été rejouée à la manette scriptée sur une ROM PAL, image par image. Le recompilateur n'y
est pour rien (aucun vidage, presque aucune invalidation) ni les nouvelles textures : c'est la scène la plus lourde du début du jeu, trois fois plus
de lots que la forêt Kokiri, le R4300 quatre fois plus occupé. Trois défauts s'y additionnaient :

* **La cadence était celle du NTSC.** `n64emu` déclarait 60 Hz en dur ; une ROM PAL tourne à 50 (`m_pal`). Tant que la machine tenait 60 images/s,
  le flux du son freinait le rythmeur par à-coups ; dès qu'elle ne les tenait plus, EmuKit la croyait en retard, alors qu'elle était à l'heure, et sautait
  des images. La cadence suit maintenant la région de la ROM, comme chez Onyx.
* **Chaque image du jeu était rendue trois fois.** Le jeu fait une image pour trois VI : deux rendus GPU, deux écritures vers la RDRAM et deux
  mises à l'écran sur trois ne changeaient rien. `emu_frame_same` laisse l'image publiée à sa place.
* **La texture était hachée à chaque triangle.** `gfxTexture` repassait sur toute la TMEM pour retrouver la même texture : 460 000 mots par
  tâche. Le dernier résultat de chaque tuile est gardé (`texMemo`) tant que la TMEM et la tuile ne changent pas : huit fois moins de mots, la tâche
  graphique deux fois plus courte (2,6 à 1,4 ms sur le PC).

À la bonne cadence, plus rien ne gonfle la file du son : elle se stabilise au seuil bas du rythmeur. `EM_Q_LOW` passe donc de 23 à 46 ms, pour qu'un VI
lourd n'y creuse pas de trou (Onyx garde environ 59 ms). Le rythmeur freine à 70 ms et `ak_write` refuse au-delà de 93 ms : une écriture ordinaire
(20 ms) tient entre les deux, et le retour d'`ak_write` reste sans traitement.

## GameCube (`src/apps/gcemu`)

**La feuille de route l'avait écartée comme trop lente. C'était une estimation ; Onyx a une mesure qui la contredit.** Sur le vrai Pi, au 28/09/2026, Onyx tenait Wind Waker PAL à 46-49 trames/s sur 50 à Outset, avec un Pi qui chauffait à 81-84 °C et descendait à 1,3 GHz (`docs/HANDOFF.md` d'Onyx). **Ce chiffre n'est pas le nôtre** : Onyx réservait des cœurs en EL1, ARMPI a des threads EL0, Mesa au lieu de shaders QPU écrits à la main, et Elegant qui compose. Sur ARMPI, voir **Sur le vrai Pi** plus bas : de 36 à 63 trames/s sur 50 sur Wind Waker, sans son. Il faut s'attendre à ce qu'il chauffe : il a dû être éteint pendant les essais du 2/11 ; un refroidissement sera indispensable.

### Ce qui reste en C++, et pourquoi

| Partie | Langage | Pourquoi |
|---|---|---|
| Recompilateur (`gc_jit.cpp`) | C++ | le code machine est produit à l'exécution ; sur le N64, recompilateur C++ et asm donnent le même temps (10,4-11,0 ms contre 10,5-11,3) |
| Gekko, mémoire, registres, DVD, DSP, carte | C++ | Onyx mesure 98 % du temps de la machine dans le code recompilé : réécrire le reste ne peut pas gagner plus de 2 % |
| Parler à GL (`gxes_gl.cpp`) | C++ | de la mise en forme de texte et des appels à Mesa |
| Décodage des sommets, positions, primitives, YUV (`gx_asm.asm`) | **asm** | le thread du GX est le seul où le temps est dans notre code. Assemblé par FASMARM (`format ELF64`), relié au C++ |

**Ce choix est à toi de le confirmer** : le cœur n'est pas réécrit en assembleur comme ceux du NES au N64. Le gain attendu ne le justifie pas, mais la préférence pour l'asm est la tienne. Reste à passer en asm si on le veut : enregistreur (couleurs, texgens), décodage des textures, mixage Zelda.

### Le rendu

Trois threads : la machine, le GX (FIFO, enregistreur), la fenêtre (le contexte GL). `gxes.h` :

* **L'enregistreur** reprend le plan de `gxv3d.h` d'Onyx : sommets transformés et éclairés sur le processeur, draws fondus en lots (4 800 draws de 5 sommets par image deviennent environ 370 appels à Mesa). Une copie d'EFB est un ordre du flux.
* **Le lecteur** joue les ordres sur un FBO de 640 x 528. Le TEV de chaque configuration est un fragment shader GLSL ES **généré**, entier comme le matériel, réduit à ce que les étages lisent : Mesa compile sur le Pi, chaque ligne se paie. Indirect, brouillard, texture de z, formats d'EFB, alpha de destination y sont.
* **Cache et repli** : un programme par configuration ; au-delà de `budgetUs` de compilation par image, le lot passe par un programme simple (carte x couleur) jusqu'à l'image suivante. Un fichier `.gxs` garde les configurations vues, recompilées au début de la partie suivante.
* **Copies d'EFB** : un FBO par emplacement, le format converti par shader. Une image n'est jamais sautée entière : ses copies peuvent servir longtemps. En retard, seuls les lots utiles aux copies sont joués.
* **Fenêtre** : `emu_gl_hook`, à chaque tour d'EmuKit ; `armgl_window`, sans copie, refaite si la vue change. Le jeu qui écrit lui-même son XFB passe par `showFb`.
* **Fichiers** : la carte mémoire et `.gxs` vont dans `SD0:/home/gamelib/gc`, jamais à côté du disque (`SD1:` est intouchable) ; un `<disque>.sav` y est seulement lu, la première fois. Le format est celui de Dolphin et d'Onyx.
* **Sans GPU** (QEMU) : `soft.cpp`, TEV par sommet, sans copies d'EFB. Pour voir que le jeu tourne, pas pour y jouer.

Écarts connus avec Onyx : pas de double source en ES 3.0, donc l'alpha de destination seulement quand le mélange ne lit pas celui du TEV ; pas d'opérations logiques. Un générateur commun avec le N64 ou la 3DS n'a pas été fait : le découpage qui resservirait est `Player` (cache, repli, budget) et `emu_gl_hook`.

### Ce qui est prouvé, et ce qui ne l'est pas

| Essai | Résultat | Portée |
|---|---|---|
| `gxes_host` : gxes contre `gxgl.cpp` d'Onyx, même machine, même llvmpipe | Mario Kart Double Dash, 2 287 images, film THP compris : 0 pixel à plus de 8 niveaux sauf 1 ; Wind Waker jusqu'à 1 000 images : écran-titre exact, au plus 0,04 % de pixels sur des arêtes ; Twilight Princess, écran-titre 3D : 0,10 % | **logiciel** : prouve le GLSL et les ordres, pas le GPU du Pi |
| `check_gc.sh` : `gx_asm.asm` contre son C++, `qemu-aarch64`, 700 trames de Wind Waker et de Mario Kart | sommes identiques, image après image, 72 millions de flottants ; une faute volontaire est bien détectée | `gx_decode`, `gx_positions`, `gx_expand`, `gx_yuv` |
| `gcgl --dry --shaders` dans ARMPI, QEMU | 169 shaders des trois jeux compilés par le vrai compilateur v3d de Mesa : 0 refusé par GL, aucune erreur affichée | à blanc ; sur le vrai V3D : plus bas |
| `gcemu --gldry` dans ARMPI, QEMU | 600 images, recompilateur, thread du GX, FBO, copies d'EFB, 2 696 lots par le repli, sans incident ; le cache `.gxs` et la carte vont sur `SD0`, rien sur `SD1` | à blanc ; le vrai GPU : plus bas |
| `gcemu` en repli logiciel, QEMU | Wind Waker avec l'asm : logos, puis l'introduction en 3D à l'écran | image dégradée, c'est le repli |

### Sur le vrai Pi (2/11/2026)

Par `gcgl` : le cœur, le GX et GL de gcemu, sans EmuKit, **sans son ni cadence**, la machine allant aussi vite qu'elle peut. Pas de manette, donc seulement les débuts des jeux, sans carte mémoire. Ces chiffres ne viennent pas de la vraie app.

| Essai | Résultat |
|---|---|
| 169 shaders sur le vrai V3D | 0 refusé. Wind Waker : 71 en 746 ms, 39 ms au pire ; Twilight Princess : 55 en 684 ms, 28 ms au pire ; Mario Kart : 43 en 363 ms, 31 ms au pire |
| Tenue | trois jeux, 4 200 trames, 1 500 d'affilée sur Wind Waker, sans blocage du GPU, 0 shader refusé |
| Image du GPU contre llvmpipe | Wind Waker, image 450 : 0,01 % de pixels à plus de 8 niveaux, à la même position. Image 750 : même rendu à l'œil, 25,6 % d'écart, car les deux machines ne sont pas synchronisées et l'animation a bougé. **Il n'y a donc pas de preuve au pixel près sur une scène animée** |

| Scène | Trames/s (machine) | Images/s | Machine | GX | GL par image | La machine attend |
|---|---|---|---|---|---|---|
| Wind Waker PAL (50 Hz), début du titre | 53 à 63 | 32 à 38 | 16 à 19 ms | 3 à 5 ms | 13 à 15 ms | 0 |
| Wind Waker, survol de l'île | **36 à 40** | 22 à 24 | 25 à 27 ms | 19 à 25 ms | 29 à 44 ms | 1 à 1,5 ms |
| Wind Waker, fin du survol | 46 à 49 | 28 à 29 | 21 à 22 ms | 11 ms | 25 à 27 ms | non relevée |
| Mario Kart, film THP | **18** | 18 | 55 ms | 5 ms | 8 ms | 0 |
| Logos de début de jeu, les trois | 8 à 19 | 8 à 18 | 53 à 118 ms | moins de 1 ms | 3 à 4 ms | 0 |

Ce que ces chiffres disent :

* **La GameCube n'est pas hors de portée du Pi.** La cadence de 50 Hz est dépassée dans les scènes légères, et il manque un quart dans le survol, la scène lourde que nous avons pu mesurer.
* **Le frein est la machine**, comme chez Onyx : elle n'attend presque jamais le GX ni GL. Le GX suit de près dans le survol ; GL prend 33 ms par image, mais une image sur deux trames environ.
* **Onyx faisait mieux en jeu, ailleurs** (46-49 sur 50 à Outset) : la scène n'est pas la même, on ne peut pas comparer. À Onyx, ce même survol faisait ~20 trames/s au 28/09/2026.
* **Le film et les logos sont lents pour une raison non établie.** Le GX n'y fait presque rien, le temps est sur le thread de la machine. Hypothèses à vérifier : le disque est lu sur ce thread, et le film se décode en PowerPC. À mesurer avant d'y toucher.
* **Toute la machine sera plus chargée en vrai** : le son, EmuKit et le rythmeur ne sont pas dans ces chiffres.

Pas mesuré : une partie jouée (`gcgl` n'a pas de manette), la température pendant ces essais, le son, le coût réel du repli à l'écran (2 000 lots environ par le repli sur la première passe, moins de 30 ms par shader). `emu_gl_hook`, la maximisation et F9 n'ont tourné qu'à blanc.

**La vraie app sur le Pi (3/11/2026) :** `gcemu.app` y est installée et a tourné dans sa fenêtre sur le vrai GPU : Wind Waker, écran-titre, image juste à l'œil par capture VNC, 270 Mo. L'utilisateur l'a lancée. Le son, les manettes et la vitesse de cette app n'ont pas été relevés. Le Pi chauffe encore assez pour qu'il faille l'éteindre après ces essais.

**Pour reprendre :** `gcgl` est dans `SD0:/gcgl`, avec `ww.gxs`, `tp.gxs` et `mk.gxs`. Rien n'y écrit dans `SD1`.

**Installer EmuKit à part est un piège.** Le 2/11, le `emukit.so` de `main` posé seul sur le Pi a arrêté tous les émulateurs (`kit absent ou trop ancien : uikit`, code 127). Deux règles de l'OS : `kit_use` exige la version de l'`.abi` contre laquelle le kit a été assemblé, pas seulement les rangs appelés ; et un kit reste en mémoire jusqu'au redémarrage, remettre l'ancien fichier ne suffit pas. Avant de poser un kit : comparer les versions (le journal les donne), l'essayer sous QEMU avec les vrais kits de la carte (`GC_LIB=dossier python -I tools/emu/gc_testimage.py`), puis prévoir un redémarrage. L'ancien kit du Pi est gardé dans `SD0:/gcgl/emukit.so.avant`.

## État réel (31/10/2026)

Essais sous QEMU (`raspi4b`, sans GPU), images lues sur capture, avec les ROM de l'utilisateur, copiées sur une carte
d'essai jetable et jamais sur la carte du Pi ni dans le dépôt.

| Console | App | Langage | Jeu essayé | Vérifié sous QEMU | Fidélité |
|---|---|---|---|---|---|
| NES | `nesemu` | asm | Super Mario Bros. 3 (carte du monde) | image, touches, plein écran | identique au C++ d'Onyx : images, son, cycles, sur 3 000 images (SMB3, Zelda, Zelda II) et des ROM au hasard, mappers 0 1 2 3 4 7 66, NTSC et PAL |
| GB / GBC | `gbemu` | asm | Link's Awakening (introduction) | image, touches | identique au C++ d'Onyx sur 3 000 images (5 ROM) |
| SNES | `snesemu` | asm | A Link to the Past (écran du nom) | image, touches, son, pause | identique au C++ d'Onyx : images, son, horloge, état, sauvegarde, sur 3 000 images (A Link to the Past) et 8 ROM au hasard (`romgen.py snes-stress` : LoROM, HiROM, PAL, tous les codes du 65C816, tous les modes du PPU) |
| GBA | `gbaemu` | asm | Minish Cap (choix de la langue) | image, touches, son, pause | identique au C++ d'Onyx : images, son, cycles, état, sauvegarde, sur 3 000 images (Minish Cap, Pokémon Émeraude) et 8 ROM au hasard (`romgen.py gba-stress` : ARM, Thumb, modes 0-5, tous les appels du BIOS, SRAM, Flash, EEPROM) ; BIOS de remplacement en asm aussi |
| N64 | `n64emu` | asm, recompilateur compris | Ocarina of Time (écran-titre, rendu logiciel) | image, son, pause, menu CPU, `.sav`, touches (Start jusqu'à la saisie du nom) | identique au C++ : cycles, registres, RDRAM, image, son, listes d'affichage, sur 3 000 images (Ocarina of Time, Majora's Mask), à l'interprète et au recompilateur, et 8 ROM au hasard (`romgen.py n64-stress` : 98,6 % des lignes de l'interprète, avec TLB, exceptions, FPU, DMA ; ordres d'octets `.v64` et `.n64`) ; rendu logiciel et écriture des images en RDRAM identiques au pixel près, 1 500 images d'Ocarina of Time et 900 de Majora's Mask ; **GpuKit non essayé** (QEMU n'a pas de GPU) : `gpu_frame` est porté sans avoir tourné ; rendu par Mesa : voir plus haut |
| NDS | `ndsemu` | C++ | Pokémon Donjon Mystère : Explorateurs du Ciel (écran-titre) ; `gfx3d.nds` d'Onyx (3D) | image, recompilateur | le cœur d'Onyx, tel quel |

Son mesuré sur l'enregistrement de QEMU (`tools/emu/wavcheck.py`) : présent sur les six ; au N64, il commence après 80 s, le
jeu tournant au ralenti sous QEMU. Pause (P) : image figée sur les six. `<rom>.sav` : écrit pour alttp, mc, oot, zla.
Entrées : clavier par HIDManager sur les six ; mapping `gamepad.ini` essayé (`start = s`).

Défaut connu, hérité du cœur d'Onyx : le GBC sort un fort décalage continu (une moyenne de -20 000 environ) : la voie
muette vaut -15 à son convertisseur. Le signal s'entend, mais il perd de la marge : un filtre passe-haut reste à ajouter.

**Rien n'a été essayé sur le vrai Pi** : ni la vitesse réelle, ni le son HDMI / jack, ni GpuKit pour le N64. QEMU ne dit rien
de la vitesse du Pi : les images/s qu'il montre n'ont pas de valeur. Le titre (F12) donne sur le Pi les images/s et les ms
par image.

Manque connu, hérité d'Onyx : SA-1, Super FX, DSP-1 (SNES), mappers NES autres que ceux listés, Wi-Fi et fente GBA (NDS).

## 3DS : `n3dsemu`

Le cœur d'Onyx (`user/Emulators/n3ds`) tourne ici dans `src/apps/n3dsemu`, sur EmuKit, sans pont. **La feuille de route l'avait écartée comme trop lente : ce n'est pas fondé.** Mesuré sur le Pi (3/11/2026) : le titre d'*A Link Between Worlds*, la scène la plus lourde essayée, tourne à 36 images/s sur le GPU, sans fenêtre ni cadence. Onyx mesurait 40 sur la même scène.

| Partie | Langage | Pourquoi |
|---|---|---|
| Système (noyau, services, FS, DSP de haut niveau, chargeur) | C++, copié d'Onyx | du suivi de structures, peu de temps machine : l'asm n'apporte rien |
| ARM11 : interprète (`n3ds_arm.cpp`), recompilateur (`n3ds_jit.cpp`) | C++, écrit ici | le temps est dans le code produit ; le N64 a montré qu'un émetteur en asm ne gagne rien. **Remplace Dynarmic**, qui ne sert plus que de référence |
| PICA200 logiciel, shaders JIT | C++ d'Onyx | repli et référence |
| Rendu sur Mesa (`n3ds_gl.cpp`), GLSL (`n3ds_glsl.cpp`) | C++ | parle à GL |
| Boucles de pixels : textures (14 formats, ETC1), recopie vers l'écran (`n3ds_kern.S`) | **asm** | là où une boucle compte |

**Le recompilateur** part de celui du NDS : même assembleur, Thumb réécrit en ARM, drapeaux dans ceux de l'hôte. Différences : les 15 registres fixés en permanence, aucun appel C dans le code produit, tout cas lent sort avant l'instruction et l'interprète la fait, VFP scalaire traduit directement (FPCR réglée comme le FPSCR), vecteurs courts laissés à l'interprète. Un tick par instruction, compté avant de la faire ; un bloc ne part que s'il tient dans ce qui reste : l'interprète et les blocs donnent les mêmes ticks.

**Le rendu.** Une cible du jeu est un FBO. Le combineur est refait en entiers dans un fragment shader généré par configuration (mêmes opérandes, même division par 255, tampon en retard de deux étages), avec un frère réduit le temps que le vrai soit compilé : 3 ms de compilation par image au plus. Un vertex shader du jeu spécialisé (le `Spec` d'Onyx) devient du GLSL, éclairage du sommet compris ; tant qu'il n'est pas compilé, le processeur ombre ce lot et l'image reste la même. Sans GPU, tout retombe sur le rendu logiciel. Ajouts par rapport à Onyx : pochoir réel, couleur constante de mélange, texture procédurale, filtrage des textures.

### Mesuré sur le Pi (3/11/2026)

Banc `run_n3ds`, sans fenêtre ni cadence ni son, un seul thread, ROM lues sur `SD1:` sans y écrire. Temps par image de la console (59,83 par seconde, soit 16,7 ms) :

| Scène | Rendu logiciel | GPU (Mesa) |
|---|---|---|
| *Mario 3D Land*, logos du début | 100 à 180 ms | 8 à 19 ms |
| *Mario 3D Land*, menus (images inchangées) | | 6 à 9 ms |
| *A Link Between Worlds*, écran noir et logos | | 6 à 9 ms |
| *A Link Between Worlds*, le titre, images 1 020 à 1 200 | | **26 à 33 ms** : listes 8 à 14, GPU et relecture 10 à 12, le reste au processeur |

| Sur le titre, en moyenne | |
|---|---|
| Sommets par GLSL | un million sur 60 images, 17 000 par image |
| Programmes | 77, dont 25 passés d'abord par le frère réduit, aucune image refusée |
| Une image du GPU | 1,78 ms de commandes, 2,52 ms de relecture |

Le pic de 83 ms autour de l'image 960 est l'arrivée des shaders de la scène : le budget de 3 ms par image ne couvre pas les listes du processeur quand un vertex shader manque encore. L'image 1 199 sortie du GPU du Pi est juste à l'œil, logo, décor, écran du bas. Contre le rendu logiciel de la même image : écran du bas, 3 pixels sur 96 000 à plus de 8 niveaux ; écran du haut, 48 % à plus de 8 et 8 % à plus de 32. Le GPU filtre les textures et éclaire par sommet, le logiciel ni l'un ni l'autre ; la part de chacun n'est pas mesurée, et llvmpipe sans filtrage donne, lui, 0,06 %. Les mesures ne comptent pas la composition d'Elegant ni le son. **Un vrai niveau de jeu n'a pas été mesuré**, les touches du banc sont restées dans les menus de *Mario*.

### Ce qui est prouvé

| Essai | Résultat |
|---|---|
| Programmes d'essai d'Onyx (`kernel`, `gfx`, `gpu`) | 233 vérifications sur 233, à l'interprète et au recompilateur |
| *A Link Between Worlds*, *Super Mario 3D Land*, 600 images avec touches, contre Dynarmic pas à pas (`check_n3ds.sh`) | CRC des deux écrans, temps de la machine, appels système **identiques** |
| Rendu GL contre rendu logiciel, llvmpipe (`n3ds_replay`) | Zelda : 490 pixels sur 864 000 à plus de 8 niveaux, 2 à plus de 32. Mario : 0 sur 576 000 |
| Vertex shaders GLSL contre processeur, llvmpipe | Zelda, titre : 7 pixels sur 384 000 à plus de 8 niveaux |
| Texture procédurale GLSL contre le calcul du cœur, llvmpipe (`n3ds_replay --proc`) | 500 configurations, 2 048 000 pixels, 0 différent ; aucun des deux jeux ne l'emploie sur ces images |
| Textures en asm contre le C++ (`N3DS_TEXCHECK`) | 1 562 880 texels des deux jeux, 0 faux ; ETC1 et les formats que ces 600 images emploient, pas forcément les 14 |
| `n3dsemu` sous QEMU | `gpu.elf` et *Super Mario 3D Land* (logo) en logiciel, deux écrans. Mario `--dry` : Mesa tourne dans l'OS, image non significative |

Défaut trouvé par ce banc : l'interprète perdait le bit Thumb de LR sur `BLX Rm` ; la trace par instruction (`N3DS_TRACE_RANGE`) l'a montré à la 417 277e. Autre écart : sous Dynarmic, le système émulé calcule avec la FPCR de l'invité (arrondi vers zéro), ce qui décalait des couleurs d'une unité. La référence du banc remet la FPCR de l'hôte pendant les appels système.

### Ce qui ne l'est pas, ou manque

* **L'application elle-même, avec sa fenêtre, n'a pas tourné sur le Pi** : seul le banc `run_n3ds` y a tourné. Déployée le 3/11/2026 (`SD0:/apps/n3dsemu.app`, `emukit.so`, `gamelib`), elle refuse de partir tant que le Pi n'a pas redémarré : `kit absent ou trop ancien : uikit`, le noyau gardant l'ancien UIKit en mémoire ; `nesemu` déjà installé échoue de même.
* Pas de niveau de jeu mesuré, pas de son ni de manette essayés sur le Pi.
* `<jeu>.sav` s'écrit à côté de la ROM : `--nosave` pour un essai qui n'écrit rien.
* Pas de brouillard, d'ombres, de géométrie shader, de mipmaps ; éclairage par sommet sur le GPU (par fragment en logiciel). Hérité d'Onyx.
* Les opérations logiques autres que copier, garder, zéro : l'image retombe sur le rendu logiciel. OpenGL ES 3.0 n'en a pas.
* Les ROM chiffrées sont refusées : c'est le cas d'*Ocarina of Time 3D* chez l'utilisateur.
* Une relecture par image (`glReadPixels`) : la recopie sans passer par le processeur reste à faire.
* Pas de recompilation à chaud des vecteurs VFP ni des opérations parallèles (SIMD ARM) : interprète.

Bancs : `build_n3ds_bench.sh` (référence, port, rejeu), `check_n3ds.sh`, `test_n3ds.py`. Options : `--soft`, `--dry`, `--full`.

## Le socle commun

* **HIDManager et Elegant : les manettes**, comme prévu par la feuille de route. Le pilote donne l'état brut
  (`EV_PAD_RAW`). HIDManager applique le mapping aux boutons de manette et aux touches du clavier ; si cela donne un bouton,
  il dépose `EV_PAD` dans l'anneau de ses abonnés (la touche reste une touche). Elegant le transmet à la fenêtre qui a le
  clavier, comme une touche : `WEV_PAD` (`src/inc/pad.inc`). Manette 0 : le clavier ; 1 et 2 : les pilotes.
* **`SD0:/etc/gamepad.ini`**, clés d'Onyx. `[keyboard]` : `start = s`, `a = z`, une lettre, un chiffre, ou `up down left
  right enter backspace space tab esc prtsc pause none`. `[default]`, `[vvvv:pppp]` (un modèle) : `a = 3`, numéro du
  bouton brut. Par défaut : flèches, A B X Y L R tels quels, Impr. écran et Retour = Select, Pause et Entrée = Start.
  `HID_OP_PADMAP` relit le fichier ; l'applet Manette reste à écrire.
* **`usbgamepad`** (`src/svc/usbgamepad`, `src/svc/inc/hidpad.inc`) : manettes HID, usages Joystick et Gamepad. **Jamais
  essayé** : QEMU n'émule aucune manette USB. Les manettes XInput (Xbox, classe FF) ne sont pas prises en charge.
* **EmuKit** (`src/kits/emukit`) : tout ce qui n'est pas la console : ROM (argument ou dialogue), fenêtre et menus, image à
  l'échelle dans une surface UIKit, thread de la machine, cadence par l'horloge corrigée par le flux du son, AudioKit,
  `<rom>.sav` à côté de la ROM, pause (P), remise à zéro, vitesse (F12). S'appelle depuis l'asm et le C / C++ ; une app n'est
  plus qu'un cœur et une petite fonction `main`.
* **Trois vues** pour chaque émulateur : fenêtrée (View > Zoom 1x, 2x, 3x), maximisée (bouton ou View > Maximize : l'image
  prend toute la place de la fenêtre), plein écran (F9 ou View > Full Screen, Échap pour sortir : fenêtre sans cadre, image
  étirée, proportions gardées). F10 et F11 sont à Elegant (diagnostic).
* Options : `--full rom`, `--thumb sortie rom` (fabrique l'image d'une carte, sans fenêtre).

## Game Library

`gamelib`, en assembleur (`src/apps/gamelib`) : consoles à gauche, cartes à droite, images faites par les émulateurs
(`--thumb`) une à la fois en arrière-plan, gardées dans `SD0:/home/gamelib/thumbs`. N64, DS et 3DS : le nom seul. Dossiers :
`SD0:/home/roms`, `SD1:/roms`, plus `SD0:/home/gamelib/folders.txt`. Par rapport à Onyx : pas de tiroirs de dossiers dans la
colonne, ni de menu contextuel, ni de bannière DS ni d'étiquette N64.

## Construction et essais

* `tools/emu/build.sh` : les apps C++ (`ndsemu`, `n3dsemu`, celle-ci reliée à Mesa) par la chaîne croisée (`-O3`), vers `build/emu/sdroot` ; `pibuild app
  /src/apps/ndsemu ndsemu` sous ARMPI (`-O2`), pas encore essayé.
* `tools/emu/check_nes.sh`, `check_gb.sh`, `check_snes.sh`, `check_gba.sh`, `check_n64.sh` : le cœur asm contre le cœur C++, sous `qemu-aarch64` ;
  `trace_nes.sh`, `trace_snes.sh`, `trace_gba.sh`, `trace_n64.sh` : la première instruction qui diverge ; `cover_snes.sh`, `cover_gba.sh`, `cover_n64.sh` : ce
  qu'une ROM met à l'épreuve ; `time_snes.sh`, `time_gba.sh`, `time_n64.sh` : le temps par image des deux. Les cœurs C++ du SNES et du GBA ne sont
  plus dans `src/apps` : la référence est lue chez Onyx. Celui du N64 a des ajouts (le recompilateur) qu'Onyx n'a pas : il reste
  dans `tools/emu/n64ref`, hors de l'OS, seulement comme référence.
* `tools/emu/wavcheck.py` : le son enregistré par QEMU n'est pas muet.
* GameCube : `tools/emu/build_gcemu.sh` (Mesa relié en statique, armgl de ce dépôt compilé à part) ; `build_gxes_host.sh` et `gxes_host` : gxes contre `gxgl.cpp` d'Onyx, sur le même llvmpipe, chaque draw allant aux deux ; `check_gc.sh` : `gx_asm.asm` contre son C++ sous `qemu-aarch64` ; `gc_testimage.py` : carte d'essai jetable de 4 Go ; `gcgl` : gcemu sans EmuKit, pour mesurer sur le Pi sans changer le système (`--shaders fichier.gxs` : chaque configuration par Mesa). `C:\Temp\ARMPI\SD1\Roms` a les jeux de l'utilisateur.

## ROM d'essai

Aucune ROM commerciale, aucun BIOS ni firmware n'entre au dépôt ni sur la carte du Pi. `tools/emu/romgen.py` assemble de
petits programmes et des suites d'instructions au hasard, libres puisqu'à nous ; `gfx3d.nds` et ses voisines sont les ROM
d'essai d'Onyx (hors dépôt). Les jeux de `C:\Temp\Roms` sont ceux de l'utilisateur, utilisés à sa demande sur des copies.
