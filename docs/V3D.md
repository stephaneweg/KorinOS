# V3D — le GPU 3D du Raspberry Pi 4 dans ARMPI

Depuis le 30/10/2026, le GPU est **hors du noyau** (décision de l'utilisateur) :

* le **service `v3d`** (`src/svc/v3d`, `SD0:/sys/svc/v3d`, lancé par init2 depuis
  `services.txt`) possède le GPU : mise en route (firmware : domaine 10 et horloge 5 au
  maximum ; reset ; ponts AXI), puis les jobs des applications l'un après l'autre (binning puis
  rendu, fin par scrutation des compteurs BFC / RFC, délai de 500 ms, pool de débordement du
  binner) ; un job qui ne finit pas laisse le GPU éteint jusqu'au redémarrage ;
* **GpuKit** (`src/kits/gpukit`, `SD0:/lib/gpukit.so`) fait tout le reste **dans le process de
  l'application** : un tampon de travail par process (tampon partagé sous 1 Go : état permanent,
  shaders, listes, sommets, état et allocation des tuiles), les listes de commandes avec les
  adresses physiques, le cache du processeur (`DC CVAC` / `CIVAC` en EL0), et le **repli sur le
  CPU** (pas de service : QEMU ou `no_gpu` ; GPU éteint ; cible hors d'un tampon partagé du
  premier Go ; plus de 2048 × 2048) : l'appelant n'a jamais rien à refaire ;
* `gpuinfo` : l'état du service, les jobs faits, les remises à zéro, la température, les fréquences et les alertes du firmware (`V3D_OP_HEALTH`) ;
* job sans fin : `V3D_E_LOST` après 500 ms, puis remise à zéro du GPU (ponts AXI, reset, domaine, `power_on`) ; le job suivant repart. Après 3 blocages sans job réussi entre eux, le GPU reste éteint jusqu'au redémarrage. `gpuhang` provoque un blocage pour l'essayer ;
* protocole : `src/inc/v3d.inc` (`V3D_OP_INFO`, `V3D_OP_RUN` : un message de 512 o avec le numéro
  du tampon de travail et les zones des listes ; le service vérifie que le tampon est à
  l'appelant — pid en +44 du message — et que chaque zone y tient) ;
* noyau : rien que **`BUF_INFO`** (0x0D : adresse physique, taille, créateur d'un tampon
  partagé ; un tampon partagé est d'un seul bloc de pages, sous 1 Go avec `BUF_LOW`). Le GPU
  n'ayant pas de MMU ici, il ne lit et n'écrit que des tampons partagés.

Coût d'un job vu de l'application : la construction des listes (quelques µs) + un aller-retour
vers le service (~6 µs, `svctest`) + le GPU.

| GpuKit | Entrées | Sortie |
|---|---|---|
| `gk_info` | tampon `GKI_SIZE` ou 0 | 1 GPU, 0 CPU ; état du service, révision, jobs, durée du dernier job |
| `gk_alloc` / `gk_free` | octets (64 Mo au plus) / adresse | tampon partagé sous 1 Go (à zéro) que le GPU atteint |
| `gk_fill` | pixels, pas, x, y, l, h, couleur 0x00RRGGBB | 1 GPU, 0 CPU (fait quand même) |
| `gk_copy` | source, pas, destination, pas, l, h | 1 GPU, 0 CPU |
| `gk_demo_triangle` | pixels, pas, l, h | triangle Gouraud R/V/B sur fond bleu nuit ; 0 sans GPU |
| `gk_texture` | texture (< 0 : nouvelle), pixels 0xAARRGGBB (0 : libérer), l, h (≤ 2048), pas en pixels | numéro, `GK_E_*` |
| `gk_texture_rect` | texture, x, y, l, h, pixels, pas | 0 : ce rectangle remplacé, rien d'autre renvoyé au GPU |
| `gk_render` | cible `GKF_*`, sommets `GKV_*`, n, lots `GKB_*`, n | 0 : pixels dans la cible au retour, `GK_E_*` |
| `gk_debug` | `GK_DBG_DRY` / `LISTS` / `TEX` | essais : listes à blanc, listes du dernier job, texels |
| `gk_draw` | cible `GKF_*`, sommets `GKV_*`, n, lots `GKB_*`, n, textures `GKT_*` | transforme, découpe, GPU sinon CPU : 1 GPU, 0 CPU, `GK_E_*` |
| `gk_texture_info` | texture, sortie 48 o | 0 : adresse physique, octets, `TL_*`, l, h, adresse ; pour rendre dedans avec Mesa (docs/MESA.md) |

### Textures et rendu (30/10/2026)

`gk_texture`, `gk_texture_rect` et `gk_render` sont kapi `gpu_texture`, `gpu_texture_rect` et
`gpu_render` d'Onyx (paliers v53–v72, `kernel/sys/v3d.cpp`) portés côté application : **mêmes
structures, drapeaux et codes d'erreur** (`gpukit.inc` : sommet de 32 o, lot de 80 o avec sa
matrice 4 × 4, cible avec pas en pixels, `GK_F_KEEP`, `GK_F_ALPHA`, mélanges 0–12 dont les
préréglages de composition MULCOL … DSTOUT), pour que gpucomp s'y porte tel quel.

* **Sans GPU, pas de repli** : `GK_E_NOGPU` (-1) ; c'est à l'appelant (gpucomp a son chemin
  CPU). `GK_E_LOST` (-3) : le GPU s'est arrêté pendant l'image, le CPU désormais.
* **Textures** : texels RGBA8 rangés comme le TMU lit le niveau 0 (LT, UBLINEAR 1 / 2 colonnes,
  UIF avec `ub_pad` et XOR : `v3d_tiling.h` d'Onyx). Elles vivent dans des **morceaux** de
  4 Mo (plus pour une grande texture) : tampons partagés sous 1 Go, pages de 4 Ko distribuées
  par une table de bits, car il n'y a que 128 tampons partagés dans tout le système. Leur
  `TEXTURE_SHADER_STATE` est gardé dans la fiche et copié dans la liste indirecte, juste après
  le sampler, à chaque lot qui les lit. 512 textures par process.
* **Une image** : jusqu'à 1024 lots et 196 608 sommets (comme Onyx ; Cosmo Zoom! en envoie 180 000) ; par lot, uniforms (matrice, échelles),
  sampler, état de texture et shader state record (4 attributs) dans la liste indirecte ;
  `CFG_BITS`, mélange (s'il change), `GL_SHADER_STATE`, `VERTEX_ARRAY_PRIMS` dans le binning.
  Shaders `VS_CLIP`, `CS_CLIP`, `FS_COLOR`, `FS_TEX` et leurs variantes `_AT` (test d'alpha),
  écrits à la main pour Onyx (`v3d_shaders.qasm`).
* **Cible** : directement si elle est dans un tampon partagé du premier Go, sinon par un
  **tampon d'étape** du process (recopié avant avec `GK_F_KEEP`, puis après).
* **Pas de découpe sur le processeur** (Onyx découpe) : un sommet hors de la bande de garde
  après la matrice (w ≤ 0, |x| ou |y| > 4 w, z < -w) fait refuser l'image (`GK_E_INVAL`).
  Des coordonnées extrêmes bloquent le binner. gpucomp découpe déjà ses quads à la cible.

### gk_draw : une image 3D sans s'occuper du reste (31/10/2026)

`gk_render` veut des sommets dans la bande de garde et s'arrête sans GPU. `gk_draw` (`draw.asm`) fait le reste sur le
processeur, en assembleur :

* chaque triangle transformé par la matrice de son lot, rejeté s'il sort de l'image d'un même côté, découpé contre le plan
  proche et la bande de garde (recette de `v3d_clip.h` d'Onyx) ; 196 608 sommets en sortie au plus, le reste est laissé ;
* l'image donnée à `gk_render`, si toutes les textures du cadre y sont ;
* sinon, ou si `gk_render` échoue, le **rastériseur du processeur** : par lignes, avec les gradients du triangle ; une
  boucle à part pour les triangles unis et opaques, l'essentiel d'un décor en low poly ; une division par pixel seulement si
  la couleur ou la texture varient ; tests de profondeur, faces, test d'alpha, filtrage, répétition des textures. Mélanges 0
  à 4 ; au-delà, opaque. `GK_F_ALPHA` n'y est pas gérée.

Le lot y désigne une fiche `GKT_*` (pixels, taille, numéro `gk_texture`) d'une table de l'appelant : le CPU lit les pixels, le
GPU prend le numéro ; un numéro négatif envoie l'image au CPU.

Les tampons sont réservés au premier appel, en mémoire à la demande : sommets découpés (6,3 Mo), lots, profondeur.

Vérifié (Cosmo Zoom!, `src/apps/cosmozoom/README.md`) : le rastériseur contre `bas3d.h` d'Onyx sur de vraies images, et le
chemin vers `gk_render` en `GK_DBG_DRY`. **Le rendu par le GPU n'a jamais tourné sur le Pi.**

**Vérification sans GPU** : `gputest lists` (mode `GK_DBG_DRY` : les listes sont construites,
rien n'est envoyé, même sous QEMU) affiche les hash des texels de 7 textures (un cas par
rangement) et les listes de trois images (cinq lots, test d'alpha, matrice, faces, profondeur
GREATER ; `KEEP` + `ALPHA` au milieu d'un tampon ; tampon d'étape). `tools/v3d_ref/gk_ref.cpp`
les reconstruit avec les paquets et le rangement d'Onyx :
**identiques octet pour octet** (30/10/2026, QEMU).

```
g++ -std=c++20 -I. -Istub gk_ref.cpp -o gk_ref
./gk_ref < sortie_de_gputest_lists.txt | diff - attendu
```

Essai : `gputest` (remplissage, rectangle à x impair, copie, triangle, repli CPU ; puis
texture 64 × 64 recopiée au texel près, mélange prémultiplié sur du rouge, `gk_texture_rect`,
tampon d'étape, sommet refusé ; pixels vérifiés). Sous QEMU : tout par le CPU, et
`GK_E_NOGPU` pour les textures, PASS. **Sur le PocketTerm : tout PASS par le GPU** (30/10/2026 :
plein écran 250 µs, image texturée 64 × 64 en 22 µs, texture exacte au texel près). Jet y compose
par le GPU (`backend GPU`, première image en 1 ms).

Mesa utilise le même service par `V3D_OP_SUBMIT` (adresses physiques, sans passer par GpuKit), ses identités dans `V3D_OP_INFO` et des tampons `BUF_NOCACHE` : [MESA.md](MESA.md).

Suite (docs/FEUILLE-DE-ROUTE.md) : gpucomp sur GpuKit (remplacer les `gpc_*` factices de
`tools/webkit/glue/webkit2_armpi.c`), puis Jet relié avec la composition par le GPU (aucune
reconstruction de WebKit).

## L'ancien pilote du noyau (retiré le 30/10/2026)

Ce qui suit décrit `arch/pi4/v3d.asm`, le pilote du noyau d'avant ; ses listes de commandes
sont reprises telles quelles par GpuKit (mêmes octets), sa mise en route par le service.

## API noyau (ancien)

| Fonction | Entrées | Sortie |
|---|---|---|
| `v3d_init` | — | x0 = 1 si le GPU est prêt (0 sous QEMU ou en cas d'échec, avec message console) |
| `v3d_fill_rect` | x0 destination (phys.), x1 pas en octets, x2 x, x3 y, x4 l, x5 h, w6 couleur 0x00RRGGBB | x0 = 1 si fait par le GPU |
| `v3d_blit_alpha` | x0 source, x1 pas source, x2 destination, x3 pas dest., x4 l, x5 h, x6 alpha 0..255 | x0 = 1 si fait ; **alpha < 255 → 0** (composer sur le CPU) |
| `v3d_demo_triangle` | x0 destination, x1 pas, x2 l, x3 h | triangle Gouraud R/V/B sur fond bleu nuit |

* Les entrées sont limitées à 2048×2048. Un seul job à la fois (`v3d_mutex`). La fonction
  bloque l'appelant (scrutation) jusqu'à la fin du job, avec un délai de 500 ms.
* Après un délai dépassé, l'état des CLE est affiché et **le GPU reste éteint jusqu'au
  redémarrage** (comme Onyx). Tous les appels renvoient alors 0 et le CPU prend le relais.
* Aucun flottant n'est utilisé : on peut l'appeler depuis tout thread noyau.
* Les cibles sont « directes » : les tuiles sont écrites en raster RGBA8 avec R/B échangés,
  donc la mémoire contient 0x00RRGGBB, et l'alpha est mis à 0 par le clear. La
  maintenance des caches CPU (nettoyage et invalidation sur la cible et la source, avant et
  après) est faite par le pilote.

## Séquence de mise en route

1. Détection de QEMU : `mbox_get_fw_rev` < 0x50000000 → aucun accès matériel.
2. Firmware : `SET_DOMAIN_STATE` (0x38030) domaine 10 actif, `SET_CLOCK_STATE` (0x38001)
   horloge 5 active, `GET_MAX_CLOCK_RATE` (0x30004) puis `SET_CLOCK_RATE` (0x38002) au maximum.
3. `PM_GRAFX` (0xFE10010C) : mot de passe 0x5A000000, 24 bits bas conservés, `V3DRSTN`
   (bit 6) pour sortir du reset.
4. Ponts AXI : `ASB_V3D_M_CTRL` (0xFEC1100C) puis `ASB_V3D_S_CTRL` (0xFEC11008). On efface
   `REQ_STOP` (bit 0) et on attend que `ACK` (bit 1) tombe, en 100 ms au plus.
5. `HUB_IDENT1` (0xFEC0000C) bits 3:0 = 4, sinon abandon.
6. Tampons pmm : BCL 16 Ko, RCL 64 Ko, liste indirecte 16 Ko, état 16 Ko, sommets 64 Ko,
   allocation des tuiles 2 Mo + 64 Ko, état des tuiles 256 Ko, débordement 16 × 256 Ko.
7. État permanent : 64 valeurs d'attributs par défaut ((0,0,0,1) × 16), puis les 3 shaders
   Mesa (fragment +256, vertex +512, coordonnées +1024), nettoyés vers la RAM.
8. Interruptions : hub entièrement masqué. Côté cœur, tout est masqué sauf FRDONE, FLDONE
   et OUTOMEM, qui sont effacés puis démasqués. La ligne GIC SPI 74 n'est jamais activée :
   on fonctionne par **scrutation**.

## Un job

```
listes, sommets -> RAM (DC CIVAC) ; DSB
vider les caches GPU : L2TFLSTA=0, L2TFLEND=~0, L2TCACTL=1, SLCACTL=~0
INT_CLR = INT_MSK_CLR = 7 ; bfc = BFC & 0xFF
CT0QMA = allocation tuiles ; CT0QMS = ((tuiles*64+4095)&~4095)+2 Mo ; CT0QTS = état|2
CT0QBA = BCL ; CT0QEA = BCL+taille           -> binner
attendre BFC != bfc (OUTOMEM -> PTB_BPOA/BPOS : bloc de 256 Ko du pool)
vider les caches GPU ; rfc = RFC & 0xFF
CT1QBA = RCL ; CT1QEA = RCL+taille           -> rendu
attendre RFC != rfc
```

* **BCL** : TILE_BINNING_MODE_CFG, FLUSH_VCD_CACHE, OCCLUSION_QUERY_COUNTER, START_TILE_BINNING,
  CLIP_WINDOW, CFG_BITS (LESS + écriture Z), POINT_SIZE, LINE_WIDTH, CLIPPER_XY_SCALING
  (1/256 px), Z scale/offset, Z min/max, VIEWPORT_OFFSET, COLOR_WRITE_MASKS 0x8, BLEND_CONSTANT,
  les trois ZERO_ALL_*, TRANSFORM_FEEDBACK_SPECS, SAMPLE_STATE, VCM_CACHE_SIZE, puis
  [GL_SHADER_STATE + VERTEX_ARRAY_PRIMS seulement s'il y a des sommets], et enfin FLUSH.
* **RCL** : CFG_COMMON, CLEAR_COLORS, CFG_COLOR, ZS_CLEAR, TILE_LIST_INITIAL_BLOCK_SIZE,
  TILE_LIST_SET_BASE et SUPERTILE_CFG (moins de 256 trames). Viennent ensuite deux tuiles
  factices (0,0) clear + store NONE (contournement d'une course matérielle, recette Mesa),
  FLUSH_VCD_CACHE, START_ADDRESS_OF_GENERIC_TILE_LIST, une SUPERTILE_COORDINATES par
  supertuile et END_OF_RENDERING.
* **Liste générique par tuile** (dans la liste indirecte) : TILE_COORDINATES_IMPLICIT, puis
  [LOAD raster de la source pour une copie], END_OF_LOADS, PRIM_LIST_FORMAT,
  BRANCH_TO_IMPLICIT_TILE_LIST, STORE raster, CLEAR_TILE_BUFFERS, END_OF_TILE_MARKER et
  RETURN_FROM_SUB_LIST.

### Écarts volontaires par rapport à Onyx

1. **Job sans sommets** (remplissage, copie) : ni uniforms, ni shader state, ni
   VERTEX_ARRAY_PRIMS, comme les « clears » de Mesa. Onyx émet toujours un draw, y compris
   de longueur 0.
2. **Copie** : chargement depuis une **source distincte** (même paquet LOAD que
   `V3dLoadTarget`, même disposition de bits, vérifiée par la référence). Onyx recharge la
   cible elle-même.

## Ce qu'il faut vérifier sur le vrai Pi 4

1. Le message `GPU : V3D 4.2 prêt` au démarrage, et la révision affichée.
2. `v3d_demo_triangle` sur une fenêtre (adresse physique de la surface `W_SURF` d'une
   fenêtre de `gui.asm`) : triangle visible, couleurs R/V/B correctes, sans inversion R/B.
3. `v3d_fill_rect` au milieu d'une surface avec un pas plus grand que 4×largeur. Les
   cibles décalées (x quelconque, adresse alignée seulement sur 4 octets) sont acceptées
   par Onyx pour des canevas, mais **un x impair doit être vérifié** (alignement minimal
   d'un STORE raster).
4. La copie : le LOAD raster d'une source avec un pas différent de celui de la cible.
5. Que l'horloge V3D maximale (GET_MAX_CLOCK_RATE) ne pose pas de problème d'alimentation
   ou de chauffe. Onyx l'utilise.
6. Le temps d'un job : environ 1 ms attendu pour un triangle plein écran. Un délai dépassé
   signale un problème de liste ; voir le message des CLE.

## Intégration (non faite dans `kernel.asm`)

Dans `src/kernel/kernel.asm` :

```asm
include 'audio.asm'
include 'v3d.asm'            ; APRES audio.asm (utilise mbox_get_fw_rev) et sd.asm (delay)
...
	bl	gui_init
	bl	v3d_init             ; GPU V3D (absent sous QEMU : message et retour 0)
```

Pas de BSS à ajouter : l'état est dans le module. Avec `V3D_SELFTEST = 1` défini avant
l'include, `v3d_selftest` (à appeler après `v3d_init`) vide les listes de 4 jobs sur la
console (lignes `V3DT`), pour comparaison avec `v3d_ref.cpp`.

### Pour le compositeur (`gui.asm`), plus tard

* `bb_fill` d'un grand rectangle (fond, barre des tâches) : `v3d_fill_rect`, avec
  destination = tampon arrière (adresse physique), pas = `SCR_PITCH`. Repli CPU si 0.
* Recopie du tampon arrière vers le framebuffer (`gui_flush`) : impossible directement, car
  le framebuffer est dans la mémoire VideoCore au-delà de la RAM ARM, une zone non vérifiée
  pour le V3D. À tester, sinon garder le CPU ou le DMA.
* `bb_blit` d'une surface opaque : `v3d_blit_alpha` avec alpha = 255.
* Pour la vraie composition alpha (opacité, ombres), il faudra porter les shaders `FS_TEX`
  d'Onyx et le retuilage UIF des textures (`v3d_tiling.h`, environ 500 lignes) : c'est le
  palier 3c.

### Appels système proposés (non branchés)

| N° | Nom | Arguments | Résultat |
|---|---|---|---|
| 70 | gpu_info | tampon {état, version, révision} | 0 |
| 71 | gpu_fill | poignée de fenêtre, x, y, l, h, couleur | 1 GPU / 0 à faire par l'app |
| 72 | gpu_blit | poignée source (fenêtre), poignée destination, x, y, l, h | 1 / 0 |

Ils travailleraient sur les surfaces de fenêtres, qui sont physiquement contiguës et sous
1 Go (`W_SURF`), en vérifiant les bornes. Le noyau ne doit jamais accepter d'adresse
physique venant d'une application.

## Shaders

Les trois shaders QPU (fragment couleur, vertex, coordonnées) sont le code Mesa compilé
depuis GLSL du projet *rpi4-gpu-bare-metal-examples* (Random06457, licence MIT), repris
tels quels par Onyx (« v52 ») puis par ARMPI en données `dd` (64 bits).
