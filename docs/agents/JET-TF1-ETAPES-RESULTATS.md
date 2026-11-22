# Résultats : les étapes de l'affichage de TF1+ avec jet 1.4.6 (pour la session du cloud)

Réponse à `docs/agents/JET-TF1-ETAPES.md`. Mesuré le 22/11/2026 sur le ZBook, **jet 1.4.6** (lignes `steps:` du
patch 0014), app `tf1plus` 1.0.1, fenêtre 1000 × 666, sonde de défilement automatique de
`JET-TF1-DEFILEMENT.md` (`scrollBy(0, 60)` toutes les 100 ms à partir de 10 s), plus **un saut à `scrollY` 7000 à
45 s** pour avoir des fenêtres du bas de la page. Sortie de l'app par un tube ; les lignes `steps:` arrivent **au
journal** (pas sur la sortie de l'app), relevé toutes les 3 s. Relevés bruts sur la machine : `~/jet-mesures/9/`.

## En bref

- **L'étape coupable est `other`** : ce qu'aucune étape instrumentée ne couvre. Dans les fenêtres lentes, les mises
  à jour lourdes passent **≈ 400 à 420 ms dans `other`**, à chaque fois la même valeur (`worst` = 403 à 485 ms
  quand il n'y a pas de `layout` dans la fenêtre) ; dans les fenêtres rapides, `other` vaut ≈ 0.
- En plus, **une fenêtre lente sur deux** a un `layout` de **≈ 365 à 425 ms** (une seule mise à jour), même valeur
  à chaque fois (pas tranché : peut-être la minuterie du site de ≈ 400 ms vue par la sonde, qui modifierait la page).
- **Le suspect n° 1 est innocent** : `compositing layout` (la mise à jour des couches composées) vaut 0 à 2 ms par
  fenêtre, `layer flush` 0, `IntersectionObservations` 1 à 14 ms, `AnimationFrameCallbacks` 0 à 1 ms,
  `UpdateContentRelevancy` 0 dans les fenêtres lentes.
- **Ce n'est pas une attente** : pendant le défilement lent, le fil principal du process web consomme **5,9 s de
  processeur en 6 s** (`cpuwatch` : 18 015 → 23 952 ms) — c'est du calcul.
- **Sans GPU (`--nogpu`)**, même étape coupable (`other` ≈ 400 ms par mise à jour lourde) ; mais le bas de la page
  (après le saut, et par moments vers `scrollY` 2400-3100) redevient fluide : 40 à 63 images/s, `other` ≈ 0.
  **Avec le GPU, le bas de la page reste lent** (1 à 1,5 image/s jusqu'à `scrollY` 10 000) : là, le GPU change
  quelque chose (pas dans `layer flush` ni `compositing layout`).

## Les fenêtres (moyennes par mise à jour)

« Lentes » : fenêtres de 2 s pendant le défilement automatique avec au plus 8 mises à jour et plus de 400 ms au
total ; « rapides » : au moins 100 mises à jour et `worst` > 8 ms (défilement fluide) ; « repos » : au moins 240
mises à jour, `worst` ≤ 10 ms (en bas, défilement fini).

| Passage | Fenêtres | Mises à jour / fenêtre | ms / mise à jour | worst moyen | other | layout | IntersectionObservations | MediaQueryEvaluation | compositing layout | layer flush | UpdateContentRelevancy |
|---|---|---|---|---|---|---|---|---|---|---|---|
| GPU, lentes | 26 | 5,4 | 229,9 | 529 ms | **200,2** | 27,4 | 1,1 | 0,6 | 0,1 | 0 | 0 |
| GPU, rapides | 0 | — | — | — | — | — | — | — | — | — | — |
| `--nogpu`, lentes | 11 | 6,0 | 169,7 | 620 ms | **126,9** | 40,9 | 0,8 | 0,7 | 0 | 0 | 0 |
| `--nogpu`, rapides | 10 | 196,6 | 1,7 | 62 ms | 0,2 | 0 | 0,3 | 0,6 | 0 | 0 | 0,4 |
| `--nogpu`, repos | 10 | 250,4 | 0,8 | 5 ms | 0,1 | 0 | 0,2 | 0,5 | 0 | 0 | 0 |

Les moyennes par mise à jour cachent la forme : dans une fenêtre lente, 2 à 3 mises à jour lourdes (≈ 400 ms
d'`other` chacune, plus parfois un `layout` de ≈ 370 ms) et quelques mises à jour presque vides. Exemples (GPU,
défilement du haut de la page) :

```
4 updates,  764 ms (worst 388 ms); layout 0,   compositing layout 0, after 0, layer flush 0, other 759, MediaQueryEvaluation 2, IntersectionObservations 1
4 updates, 1198 ms (worst 793 ms); layout 375, compositing layout 0, after 0, layer flush 0, other 807, IntersectionObservations 12, MediaQueryEvaluation 2
6 updates, 1248 ms (worst 427 ms); layout 1,   compositing layout 0, after 0, layer flush 0, other 1232, IntersectionObservations 7, MediaQueryEvaluation 5
```

et après le saut à 7000 (GPU, toujours lent) :

```
6 updates, 1240 ms (worst 425 ms); layout 4,   compositing layout 1, after 0, layer flush 0, other 1220, IntersectionObservations 7, MediaQueryEvaluation 4, Animations 1
6 updates, 2386 ms (worst 801 ms); layout 765, compositing layout 0, after 0, layer flush 0, other 1606, IntersectionObservations 9, MediaQueryEvaluation 4
```

## L'étape coupable

**`other`** : ≈ 400 ms par mise à jour lourde dans les fenêtres lentes (GPU et sans GPU), ≈ 0 dans les rapides. Ce
qu'elle contient n'est pas instrumenté : la prochaine construction doit découper ce qui, dans
`Page::updateRendering` (ou autour, dans le même chronomètre), n'est ni `layout`, ni `compositing layout`, ni
`after`, ni `layer flush`, ni une étape `RenderingUpdateStep`. Pistes, puisque la mise en page forcée par la sonde
après chaque pas coûte 0 à 3 ms (`JET-TF1-DEFILEMENT.md`) :

- le **style** (recalcul des styles) s'il est compté hors de `layout` ;
- les **événements de défilement** et le travail de `FrameView` sur un changement de position (`scrollPositionChanged`,
  `updateWidgetPositions`, les positions des `fixed`/`sticky`, l'invalidation pour repeinture), s'ils tombent dans
  le chronomètre ;
- la **repeinture** calculée côté WebCore (`RenderView::repaint`, régions sales sur ≈ 12 000 éléments), ou
  l'accessibilité si elle est active.

La valeur presque constante (≈ 400-420 ms) laisse penser à un parcours de toute la page (ou d'une grande liste)
à chaque mise à jour déclenchée par le défilement, plutôt qu'à un travail proportionnel à ce qui change.

Le `layout` de ≈ 370 ms une fenêtre sur deux est un second coupable, plus petit : à découper aussi (style ou
layout, qui l'a invalidé).

## Sans GPU

Même étape coupable (`other` ≈ 400 ms par mise à jour lourde, `layout` ≈ 365-425 ms une fenêtre sur deux) dans le
haut de la page. Différence : **sans GPU, le bas de la page est fluide** (après le saut : 170 à 250 mises à jour
par fenêtre, 1,3 à 2 ms chacune, 40 à 63 images/s), **avec le GPU il reste lent** (6 mises à jour par fenêtre,
`other` ≈ 1200 ms par fenêtre). Le GPU ajoute donc quelque chose qui garde `other` lourd sur toute la page, alors
que `layer flush` et `compositing layout` restent à ≈ 0 : à regarder dans ce que le mode composé fait de plus
pendant `updateRendering` (couches à repeindre, invalidations par couche, `RenderLayerBacking`).

## Annexe

### Lignes brutes `steps:` (horodatage du journal) et bilans de la sonde, passage GPU

```
6118.0 sonde t=2.3 s : 7.0 im/s, JS bloqué 1672 ms, scrollY 0
6118.0 steps: 30 updates, 282 ms (worst 263 ms); layout 243, compositing layout 1, after 0, layer flush 0, other 10, ResizeObservations 17, IntersectionObservations 7
6120.3 steps: 24 updates, 7 ms (worst 1 ms); layout 7, compositing layout 0, after 0, layer flush 0, other -15, IntersectionObservations 12, MediaQueryEvaluation 1
6120.9 sonde t=5.2 s : 6.5 im/s, JS bloqué 2510 ms, scrollY 0
6122.7 steps: 8 updates, 370 ms (worst 342 ms); layout 344, compositing layout 1, after 0, layer flush 0, other 15, MediaQueryEvaluation 5, IntersectionObservations 2
6122.9 sonde t=7.2 s : 5.0 im/s, JS bloqué 1664 ms, scrollY 0
6124.9 sonde t=9.2 s : 1.5 im/s, JS bloqué 1876 ms, scrollY 0
6125.5 steps: 20 updates, 51 ms (worst 23 ms); layout 28, compositing layout 1, after 0, layer flush 0, other -1, MediaQueryEvaluation 13, IntersectionObservations 7
6126.1 sonde tf1 10.3 s: DEFILE début du défilement automatique (10 s)
6127.9 sonde t=12.2 s : 1.5 im/s, JS bloqué 2030 ms, scrollY 120
6128.3 steps: 6 updates, 410 ms (worst 390 ms); layout 1, compositing layout 1, after 0, layer flush 0, other 396, MediaQueryEvaluation 4, IntersectionObservations 2, Animations 2, Scroll 1
6130.5 sonde t=14.8 s : 1.0 im/s, JS bloqué 2874 ms, scrollY 240
6131.0 steps: 4 updates, 764 ms (worst 388 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 759, MediaQueryEvaluation 2, IntersectionObservations 1
6133.1 sonde t=17.4 s : 1.0 im/s, JS bloqué 2887 ms, scrollY 360
6133.9 steps: 4 updates, 883 ms (worst 444 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 869, IntersectionObservations 9, MediaQueryEvaluation 2, Animations 1
6136.0 sonde t=20.2 s : 1.0 im/s, JS bloqué 2781 ms, scrollY 480
6136.8 steps: 4 updates, 821 ms (worst 422 ms); layout 0, compositing layout 2, after 0, layer flush 0, other 813, MediaQueryEvaluation 2, IntersectionObservations 1
6138.8 sonde t=23.1 s : 1.0 im/s, JS bloqué 2345 ms, scrollY 600
6139.3 steps: 4 updates, 1198 ms (worst 793 ms); layout 375, compositing layout 0, after 0, layer flush 0, other 807, IntersectionObservations 12, MediaQueryEvaluation 2
6141.6 sonde t=25.8 s : 1.0 im/s, JS bloqué 2624 ms, scrollY 720
6142.0 steps: 4 updates, 870 ms (worst 457 ms); layout 5, compositing layout 0, after 0, layer flush 0, other 859, MediaQueryEvaluation 2, IntersectionObservations 1
6144.3 sonde t=28.6 s : 1.0 im/s, JS bloqué 2677 ms, scrollY 840
6144.7 steps: 4 updates, 1194 ms (worst 781 ms); layout 367, compositing layout 0, after 0, layer flush 0, other 822, MediaQueryEvaluation 2, IntersectionObservations 1
6147.0 sonde t=31.3 s : 1.0 im/s, JS bloqué 2633 ms, scrollY 960
6147.5 steps: 4 updates, 1195 ms (worst 786 ms); layout 371, compositing layout 0, after 0, layer flush 0, other 813, IntersectionObservations 6, MediaQueryEvaluation 2
6149.3 sonde t=33.6 s : 1.0 im/s, JS bloqué 2133 ms, scrollY 1080
6149.7 steps: 4 updates, 830 ms (worst 422 ms); layout 3, compositing layout 0, after 0, layer flush 0, other 821, MediaQueryEvaluation 2, IntersectionObservations 1
6151.4 sonde t=35.7 s : 1.5 im/s, JS bloqué 2381 ms, scrollY 1260
6152.2 steps: 6 updates, 1966 ms (worst 788 ms); layout 751, compositing layout 0, after 0, layer flush 0, other 1202, IntersectionObservations 6, MediaQueryEvaluation 3, Scroll 1
6153.6 sonde t=37.9 s : 1.0 im/s, JS bloqué 2002 ms, scrollY 1440
6154.9 steps: 6 updates, 1241 ms (worst 420 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1234, MediaQueryEvaluation 3, IntersectionObservations 2
6156.2 sonde t=40.4 s : 1.5 im/s, JS bloqué 1909 ms, scrollY 1740
6157.4 steps: 6 updates, 1250 ms (worst 430 ms); layout 3, compositing layout 0, after 0, layer flush 0, other 1232, IntersectionObservations 8, MediaQueryEvaluation 3, Animations 1
6158.7 sonde t=42.9 s : 1.5 im/s, JS bloqué 2318 ms, scrollY 1980
6159.9 steps: 6 updates, 1248 ms (worst 427 ms); layout 1, compositing layout 0, after 0, layer flush 0, other 1232, IntersectionObservations 7, MediaQueryEvaluation 5
6161.3 sonde t=45.5 s : 1.5 im/s, JS bloqué 2275 ms, scrollY 2340
6161.3 sonde tf1 45.5 s: DEFILE saut à scrollY 7000
6162.6 steps: 6 updates, 1282 ms (worst 467 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1262, IntersectionObservations 14, MediaQueryEvaluation 3
6163.9 sonde t=48.2 s : 1.5 im/s, JS bloqué 2434 ms, scrollY 7180
6165.2 steps: 6 updates, 1240 ms (worst 425 ms); layout 4, compositing layout 1, after 0, layer flush 0, other 1220, IntersectionObservations 7, MediaQueryEvaluation 4, Animations 1
6165.6 sonde t=49.9 s : 1.0 im/s, JS bloqué 1871 ms, scrollY 7480
6167.7 steps: 6 updates, 1233 ms (worst 415 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1219, IntersectionObservations 8, MediaQueryEvaluation 3
6168.1 sonde t=52.4 s : 1.5 im/s, JS bloqué 1906 ms, scrollY 7720
6170.2 steps: 6 updates, 1204 ms (worst 403 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1189, IntersectionObservations 8, MediaQueryEvaluation 4
6170.6 sonde t=54.9 s : 1.5 im/s, JS bloqué 2266 ms, scrollY 8020
6172.8 steps: 6 updates, 1236 ms (worst 413 ms); layout 2, compositing layout 1, after 0, layer flush 0, other 1218, IntersectionObservations 7, MediaQueryEvaluation 4
6173.2 sonde t=57.5 s : 1.5 im/s, JS bloqué 2384 ms, scrollY 8260
6175.5 steps: 6 updates, 1799 ms (worst 881 ms); layout 410, compositing layout 0, after 0, layer flush 0, other 1381, MediaQueryEvaluation 4, IntersectionObservations 2
6175.6 sonde t=59.8 s : 1.5 im/s, JS bloqué 2557 ms, scrollY 8440
6177.6 sonde t=61.9 s : 1.0 im/s, JS bloqué 1947 ms, scrollY 8620
6178.0 steps: 6 updates, 1613 ms (worst 793 ms); layout 385, compositing layout 0, after 0, layer flush 0, other 1212, IntersectionObservations 7, MediaQueryEvaluation 3, Animations 1
6180.3 sonde t=64.5 s : 1.5 im/s, JS bloqué 2027 ms, scrollY 8860
6180.7 steps: 6 updates, 1288 ms (worst 471 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1277, IntersectionObservations 4, MediaQueryEvaluation 3
6182.9 sonde t=67.2 s : 1.5 im/s, JS bloqué 2414 ms, scrollY 9100
6183.4 steps: 6 updates, 1262 ms (worst 430 ms); layout 3, compositing layout 2, after 0, layer flush 0, other 1250, MediaQueryEvaluation 3, IntersectionObservations 2
6185.3 sonde t=69.5 s : 1.5 im/s, JS bloqué 2461 ms, scrollY 9400
6186.1 steps: 6 updates, 1319 ms (worst 485 ms); layout 389, compositing layout 1, after 0, layer flush 0, other 915, IntersectionObservations 7, MediaQueryEvaluation 3, Animations 1
6187.8 sonde t=72.1 s : 1.5 im/s, JS bloqué 2405 ms, scrollY 9580
6188.6 steps: 6 updates, 2386 ms (worst 801 ms); layout 765, compositing layout 0, after 0, layer flush 0, other 1606, IntersectionObservations 9, MediaQueryEvaluation 4
6189.9 sonde t=74.2 s : 1.0 im/s, JS bloqué 1885 ms, scrollY 9820
6191.2 steps: 6 updates, 1228 ms (worst 410 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1213, IntersectionObservations 8, MediaQueryEvaluation 4, Animations 1
6192.4 sonde t=76.7 s : 1.5 im/s, JS bloqué 2000 ms, scrollY 10000
6193.7 steps: 6 updates, 1230 ms (worst 416 ms); layout 1, compositing layout 2, after 0, layer flush 0, other 1213, IntersectionObservations 8, MediaQueryEvaluation 4
```

### Lignes `times:` et `frames (` du passage GPU

```
6117.914 2.4 s: 14 frames (0 without a page update), 0 video pictures, 24 tiles painted, 6 rects repainted, 5732394 px, paint 907 ms, upload 22 ms, composite 20 ms; 16 textures (8 MB), 12 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6117.914 times: 16 frames rendered: page update 282.2 ms, layer walk 937.3 ms (of it 19 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 6 frames: record 868.2 ms, raster 39.0 ms of wall time for 133.0 ms of core time in 62 jobs), composite 19.9 ms
6120.739 2.8 s: 1 frames (0 without a page update), 0 video pictures, 2 tiles painted, 9 rects repainted, 1580464 px, paint 1339 ms, upload 3 ms, composite 1 ms; 16 textures (8 MB), 12 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6120.739 times: 13 frames rendered: page update 25.9 ms, layer walk 1344.9 ms (of it 10 paints: direct 177.7 ms = setup 0.01 + WebCore 177.67 + end 0.00; on the cores, 2 frames: record 1154.6 ms, raster 6.5 ms of wall time for 14.1 ms of core time in 21 jobs), composite 1.5 ms
6122.740 2.0 s: 1 frames (0 without a page update), 0 video pictures, 0 tiles painted, 14 rects repainted, 3260170 px, paint 1077 ms, upload 5 ms, composite 2 ms; 16 textures (8 MB), 12 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6122.740 times: 7 frames rendered: page update 359.7 ms, layer walk 1086.0 ms (of it 14 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 1065.9 ms, raster 11.0 ms of wall time for 51.6 ms of core time in 33 jobs), composite 1.5 ms
6124.864 2.1 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 15 rects repainted, 3847028 px, paint 1288 ms, upload 13 ms, composite 3 ms; 14 textures (8 MB), 10 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6124.864 times: 6 frames rendered: page update 43.5 ms, layer walk 1306.9 ms (of it 15 paints: direct 0.7 ms = setup 0.03 + WebCore 0.62 + end 0.01; on the cores, 2 frames: record 1264.5 ms, raster 23.4 ms of wall time for 83.7 ms of core time in 33 jobs), composite 3.4 ms
6127.036 2.2 s: 3 frames (0 without a page update), 0 video pictures, 43 tiles painted, 10 rects repainted, 11727495 px, paint 669 ms, upload 38 ms, composite 10 ms; 59 textures (38 MB), 35 layers, 2 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6127.036 times: 3 frames rendered: page update 410.5 ms, layer walk 734.5 ms (of it 34 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 653.4 ms, raster 16.1 ms of wall time for 82.6 ms of core time in 114 jobs), composite 9.6 ms
6130.087 3.1 s: 2 frames (0 without a page update), 0 video pictures, 0 tiles painted, 12 rects repainted, 5852601 px, paint 837 ms, upload 11 ms, composite 4 ms; 35 textures (19 MB), 28 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6130.087 times: 2 frames rendered: page update 764.8 ms, layer walk 851.7 ms (of it 12 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 823.6 ms, raster 13.1 ms of wall time for 79.7 ms of core time in 56 jobs), composite 3.6 ms
6133.026 2.9 s: 2 frames (0 without a page update), 0 video pictures, 0 tiles painted, 10 rects repainted, 4260000 px, paint 1155 ms, upload 8 ms, composite 4 ms; 35 textures (19 MB), 28 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6133.026 times: 2 frames rendered: page update 883.4 ms, layer walk 1166.2 ms (of it 10 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 1145.0 ms, raster 9.7 ms of wall time for 60.6 ms of core time in 40 jobs), composite 3.8 ms
6135.925 2.9 s: 2 frames (0 without a page update), 0 video pictures, 34 tiles painted, 9 rects repainted, 11725856 px, paint 1135 ms, upload 29 ms, composite 40 ms; 21 textures (12 MB), 16 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6135.925 times: 2 frames rendered: page update 821.3 ms, layer walk 1170.7 ms (of it 30 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 1112.3 ms, raster 22.9 ms of wall time for 112.9 ms of core time in 105 jobs), composite 40.5 ms
6138.411 2.5 s: 2 frames (0 without a page update), 0 video pictures, 0 tiles painted, 6 rects repainted, 3897000 px, paint 820 ms, upload 7 ms, composite 3 ms; 21 textures (12 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6138.411 times: 2 frames rendered: page update 1198.6 ms, layer walk 828.6 ms (of it 6 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 796.8 ms, raster 23.0 ms of wall time for 113.5 ms of core time in 36 jobs), composite 2.9 ms
6141.136 2.7 s: 2 frames (0 without a page update), 0 video pictures, 2 tiles painted, 8 rects repainted, 4409000 px, paint 1038 ms, upload 8 ms, composite 3 ms; 23 textures (14 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6141.136 times: 2 frames rendered: page update 870.8 ms, layer walk 1048.9 ms (of it 9 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 1021.7 ms, raster 16.1 ms of wall time for 85.2 ms of core time in 40 jobs), composite 3.2 ms
6143.911 2.8 s: 2 frames (0 without a page update), 0 video pictures, 0 tiles painted, 12 rects repainted, 5012000 px, paint 1097 ms, upload 7 ms, composite 3 ms; 23 textures (14 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6143.911 times: 2 frames rendered: page update 1195.0 ms, layer walk 1108.3 ms (of it 12 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 1083.9 ms, raster 13.4 ms of wall time for 77.6 ms of core time in 42 jobs), composite 2.9 ms
6146.637 2.7 s: 2 frames (0 without a page update), 0 video pictures, 0 tiles painted, 12 rects repainted, 4533000 px, paint 1078 ms, upload 7 ms, composite 3 ms; 23 textures (14 MB), 16 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6146.637 times: 2 frames rendered: page update 1195.0 ms, layer walk 1088.5 ms (of it 12 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 1063.5 ms, raster 14.1 ms of wall time for 92.0 ms of core time in 42 jobs), composite 2.8 ms
6148.880 2.2 s: 2 frames (0 without a page update), 0 video pictures, 0 tiles painted, 10 rects repainted, 4409000 px, paint 548 ms, upload 7 ms, composite 3 ms; 23 textures (14 MB), 16 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6148.880 times: 2 frames rendered: page update 830.7 ms, layer walk 557.6 ms (of it 10 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 2 frames: record 534.9 ms, raster 13.0 ms of wall time for 66.5 ms of core time in 40 jobs), composite 3.5 ms
6151.401 2.5 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 21 rects repainted, 6718158 px, paint 36 ms, upload 10 ms, composite 4 ms; 21 textures (12 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6151.401 times: 3 frames rendered: page update 1966.3 ms, layer walk 49.2 ms (of it 21 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 13.0 ms, raster 22.6 ms of wall time for 111.3 ms of core time in 61 jobs), composite 3.8 ms
6154.018 2.6 s: 3 frames (0 without a page update), 0 video pictures, 2 tiles painted, 17 rects repainted, 6671000 px, paint 29 ms, upload 12 ms, composite 5 ms; 23 textures (14 MB), 16 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6154.018 times: 3 frames rendered: page update 1241.9 ms, layer walk 43.7 ms (of it 18 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 13.0 ms, raster 15.9 ms of wall time for 66.5 ms of core time in 60 jobs), composite 5.1 ms
6156.609 2.6 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 20 rects repainted, 5578836 px, paint 27 ms, upload 9 ms, composite 4 ms; 21 textures (12 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6156.609 times: 3 frames rendered: page update 1250.2 ms, layer walk 40.9 ms (of it 20 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 15.0 ms, raster 12.3 ms of wall time for 52.3 ms of core time in 55 jobs), composite 3.9 ms
6159.093 2.5 s: 3 frames (0 without a page update), 0 video pictures, 2 tiles painted, 15 rects repainted, 4220106 px, paint 18 ms, upload 7 ms, composite 4 ms; 23 textures (14 MB), 16 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6159.093 times: 3 frames rendered: page update 1248.2 ms, layer walk 28.8 ms (of it 16 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 10.7 ms, raster 6.9 ms of wall time for 25.1 ms of core time in 43 jobs), composite 4.2 ms
6161.680 2.6 s: 3 frames (0 without a page update), 0 video pictures, 8 tiles painted, 14 rects repainted, 6500106 px, paint 43 ms, upload 10 ms, composite 4 ms; 19 textures (10 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6161.680 times: 3 frames rendered: page update 1282.7 ms, layer walk 60.6 ms (of it 18 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 37.5 ms, raster 5.6 ms of wall time for 35.9 ms of core time in 59 jobs), composite 3.8 ms
6164.381 2.7 s: 3 frames (0 without a page update), 0 video pictures, 6 tiles painted, 14 rects repainted, 6412296 px, paint 47 ms, upload 11 ms, composite 4 ms; 25 textures (16 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6164.381 times: 3 frames rendered: page update 1240.6 ms, layer walk 69.8 ms (of it 17 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 31.9 ms, raster 15.4 ms of wall time for 79.1 ms of core time in 68 jobs), composite 3.9 ms
6166.892 2.5 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 21 rects repainted, 5098836 px, paint 42 ms, upload 8 ms, composite 4 ms; 23 textures (14 MB), 16 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6166.892 times: 3 frames rendered: page update 1233.5 ms, layer walk 54.5 ms (of it 21 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 32.7 ms, raster 9.3 ms of wall time for 46.1 ms of core time in 55 jobs), composite 4.0 ms
6169.400 2.5 s: 3 frames (0 without a page update), 0 video pictures, 2 tiles painted, 13 rects repainted, 4744994 px, paint 25 ms, upload 9 ms, composite 4 ms; 23 textures (14 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6169.400 times: 3 frames rendered: page update 1204.0 ms, layer walk 38.4 ms (of it 14 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 16.8 ms, raster 8.2 ms of wall time for 39.7 ms of core time in 47 jobs), composite 4.2 ms
6171.974 2.6 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 14 rects repainted, 4343000 px, paint 18 ms, upload 6 ms, composite 4 ms; 23 textures (14 MB), 16 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6171.974 times: 3 frames rendered: page update 1236.5 ms, layer walk 29.3 ms (of it 14 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 14.2 ms, raster 3.4 ms of wall time for 18.6 ms of core time in 40 jobs), composite 4.1 ms
6174.673 2.7 s: 3 frames (0 without a page update), 0 video pictures, 2 tiles painted, 11 rects repainted, 4142738 px, paint 32 ms, upload 8 ms, composite 4 ms; 25 textures (16 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6174.673 times: 3 frames rendered: page update 1799.8 ms, layer walk 42.5 ms (of it 12 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 25.8 ms, raster 5.8 ms of wall time for 28.5 ms of core time in 39 jobs), composite 4.1 ms
6177.181 2.5 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 12 rects repainted, 3262836 px, paint 25 ms, upload 6 ms, composite 4 ms; 23 textures (14 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6177.181 times: 3 frames rendered: page update 1613.2 ms, layer walk 31.6 ms (of it 12 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 16.6 ms, raster 8.1 ms of wall time for 28.3 ms of core time in 39 jobs), composite 3.9 ms
6179.857 2.7 s: 3 frames (0 without a page update), 0 video pictures, 2 tiles painted, 11 rects repainted, 4701568 px, paint 17 ms, upload 8 ms, composite 4 ms; 25 textures (16 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6179.857 times: 3 frames rendered: page update 1288.4 ms, layer walk 26.7 ms (of it 12 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 10.6 ms, raster 6.5 ms of wall time for 23.2 ms of core time in 44 jobs), composite 4.1 ms
6182.521 2.7 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 18 rects repainted, 4373090 px, paint 37 ms, upload 7 ms, composite 4 ms; 23 textures (14 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6182.521 times: 3 frames rendered: page update 1262.4 ms, layer walk 46.6 ms (of it 18 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 25.0 ms, raster 12.5 ms of wall time for 49.1 ms of core time in 45 jobs), composite 4.0 ms
6185.232 2.7 s: 3 frames (0 without a page update), 0 video pictures, 2 tiles painted, 15 rects repainted, 4525454 px, paint 26 ms, upload 7 ms, composite 4 ms; 25 textures (16 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6185.232 times: 3 frames rendered: page update 1319.7 ms, layer walk 38.3 ms (of it 16 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 20.2 ms, raster 5.4 ms of wall time for 28.7 ms of core time in 46 jobs), composite 4.0 ms
6187.775 2.5 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 20 rects repainted, 4153508 px, paint 72 ms, upload 6 ms, composite 4 ms; 23 textures (14 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6187.775 times: 3 frames rendered: page update 2386.2 ms, layer walk 83.1 ms (of it 20 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 59.8 ms, raster 12.4 ms of wall time for 57.2 ms of core time in 48 jobs), composite 3.9 ms
6190.308 2.5 s: 3 frames (0 without a page update), 0 video pictures, 2 tiles painted, 12 rects repainted, 4385432 px, paint 24 ms, upload 7 ms, composite 4 ms; 25 textures (16 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6190.308 times: 3 frames rendered: page update 1228.9 ms, layer walk 36.9 ms (of it 13 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 19.8 ms, raster 4.6 ms of wall time for 22.1 ms of core time in 41 jobs), composite 3.9 ms
6192.865 2.6 s: 3 frames (0 without a page update), 0 video pictures, 0 tiles painted, 15 rects repainted, 4262026 px, paint 22 ms, upload 6 ms, composite 4 ms; 23 textures (14 MB), 14 layers, 0 groups made (last frame: 1 GPU calls, 0 layers by the CPU)
6192.865 times: 3 frames rendered: page update 1230.6 ms, layer walk 32.6 ms (of it 15 paints: direct 0.0 ms = setup 0.00 + WebCore 0.00 + end 0.00; on the cores, 3 frames: record 16.0 ms, raster 6.0 ms of wall time for 26.2 ms of core time in 40 jobs), composite 4.2 ms
```

### Lignes brutes `steps:` et bilans de la sonde, passage `--nogpu`

```
6247.5 steps: 33 updates, 124 ms (worst 103 ms); layout 82, compositing layout 0, after 1, layer flush 0, other 12, ResizeObservations 14, IntersectionObservations 7, Animations 5
6248.0 sonde t=2.3 s : 10.5 im/s, JS bloqué 1614 ms, scrollY 0
6249.8 steps: 20 updates, 266 ms (worst 259 ms); layout 248, compositing layout 0, after 0, layer flush 0, other -1, IntersectionObservations 14, MediaQueryEvaluation 3
6250.1 sonde t=4.3 s : 3.0 im/s, JS bloqué 1859 ms, scrollY 0
6252.1 steps: 48 updates, 73 ms (worst 15 ms); layout 25, compositing layout 0, after 0, layer flush 0, other 0, MediaQueryEvaluation 30, IntersectionObservations 14, AnimationFrameCallbacks 1
6252.3 sonde t=6.5 s : 12.0 im/s, JS bloqué 1497 ms, scrollY 0
6254.3 sonde t=8.6 s : 3.0 im/s, JS bloqué 2045 ms, scrollY 0
6254.3 steps: 14 updates, 355 ms (worst 314 ms); layout 333, compositing layout 0, after 0, layer flush 0, other 5, MediaQueryEvaluation 10, IntersectionObservations 3, Animations 2
6255.7 sonde tf1 10.0 s: DEFILE début du défilement automatique (10 s)
6256.8 sonde t=11.1 s : 33.0 im/s, JS bloqué 1031 ms, scrollY 120
6257.2 steps: 132 updates, 449 ms (worst 338 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 342, MediaQueryEvaluation 67, IntersectionObservations 34, AnimationFrameCallbacks 1
6258.9 sonde t=13.2 s : 1.0 im/s, JS bloqué 1993 ms, scrollY 240
6259.3 steps: 4 updates, 733 ms (worst 370 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 727, MediaQueryEvaluation 3, IntersectionObservations 1
6261.1 sonde t=15.4 s : 1.5 im/s, JS bloqué 2105 ms, scrollY 360
6262.2 steps: 8 updates, 1143 ms (worst 752 ms); layout 370, compositing layout 0, after 0, layer flush 0, other 757, IntersectionObservations 7, MediaQueryEvaluation 5
6263.7 sonde t=18.0 s : 1.5 im/s, JS bloqué 2843 ms, scrollY 540
6264.9 steps: 6 updates, 1205 ms (worst 810 ms); layout 373, compositing layout 0, after 0, layer flush 0, other 813, IntersectionObservations 12, MediaQueryEvaluation 4, Scroll 1
6266.7 sonde t=21.0 s : 2.0 im/s, JS bloqué 2442 ms, scrollY 720
6267.5 steps: 6 updates, 876 ms (worst 482 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 869, MediaQueryEvaluation 3, IntersectionObservations 2
6269.2 sonde t=23.5 s : 1.0 im/s, JS bloqué 2334 ms, scrollY 900
6269.6 steps: 4 updates, 1137 ms (worst 751 ms); layout 364, compositing layout 0, after 0, layer flush 0, other 767, MediaQueryEvaluation 3, IntersectionObservations 1
6271.4 sonde t=25.6 s : 2.0 im/s, JS bloqué 1951 ms, scrollY 1080
6271.7 steps: 8 updates, 1150 ms (worst 758 ms); layout 364, compositing layout 0, after 0, layer flush 0, other 769, IntersectionObservations 8, MediaQueryEvaluation 6, Animations 1
6273.4 sonde t=27.7 s : 1.5 im/s, JS bloqué 2335 ms, scrollY 1260
6273.8 steps: 28 updates, 1618 ms (worst 780 ms); layout 365, compositing layout 0, after 0, layer flush 0, other 1188, UpdateContentRelevancy 23, MediaQueryEvaluation 22, IntersectionObservations 14
6275.5 sonde t=29.7 s : 46.0 im/s, JS bloqué 334 ms, scrollY 2400
6275.8 steps: 200 updates, 425 ms (worst 53 ms); layout 6, compositing layout 0, after 1, layer flush 0, other 35, MediaQueryEvaluation 141, UpdateContentRelevancy 128, IntersectionObservations 98, Scroll 3, Animations 2, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1
6278.3 sonde t=32.5 s : 27.0 im/s, JS bloqué 1370 ms, scrollY 3120
6278.7 steps: 70 updates, 578 ms (worst 406 ms); layout 1, compositing layout 1, after 0, layer flush 0, other 403, UpdateContentRelevancy 82, MediaQueryEvaluation 50, IntersectionObservations 33, Scroll 1
6280.5 sonde t=34.7 s : 1.0 im/s, JS bloqué 2121 ms, scrollY 3240
6280.9 steps: 4 updates, 818 ms (worst 415 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 812, MediaQueryEvaluation 3, IntersectionObservations 1
6283.1 sonde t=37.4 s : 2.0 im/s, JS bloqué 2196 ms, scrollY 3360
6283.2 steps: 8 updates, 1228 ms (worst 813 ms); layout 403, compositing layout 0, after 0, layer flush 0, other 807, IntersectionObservations 8, MediaQueryEvaluation 7
6285.5 steps: 4 updates, 426 ms (worst 424 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 417, IntersectionObservations 5, MediaQueryEvaluation 2
6285.9 sonde t=40.2 s : 1.5 im/s, JS bloqué 3307 ms, scrollY 3600
6288.2 steps: 6 updates, 840 ms (worst 425 ms); layout 425, compositing layout 0, after 0, layer flush 0, other 407, MediaQueryEvaluation 4, IntersectionObservations 2
6288.6 sonde t=42.9 s : 1.5 im/s, JS bloqué 2560 ms, scrollY 3780
6290.4 steps: 8 updates, 1647 ms (worst 819 ms); layout 402, compositing layout 0, after 0, layer flush 0, other 1229, MediaQueryEvaluation 7, IntersectionObservations 6
6291.1 sonde t=45.3 s : 2.0 im/s, JS bloqué 1885 ms, scrollY 4020
6291.8 sonde tf1 46.0 s: DEFILE saut à scrollY 7000
6292.4 steps: 34 updates, 474 ms (worst 417 ms); layout 7, compositing layout 0, after 0, layer flush 0, other 413, MediaQueryEvaluation 30, IntersectionObservations 20, Scroll 1
6292.7 sonde t=46.9 s : 12.5 im/s, JS bloqué 1286 ms, scrollY 7480
6294.4 steps: 186 updates, 384 ms (worst 30 ms); layout 3, compositing layout 2, after 1, layer flush 0, other 0, UpdateContentRelevancy 140, MediaQueryEvaluation 138, IntersectionObservations 88, Scroll 3, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1
6294.7 sonde t=48.9 s : 49.5 im/s, JS bloqué 124 ms, scrollY 8620
6296.5 steps: 178 updates, 369 ms (worst 49 ms); layout 7, compositing layout 2, after 0, layer flush 0, other -24, UpdateContentRelevancy 159, MediaQueryEvaluation 132, IntersectionObservations 81, Scroll 2, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6296.7 sonde t=51.0 s : 40.0 im/s, JS bloqué 330 ms, scrollY 9760
6298.5 steps: 172 updates, 406 ms (worst 33 ms); layout 4, compositing layout 2, after 0, layer flush 0, other 29, UpdateContentRelevancy 155, MediaQueryEvaluation 126, IntersectionObservations 79, Scroll 2, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6298.7 sonde t=53.0 s : 46.5 im/s, JS bloqué 138 ms, scrollY 10900
6300.5 steps: 182 updates, 335 ms (worst 42 ms); layout 3, compositing layout 0, after 1, layer flush 0, other 0, MediaQueryEvaluation 134, UpdateContentRelevancy 112, IntersectionObservations 73, Scroll 2, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6300.7 sonde t=55.0 s : 45.0 im/s, JS bloqué 196 ms, scrollY 12040
6302.5 steps: 210 updates, 364 ms (worst 47 ms); layout 4, compositing layout 0, after 1, layer flush 0, other 3, MediaQueryEvaluation 151, UpdateContentRelevancy 118, IntersectionObservations 74, Scroll 2, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1
6302.7 sonde t=57.0 s : 53.5 im/s, JS bloqué 118 ms, scrollY 13180
6304.5 steps: 210 updates, 206 ms (worst 13 ms); layout 2, compositing layout 0, after 1, layer flush 0, other 0, MediaQueryEvaluation 131, IntersectionObservations 49, UpdateContentRelevancy 11, Scroll 2, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6304.7 sonde t=59.0 s : 52.5 im/s, JS bloqué 386 ms, scrollY 13784
6306.5 steps: 252 updates, 205 ms (worst 2 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 2, MediaQueryEvaluation 139, IntersectionObservations 54, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1
6306.7 sonde t=61.0 s : 62.5 im/s, JS bloqué 0 ms, scrollY 13784
6308.5 steps: 252 updates, 244 ms (worst 4 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 53, MediaQueryEvaluation 132, IntersectionObservations 48, Animations 2, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1
6308.7 sonde t=63.0 s : 63.0 im/s, JS bloqué 0 ms, scrollY 13784
6310.5 steps: 248 updates, 176 ms (worst 2 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1, MediaQueryEvaluation 120, IntersectionObservations 45, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6310.7 sonde t=65.0 s : 62.0 im/s, JS bloqué 0 ms, scrollY 13784
6312.5 steps: 248 updates, 211 ms (worst 10 ms); layout 1, compositing layout 0, after 0, layer flush 0, other 26, MediaQueryEvaluation 126, IntersectionObservations 46, Animations 3, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6312.7 sonde t=67.0 s : 62.5 im/s, JS bloqué 27 ms, scrollY 13784
6314.5 steps: 252 updates, 217 ms (worst 6 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 29, MediaQueryEvaluation 129, IntersectionObservations 47, Animations 2, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6314.7 sonde t=69.0 s : 62.5 im/s, JS bloqué 0 ms, scrollY 13784
6316.5 steps: 252 updates, 179 ms (worst 2 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1, MediaQueryEvaluation 124, IntersectionObservations 45, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6316.7 sonde t=71.0 s : 63.0 im/s, JS bloqué 0 ms, scrollY 13784
6318.5 steps: 248 updates, 239 ms (worst 8 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 53, MediaQueryEvaluation 126, IntersectionObservations 47, Animations 3, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6318.7 sonde t=73.0 s : 61.5 im/s, JS bloqué 25 ms, scrollY 13784
6320.5 steps: 252 updates, 184 ms (worst 2 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 1, MediaQueryEvaluation 125, IntersectionObservations 49, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1
6320.7 sonde t=75.0 s : 63.0 im/s, JS bloqué 0 ms, scrollY 13784
6322.5 steps: 248 updates, 206 ms (worst 9 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 26, MediaQueryEvaluation 124, IntersectionObservations 46, Animations 2, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1
6322.7 sonde t=77.0 s : 62.0 im/s, JS bloqué 0 ms, scrollY 13784
6324.6 steps: 252 updates, 212 ms (worst 6 ms); layout 0, compositing layout 0, after 0, layer flush 0, other 28, MediaQueryEvaluation 125, IntersectionObservations 48, Animations 2, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1
6324.7 sonde t=79.0 s : 63.0 im/s, JS bloqué 0 ms, scrollY 13784
```

### `cpuwatch` pendant le défilement lent (GPU)

```
== 18:29:09
1728 4925 8 127 tf1plus
1735 4934 6 18015 tf1plus
1736 4935 15 131 tf1plus
== 18:29:15
1728 4925 4 133 tf1plus
1735 4934 5 23952 tf1plus
1736 4935 9 132 tf1plus
```

(Colonnes de `cpuwatch` : process, thread, une colonne non interprétée ici, temps de processeur cumulé en ms, nom ;
le thread 4934, le plus chargé, est le fil principal du process web : + 5937 ms en 6 s.)
