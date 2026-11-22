# Résultats : TF1+, deuxième mesure avec jet 1.4.7 (pour la session du cloud)

Réponse à `docs/agents/JET-TF1-ETAPES-2.md`. Mesuré le 22/11/2026 sur le ZBook, **jet 1.4.7** (patchs 0014 et 0015),
`tf1plus` 1.0.2 (départ sur `https://www.tf1.fr/fr-be/`), fenêtre 1000 × 666, même passage que la dernière fois
(défilement automatique à partir de 10 s, saut à `scrollY` 7000 vers 45 s), avec et sans GPU, plus la sonde des
mutations. Les lignes `steps:` arrivent cette fois **sur la sortie de l'app** (le tube), plus au journal. Relevés
bruts : `~/jet-mesures/10/` (sur la machine).

## En bref

- **L'étape coupable est `middle layout`** : ≈ 1000 à 1070 ms par fenêtre lente (172 ms par mise à jour en moyenne,
  ≈ 350 ms par mise à jour lourde). `other` tombe à ≈ 0 : le patch 0015 couvre tout.
- **Dedans, c'est le style** : ≈ 2000 ms par fenêtre lente, 7 à 8 recalculs, **≈ 52 000 éléments** par fenêtre, soit
  ≈ 4 recalculs complets de la page (≈ 13 000 éléments chacun). `tree layout` 4 à 18 ms, `post layout` 0 à 3 ms,
  **0 rebuild** : ni la mise en page de l'arbre ni les couches ne comptent.
- Le style (≈ 2000 ms) vaut ≈ deux fois la somme des mises à jour (≈ 1050 ms) : la moitié des recalculs est forcée
  par le JavaScript du site, hors de la mise à jour (il lit la géométrie juste après avoir changé le style).
- **Ce qui invalide le style** : la seule mutation fréquente pendant le défilement lent est **`body.style` →
  `pointer-events: none;`** puis `""`, 4 à 6 fois par fenêtre (une bascule = un recalcul complet).
- **Le cercle vicieux** : le site pose `pointer-events: none` sur `<body>` quand le défilement commence et l'ôte après
  une pause. Défilement fluide (un événement toutes les 16 ms) : jamais de pause, **aucune bascule** dans les fenêtres
  rapides (sans GPU après le saut : 56 à 61 images/s, 0 mutation de `body.style`). Défilement saccadé : chaque image
  arrive après une longue pause, le site ôte puis remet la propriété, deux recalculs complets par pas : il reste lent.
  Avec ou sans GPU ne change pas le coût ; seulement de quel côté le passage tombe (sans GPU, il a basculé du côté
  rapide au saut).

## Les fenêtres

Lentes : au plus 8 mises à jour et plus de 400 ms ; rapides : au moins 100 mises à jour et `worst` > 8 ms ; repos :
au moins 200 mises à jour et `worst` ≤ 10 ms. Champs de gauche **par mise à jour**, de droite **par fenêtre de 2 s**.

### GPU

```
lente   24 fen. | par màj :   5.5 màj/fen, total  189.9, layout  16.4, middle layout 172.0, timelines  0.0, other  -0.3, compositing  0.1, flush  0.0 | par fen. : style   1851 ms,  7.1 recalcs,   47744 él., 0.0 rebuilds, tree layout     8 ms, post layout    2 ms,  7.9 full, 0.1 subtree
rapide  -
repos   -
```

### Sans GPU (`--nogpu`)

```
lente    7 fen. | par màj :   6.6 màj/fen, total  157.4, layout  54.4, middle layout 107.4, timelines  0.0, other  -6.3, compositing  0.0, flush  0.0 | par fen. : style   1343 ms,  5.7 recalcs,   34623 él., 0.0 rebuilds, tree layout    21 ms, post layout    2 ms, 12.9 full, 0.1 subtree
rapide  10 fen. | par màj : 231.4 màj/fen, total    1.8, layout   0.3, middle layout   0.0, timelines  0.0, other   0.3, compositing  0.0, flush  0.0 | par fen. : style    149 ms, 15.7 recalcs,    3119 él., 0.0 rebuilds, tree layout    60 ms, post layout    9 ms, 47.1 full, 4.3 subtree
repos    4 fen. | par màj : 252.0 màj/fen, total    0.7, layout   0.0, middle layout   0.0, timelines  0.0, other   0.0, compositing  0.0, flush  0.0 | par fen. : style      0 ms,  0.2 recalcs,       0 él., 0.0 rebuilds, tree layout     0 ms, post layout    0 ms,  0.0 full, 0.2 subtree
```

## L'étape coupable

`middle layout` (le `layoutIfNeeded()` après les `requestAnimationFrame`) : il fait le **style** rendu nécessaire par
la bascule de `pointer-events` sur `<body>` (propriété héritée : tout le sous-arbre, ≈ 13 000 éléments, ≈ 20 µs par
élément ≈ 300-350 ms). Mesures directes au repos (premier rapport, section « Suite ») : une propriété héritée changée
sur `<body>` coûte 220 à 440 ms (`pointer-events`, `visibility` — qui a le raccourci `fast-path-inherited` —,
`cursor`, une variable CSS) ; la même annulée par `body { pointer-events: auto !important }` : 0 à 1 ms.

Correctif essayé dans Jet (feuille de style de l'app, `user.css`, voir le premier rapport) : défilement de tout TF1+ à
36-58 images/s. Il est dans le dépôt de l'agent local, à construire avec le SDK.

## Les mutations (GPU, fenêtres de 2 s, extraits)

```
11.0 s: MUTATIONS body.style 2 (« pointer-events: none; »), fieldset#jQuery37105956856295.id 1 (« jQuery37105956856295224018 »), input.name 1 (« D »), fieldset.disabled 1 («  »)
13.3 s: MUTATIONS body.style 4 (« pointer-events: none; »), header.z-50.class 1 (« z-50 w-full bg-linear-to-b from-(--bg-default) to- »)
16.1 s: MUTATIONS body.style 4 (« pointer-events: none; »), a.absolute.aria-label 3 (« En direct sur Chaîne TFX, Ghosts : Fantômes à la m »), a.absolute.title 3 (« En direct sur Chaîne TFX, Ghosts : Fantômes à la m »), div.h-full.style 3 (« width: 19%; »), a.group/button.tabindex 3 (« -1 »), button.group/button.aria-label 2 (« Mise en avant précédente 2 sur 4 »), button.group/b
18.8 s: MUTATIONS body.style 5 (« pointer-events: none; »)
21.5 s: MUTATIONS body.style 5 («  »)
24.7 s: MUTATIONS body.style 5 (« pointer-events: none; »), a.absolute.aria-label 4 (« En direct sur Chaîne TFX, Ghosts : Fantômes à la m »), a.absolute.title 4 (« En direct sur Chaîne TFX, Ghosts : Fantômes à la m »), div.h-full.style 4 (« width: 20%; »), button.group/button.aria-label 2 (« Mise en avant précédente 3 sur 4 »), button.group/button.title 2 (« Mise en avant préc…
27.2 s: MUTATIONS body.style 5 («  »)
29.4 s: MUTATIONS body.style 6 (« pointer-events: none; »)
31.5 s: MUTATIONS body.style 6 (« pointer-events: none; »), a.group/button.tabindex 3 (« 0 »), button.group/button.aria-label 2 (« Mise en avant précédente 4 sur 4 »), button.group/button.title 2 (« Mise en avant précédente 4 sur 4 »), li.w-8.aria-current 2 (« true »), li.w-8.class 2 (« w-8 border-strong border-t-8 »), a.data-[programmatic-focus]:!out.tabindex 2 (« 0 »), div.size
34.0 s: MUTATIONS body.style 6 (« pointer-events: none; »)
36.5 s: MUTATIONS body.style 7 (« pointer-events: none; »), a.absolute.aria-label 3 (« En direct sur Chaîne TF1, Sept à huit, Sept à huit »), a.absolute.title 3 (« En direct sur Chaîne TF1, Sept à huit, Sept à huit »), div.h-full.style 3 (« width: 51%; »)
39.0 s: MUTATIONS body.style 7 («  »), a.group/button.tabindex 4 (« -1 »), button.group/button.aria-label 2 (« Mise en avant précédente 1 sur 4 »), button.group/button.title 2 (« Mise en avant précédente 1 sur 4 »), li.w-8.aria-current 2 (« null »), li.w-8.class 2 (« w-8 border-regular border-t-4 »), a.data-[programmatic-focus]:!out.tabindex 2 (« -1 »), div.size-full.class 2 (« 
```

## Annexe : les lignes brutes

### GPU — tableau (heure de la sonde, `scrollY`, images/s, puis la ligne `steps:`)

```
t=  2.2 y=    0 10.0 im/s |  41 màj   238 ms (worst  225) layout  200 middle   12 other   -1 | style   509 ms  21 recalcs  19330 él. 10 rebuilds | tree   93 post   6 (38 full 3 sub)
t=  4.4 y=    0  3.0 im/s |  12 màj    18 ms (worst   13) layout    6 middle    0 other   -4 | style   387 ms   3 recalcs  16817 él.  0 rebuilds | tree   70 post   3 (32 full 0 sub)
t=  6.7 y=    0  8.5 im/s |  34 màj   309 ms (worst  280) layout  288 middle    0 other   -4 | style   278 ms  19 recalcs   8831 él. 10 rebuilds | tree  118 post   3 (36 full 3 sub)
t=  8.7 y=    0  2.5 im/s |  10 màj    38 ms (worst   12) layout  254 middle    7 other -235 | style   514 ms  16 recalcs  17660 él.  6 rebuilds | tree   15 post   2 (8 full 2 sub)
t= 11.3 y=  120 22.5 im/s |  90 màj   588 ms (worst  291) layout    0 middle  610 other  -69 | style   901 ms  13 recalcs  25885 él.  0 rebuilds | tree    0 post   0 (4 full 0 sub)
t= 13.6 y=  240  1.0 im/s |   4 màj   908 ms (worst  591) layout  273 middle  646 other  -15 | style  1204 ms   5 recalcs  34407 él.  0 rebuilds | tree    0 post   0 (3 full 0 sub)
t= 16.1 y=  360  1.0 im/s |   4 màj   672 ms (worst  339) layout    3 middle  682 other  -23 | style  1288 ms   6 recalcs  34579 él.  0 rebuilds | tree    8 post   3 (7 full 0 sub)
t= 18.8 y=  540  1.5 im/s |   6 màj  1285 ms (worst  604) layout  281 middle  973 other   15 | style  1873 ms   6 recalcs  51611 él.  0 rebuilds | tree    0 post   0 (4 full 1 sub)
t= 21.5 y=  720  1.0 im/s |   4 màj   691 ms (worst  350) layout    0 middle  696 other   -9 | style  1310 ms   5 recalcs  34677 él.  0 rebuilds | tree    4 post   2 (3 full 0 sub)
t= 21.5 y=  720  1.0 im/s |   4 màj  1001 ms (worst  650) layout  614 middle  672 other -290 | style  1285 ms   4 recalcs  34528 él.  0 rebuilds | tree    3 post   0 (6 full 0 sub)
t= 24.7 y=  900  1.5 im/s |   4 màj   994 ms (worst  643) layout  320 middle  708 other  -45 | style  1327 ms   5 recalcs  34426 él.  0 rebuilds | tree    7 post   0 (5 full 0 sub)
t= 27.5 y= 1020  1.0 im/s |   6 màj  1700 ms (worst  687) layout  664 middle 1059 other  -30 | style  2094 ms   8 recalcs  52393 él.  0 rebuilds | tree   12 post   2 (7 full 0 sub)
t= 29.7 y= 1200  1.5 im/s |   6 màj  1411 ms (worst  718) layout    0 middle 1050 other  350 | style  2002 ms   8 recalcs  51723 él.  0 rebuilds | tree    4 post   0 (6 full 0 sub)
t= 31.8 y= 1380  1.5 im/s |   6 màj  1030 ms (worst  368) layout    0 middle  994 other   23 | style  1982 ms   7 recalcs  51972 él.  0 rebuilds | tree    6 post   3 (9 full 0 sub)
t= 34.0 y= 1740  1.5 im/s |   6 màj  1043 ms (worst  350) layout    1 middle 1039 other   -9 | style  2064 ms   8 recalcs  52450 él.  0 rebuilds | tree   13 post   3 (10 full 0 sub)
t= 36.5 y= 2100  2.0 im/s |   6 màj  1037 ms (worst  354) layout    0 middle 1019 other   10 | style  2040 ms   8 recalcs  52468 él.  0 rebuilds | tree   17 post   3 (10 full 0 sub)
t= 39.0 y= 2400  1.5 im/s |   6 màj  1077 ms (worst  376) layout    0 middle 1070 other   -5 | style  2057 ms   8 recalcs  52011 él.  0 rebuilds | tree    7 post   3 (9 full 0 sub)
t= 41.2 y= 2760  1.5 im/s |   6 màj  1030 ms (worst  349) layout    0 middle 1013 other    7 | style  1990 ms   7 recalcs  52474 él.  0 rebuilds | tree   14 post   3 (10 full 0 sub)
t= 43.3 y= 3120  1.5 im/s |   6 màj  1002 ms (worst  341) layout    0 middle  993 other   -1 | style  2016 ms   7 recalcs  52275 él.  0 rebuilds | tree    8 post   2 (9 full 0 sub)
--- saut à 7000
t= 45.8 y= 7000  1.0 im/s |   4 màj   684 ms (worst  343) layout    0 middle  692 other  -19 | style  1460 ms   6 recalcs  35781 él.  0 rebuilds | tree   13 post   3 (9 full 0 sub)
t= 48.1 y= 7180  1.5 im/s |   6 màj  1058 ms (worst  363) layout    0 middle 1038 other    7 | style  2014 ms   7 recalcs  51948 él.  0 rebuilds | tree   18 post   3 (9 full 0 sub)
t= 50.3 y= 7540  1.5 im/s |   6 màj  1053 ms (worst  357) layout    0 middle 1045 other   -5 | style  2024 ms   7 recalcs  51799 él.  0 rebuilds | tree    5 post   0 (9 full 0 sub)
t= 52.5 y= 7780  1.5 im/s |   6 màj  1073 ms (worst  365) layout    4 middle 1041 other   17 | style  2080 ms   9 recalcs  52089 él.  0 rebuilds | tree   13 post   3 (11 full 0 sub)
t= 55.0 y= 8080  2.0 im/s |   6 màj  1048 ms (worst  353) layout    0 middle 1042 other   -5 | style  2005 ms   8 recalcs  51946 él.  0 rebuilds | tree    7 post   3 (9 full 1 sub)
t= 57.5 y= 8380  1.5 im/s |   6 màj  1047 ms (worst  350) layout    0 middle 1032 other    5 | style  1999 ms   7 recalcs  52118 él.  0 rebuilds | tree   15 post   3 (10 full 0 sub)
t= 57.5 y= 8380  1.5 im/s |   6 màj  1043 ms (worst  355) layout    0 middle 1037 other   -6 | style  2101 ms   9 recalcs  52344 él.  0 rebuilds | tree    4 post   2 (8 full 0 sub)
t= 60.1 y= 8800  2.0 im/s |   6 màj  1049 ms (worst  357) layout    2 middle 1064 other  -26 | style  2083 ms   7 recalcs  51900 él.  0 rebuilds | tree   12 post   2 (8 full 0 sub)
t= 62.7 y= 9040  1.5 im/s |   6 màj  1091 ms (worst  380) layout    0 middle 1049 other   29 | style  2077 ms   8 recalcs  51918 él.  0 rebuilds | tree   10 post   3 (10 full 0 sub)
t= 64.8 y= 9340  1.5 im/s |   6 màj  1046 ms (worst  352) layout    1 middle 1049 other  -13 | style  2041 ms  10 recalcs  52012 él.  0 rebuilds | tree    4 post   0 (9 full 1 sub)
```

### Sans GPU — tableau

```
t=    ? y=    ?    ? im/s |  14 màj   493 ms (worst  272) layout  430 middle   12 other    0 | style   701 ms  18 recalcs  27608 él. 10 rebuilds | tree   21 post   5 (9 full 0 sub)
t=  2.1 y=    0  7.5 im/s |  38 màj   261 ms (worst  234) layout  234 middle    0 other   -2 | style   436 ms  13 recalcs  16838 él.  4 rebuilds | tree   83 post   2 (34 full 3 sub)
t=  4.1 y=    0  7.0 im/s | 100 màj    94 ms (worst    8) layout   12 middle    0 other    2 | style    13 ms  25 recalcs    211 él. 11 rebuilds | tree   78 post   3 (38 full 0 sub)
t=  6.0 y=    0 25.0 im/s | 170 màj    98 ms (worst    8) layout    1 middle    8 other   -2 | style    11 ms   6 recalcs    177 él.  0 rebuilds | tree   12 post   0 (6 full 1 sub)
t=  8.2 y=    0 41.0 im/s | 174 màj   103 ms (worst    5) layout    0 middle    4 other    3 | style     3 ms   3 recalcs     59 él.  0 rebuilds | tree    0 post   0 (1 full 0 sub)
t= 10.7 y=   60 48.5 im/s |  24 màj   948 ms (worst  633) layout  288 middle  981 other -336 | style  1578 ms   6 recalcs  42986 él.  0 rebuilds | tree    5 post   0 (6 full 0 sub)
t= 13.5 y=  300  1.5 im/s |   8 màj   999 ms (worst  646) layout  606 middle  694 other -316 | style  1304 ms   7 recalcs  34547 él.  0 rebuilds | tree   11 post   2 (10 full 0 sub)
t= 16.2 y=  540  2.0 im/s |   6 màj  1009 ms (worst  654) layout    0 middle  707 other  284 | style  1312 ms   4 recalcs  34431 él.  0 rebuilds | tree   13 post   0 (8 full 0 sub)
t= 19.1 y=  720  2.0 im/s |   6 màj  1028 ms (worst  656) layout  290 middle  727 other    4 | style  1341 ms   5 recalcs  34661 él.  0 rebuilds | tree    7 post   2 (5 full 0 sub)
t= 21.7 y=  840  1.5 im/s |   6 màj  1382 ms (worst  658) layout  618 middle 1059 other -303 | style  1982 ms   7 recalcs  51688 él.  0 rebuilds | tree    6 post   0 (9 full 0 sub)
t= 23.7 y= 1080  3.0 im/s |  94 màj  1447 ms (worst  674) layout  303 middle  365 other  668 | style   721 ms   6 recalcs  18354 él.  0 rebuilds | tree   39 post   5 (25 full 0 sub)
t= 25.7 y= 2220 61.0 im/s | 248 màj   319 ms (worst   50) layout    3 middle    0 other  -40 | style   131 ms   4 recalcs   2776 él.  0 rebuilds | tree   59 post  11 (47 full 1 sub)
t= 28.2 y= 3120 42.0 im/s |  84 màj   540 ms (worst  390) layout    5 middle  753 other -329 | style  1386 ms   6 recalcs  35142 él.  0 rebuilds | tree   32 post   3 (23 full 0 sub)
t= 31.3 y= 3360  2.5 im/s |   8 màj  1084 ms (worst  694) layout  322 middle  377 other  369 | style   718 ms   5 recalcs  17532 él.  0 rebuilds | tree   89 post   6 (39 full 1 sub)
t= 31.3 y= 3360  2.5 im/s |   6 màj   395 ms (worst  389) layout    0 middle  771 other -387 | style  1409 ms   5 recalcs  34393 él.  0 rebuilds | tree   11 post   0 (8 full 0 sub)
t= 35.0 y= 3540  2.5 im/s |   6 màj   395 ms (worst  390) layout    3 middle  345 other   39 | style  1023 ms   8 recalcs  26152 él.  1 rebuilds | tree   14 post   2 (8 full 1 sub)
t= 37.4 y= 3780  1.5 im/s |   6 màj   718 ms (worst  356) layout  330 middle  696 other -316 | style  1372 ms   7 recalcs  34802 él.  0 rebuilds | tree   12 post   0 (11 full 0 sub)
t= 39.4 y= 3900  1.0 im/s |   6 màj  1020 ms (worst  667) layout  337 middle  680 other  -12 | style  1372 ms   5 recalcs  34697 él.  0 rebuilds | tree   10 post   2 (8 full 0 sub)
t= 41.3 y= 4080  2.0 im/s | 196 màj  1014 ms (worst  680) layout    3 middle   48 other  679 | style   163 ms  63 recalcs   2102 él.  0 rebuilds | tree   56 post  10 (47 full 0 sub)
--- saut à 7000
t= 43.3 y= 5220 56.5 im/s | 246 màj   311 ms (worst   22) layout    5 middle    0 other   -4 | style   107 ms   8 recalcs   1932 él.  0 rebuilds | tree   72 post  12 (52 full 0 sub)
t= 45.3 y= 7180 58.5 im/s | 236 màj   388 ms (worst   28) layout    2 middle   63 other    8 | style   105 ms  55 recalcs   1414 él.  0 rebuilds | tree   83 post  14 (99 full 1 sub)
t= 47.4 y= 8380 59.5 im/s | 236 màj   382 ms (worst   59) layout    4 middle    0 other    1 | style   130 ms   6 recalcs   1872 él.  0 rebuilds | tree   71 post  12 (50 full 0 sub)
t= 49.4 y= 9520 61.0 im/s | 242 màj   325 ms (worst   32) layout    2 middle    0 other    0 | style    85 ms   5 recalcs   1345 él.  0 rebuilds | tree   57 post   9 (47 full 1 sub)
t= 51.4 y=10660 60.0 im/s | 246 màj   346 ms (worst   25) layout    3 middle    0 other    2 | style    86 ms   8 recalcs   1543 él.  0 rebuilds | tree   66 post  10 (50 full 0 sub)
t= 53.4 y=11860 61.5 im/s | 262 màj   277 ms (worst   42) layout    3 middle    0 other    1 | style    55 ms   3 recalcs    851 él.  0 rebuilds | tree   58 post   5 (45 full 1 sub)
t= 55.4 y=12947 67.5 im/s | 158 màj   454 ms (worst  338) layout  635 middle    0 other -298 | style   629 ms   4 recalcs  17352 él.  0 rebuilds | tree   25 post   4 (13 full 39 sub)
t= 57.4 y=12947 38.0 im/s | 244 màj   461 ms (worst  301) layout    0 middle    0 other  300 | style     0 ms   1 recalcs      0 él.  0 rebuilds | tree   55 post   1 (21 full 0 sub)
t= 59.4 y=12947 61.0 im/s | 252 màj   166 ms (worst    2) layout    0 middle    0 other    0 | style     0 ms   0 recalcs      0 él.  0 rebuilds | tree    0 post   0 (0 full 0 sub)
t= 61.4 y=12947 63.0 im/s | 252 màj   169 ms (worst    3) layout    0 middle    0 other    1 | style     0 ms   1 recalcs      1 él.  0 rebuilds | tree    0 post   0 (0 full 1 sub)
t= 63.4 y=12947 62.5 im/s | 252 màj   160 ms (worst    2) layout    0 middle    0 other    0 | style     0 ms   0 recalcs      0 él.  0 rebuilds | tree    0 post   0 (0 full 0 sub)
t= 65.4 y=12947 63.0 im/s | 252 màj   169 ms (worst    2) layout    0 middle    0 other    2 | style     0 ms   0 recalcs      0 él.  0 rebuilds | tree    0 post   0 (0 full 0 sub)
```

### GPU — lignes `steps:`

```
steps: 41 updates, 238 ms (worst 225 ms); layout 200, compositing layout 1, after 0, layer flush 0, middle layout 12, timelines 0, other -1, ResizeObservations 15, IntersectionObservations 7; style 509 ms (21 recalcs, 19330 elements, 10 rebuilds), tree layout 93 ms, post layout 6 ms (38 full, 3 subtree)
steps: 12 updates, 18 ms (worst 13 ms); layout 6, compositing layout 0, after 0, layer flush 0, middle layout 0, timelines 0, other -4, IntersectionObservations 9, MediaQueryEvaluation 5, Images 1; style 387 ms (3 recalcs, 16817 elements, 0 rebuilds), tree layout 70 ms, post layout 3 ms (32 full, 0 subtree)
steps: 34 updates, 309 ms (worst 280 ms); layout 288, compositing layout 0, after 0, layer flush 0, middle layout 0, timelines 0, other -4, MediaQueryEvaluation 17, IntersectionObservations 5; style 278 ms (19 recalcs, 8831 elements, 10 rebuilds), tree layout 118 ms, post layout 3 ms (36 full, 3 subtree)
steps: 10 updates, 38 ms (worst 12 ms); layout 254, compositing layout 0, after 0, layer flush 0, middle layout 7, timelines 0, other -235, MediaQueryEvaluation 5, IntersectionObservations 2, Animations 1; style 514 ms (16 recalcs, 17660 elements, 6 rebuilds), tree layout 15 ms, post layout 2 ms (8 full, 2 subtree)
steps: 90 updates, 588 ms (worst 291 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 610, timelines 0, other -69, MediaQueryEvaluation 28, IntersectionObservations 13, Animations 1; style 901 ms (13 recalcs, 25885 elements, 0 rebuilds), tree layout 0 ms, post layout 0 ms (4 full, 0 subtree)
steps: 4 updates, 908 ms (worst 591 ms); layout 273, compositing layout 0, after 0, layer flush 0, middle layout 646, timelines 0, other -15, MediaQueryEvaluation 2; style 1204 ms (5 recalcs, 34407 elements, 0 rebuilds), tree layout 0 ms, post layout 0 ms (3 full, 0 subtree)
steps: 4 updates, 672 ms (worst 339 ms); layout 3, compositing layout 0, after 0, layer flush 0, middle layout 682, timelines 0, other -23, IntersectionObservations 5, MediaQueryEvaluation 2, Animations 1; style 1288 ms (6 recalcs, 34579 elements, 0 rebuilds), tree layout 8 ms, post layout 3 ms (7 full, 0 subtree)
steps: 6 updates, 1285 ms (worst 604 ms); layout 281, compositing layout 0, after 0, layer flush 0, middle layout 973, timelines 0, other 15, IntersectionObservations 11, MediaQueryEvaluation 3; style 1873 ms (6 recalcs, 51611 elements, 0 rebuilds), tree layout 0 ms, post layout 0 ms (4 full, 1 subtree)
steps: 4 updates, 691 ms (worst 350 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 696, timelines 0, other -9, MediaQueryEvaluation 2, IntersectionObservations 1; style 1310 ms (5 recalcs, 34677 elements, 0 rebuilds), tree layout 4 ms, post layout 2 ms (3 full, 0 subtree)
steps: 4 updates, 1001 ms (worst 650 ms); layout 614, compositing layout 1, after 0, layer flush 0, middle layout 672, timelines 0, other -290, MediaQueryEvaluation 2, IntersectionObservations 1; style 1285 ms (4 recalcs, 34528 elements, 0 rebuilds), tree layout 3 ms, post layout 0 ms (6 full, 0 subtree)
steps: 4 updates, 994 ms (worst 643 ms); layout 320, compositing layout 0, after 0, layer flush 0, middle layout 708, timelines 0, other -45, IntersectionObservations 7, MediaQueryEvaluation 2; style 1327 ms (5 recalcs, 34426 elements, 0 rebuilds), tree layout 7 ms, post layout 0 ms (5 full, 0 subtree)
steps: 6 updates, 1700 ms (worst 687 ms); layout 664, compositing layout 0, after 0, layer flush 0, middle layout 1059, timelines 0, other -30, MediaQueryEvaluation 4, IntersectionObservations 2; style 2094 ms (8 recalcs, 52393 elements, 0 rebuilds), tree layout 12 ms, post layout 2 ms (7 full, 0 subtree)
steps: 6 updates, 1411 ms (worst 718 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 1050, timelines 0, other 350, IntersectionObservations 5, MediaQueryEvaluation 3; style 2002 ms (8 recalcs, 51723 elements, 0 rebuilds), tree layout 4 ms, post layout 0 ms (6 full, 0 subtree)
steps: 6 updates, 1030 ms (worst 368 ms); layout 0, compositing layout 1, after 0, layer flush 0, middle layout 994, timelines 0, other 23, IntersectionObservations 5, MediaQueryEvaluation 3; style 1982 ms (7 recalcs, 51972 elements, 0 rebuilds), tree layout 6 ms, post layout 3 ms (9 full, 0 subtree)
steps: 6 updates, 1043 ms (worst 350 ms); layout 1, compositing layout 0, after 0, layer flush 0, middle layout 1039, timelines 0, other -9, IntersectionObservations 7, MediaQueryEvaluation 3; style 2064 ms (8 recalcs, 52450 elements, 0 rebuilds), tree layout 13 ms, post layout 3 ms (10 full, 0 subtree)
steps: 6 updates, 1037 ms (worst 354 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 1019, timelines 0, other 10, MediaQueryEvaluation 3, IntersectionObservations 2; style 2040 ms (8 recalcs, 52468 elements, 0 rebuilds), tree layout 17 ms, post layout 3 ms (10 full, 0 subtree)
steps: 6 updates, 1077 ms (worst 376 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 1070, timelines 0, other -5, IntersectionObservations 6, MediaQueryEvaluation 3; style 2057 ms (8 recalcs, 52011 elements, 0 rebuilds), tree layout 7 ms, post layout 3 ms (9 full, 0 subtree)
steps: 6 updates, 1030 ms (worst 349 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 1013, timelines 0, other 7, MediaQueryEvaluation 4, IntersectionObservations 3; style 1990 ms (7 recalcs, 52474 elements, 0 rebuilds), tree layout 14 ms, post layout 3 ms (10 full, 0 subtree)
steps: 6 updates, 1002 ms (worst 341 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 993, timelines 0, other -1, IntersectionObservations 6, MediaQueryEvaluation 3; style 2016 ms (7 recalcs, 52275 elements, 0 rebuilds), tree layout 8 ms, post layout 2 ms (9 full, 0 subtree)
steps: 4 updates, 684 ms (worst 343 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 692, timelines 0, other -19, IntersectionObservations 7, MediaQueryEvaluation 2; style 1460 ms (6 recalcs, 35781 elements, 0 rebuilds), tree layout 13 ms, post layout 3 ms (9 full, 0 subtree)
steps: 6 updates, 1058 ms (worst 363 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 1038, timelines 0, other 7, IntersectionObservations 7, MediaQueryEvaluation 3; style 2014 ms (7 recalcs, 51948 elements, 0 rebuilds), tree layout 18 ms, post layout 3 ms (9 full, 0 subtree)
steps: 6 updates, 1053 ms (worst 357 ms); layout 0, compositing layout 2, after 0, layer flush 0, middle layout 1045, timelines 0, other -5, IntersectionObservations 7, MediaQueryEvaluation 3; style 2024 ms (7 recalcs, 51799 elements, 0 rebuilds), tree layout 5 ms, post layout 0 ms (9 full, 0 subtree)
steps: 6 updates, 1073 ms (worst 365 ms); layout 4, compositing layout 3, after 0, layer flush 0, middle layout 1041, timelines 0, other 17, MediaQueryEvaluation 3, IntersectionObservations 1; style 2080 ms (9 recalcs, 52089 elements, 0 rebuilds), tree layout 13 ms, post layout 3 ms (11 full, 0 subtree)
steps: 6 updates, 1048 ms (worst 353 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 1042, timelines 0, other -5, IntersectionObservations 5, MediaQueryEvaluation 4; style 2005 ms (8 recalcs, 51946 elements, 0 rebuilds), tree layout 7 ms, post layout 3 ms (9 full, 1 subtree)
steps: 6 updates, 1047 ms (worst 350 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 1032, timelines 0, other 5, IntersectionObservations 3, MediaQueryEvaluation 3; style 1999 ms (7 recalcs, 52118 elements, 0 rebuilds), tree layout 15 ms, post layout 3 ms (10 full, 0 subtree)
steps: 6 updates, 1043 ms (worst 355 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 1037, timelines 0, other -6, IntersectionObservations 6, MediaQueryEvaluation 3; style 2101 ms (9 recalcs, 52344 elements, 0 rebuilds), tree layout 4 ms, post layout 2 ms (8 full, 0 subtree)
steps: 6 updates, 1049 ms (worst 357 ms); layout 2, compositing layout 0, after 0, layer flush 0, middle layout 1064, timelines 0, other -26, IntersectionObservations 5, MediaQueryEvaluation 3; style 2083 ms (7 recalcs, 51900 elements, 0 rebuilds), tree layout 12 ms, post layout 2 ms (8 full, 0 subtree)
steps: 6 updates, 1091 ms (worst 380 ms); layout 0, compositing layout 2, after 0, layer flush 0, middle layout 1049, timelines 0, other 29, IntersectionObservations 6, MediaQueryEvaluation 3; style 2077 ms (8 recalcs, 51918 elements, 0 rebuilds), tree layout 10 ms, post layout 3 ms (10 full, 0 subtree)
steps: 6 updates, 1046 ms (worst 352 ms); layout 1, compositing layout 0, after 0, layer flush 0, middle layout 1049, timelines 0, other -13, MediaQueryEvaluation 4, Animations 1, IntersectionObservations 1; style 2041 ms (10 recalcs, 52012 elements, 0 rebuilds), tree layout 4 ms, post layout 0 ms (9 full, 1 subtree)
```

### Sans GPU — lignes `steps:`

```
steps: 14 updates, 493 ms (worst 272 ms); layout 430, compositing layout 0, after 1, layer flush 0, middle layout 12, timelines 0, other 0, ResizeObservations 23, Animations 21, IntersectionObservations 3; style 701 ms (18 recalcs, 27608 elements, 10 rebuilds), tree layout 21 ms, post layout 5 ms (9 full, 0 subtree)
steps: 38 updates, 261 ms (worst 234 ms); layout 234, compositing layout 0, after 4, layer flush 0, middle layout 0, timelines 0, other -2, IntersectionObservations 12, MediaQueryEvaluation 11; style 436 ms (13 recalcs, 16838 elements, 4 rebuilds), tree layout 83 ms, post layout 2 ms (34 full, 3 subtree)
steps: 100 updates, 94 ms (worst 8 ms); layout 12, compositing layout 0, after 10, layer flush 0, middle layout 0, timelines 0, other 2, MediaQueryEvaluation 43, IntersectionObservations 18, AnimationFrameCallbacks 2, Images 1; style 13 ms (25 recalcs, 211 elements, 11 rebuilds), tree layout 78 ms, post layout 3 ms (38 full, 0 subtree)
steps: 170 updates, 98 ms (worst 8 ms); layout 1, compositing layout 0, after 13, layer flush 0, middle layout 8, timelines 0, other -2, MediaQueryEvaluation 51, IntersectionObservations 20, Animations 3; style 11 ms (6 recalcs, 177 elements, 0 rebuilds), tree layout 12 ms, post layout 0 ms (6 full, 1 subtree)
steps: 174 updates, 103 ms (worst 5 ms); layout 0, compositing layout 0, after 14, layer flush 0, middle layout 4, timelines 0, other 3, MediaQueryEvaluation 54, IntersectionObservations 22; style 3 ms (3 recalcs, 59 elements, 0 rebuilds), tree layout 0 ms, post layout 0 ms (1 full, 0 subtree)
steps: 24 updates, 948 ms (worst 633 ms); layout 288, compositing layout 0, after 1, layer flush 0, middle layout 981, timelines 0, other -336, MediaQueryEvaluation 7, IntersectionObservations 3, Scroll 1; style 1578 ms (6 recalcs, 42986 elements, 0 rebuilds), tree layout 5 ms, post layout 0 ms (6 full, 0 subtree)
steps: 8 updates, 999 ms (worst 646 ms); layout 606, compositing layout 0, after 1, layer flush 0, middle layout 694, timelines 0, other -316, IntersectionObservations 6, MediaQueryEvaluation 4, Animations 1; style 1304 ms (7 recalcs, 34547 elements, 0 rebuilds), tree layout 11 ms, post layout 2 ms (10 full, 0 subtree)
steps: 6 updates, 1009 ms (worst 654 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 707, timelines 0, other 284, IntersectionObservations 11, MediaQueryEvaluation 3; style 1312 ms (4 recalcs, 34431 elements, 0 rebuilds), tree layout 13 ms, post layout 0 ms (8 full, 0 subtree)
steps: 6 updates, 1028 ms (worst 656 ms); layout 290, compositing layout 0, after 0, layer flush 0, middle layout 727, timelines 0, other 4, MediaQueryEvaluation 3, IntersectionObservations 2; style 1341 ms (5 recalcs, 34661 elements, 0 rebuilds), tree layout 7 ms, post layout 2 ms (5 full, 0 subtree)
steps: 6 updates, 1382 ms (worst 658 ms); layout 618, compositing layout 0, after 1, layer flush 0, middle layout 1059, timelines 0, other -303, MediaQueryEvaluation 3, IntersectionObservations 1; style 1982 ms (7 recalcs, 51688 elements, 0 rebuilds), tree layout 6 ms, post layout 0 ms (9 full, 0 subtree)
steps: 94 updates, 1447 ms (worst 674 ms); layout 303, compositing layout 0, after 11, layer flush 0, middle layout 365, timelines 0, other 668, MediaQueryEvaluation 41, IntersectionObservations 29, UpdateContentRelevancy 21, Scroll 2; style 721 ms (6 recalcs, 18354 elements, 0 rebuilds), tree layout 39 ms, post layout 5 ms (25 full, 0 subtree)
steps: 248 updates, 319 ms (worst 50 ms); layout 3, compositing layout 0, after 32, layer flush 0, middle layout 0, timelines 0, other -40, UpdateContentRelevancy 156, MediaQueryEvaluation 95, IntersectionObservations 64, Scroll 3, Images 1, AnimationFrameCallbacks 1; style 131 ms (4 recalcs, 2776 elements, 0 rebuilds), tree layout 59 ms, post layout 11 ms (47 full, 1 subtree)
steps: 84 updates, 540 ms (worst 390 ms); layout 5, compositing layout 0, after 12, layer flush 0, middle layout 753, timelines 0, other -329, UpdateContentRelevancy 38, MediaQueryEvaluation 35, IntersectionObservations 19, Scroll 1; style 1386 ms (6 recalcs, 35142 elements, 0 rebuilds), tree layout 32 ms, post layout 3 ms (23 full, 0 subtree)
steps: 8 updates, 1084 ms (worst 694 ms); layout 322, compositing layout 0, after 1, layer flush 0, middle layout 377, timelines 0, other 369, IntersectionObservations 6, MediaQueryEvaluation 5, Scroll 1; style 718 ms (5 recalcs, 17532 elements, 0 rebuilds), tree layout 89 ms, post layout 6 ms (39 full, 1 subtree)
steps: 6 updates, 395 ms (worst 389 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 771, timelines 0, other -387, IntersectionObservations 5, MediaQueryEvaluation 3; style 1409 ms (5 recalcs, 34393 elements, 0 rebuilds), tree layout 11 ms, post layout 0 ms (8 full, 0 subtree)
steps: 6 updates, 395 ms (worst 390 ms); layout 3, compositing layout 0, after 0, layer flush 0, middle layout 345, timelines 0, other 39, MediaQueryEvaluation 3, IntersectionObservations 1; style 1023 ms (8 recalcs, 26152 elements, 1 rebuilds), tree layout 14 ms, post layout 2 ms (8 full, 1 subtree)
steps: 6 updates, 718 ms (worst 356 ms); layout 330, compositing layout 0, after 0, layer flush 0, middle layout 696, timelines 0, other -316, MediaQueryEvaluation 3, IntersectionObservations 1; style 1372 ms (7 recalcs, 34802 elements, 0 rebuilds), tree layout 12 ms, post layout 0 ms (11 full, 0 subtree)
steps: 6 updates, 1020 ms (worst 667 ms); layout 337, compositing layout 2, after 0, layer flush 0, middle layout 680, timelines 0, other -12, IntersectionObservations 7, MediaQueryEvaluation 3; style 1372 ms (5 recalcs, 34697 elements, 0 rebuilds), tree layout 10 ms, post layout 2 ms (8 full, 0 subtree)
steps: 196 updates, 1014 ms (worst 680 ms); layout 3, compositing layout 0, after 0, layer flush 0, middle layout 48, timelines 0, other 679, MediaQueryEvaluation 118, UpdateContentRelevancy 85, IntersectionObservations 65, Scroll 2, Animations 2, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1; style 163 ms (63 recalcs, 2102 elements, 0 rebuilds), tree layout 56 ms, post layout 10 ms (47 full, 0 subtree)
steps: 246 updates, 311 ms (worst 22 ms); layout 5, compositing layout 0, after 1, layer flush 0, middle layout 0, timelines 0, other -4, MediaQueryEvaluation 134, UpdateContentRelevancy 83, IntersectionObservations 79, Scroll 3, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1; style 107 ms (8 recalcs, 1932 elements, 0 rebuilds), tree layout 72 ms, post layout 12 ms (52 full, 0 subtree)
steps: 236 updates, 388 ms (worst 28 ms); layout 2, compositing layout 0, after 1, layer flush 0, middle layout 63, timelines 0, other 8, MediaQueryEvaluation 134, UpdateContentRelevancy 92, IntersectionObservations 72, Scroll 2, Animations 2, Images 2, AnimationFrameCallbacks 2, ResizeObservations 1; style 105 ms (55 recalcs, 1414 elements, 0 rebuilds), tree layout 83 ms, post layout 14 ms (99 full, 1 subtree)
steps: 236 updates, 382 ms (worst 59 ms); layout 4, compositing layout 0, after 1, layer flush 0, middle layout 0, timelines 0, other 1, UpdateContentRelevancy 159, MediaQueryEvaluation 134, IntersectionObservations 70, Scroll 2, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1; style 130 ms (6 recalcs, 1872 elements, 0 rebuilds), tree layout 71 ms, post layout 12 ms (50 full, 0 subtree)
steps: 242 updates, 325 ms (worst 32 ms); layout 2, compositing layout 0, after 1, layer flush 0, middle layout 0, timelines 0, other 0, MediaQueryEvaluation 138, UpdateContentRelevancy 106, IntersectionObservations 67, Scroll 2, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1; style 85 ms (5 recalcs, 1345 elements, 0 rebuilds), tree layout 57 ms, post layout 9 ms (47 full, 1 subtree)
steps: 246 updates, 346 ms (worst 25 ms); layout 3, compositing layout 0, after 1, layer flush 0, middle layout 0, timelines 0, other 2, MediaQueryEvaluation 140, UpdateContentRelevancy 112, IntersectionObservations 75, Scroll 2, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1; style 86 ms (8 recalcs, 1543 elements, 0 rebuilds), tree layout 66 ms, post layout 10 ms (50 full, 0 subtree)
steps: 262 updates, 277 ms (worst 42 ms); layout 3, compositing layout 0, after 1, layer flush 0, middle layout 0, timelines 0, other 1, MediaQueryEvaluation 148, UpdateContentRelevancy 68, IntersectionObservations 44, Scroll 2, Images 2, AnimationFrameCallbacks 2, ResizeObservations 1; style 55 ms (3 recalcs, 851 elements, 0 rebuilds), tree layout 58 ms, post layout 5 ms (45 full, 1 subtree)
steps: 158 updates, 454 ms (worst 338 ms); layout 635, compositing layout 0, after 0, layer flush 0, middle layout 0, timelines 0, other -298, MediaQueryEvaluation 84, IntersectionObservations 27, Images 1, AnimationFrameCallbacks 1; style 629 ms (4 recalcs, 17352 elements, 0 rebuilds), tree layout 25 ms, post layout 4 ms (13 full, 39 subtree)
steps: 244 updates, 461 ms (worst 301 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 0, timelines 0, other 300, MediaQueryEvaluation 114, IntersectionObservations 38, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1; style 0 ms (1 recalcs, 0 elements, 0 rebuilds), tree layout 55 ms, post layout 1 ms (21 full, 0 subtree)
steps: 252 updates, 166 ms (worst 2 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 0, timelines 0, other 0, MediaQueryEvaluation 116, IntersectionObservations 40, Images 1, AnimationFrameCallbacks 1, ResizeObservations 1; style 0 ms (0 recalcs, 0 elements, 0 rebuilds), tree layout 0 ms, post layout 0 ms (0 full, 0 subtree)
steps: 252 updates, 169 ms (worst 3 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 0, timelines 0, other 1, MediaQueryEvaluation 118, IntersectionObservations 39, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1; style 0 ms (1 recalcs, 1 elements, 0 rebuilds), tree layout 0 ms, post layout 0 ms (0 full, 1 subtree)
steps: 252 updates, 160 ms (worst 2 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 0, timelines 0, other 0, MediaQueryEvaluation 112, IntersectionObservations 38, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1; style 0 ms (0 recalcs, 0 elements, 0 rebuilds), tree layout 0 ms, post layout 0 ms (0 full, 0 subtree)
steps: 252 updates, 169 ms (worst 2 ms); layout 0, compositing layout 0, after 0, layer flush 0, middle layout 0, timelines 0, other 2, MediaQueryEvaluation 117, IntersectionObservations 40, Images 2, AnimationFrameCallbacks 1, ResizeObservations 1; style 0 ms (0 recalcs, 0 elements, 0 rebuilds), tree layout 0 ms, post layout 0 ms (0 full, 0 subtree)
```
