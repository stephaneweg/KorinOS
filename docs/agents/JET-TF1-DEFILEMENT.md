# Jet : TF1+ défile à 1 image/s — la mise à jour de l'affichage de WebCore (≈ 600 ms par image)

Mesuré le 22/11/2026 sur le ZBook (Korin PC, jet 1.4.5, app `tf1plus` 1.0.1, fenêtre 1000 × 666), par une sonde
JavaScript (`/etc/web-probe.js` + `/etc/web-console`), les lignes `web: gpu:` (`/etc/web-gpu-debug`,
`ONYX_WEB_GPU_DEBUG=1`), la trace réseau (`WEBKIT_TRACE_NET=1`) et des pages d'essai locales. Relevés bruts sur la
machine : `~/jet-mesures/6/` à `~/jet-mesures/8/` (pas dans le dépôt) ; les sondes et le script des passages
automatiques : `~/jet-mesures/8/sondes/`.

## En bref

- La page arrive vite ; **au repos elle tourne à 59 images/s**, sans blocage du fil principal.
- **Dès qu'on défile, dans le haut de la page (jusqu'à ≈ 5000 px de `scrollY`) : 1 à 2 images/s**, le fil principal
  bloqué 2 à 3,5 s par tranche de 2 s (des blocages de 1 à 2,6 s d'un coup). Plus bas (au-delà de ≈ 5000 px), le
  même défilement passe à **61 images/s** sans blocage.
- Le temps est dans la **mise à jour de l'affichage de WebCore** (« page update » des lignes `times:`) : ≈ 1,1 à
  1,3 s par fenêtre de 2 s pour 2 à 3 images, soit **≈ 400 à 650 ms par image** (1,5 ms par image au repos). Puis,
  moins, l'enregistrement du dessin (≈ 350 ms par fenêtre, avec des pics de 1 à 2 s quand des vignettes entrent).
- Ce n'est **ni le JavaScript du site** (10 à 660 ms par fenêtre, surtout une minuterie de ≈ 400 ms), **ni la mise
  en page** (forcée après chaque pas de défilement : 0 à 3 ms), **ni le GPU** (composition 2 à 13 ms par fenêtre ;
  le rendu sans GPU, `--nogpu`, est aussi lent), **ni le chargement différé des images**, **ni le décodage des
  images**, ni les vidéos, ni l'en-tête fixe.
- Les images « lentes à s'afficher » sont une conséquence : elles sont en `loading="lazy"`, WebKit ne les demande
  qu'à une mise à jour de l'affichage, et il n'en fait plus qu'une par seconde ou moins : elles partent par paquets
  après 4 à 12 s d'attente (le réseau, lui, est rapide).

**À trouver dans une construction de WebKit** : quelle étape de `Page::updateRendering` (ou de la mise à jour des
couches qui la suit) coûte ≈ 500 ms par image quand le haut de cette page défile.

## La page

`https://www.tf1.fr/` (puis `/fr-be/` : le site propose la page belge et recharge) : **5,7 Mo de HTML** dont
3,4 Mo de scripts en ligne (Next.js, Tailwind), ≈ 11 600 à 13 300 éléments, 510 `<img>` (468 en
`loading="lazy"`), 1033 `<source>`, 494 `<svg>`, 516 classes `transform`, 2 `<video>` (en pause, 1280 × 720 et
1920 × 1080), 1 `backdrop-blur`, 2 `sticky`, 6 `fixed`, 34 `<section>`, 59 `data-carousel` dans le HTML (introuvables
par `querySelectorAll("[data-carousel]")` après l'hydratation). Les vignettes portrait ont `sizes=""` (donc 100vw) :
le navigateur prend la plus grande (`@3x`, 651 × 867 en WebP ; Chrome ferait de même). Pas de CSP.

## Les mesures

Sonde dans la page principale, toutes les 2 s : images/s vues par `requestAnimationFrame`, blocage du fil (retard
d'une minuterie de 50 ms), images (insérée → demandée → reçue → chargée, Resource Timing), le temps passé dans le
JavaScript du site par sorte de rappel (événements, `requestAnimationFrame`, observateurs, minuteries, microtâches :
les fonctions du site enveloppées avant ses scripts), le nombre d'éléments ; et un défilement automatique
(`scrollBy(0, 60)` toutes les 100 ms) avec, après chaque pas, le temps de la mise en page forcée
(`getBoundingClientRect` + `offsetHeight`).

### Défilement à la molette (l'utilisateur), `~/jet-mesures/7`

| Moment | im/s | JS bloqué / 2 s | JS du site / 2 s | « page update » / fenêtre | record / fenêtre |
|---|---|---|---|---|---|
| Défilement, haut de page | 1 à 3,5 | 1,8 à 3,6 s | 10 à 660 ms | 0,6 à 1,5 s | 10 ms à 1,2 s |
| Repos (scrollY 6480, 10080) | 59 | 0 | 4 à 10 ms | 175 à 205 ms (≈ 1,5 ms par image) | 0 |

### Défilement automatique, quatre passages, `~/jet-mesures/8` (moyennes des fenêtres lentes)

| Passage | Fenêtres lentes | Images / fenêtre | page update | layer walk (record) | composite |
|---|---|---|---|---|---|
| `gpu2` (GPU, par défaut) | 27 | 2,8 | 1133 ms | 361 (343) ms | 8 ms |
| `nogpu` (`--nogpu`) | 14 | 2,2 | 1289 ms | 234 (219) ms | 13 ms |
| `eager` (toutes les images en `loading="eager"`) | 24 | 2,9 | 1238 ms | 429 (408) ms | 7 ms |
| `bisect` (phases ci-dessous) | 35 | 3,1 | 1318 ms | 475 (450) ms | 4 ms |

- Mise en page forcée après un pas de défilement : **0 à 3 ms** (tous les passages) : le défilement n'invalide pas
  la mise en page.
- `nogpu` : lent jusqu'à `scrollY` ≈ 5000, puis **61 images/s**, JS bloqué 0 à 84 ms (même défilement automatique).
- `eager` : à 35 s, les 551 images sont chargées, plus aucune en attente — et le défilement reste à 1 à 1,5 im/s.
- `bisect`, phases de 10 s pendant le défilement automatique : telle quelle ; vidéos en pause ; vidéos retirées
  (`src` ôté, `display: none`) ; « carrousels » masqués (0 trouvé : sélecteur faux, non tranché) ; en-tête et
  éléments `fixed`/`sticky`/`backdrop-blur` retirés (10). **Aucune phase ne change rien** (1 à 1,5 im/s, 2 à
  2,8 s bloquées par fenêtre).
- Pendant le chargement, même sans défiler, la page est déjà à 2 à 15 im/s (fil bloqué 1,5 à 2 s / 2 s) pendant
  ≈ 10 s.

### Écartés par des essais locaux (Jet sur une page servie en `http://127.0.0.1`)

- **Décodage des images** : 30 vraies vignettes de TF1+ (WebP) affichées d'un coup : `@3x` (651 × 867) ≈ 60-70 ms
  d'enregistrement en tout, `@1x` ≈ 25-30 ms, aucun blocage du fil, 59 im/s.
- **AVIF** : Jet ne le lit pas (`error` sur chaque image) : il prend les `<source>` WebP.
- **Densité d'écran** : `devicePixelRatio` = 1, `srcset` choisit le 1x ; `inner` 1000 × 606, `screen` 1920 × 1080.
- **JIT de JSC** : actif sur le PC (de base, DFG, FTL : `tools/mesa/build-webkit.sh`).

## Où chercher dans WebKit

« page update » est mesuré dans le port Onyx de WebKit (pas dans ce dépôt) : à vérifier d'abord ce qu'il couvre
exactement (`Page::updateRendering` seul, ou aussi `RenderLayerCompositor::updateCompositingLayers` / le vidage
des couches).

1. **Chronométrer chaque étape** de `Page::updateRendering` (`Source/WebCore/page/Page.cpp` : la liste
   `RenderingUpdateStep` — animations, `requestAnimationFrame`, `ResizeObserver`, `IntersectionObserver`,
   style et layout, mise à jour des images lazy, `updateContentRelevancy` / `content-visibility`, accessibilité…)
   et la mise à jour des couches qui suit, derrière `ONYX_WEB_GPU_DEBUG` ; une ligne par fenêtre de 2 s comme
   `times:`. Le coupable est l'étape à ≈ 500 ms par image pendant le défilement du haut de la page.
2. Suspects, d'après les mesures :
   - **la mise à jour des couches composées** (`RenderLayerCompositor` : `computeCompositingRequirements`, carte de
     recouvrement, ≈ 500 transformations, défilement de la page principale sans défilement asynchrone) — chaque
     défilement la refait ; la quantité dépend de ce qui est autour de la zone affichée (le bas de page est rapide) ;
   - **`IntersectionObserver`** (celui du site et celui des images lazy) : le calcul des rectangles de centaines de
     cibles à chaque image (pas tranché par `eager` : le site a aussi les siens) ;
   - l'**accessibilité** ou `content-visibility` / pertinence du contenu, si elles sont actives dans le port.
3. Comparer avec un autre WebKit sur la même page (MiniBrowser de WebKitGTK, Epiphany ou Safari sur la même
   adresse, défilement automatique) : si lui aussi rame, c'est WebKit (et la piste est un réglage, par exemple le
   défilement asynchrone) ; sinon c'est le port Onyx.
4. Essai déjà prêt : la sonde de défilement automatique (`~/jet-mesures/8/sondes/tf1-probe.js`, et
   `run-exp.sh` / `run-exp2.sh` : `DUR=68 bash run-exp2.sh NOM ONYX_WEB_GPU_DEBUG=1 -- [--nogpu]`), à rejouer
   sur le ZBook après une construction instrumentée. **Sortie par un tube** (`… 2>&1 | cat > fichier`) : avec `>`
   seul, la console des pages n'arrive plus au journal.

## Pièges rencontrés

- Le journal du noyau ne garde que 64 Ko et les lignes `web: gpu:` le remplissent vite : le copier toutes les
  3 s (`journal` en tâche de fond, puis `cp /journal.txt`), fusionner sur **la ligne entière** (`sort -u`, pas sur
  l'horodatage : les lignes `frames (` et `times:` ont le même).
- Les fonctions de la sonde (minuteries, `requestAnimationFrame`, écouteurs) passent par les originaux gardés avant
  l'enveloppe, sinon elles se comptent dans le JavaScript du site.
- `cpuwatch` : ne pas tronquer sa sortie (`head`) : les threads du process web sont en fin de liste.

## La mesure par étapes (jet 1.4.6, patch `0014-rendering-update-times.patch`)

`Page::updateRendering` est chronométré étape par étape (dans le cloud, 22/11) : avec `ONYX_WEB_GPU_DEBUG=1` ou
`/etc/web-gpu-debug`, une ligne toutes les 2 s :

```
web: gpu: steps: 97 updates, 349 ms (worst 5 ms); layout 3, compositing layout 23, after 8, layer flush 1, other 32,
  AnimationFrameCallbacks 169, Scroll 5, …
```

- `updates` : mises à jour de l'affichage dans la fenêtre ; `ms` leur total, `worst` la plus longue ;
- `layout` : la mise en page du début ; `compositing layout` : la mise en page finale **avec la mise à jour des
  couches composées** (`RenderLayerCompositor::updateCompositingLayers`, la carte de recouvrement) ; `after` : les
  étapes d'après (curseur, minutage de la peinture, sélection, surlignages) ; `layer flush` : le vidage des couches
  (`flushCompositingStateIncludingSubframes`, avec le GPU seulement) ; `other` : le reste (sous QEMU, surtout le coût
  des mesures : chaque lecture de l'horloge est un appel au noyau) ;
- puis les étapes de plus d'1 ms, de la plus chère à la moins chère (`AnimationFrameCallbacks` = les
  `requestAnimationFrame` du site, `IntersectionObservations`, `ResizeObservations`, `UpdateContentRelevancy`,
  `Images` (les images lazy)…).

À faire sur le ZBook avec jet 1.4.6 : le défilement automatique de `~/jet-mesures/8/sondes/` (passage `gpu2`),
sortie par un tube, puis `grep "steps:\|times:\|frames (" fichier`. L'étape qui prend ≈ 500 ms par image dans le haut
de la page est le coupable (si c'est `compositing layout` : la mise à jour des couches ; si c'est `other` sur le
ZBook : à découper encore). Sous QEMU, TF1+ n'est pas joignable depuis le cloud (politique réseau) : pas mesuré ici.
